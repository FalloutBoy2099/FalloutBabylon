//! Open files: the table of lines, the two line editors and the commands that act on them.

use super::{with_app, App, Editor, InfoKind};
use crate::{Colors, InfoAction, MainWindow, RowItem, TabItem};
use babylon_core::doc_model::DocModel;
use babylon_core::grammar::{engine_for, GrammarClient, GrammarIssue};
use babylon_core::i18n::{tr, trf};
use babylon_core::project::SegmentStatus;
use babylon_core::statistics;
use slint::{ComponentHandle, Model, ModelRc, SharedString, TimerMode, VecModel};
use std::collections::BTreeSet;
use std::rc::Rc;
use std::time::Duration;

const PAGE_ROWS: i32 = 15;

/// One line preview of a text for the table.
fn preview(text: &str) -> SharedString {
    let flat: String = text.chars().map(|c| if c == '\n' || c == '\r' { ' ' } else { c }).collect();
    flat.into()
}

fn status_code(status: SegmentStatus) -> i32 {
    match status {
        SegmentStatus::Untranslated => 0,
        SegmentStatus::Translated => 1,
        SegmentStatus::ToReview => 2,
    }
}

/// Replaces the contents of a model, touching only the rows that differ when the size is the same (the
/// list keeps its scroll position).
pub(crate) fn sync_model<T: Clone + PartialEq + 'static>(model: &VecModel<T>, rows: Vec<T>) {
    if model.row_count() == rows.len() {
        for (i, row) in rows.into_iter().enumerate() {
            if model.row_data(i).as_ref() != Some(&row) {
                model.set_row_data(i, row);
            }
        }
    } else {
        model.set_vec(rows);
    }
}

fn filter_accepts(model: &DocModel, i: usize, filter: usize, search: &str) -> bool {
    let row = model.row(i);
    let validated = row.validated != 0 && !row.changed_since_validation();
    let ok = match filter {
        0 => true,
        1 => model.status(i) == SegmentStatus::Untranslated,
        2 => model.status(i) == SegmentStatus::Translated,
        3 => model.status(i) == SegmentStatus::ToReview,
        4 => model.issue_count(i) > 0,
        5 => validated,
        6 => !validated,
        7 => row.changed_since_validation(),
        8 => !row.comment.is_empty(),
        _ => true,
    };
    if !ok {
        return false;
    }
    if search.is_empty() {
        return true;
    }
    let needle = search.to_lowercase();
    row.source.to_lowercase().contains(&needle)
        || row.target.to_lowercase().contains(&needle)
        || row.id.to_string() == search
}

impl Editor {
    fn row_item(&self, i: usize) -> RowItem {
        let r = self.model.row(i);
        RowItem {
            row: i as i32,
            id: r.id,
            source: preview(&r.source),
            target: preview(&r.target),
            status: status_code(self.model.status(i)),
            issues: self.model.issue_count(i) as i32,
            comment: !r.comment.is_empty(),
            validated: if r.validated == 0 {
                0
            } else if r.changed_since_validation() {
                2
            } else {
                1
            },
            selected: self.selection.len() > 1 && self.selection.contains(&i),
        }
    }

    fn rebuild_visible(&mut self) {
        let (filter, search) = (self.filter, self.search.clone());
        self.visible = (0..self.model.len()).filter(|&i| filter_accepts(&self.model, i, filter, &search)).collect();
        if let Some((column, ascending)) = self.sort {
            let model = &self.model;
            self.visible.sort_by(|&a, &b| {
                let (ra, rb) = (model.row(a), model.row(b));
                let order = match column {
                    0 => ra.id.cmp(&rb.id),
                    1 => ra.source.to_lowercase().cmp(&rb.source.to_lowercase()),
                    2 => ra.target.to_lowercase().cmp(&rb.target.to_lowercase()),
                    _ => (model.status(a) as i32).cmp(&(model.status(b) as i32)),
                };
                // equal keys keep the order of the file
                let order = order.then(a.cmp(&b));
                if ascending {
                    order
                } else {
                    order.reverse().then(a.cmp(&b))
                }
            });
        }
        let items: Vec<RowItem> = self.visible.iter().map(|&i| self.row_item(i)).collect();
        sync_model(&self.rows, items);
    }

    fn position(&self, row: usize) -> Option<usize> {
        self.visible.iter().position(|&r| r == row)
    }

    pub(crate) fn refresh_row(&self, row: usize) {
        if let Some(pos) = self.position(row) {
            self.rows.set_row_data(pos, self.row_item(row));
        }
    }

    pub(crate) fn refresh_all_items(&self) {
        for (pos, &row) in self.visible.iter().enumerate() {
            self.rows.set_row_data(pos, self.row_item(row));
        }
    }

