//! Application state and the glue between the core and the Slint window.
//!
//! The state lives in a thread-local of the UI thread; background threads (statistics, glossary, grammar)
//! post their results with `Weak::upgrade_in_event_loop` and re-enter it from there through `with_app`.

mod archive;
mod deepl_service;
mod dialogs;
mod editor;
mod host;
mod import;
mod language;
mod layout;
mod mass;
mod modal;
mod panels;
mod project_actions;
mod report;
mod project_dialog;
mod session;
mod tree;
mod wiring;

use crate::{InfoAction, MainWindow, RowItem, TabItem, TreeRow};
use babylon_core::app_settings::AppSettings;
use babylon_core::doc_model::DocModel;
use babylon_core::grammar::GrammarIssue;
use babylon_core::project::{Project, TodoItem};
use babylon_core::project_store::ProjectStore;
use babylon_core::statistics::FileStats;
use babylon_core::translation_index::{Match as MemoryMatch, TranslationIndex};
use slint::{ComponentHandle, ModelRc, VecModel};
use std::cell::RefCell;
use std::collections::BTreeSet;
use std::rc::Rc;
use std::sync::Arc;

pub use language::select_language;
pub use wiring::{dialog_snapshot, install, open_dialog, shutdown};

/// A project that is open in the application.
pub(crate) struct Session {
    pub id: String,
    pub project: Arc<Project>,
    pub stats: Vec<FileStats>,
    pub expanded: bool,
    /// Game files, dialogues, other files.
    pub groups: [bool; 3],
    /// Incremented by every analysis: results of older ones are dropped.
    pub generation: u64,
    pub busy: bool,
    pub glossary_terms: usize,
    /// Translation memory of the project (suggestions); built in the background.
    pub index: Option<Arc<TranslationIndex>>,
}

/// What a button of the information bar does.
pub(crate) enum InfoKind {
    /// Replace a range of the translation.
    Replace { start: usize, len: usize, text: String },
    /// Insert at the caret of the translation.
    Insert(String),
}

/// One open file.
pub(crate) struct Editor {
    pub uid: u64,
    pub session: String,
    pub model: DocModel,
    /// Rows of the document shown by the current filter, in table order.
    pub visible: Vec<usize>,
    pub filter: usize,
    pub search: String,
    /// Column of the table the lines are sorted by (0 ID, 1 source, 2 translation, 3 state; None: file order).
    pub sort: Option<(usize, bool)>,
    /// Row of the document being edited.
    pub current: Option<usize>,
    /// Rows selected with ctrl / shift (the current row is not in it unless it was clicked that way).
    pub selection: BTreeSet<usize>,
    pub anchor: Option<usize>,
    pub rows: Rc<VecModel<RowItem>>,
    pub grammar: Vec<GrammarIssue>,
    pub grammar_serial: u64,
    pub actions: Vec<InfoKind>,
    /// Dirty state last shown in the tabs and the tree.
    pub shown_dirty: bool,
}

impl Editor {
    pub fn rel_path(&self) -> &str {
        self.model.rel_path()
    }
}

