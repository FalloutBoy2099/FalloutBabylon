//! The panels of the dock (glossary, statistics, lines to review, suggestions...).

mod dialog_tree;
mod glossary;
mod pipboy;
mod quality;
mod scripts;
mod search;
mod stats;
mod suggestions;
mod todo;

pub(crate) use dialog_tree::DialogView;
pub(crate) use glossary::GlossaryView;
pub(crate) use pipboy::PipboyState;
pub(crate) use quality::QualityState;
pub(crate) use scripts::ScriptsState;
pub(crate) use search::SearchState;

use super::host::Host;
use super::App;
use crate::{FloatPanelWindow, MainWindow, TableRow};
use crate::app::with_app;
use babylon_core::i18n::tr;
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

/// The panels, in the order of the selector. The numbers are the values of `panel-index`.
pub(crate) const PANEL_COUNT: usize = 9;
pub(crate) const GLOSSARY: i32 = 0;
pub(crate) const STATS: i32 = 1;
pub(crate) const TODO: i32 = 2;
pub(crate) const SUGGESTIONS: i32 = 3;
pub(crate) const SEARCH: i32 = 4;
pub(crate) const QUALITY: i32 = 5;
pub(crate) const SCRIPTS: i32 = 6;
pub(crate) const DIALOG: i32 = 7;
pub(crate) const PIPBOY: i32 = 8;

pub fn panel_names() -> Vec<SharedString> {
    ["Glossary", "Statistics", "To review", "Suggestions", "Search and replace", "Quality report", "Texts in the scripts", "Dialogue tree", "Pip-Boy holodisks"].iter().map(|n| SharedString::from(tr(n))).collect()
}

/// The texts of the panels that the Rust code sets (again after a change of language).
pub(crate) fn localize_panels(window: &MainWindow) {
    localize_host(&Host::Main(window.clone_strong()));
    window.set_panel_names(ModelRc::new(VecModel::from(panel_names())));
}

pub(crate) fn localize_host(host: &Host) {
    glossary::localize(host);
    quality::localize(host);
}

/// Connects the callbacks of the panels of one window (the main window or a window of a detached panel).
pub(crate) fn wire_host(host: &Host) {
    localize_host(host);
    glossary::wire(host);
    stats::wire(host);
    todo::wire(host);
    suggestions::wire(host);
    search::wire(host);
    quality::wire(host);
    scripts::wire(host);
    dialog_tree::wire(host);
    pipboy::wire(host);
}

pub(crate) fn table_row(cells: Vec<String>) -> TableRow {
    TableRow {
        cells: ModelRc::new(VecModel::from(cells.into_iter().map(SharedString::from).collect::<Vec<_>>())),
        tint: 0,
        bold: false,
        muted: false,
        warn: false,
        checkable: false,
        checked: false,
    }
}

pub(crate) fn table_model(rows: Vec<TableRow>) -> ModelRc<TableRow> {
    ModelRc::new(VecModel::from(rows))
}

/// One line version of a text (a few words of it, for tables).
pub(crate) fn one_line(text: &str, max: usize) -> String {
    let flat = text.split_whitespace().collect::<Vec<_>>().join(" ");
    if flat.chars().count() > max {
        let cut: String = flat.chars().take(max).collect();
        format!("{cut}…")
    } else {
        flat
    }
}

impl App {
    /// The project the panels talk about: the one of the current tab, else the first one.
    pub fn active_session(&self) -> Option<usize> {
        self.current
            .and_then(|c| self.editors.get(c))
            .and_then(|e| self.session_index(&e.session))
            .or(if self.sessions.is_empty() { None } else { Some(0) })
    }

    /// The panels that are shown and where: the docks follow (a dock without panel is hidden), detached panels have
    /// their own window.
    pub fn apply_panel_state(&mut self, window: &MainWindow) {
        let visible: Vec<bool> = (0..PANEL_COUNT).map(|i| self.panel_mask & (1 << i) != 0).collect();
        let bottom: Vec<bool> = (0..PANEL_COUNT).map(|i| self.panel_bottom_mask & (1 << i) != 0).collect();
        let float: Vec<bool> = (0..PANEL_COUNT).map(|i| self.panel_float_mask & (1 << i) != 0).collect();
        let docked: Vec<bool> = (0..PANEL_COUNT).map(|i| visible[i] && !float[i]).collect();
        let in_dock = |i: usize, b: bool| docked[i] && bottom[i] == b;
        // the panel in front of each dock is one of the panels it holds
        for (b, index) in [(false, window.get_panel_index()), (true, window.get_bottom_index())] {
            let ok = usize::try_from(index).is_ok_and(|i| i < PANEL_COUNT && in_dock(i, b));
            if !ok {
                if let Some(first) = (0..PANEL_COUNT).find(|&i| in_dock(i, b)) {
                    if b {
                        window.set_bottom_index(first as i32);
                    } else {
                        window.set_panel_index(first as i32);
                    }
                }
            }
        }
        window.set_dock_open((0..PANEL_COUNT).any(|i| in_dock(i, false)));
        window.set_bottom_open((0..PANEL_COUNT).any(|i| in_dock(i, true)));
        window.set_panel_visible(ModelRc::new(VecModel::from(visible.clone())));
        window.set_panel_docked(ModelRc::new(VecModel::from(docked)));
        window.set_panel_bottom(ModelRc::new(VecModel::from(bottom)));
        // the View menu is built again so that its check marks follow
        window.set_panel_names(ModelRc::new(VecModel::from(panel_names())));
        self.sync_float_windows(window, &visible, &float);
    }