    /// The rows the commands act on: the selection, else the current line.
    fn target_rows(&self) -> Vec<usize> {
        if self.selection.len() > 1 {
            self.selection.iter().copied().collect()
        } else {
            self.current.into_iter().collect()
        }
    }
}

impl App {
    pub fn current_editor(&self) -> Option<&Editor> {
        self.current.and_then(|i| self.editors.get(i))
    }

    fn current_editor_mut(&mut self) -> Option<&mut Editor> {
        self.current.and_then(|i| self.editors.get_mut(i))
    }

    fn is_dark(window: &MainWindow) -> bool {
        window.global::<Colors>().get_dark()
    }

    fn glossary_of(&self, id: &str) -> Option<std::sync::Arc<babylon_core::glossary::Glossary>> {
        self.session_index(id).map(|i| self.sessions[i].project.glossary())
    }

    // ------------------------------------------------------------------------- tabs

    pub fn open_file(&mut self, window: &MainWindow, session: &str, rel: &str) {
        if let Some(i) = self.editors.iter().position(|e| e.session == session && e.rel_path() == rel) {
            self.select_tab(window, i);
            return;
        }
        let Some(si) = self.session_index(session) else { return };
        let project = self.sessions[si].project.clone();
        match project.load_document(rel) {
            Ok(doc) => {
                let model = DocModel::new(doc, project.glossary());
                let mut ed = Editor {
                    uid: self.next_uid,
                    session: session.to_string(),
                    model,
                    visible: Vec::new(),
                    filter: 0,
                    search: String::new(),
                    sort: None,
                    current: None,
                    selection: BTreeSet::new(),
                    anchor: None,
                    rows: Rc::new(VecModel::default()),
                    grammar: Vec::new(),
                    grammar_serial: 0,
                    actions: Vec::new(),
                    shown_dirty: false,
                };
                self.next_uid += 1;
                ed.rebuild_visible();
                if let Some(&first) = ed.visible.first() {
                    ed.current = Some(first);
                }
                self.editors.push(ed);
                let index = self.editors.len() - 1;
                self.select_tab(window, index);
            }
            Err(e) => self.set_status(window, format!("{rel}: {e}")),
        }
    }

    pub fn select_tab(&mut self, window: &MainWindow, index: usize) {
        if index >= self.editors.len() {
            return;
        }
        self.current = Some(index);
        self.show_editor(window);
        self.rebuild_tree(window);
        self.update_summary(window);
    }

    pub fn close_tab(&mut self, window: &MainWindow, index: usize) {
        if index >= self.editors.len() {
            return;
        }
        let uid = self.editors[index].uid;
        self.confirm_close(window, vec![uid], move |app, w| {
            let Some(index) = app.editors.iter().position(|e| e.uid == uid) else { return };
            app.editors.remove(index);
            app.current = if app.editors.is_empty() {
                None
            } else {
                Some(match app.current {
                    Some(c) if c > index => c - 1,
                    Some(c) if c == index => index.min(app.editors.len() - 1),
                    Some(c) => c,
                    None => 0,
                })
            };
            app.show_editor(w);
            app.rebuild_tree(w);
            app.update_summary(w);
        });
    }

    /// The context menu of a tab: close it, the others, the ones on one side, or all of them.
    pub fn tab_menu(&mut self, window: &MainWindow, index: usize, action: i32) {
        if index >= self.editors.len() {
            return;
        }
        let uids: Vec<u64> = self
            .editors
            .iter()
            .enumerate()
            .filter(|(i, _)| match action {
                0 => *i == index,
                1 => *i != index,
                2 => *i < index,
                3 => *i > index,
                _ => true,
            })
            .map(|(_, e)| e.uid)
            .collect();
        self.close_tabs(window, uids);
    }

    /// Closes several tabs at once (one question for the modified ones).
    pub fn close_tabs(&mut self, window: &MainWindow, uids: Vec<u64>) {
        if uids.is_empty() {
            return;
        }
        let list = uids.clone();
        self.confirm_close(window, uids, move |app, w| {
            // the tab in front stays in front when it survives, else its neighbour
            let front = app.current.and_then(|c| app.editors.get(c)).map(|e| e.uid);
            let position = app.current.unwrap_or(0);
            app.editors.retain(|e| !list.contains(&e.uid));
            app.current = front
                .and_then(|u| app.editors.iter().position(|e| e.uid == u))
                .or(if app.editors.is_empty() { None } else { Some(position.min(app.editors.len() - 1)) });
            app.show_editor(w);
            app.rebuild_tree(w);
            app.update_summary(w);
        });
    }