pub(crate) struct App {
    pub store: ProjectStore,
    pub settings: AppSettings,
    pub sessions: Vec<Session>,
    pub editors: Vec<Editor>,
    pub current: Option<usize>,
    pub next_uid: u64,
    /// The application is closing: the window may close.
    pub closing: bool,
    /// Panels shown as tabs of the dock (bit i: panel i).
    pub panel_mask: u32,
    /// Panels in the bottom dock (bit i: panel i).
    pub panel_bottom_mask: u32,
    /// Panels detached in a window of their own (bit i: panel i).
    pub panel_float_mask: u32,
    pub floats: std::collections::HashMap<i32, crate::FloatPanelWindow>,
    /// Files to open again once their project has been read (last session).
    pub pending_open: Vec<babylon_core::app_settings::OpenFile>,
    pub tree_model: Rc<VecModel<TreeRow>>,
    pub tab_model: Rc<VecModel<TabItem>>,
    pub empty_rows: Rc<VecModel<RowItem>>,
    pub entries: Vec<tree::TreeEntry>,
    /// Ids of the saved projects listed in the File menu.
    pub saved_ids: Vec<String>,
    pub glossary_view: panels::GlossaryView,
    pub search: panels::SearchState,
    pub deepl: deepl_service::DeepLState,
    pub import: import::ImportState,
    pub report: report::ReportState,
    pub project_dialog: Option<project_dialog::ProjectDialogState>,
    pub mass: mass::MassState,
    pub scripts: panels::ScriptsState,
    pub dialog: panels::DialogView,
    pub pipboy: panels::PipboyState,
    pub archive: archive::ArchiveState,
    pub archive_timer: slint::Timer,
    pub quality: panels::QualityState,
    pub dialogs: dialogs::Dialogs,
    pub suggestions: Vec<MemoryMatch>,
    pub todo_items: Vec<TodoItem>,
    pub stats_files: Vec<String>,
    pub highlight_timer: slint::Timer,
    pub check_timer: slint::Timer,
    pub stats_timer: slint::Timer,
    pub weak: slint::Weak<MainWindow>,
}

thread_local! {
    static APP: RefCell<Option<App>> = const { RefCell::new(None) };
}

/// Runs `f` on the application state (UI thread only). `None` when the state is already borrowed or gone.
pub(crate) fn with_app<R>(f: impl FnOnce(&mut App, &MainWindow) -> R) -> Option<R> {
    let window = APP.with(|cell| cell.try_borrow().ok().and_then(|g| g.as_ref().and_then(|a| a.weak.upgrade())))?;
    APP.with(|cell| {
        let mut guard = cell.try_borrow_mut().ok()?;
        let app = guard.as_mut()?;
        Some(f(app, &window))
    })
}

impl App {
    fn new(window: &MainWindow) -> App {
        let store = ProjectStore::default_location();
        let settings = AppSettings::load(&store.settings_file());
        let tree_model = Rc::new(VecModel::<TreeRow>::default());
        let tab_model = Rc::new(VecModel::<TabItem>::default());
        let empty_rows = Rc::new(VecModel::<RowItem>::default());
        window.set_tree(ModelRc::from(tree_model.clone()));
        window.set_tabs(ModelRc::from(tab_model.clone()));
        window.set_rows(ModelRc::from(empty_rows.clone()));
        window.set_info_actions(ModelRc::new(VecModel::<InfoAction>::default()));
        App {
            store,
            settings,
            sessions: Vec::new(),
            editors: Vec::new(),
            current: None,
            next_uid: 1,
            closing: false,
            panel_mask: 1,
            panel_bottom_mask: 0,
            panel_float_mask: 0,
            floats: Default::default(),
            pending_open: Vec::new(),
            tree_model,
            tab_model,
            empty_rows,
            entries: Vec::new(),
            saved_ids: Vec::new(),
            glossary_view: Default::default(),
            search: Default::default(),
            deepl: Default::default(),
            import: Default::default(),
            report: Default::default(),
            project_dialog: None,
            mass: Default::default(),
            scripts: Default::default(),
            dialog: Default::default(),
            pipboy: Default::default(),
            archive: Default::default(),
            archive_timer: slint::Timer::default(),
            quality: Default::default(),
            dialogs: Default::default(),
            suggestions: Vec::new(),
            todo_items: Vec::new(),
            stats_files: Vec::new(),
            highlight_timer: slint::Timer::default(),
            check_timer: slint::Timer::default(),
            stats_timer: slint::Timer::default(),
            weak: window.as_weak(),
        }
    }

    pub fn session_index(&self, id: &str) -> Option<usize> {
        self.sessions.iter().position(|s| s.id == id)
    }

    pub fn set_status(&self, window: &MainWindow, text: impl Into<String>) {
        window.set_status_text(text.into().into());
    }
}