    /// One window per detached panel that is shown; the others are closed.
    fn sync_float_windows(&mut self, window: &MainWindow, visible: &[bool], float: &[bool]) {
        let names = panel_names();
        for i in 0..PANEL_COUNT {
            let want = visible[i] && float[i];
            let key = i as i32;
            if !want {
                if let Some(f) = self.floats.remove(&key) {
                    let _ = f.hide();
                }
                continue;
            }
            if let Some(f) = self.floats.get(&key) {
                f.set_name(names[i].clone());
                continue;
            }
            let Ok(f) = FloatPanelWindow::new() else { continue };
            f.set_panel(key);
            f.set_name(names[i].clone());
            wire_host(&Host::Float(f.clone_strong()));
            f.on_redock(move || {
                with_app(|app, w| app.dock_panel(w, key));
            });
            // closing the window hides the panel (not from inside its own handler)
            f.window().on_close_requested(move || {
                slint::Timer::single_shot(std::time::Duration::from_millis(0), move || {
                    with_app(|app, w| {
                        app.panel_mask &= !(1 << key);
                        app.panel_float_mask &= !(1 << key);
                        app.apply_panel_state(w);
                    });
                });
                slint::CloseRequestResponse::HideWindow
            });
            self.show_owned(&f, window);
            self.floats.insert(key, f);
            self.refresh_panel_index(window, key);
        }
    }

    /// Detaches a panel into a window of its own.
    pub fn detach_panel(&mut self, window: &MainWindow, index: i32) {
        if !(0..PANEL_COUNT as i32).contains(&index) {
            return;
        }
        self.panel_float_mask |= 1 << index;
        self.panel_mask |= 1 << index;
        self.apply_panel_state(window);
        self.refresh_panel(window);
    }

    /// Brings a detached panel back to the right dock.
    pub fn dock_panel(&mut self, window: &MainWindow, index: i32) {
        self.panel_float_mask &= !(1 << index);
        self.panel_bottom_mask &= !(1 << index);
        self.show_panel(window, index);
    }

    /// Shows a panel (and brings it to the front of its dock).
    pub fn show_panel(&mut self, window: &MainWindow, index: i32) {
        if !(0..PANEL_COUNT as i32).contains(&index) {
            return;
        }
        self.panel_mask |= 1 << index;
        if self.panel_float_mask & (1 << index) != 0 {
            // a detached panel has its window: nothing to bring to the front here
        } else if self.panel_bottom_mask & (1 << index) != 0 {
            window.set_bottom_index(index);
        } else {
            window.set_panel_index(index);
        }
        self.apply_panel_state(window);
        self.refresh_panel(window);
    }

    /// The View menu: shows or hides a panel.
    pub fn toggle_panel(&mut self, window: &MainWindow, index: i32) {
        if !(0..PANEL_COUNT as i32).contains(&index) {
            return;
        }
        if self.panel_mask & (1 << index) != 0 {
            self.panel_mask &= !(1 << index);
            self.apply_panel_state(window);
            self.refresh_panel(window);
        } else {
            self.show_panel(window, index);
        }
    }

    /// Moves a panel to the other dock (right <-> bottom) and shows it there.
    pub fn move_panel(&mut self, window: &MainWindow, index: i32) {
        if !(0..PANEL_COUNT as i32).contains(&index) {
            return;
        }
        self.panel_float_mask &= !(1 << index);
        self.panel_bottom_mask ^= 1 << index;
        self.show_panel(window, index);
    }

    /// Refreshes the panels that are on screen (the one in front of each dock).
    pub fn refresh_panel(&mut self, window: &MainWindow) {
        if window.get_dock_open() {
            self.refresh_panel_index(window, window.get_panel_index());
        }
        if window.get_bottom_open() && window.get_bottom_index() != window.get_panel_index() {
            self.refresh_panel_index(window, window.get_bottom_index());
        }
        let detached: Vec<i32> = self.floats.keys().copied().collect();
        for i in detached {
            self.refresh_panel_index(window, i);
        }
    }

    fn refresh_panel_index(&mut self, window: &MainWindow, index: i32) {
        match index {
            GLOSSARY => self.refresh_glossary_panel(window),
            STATS => self.refresh_stats_panel(window),
            TODO => self.refresh_todo_panel(window),
            SUGGESTIONS => self.refresh_suggestions_panel(window),
            QUALITY => self.refresh_quality_panel(window),
            SCRIPTS => self.refresh_scripts_panel(window),
            DIALOG => self.refresh_dialog_panel(window),
            PIPBOY => self.refresh_pipboy_panel(window),
            _ => {}
        }
    }

    pub fn wire_panels(window: &MainWindow) {
        wire_host(&Host::Main(window.clone_strong()));
        window.set_panel_names(ModelRc::new(VecModel::from(panel_names())));
    }
}