    pub fn close_current_tab(&mut self, window: &MainWindow) {
        if let Some(i) = self.current {
            self.close_tab(window, i);
        }
    }

    fn tab_title(&self, ed: &Editor) -> String {
        let name = statistics::display_name_for(ed.rel_path());
        match self.session_index(&ed.session) {
            Some(si) if self.sessions.len() > 1 => format!("{name} — {}", self.sessions[si].project.config().name),
            _ => name,
        }
    }

    pub fn update_tabs(&self) {
        let tabs: Vec<TabItem> = self
            .editors
            .iter()
            .map(|e| TabItem { title: self.tab_title(e).into(), tooltip: e.rel_path().into(), dirty: e.model.is_dirty() })
            .collect();
        sync_model(&self.tab_model, tabs);
    }

    /// Shows the current editor in the window.
    pub fn show_editor(&mut self, window: &MainWindow) {
        self.update_tabs();
        window.set_current_tab(self.current.map_or(-1, |c| c as i32));
        let Some(ed) = self.current_editor() else {
            window.set_has_editor(false);
            window.set_rows(ModelRc::from(self.empty_rows.clone()));
            window.set_line_info("".into());
            window.set_can_undo(false);
            window.set_can_redo(false);
            self.show_current_row(window);
            return;
        };
        let (source_lang, target_lang) = self
            .session_index(&ed.session)
            .map(|i| {
                let c = self.sessions[i].project.config();
                (c.source_language.clone(), c.target_language.clone())
            })
            .unwrap_or_default();
        window.set_has_editor(true);
        window.set_rows(ModelRc::from(ed.rows.clone()));
        window.set_filter_index(ed.filter as i32);
        window.set_sort_column(ed.sort.map_or(-1, |(c, _)| c as i32));
        window.set_sort_ascending(ed.sort.is_none_or(|(_, a)| a));
        window.set_search_text(ed.search.as_str().into());
        window.set_source_label(trf("Source (%1)", &[&source_lang]).into());
        window.set_target_label(trf("Translation (%1)", &[&target_lang]).into());
        self.show_current_row(window);
    }

    fn update_line_info(&self, window: &MainWindow) {
        let Some(ed) = self.current_editor() else { return };
        let total = ed.model.len();
        let text = if ed.visible.len() == total {
            trf("%1 lines", &[&total.to_string()])
        } else {
            trf("%1 / %2 lines", &[&ed.visible.len().to_string(), &total.to_string()])
        };
        window.set_line_info(text.into());
    }

    // ------------------------------------------------------------------------- filter and selection

    pub fn filter_changed(&mut self, window: &MainWindow) {
        let filter = window.get_filter_index().max(0) as usize;
        let search = window.get_search_text().to_string();
        let Some(ed) = self.current_editor_mut() else { return };
        ed.filter = filter;
        ed.search = search;
        ed.rebuild_visible();
        // keep the current line when it is still listed, otherwise the first one
        if ed.current.is_none_or(|c| ed.position(c).is_none()) {
            ed.current = ed.visible.first().copied();
            ed.selection.clear();
        }
        self.show_current_row(window);
    }

    /// A click on a title of the table: sort by that column (a second click reverses the order).
    pub fn sort_by(&mut self, window: &MainWindow, column: usize) {
        let Some(ed) = self.current_editor_mut() else { return };
        ed.sort = match ed.sort {
            Some((c, asc)) if c == column => Some((c, !asc)),
            _ => Some((column, true)),
        };
        ed.rebuild_visible();
        self.show_editor(window);
    }

    pub fn select_position(&mut self, window: &MainWindow, pos: usize) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(&row) = ed.visible.get(pos) else { return };
        ed.current = Some(row);
        ed.selection.clear();
        ed.anchor = Some(pos);
        ed.grammar.clear();
        self.show_current_row(window);
        self.schedule_check(window, 150);
    }

    pub fn row_clicked(&mut self, window: &MainWindow, pos: usize, ctrl: bool, shift: bool) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(&row) = ed.visible.get(pos) else { return };
        if shift {
            let anchor = ed.anchor.unwrap_or(pos).min(ed.visible.len().saturating_sub(1));
            let (a, b) = (anchor.min(pos), anchor.max(pos));
            ed.selection = ed.visible[a..=b].iter().copied().collect();
            ed.current = Some(row);
        } else if ctrl {
            if ed.selection.is_empty() {
                if let Some(c) = ed.current {
                    ed.selection.insert(c);
                }
            }
            if !ed.selection.remove(&row) {
                ed.selection.insert(row);
            }
            ed.anchor = Some(pos);
            ed.current = Some(row);
        } else {
            self.select_position(window, pos);
            return;
        }
        ed.grammar.clear();
        ed.refresh_all_items();
        self.show_current_row(window);
        self.schedule_check(window, 150);
    }

    pub fn step(&mut self, window: &MainWindow, delta: i32) {
        let Some(ed) = self.current_editor() else { return };
        if ed.visible.is_empty() {
            return;
        }
        let pos = ed.current.and_then(|c| ed.position(c)).map_or(0, |p| p as i32 + delta);
        let pos = pos.clamp(0, ed.visible.len() as i32 - 1) as usize;
        self.select_position(window, pos);
    }

    pub fn page(&mut self, window: &MainWindow, direction: i32) {
        self.step(window, direction * PAGE_ROWS);
    }

    pub fn go_untranslated(&mut self, window: &MainWindow, forward: bool) {
        let Some(ed) = self.current_editor() else { return };
        let count = ed.visible.len() as i32;
        if count == 0 {
            return;
        }
        let start = ed.current.and_then(|c| ed.position(c)).map_or(if forward { -1 } else { count }, |p| p as i32);
        for step in 1..=count {
            let pos = ((if forward { start + step } else { start - step }) % count + count) % count;
            if ed.model.status(ed.visible[pos as usize]) == SegmentStatus::Untranslated {
                self.select_position(window, pos as usize);
                window.invoke_focus_target();
                return;
            }
        }
    }

    // ------------------------------------------------------------------------- the current line

    /// Fills the line editors and the flags of the toolbar from the current line.
    pub fn show_current_row(&mut self, window: &MainWindow) {
        self.update_line_info(window);
        let dark = Self::is_dark(window);
        let Some(ci) = self.current else {
            self.clear_line(window);
            return;
        };
        let Some(ed) = self.editors.get(ci) else {
            self.clear_line(window);
            return;
        };
        window.set_can_undo(ed.model.can_undo());
        window.set_can_redo(ed.model.can_redo());
        window.set_undo_label(ed.model.undo_label().into());
        window.set_redo_label(ed.model.redo_label().into());
        let Some(row_index) = ed.current else {
            self.clear_line(window);
            return;
        };
        let pos = ed.position(row_index).map_or(-1, |p| p as i32);
        let row = ed.model.row(row_index);
        let glossary = self.glossary_of(&ed.session);
        let palette = crate::highlight::palette(dark);
        window.set_current_row(pos);
        window.set_source_text(row.source.as_str().into());
        window.set_target_text(row.target.as_str().into());
        let (source_spans, target_spans) = match &glossary {
            Some(g) => (
                crate::highlight::source_spans(g, &row.source, &row.target, palette),
                crate::highlight::target_spans(g, &row.target, &ed.grammar, palette),
            ),
            None => (Vec::new(), Vec::new()),
        };
        window.set_source_overlay(crate::highlight::styled(&row.source, &source_spans));
        window.set_target_overlay(crate::highlight::styled(&row.target, &target_spans));
        Self::set_tips(window, glossary.as_deref(), &row.source, &row.target, &ed.grammar);
        let messages = ed.model.issue_messages(row_index);
        window.set_issue_text(
            if messages.is_empty() { String::new() } else { format!("⚠ {} {}", tr("Glossary:"), messages.join("  |  ")) }.into(),
        );
        let previous = &row.previous;
        window.set_previous_text(
            if !previous.is_empty() && *previous != row.target { trf("Previous translation: %1", &[previous]) } else { String::new() }
                .into(),
        );
        window.set_comment_text(row.comment.as_str().into());
        window.set_review_on(row.review);
        window.set_validated_state(if row.validated == 0 {
            0
        } else if row.changed_since_validation() {
            2
        } else {
            1
        });
        window.set_editor_enabled(true);
        window.set_info_text("".into());
        window.set_info_actions(ModelRc::new(VecModel::<InfoAction>::default()));
        if let Some(ed) = self.current_editor_mut() {
            ed.actions.clear();
        }
        self.refresh_panel(window);
    }

    /// The tooltips of the two editors (glossary terms and problems).
    fn set_tips(window: &MainWindow, glossary: Option<&babylon_core::glossary::Glossary>, source: &str, target: &str, grammar: &[GrammarIssue]) {
        let (source_tip, target_tip) = match glossary {
            Some(g) => (crate::highlight::tooltip(g, source, true, &[]), crate::highlight::tooltip(g, target, false, grammar)),
            None => (None, None),
        };
        window.set_source_has_tip(source_tip.is_some());
        window.set_source_tip(source_tip.unwrap_or_default());
        window.set_target_has_tip(target_tip.is_some());
        window.set_target_tip(target_tip.unwrap_or_default());
    }

    fn clear_line(&self, window: &MainWindow) {
        window.set_source_has_tip(false);
        window.set_target_has_tip(false);
        window.set_current_row(-1);
        window.set_source_text("".into());
        window.set_target_text("".into());
        window.set_source_overlay(Default::default());
        window.set_target_overlay(Default::default());
        window.set_issue_text("".into());
        window.set_previous_text("".into());
        window.set_comment_text("".into());
        window.set_review_on(false);
        window.set_validated_state(0);
        window.set_editor_enabled(false);
        window.set_info_text("".into());
        window.set_info_actions(ModelRc::new(VecModel::<InfoAction>::default()));
    }

    /// Recomputes the glossary / grammar underlines of the two editors (text unchanged).
    pub fn refresh_overlays(&mut self, window: &MainWindow) {
        let dark = Self::is_dark(window);
        let Some(ed) = self.current_editor() else { return };
        let Some(row_index) = ed.current else { return };
        let row = ed.model.row(row_index);
        let Some(glossary) = self.glossary_of(&ed.session) else { return };
        let palette = crate::highlight::palette(dark);
        // The editor may hold text the table does not know yet (typing): take what is in the window.
        let target = window.get_target_text().to_string();
        let source_spans = crate::highlight::source_spans(&glossary, &row.source, &target, palette);
        let target_spans = crate::highlight::target_spans(&glossary, &target, &ed.grammar, palette);
        window.set_source_overlay(crate::highlight::styled(&row.source, &source_spans));
        window.set_target_overlay(crate::highlight::styled(&target, &target_spans));
        Self::set_tips(window, Some(&glossary), &row.source, &target, &ed.grammar);
        let messages = ed.model.issue_messages(row_index);
        window.set_issue_text(
            if messages.is_empty() { String::new() } else { format!("⚠ {} {}", tr("Glossary:"), messages.join("  |  ")) }.into(),
        );
    }

    pub fn refresh_all_row_items(&mut self) {
        for ed in &self.editors {
            ed.refresh_all_items();
        }
    }

    pub fn schedule_highlight(&mut self, _window: &MainWindow) {
        self.highlight_timer.start(TimerMode::SingleShot, Duration::from_millis(220), || {
            with_app(|app, w| app.refresh_overlays(w));
        });
    }

    pub fn schedule_stats(&mut self) {
        self.stats_timer.start(TimerMode::SingleShot, Duration::from_millis(300), || {
            with_app(|app, w| app.refresh_stats_of_current(w));
        });
    }

    fn refresh_stats_of_current(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor() else { return };
        let fresh = ed.model.stats();
        let id = ed.session.clone();
        if let Some(si) = self.session_index(&id) {
            if let Some(slot) = self.sessions[si].stats.iter_mut().find(|s| s.rel_path == fresh.rel_path) {
                *slot = fresh;
            }
        }
        self.rebuild_tree(window);
        self.update_summary(window);
        self.refresh_panel(window);
    }

    /// Called after every change of the document: tabs, tree marks and statistics.
    pub(crate) fn document_changed(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let dirty = ed.model.is_dirty();
        let dirty_changed = dirty != ed.shown_dirty;
        ed.shown_dirty = dirty;
        window.set_can_undo(ed.model.can_undo());
        window.set_can_redo(ed.model.can_redo());
        self.update_tabs();
        if dirty_changed {
            self.rebuild_tree(window);
        }
        self.schedule_stats();
    }

    // ------------------------------------------------------------------------- editing

    pub fn target_edited(&mut self, window: &MainWindow, text: &str) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.current else { return };
        ed.model.set_target(row, text);
        ed.grammar.clear(); // their offsets no longer match the text
        ed.refresh_row(row);
        window.set_review_on(ed.model.row(row).review);
        // stale underlines would be misplaced: show none until the timer redraws them
        window.set_target_overlay(Default::default());
        self.document_changed(window);
        self.schedule_highlight(window);
        self.schedule_check(window, 700);
    }

    pub fn comment_edited(&mut self, window: &MainWindow, text: &str) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.current else { return };
        ed.model.set_comment(row, text);
        ed.refresh_row(row);
        self.document_changed(window);
    }

    /// Replaces the translation of the current line from the application (not typed): the window follows.
    fn apply_target(&mut self, window: &MainWindow, text: &str, caret: Option<usize>) {
        window.set_target_text(text.into());
        if let Some(c) = caret {
            window.invoke_set_target_caret(c as i32);
        }
        self.target_edited(window, text);
    }

    pub fn accept_as_is(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.current else { return };
        ed.model.operation(&tr("Accept as is"), |m| m.confirm(row));
        ed.refresh_row(row);
        self.document_changed(window);
        self.show_current_row(window);
    }

    pub fn copy_source(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.current else { return };
        let source = ed.model.row(row).source.clone();
        ed.model.operation(&tr("Copy the source"), |m| m.set_target(row, &source));
        ed.refresh_row(row);
        self.document_changed(window);
        self.show_current_row(window);
        window.invoke_focus_target();
    }

    pub fn restore_previous(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.current else { return };
        let previous = ed.model.row(row).previous.clone();
        if previous.is_empty() {
            return;
        }
        ed.model.operation(&tr("Previous translation restored"), |m| m.set_target(row, &previous));
        ed.refresh_row(row);
        self.document_changed(window);
        self.show_current_row(window);
    }

    pub fn toggle_review(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let rows = ed.target_rows();
        ed.model.toggle_review(&rows);
        for &r in &rows {
            ed.refresh_row(r);
        }
        self.document_changed(window);
        self.show_current_row(window);
    }

    pub fn toggle_validated(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let rows = ed.target_rows();
        ed.model.toggle_validated(&rows);
        for &r in &rows {
            ed.refresh_row(r);
        }
        self.document_changed(window);
        self.show_current_row(window);
    }

    pub fn undo_operation(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let changed = ed.model.undo();
        self.after_history(window, changed);
    }

    pub fn redo_operation(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor_mut() else { return };
        let changed = ed.model.redo();
        self.after_history(window, changed);
    }

    fn after_history(&mut self, window: &MainWindow, changed: Option<babylon_core::doc_model::Changed>) {
        let Some(changed) = changed else { return };
        let Some(ed) = self.current_editor_mut() else { return };
        if changed.reset {
            ed.rebuild_visible();
            if ed.current.is_some_and(|c| c >= ed.model.len()) {
                ed.current = ed.visible.first().copied();
            }
            ed.selection.clear();
        } else {
            for &r in &changed.rows {
                ed.refresh_row(r);
            }
        }
        self.document_changed(window);
        self.show_current_row(window);
        self.set_status(window, "");
    }

    // ------------------------------------------------------------------------- caret information

    pub fn target_caret(&mut self, window: &MainWindow, offset: usize) {
        let Some(ed) = self.current_editor() else { return };
        let text = window.get_target_text().to_string();
        let mut info = String::new();
        let mut actions: Vec<InfoKind> = Vec::new();
        if let Some(issue) = ed.grammar.iter().find(|g| offset >= g.start && offset <= g.start + g.length) {
            info = issue.message.clone();
            for s in issue.suggestions.iter().take(4) {
                actions.push(InfoKind::Replace { start: issue.start, len: issue.length, text: s.clone() });
            }
        } else if let Some(glossary) = self.glossary_of(&ed.session) {
            for hit in glossary.find_in_target(&text) {
                if offset >= hit.m.start && offset <= hit.m.start + hit.m.length {
                    info = Self::term_info(&glossary.terms()[hit.term]);
                    break;
                }
            }
        }
        self.show_info(window, info, actions);
    }

    pub fn source_caret(&mut self, window: &MainWindow, offset: usize) {
        let Some(ed) = self.current_editor() else { return };
        let Some(row) = ed.current else { return };
        let source = ed.model.row(row).source.clone();
        let mut info = String::new();
        let mut actions: Vec<InfoKind> = Vec::new();
        if let Some(glossary) = self.glossary_of(&ed.session) {
            for hit in glossary.find_in_source(&source) {
                if offset >= hit.m.start && offset <= hit.m.start + hit.m.length {
                    let term = &glossary.terms()[hit.term];
                    info = Self::term_info(term);
                    for t in term.targets.iter().take(4) {
                        actions.push(InfoKind::Insert(t.clone()));
                    }
                    break;
                }
            }
        }
        self.show_info(window, info, actions);
    }

    fn term_info(term: &babylon_core::glossary::GlossaryTerm) -> String {
        let mut text = format!("{} ({})", term.source, term.category);
        if term.targets.is_empty() {
            text.push_str(" — ");
            text.push_str(&tr("Not translated in the game files yet"));
        } else {
            text.push_str(" → ");
            text.push_str(&term.targets.join(" / "));
            if term.conflict() {
                text.push_str("  ⚠ ");
                text.push_str(&tr("Inconsistent translations in the game files"));
            }
        }
        text
    }

    fn show_info(&mut self, window: &MainWindow, info: String, actions: Vec<InfoKind>) {
        let labels: Vec<InfoAction> = actions
            .iter()
            .map(|a| match a {
                InfoKind::Replace { text, .. } => InfoAction { label: trf("Replace with \"%1\"", &[text]).into() },
                InfoKind::Insert(text) => InfoAction { label: trf("Insert \"%1\" in the translation", &[text]).into() },
            })
            .collect();
        window.set_info_text(info.into());
        window.set_info_actions(ModelRc::new(VecModel::from(labels)));
        if let Some(ed) = self.current_editor_mut() {
            ed.actions = actions;
        }
    }

    /// Replaces in the translation of an open file (`None` when the file is not open).
    pub fn replace_in_open_editor(
        &mut self,
        window: &MainWindow,
        session_id: &str,
        rel: &str,
        options: &babylon_core::project_search::Options,
        replacement: &str,
        ids: &std::collections::HashSet<i32>,
    ) -> Option<usize> {
        let ci = self.editors.iter().position(|e| e.session == session_id && e.rel_path() == rel)?;
        let n = self.editors[ci].model.replace_in_target(options, replacement, ids);
        if n > 0 {
            self.editors[ci].refresh_all_items();
            if Some(ci) == self.current {
                self.document_changed(window);
                self.show_current_row(window);
            } else {
                self.update_tabs();
            }
        }
        Some(n)
    }

    /// The model was changed from outside the table (Pip-Boy panel): rows may have been added or removed.
    pub fn refresh_editor_after_external_change(&mut self, window: &MainWindow, keep_id: Option<i32>) {
        let Some(ed) = self.current_editor_mut() else { return };
        ed.current = keep_id.and_then(|id| ed.model.row_for_id(id)).or_else(|| ed.current.filter(|&c| c < ed.model.len()));
        ed.selection.clear();
        ed.rebuild_visible();
        self.document_changed(window);
        self.show_current_row(window);
    }

    /// Inserts text at the caret of the translation.
    pub fn insert_in_target(&mut self, window: &MainWindow, insert: &str) {
        if self.current_editor().and_then(|e| e.current).is_none() {
            return;
        }
        let text = window.get_target_text().to_string();
        let mut at = (window.invoke_current_target_caret().max(0) as usize).min(text.len());
        while !text.is_char_boundary(at) {
            at -= 1;
        }
        let new = format!("{}{}{}", &text[..at], insert, &text[at..]);
        self.apply_target(window, &new, Some(at + insert.len()));
        window.invoke_focus_target();
    }

    /// Selects the line with this id (the filter is cleared when it hides it).
    pub fn select_id(&mut self, window: &MainWindow, id: i32) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.model.row_for_id(id) else { return };
        if ed.position(row).is_none() {
            ed.filter = 0;
            ed.search.clear();
            ed.rebuild_visible();
            window.set_filter_index(0);
            window.set_search_text("".into());
        }
        let Some(pos) = ed.position(row) else { return };
        self.select_position(window, pos);
    }

    /// Uses a suggestion of the translation memory for the current line (below a perfect match: to review).
    pub fn apply_suggestion(&mut self, window: &MainWindow, translation: &str, score: f64) {
        let Some(ed) = self.current_editor_mut() else { return };
        let Some(row) = ed.current else { return };
        ed.model.apply_suggestion(row, translation, score);
        ed.refresh_row(row);
        self.document_changed(window);
        self.show_current_row(window);
        window.invoke_focus_target();
    }

    pub fn info_action(&mut self, window: &MainWindow, index: usize) {
        let text = window.get_target_text().to_string();
        let Some(ed) = self.current_editor() else { return };
        match ed.actions.get(index) {
            Some(InfoKind::Replace { start, len, text: replacement }) => {
                let end = (start + len).min(text.len());
                if *start > end || !text.is_char_boundary(*start) || !text.is_char_boundary(end) {
                    return;
                }
                let new = format!("{}{}{}", &text[..*start], replacement, &text[end..]);
                let caret = start + replacement.len();
                self.apply_target(window, &new, Some(caret));
            }
            Some(InfoKind::Insert(insert)) => {
                let mut at = (window.invoke_current_target_caret().max(0) as usize).min(text.len());
                while !text.is_char_boundary(at) {
                    at -= 1;
                }
                let insert = insert.clone();
                let new = format!("{}{}{}", &text[..at], insert, &text[at..]);
                self.apply_target(window, &new, Some(at + insert.len()));
            }
            None => {}
        }
        window.invoke_focus_target();
    }

    // ------------------------------------------------------------------------- grammar

    pub fn schedule_check(&mut self, _window: &MainWindow, delay_ms: u64) {
        self.check_timer.start(TimerMode::SingleShot, Duration::from_millis(delay_ms), || {
            with_app(|app, w| app.run_check(w));
        });
    }

    fn run_check(&mut self, window: &MainWindow) {
        let (grammalecte, language_tool, enabled) =
            (self.settings.grammalecte_url.clone(), self.settings.language_tool_url.clone(), self.settings.check_enabled);
        let Some(ci) = self.current else { return };
        let Some(si) = self.session_index(&self.editors[ci].session) else { return };
        let language = self.sessions[si].project.config().target_language.clone();
        let ed = &mut self.editors[ci];
        let Some(row) = ed.current else { return };
        let text = ed.model.row(row).target.clone();
        let engine = if enabled && !text.trim().is_empty() { engine_for(&language, &grammalecte, &language_tool) } else { None };
        let Some((engine, url)) = engine else {
            if !ed.grammar.is_empty() {
                ed.grammar.clear();
                self.refresh_overlays(window);
            }
            return;
        };
        ed.grammar_serial += 1;
        let (uid, serial) = (ed.uid, ed.grammar_serial);
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let result = GrammarClient::new().check(engine, &url, &language, &text).map_err(|e| e.to_string());
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.grammar_ready(w, uid, serial, &text, result));
            });
        });
    }

    fn grammar_ready(&mut self, window: &MainWindow, uid: u64, serial: u64, text: &str, result: Result<Vec<GrammarIssue>, String>) {
        let Some(ci) = self.editors.iter().position(|e| e.uid == uid) else { return };
        let is_current = Some(ci) == self.current;
        let ed = &mut self.editors[ci];
        // drop answers for text that has changed since (or for another line)
        let Some(row) = ed.current else { return };
        if serial != ed.grammar_serial || ed.model.row(row).target != text {
            return;
        }
        ed.grammar = result.unwrap_or_default();
        if is_current {
            self.refresh_overlays(window);
        }
    }

    // ------------------------------------------------------------------------- saving

    /// Writes a file; returns a message for the status bar.
    pub fn save_editor(&mut self, index: usize) -> Option<String> {
        let ed = self.editors.get_mut(index)?;
        let si = self.sessions.iter().position(|s| s.id == ed.session)?;
        let project = self.sessions[si].project.clone();
        match project.save_document(ed.model.doc(), true) {
            Ok(unencodable) => {
                ed.model.mark_saved();
                ed.shown_dirty = false;
                if let Some(index) = &self.sessions[si].index {
                    index.update_file(ed.model.doc());
                }
                let session_id = self.sessions[si].id.clone();
                let fresh = ed.model.stats();
                if let Some(slot) = self.sessions[si].stats.iter_mut().find(|s| s.rel_path == fresh.rel_path) {
                    *slot = fresh;
                }
                self.schedule_archive(&session_id);
                Some(if unencodable.is_empty() {
                    tr("Saved.")
                } else {
                    trf("Saved, but these characters cannot be written in the target encoding: %1", &[&unencodable])
                })
            }
            Err(e) => Some(format!("{}: {e}", tr("Save failed"))),
        }
    }

    pub fn save(&mut self, window: &MainWindow) {
        if let Some(i) = self.current {
            if let Some(message) = self.save_editor(i) {
                self.set_status(window, message);
            }
            self.update_tabs();
            self.rebuild_tree(window);
            self.update_summary(window);
        }
    }

    pub fn save_all(&mut self, window: &MainWindow) {
        let mut last = None;
        for i in 0..self.editors.len() {
            if self.editors[i].model.is_dirty() {
                last = self.save_editor(i);
            }
        }
        if let Some(message) = last {
            self.set_status(window, message);
        }
        self.update_tabs();
        self.rebuild_tree(window);
        self.update_summary(window);
    }

    /// Everything modified is written: used when the application closes.
    pub fn flush(&mut self) {
        for i in 0..self.editors.len() {
            if self.editors[i].model.is_dirty() {
                self.save_editor(i);
            }
        }
        for s in &self.sessions {
            s.project.save_state();
        }
        self.flush_archives();
        self.remember_layout();
        let _ = self.settings.save(&self.store.settings_file());
    }
}

/// The filters of the project tree.
pub fn tree_filter_names() -> Vec<SharedString> {
    ["All files", "Not translated", "Partially translated", "Complete", "To review"].iter().map(|s| SharedString::from(tr(s))).collect()
}

/// Used by the tests of this module and by the window wiring: the label of a filter.
pub fn filter_names() -> Vec<SharedString> {
    [
        "All lines",
        "Untranslated",
        "Translated",
        "To review",
        "Glossary alerts",
        "Validated",
        "Not validated",
        "Changed since validation",
        "With a comment",
    ]
    .iter()
    .map(|s| SharedString::from(tr(s)))
    .collect()
}
