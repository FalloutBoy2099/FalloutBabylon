//! The project tree: one node per open project, then the file groups and the files.

use super::App;
use crate::{MainWindow, TreeRow};
use babylon_core::i18n::tr;
use babylon_core::project::FileKind;
use babylon_core::statistics::FileStats;

pub(crate) enum TreeEntry {
    Project(usize),
    Group(usize, usize),
    File(usize, String),
}

fn group_of(kind: i32) -> usize {
    match kind {
        k if k == FileKind::Game as i32 => 0,
        k if k == FileKind::Dialog as i32 => 1,
        _ => 2,
    }
}

impl App {
    pub fn rebuild_tree(&mut self, window: &MainWindow) {
        let needle = window.get_tree_filter_text().trim().to_lowercase();
        let filter = window.get_tree_filter_index();
        let accepts = |s: &FileStats| -> bool {
            let by_state = match filter {
                1 => s.is_untouched(),
                2 => s.is_partial(),
                3 => s.is_complete(),
                4 => s.review > 0,
                _ => true,
            };
            by_state && (needle.is_empty() || s.display_name.to_lowercase().contains(&needle) || s.rel_path.to_lowercase().contains(&needle))
        };
        let filtering = filter != 0 || !needle.is_empty();
        let titles = [tr("1 · Game files — do first"), tr("2 · Dialogues"), tr("3 · Other files")];
        let mut rows = Vec::new();
        self.entries.clear();
        for (si, session) in self.sessions.iter().enumerate() {
            let mut total = FileStats::default();
            for s in &session.stats {
                total.add(s);
            }
            let any_dirty = self.editors.iter().any(|e| e.session == session.id && e.model.is_dirty());
            rows.push(TreeRow {
                label: session.project.config().name.as_str().into(),
                detail: if session.stats.is_empty() { "…".into() } else { format!("{:.0}%", total.percent()).into() },
                indent: 0,
                percent: total.percent() as f32,
                kind: 0,
                expanded: session.expanded,
                current: false,
                open: false,
                dirty: any_dirty,
            });
            self.entries.push(TreeEntry::Project(si));
            if !session.expanded {
                continue;
            }
            for (g, title) in titles.iter().enumerate() {
                let members: Vec<&FileStats> = session.stats.iter().filter(|s| group_of(s.kind) == g && accepts(s)).collect();
                if members.is_empty() {
                    continue;
                }
                let mut sum = FileStats::default();
                for m in &members {
                    sum.add(m);
                }
                rows.push(TreeRow {
                    label: format!("{title} ({})", members.len()).into(),
                    detail: format!("{:.0}%", sum.percent()).into(),
                    indent: 1,
                    percent: sum.percent() as f32,
                    kind: 1,
                    expanded: session.groups[g] || filtering,
                    current: false,
                    open: false,
                    dirty: false,
                });
                self.entries.push(TreeEntry::Group(si, g));
                if !session.groups[g] && !filtering {
                    continue;
                }
                for m in members {
                    let editor = self.editors.iter().position(|e| e.session == session.id && e.rel_path() == m.rel_path);
                    let current = editor.is_some() && editor == self.current;
                    rows.push(TreeRow {
                        label: m.display_name.as_str().into(),
                        detail: format!("{:.0}%", m.percent()).into(),
                        indent: 2,
                        percent: m.percent() as f32,
                        kind: 2,
                        expanded: false,
                        current,
                        open: editor.is_some(),
                        dirty: editor.is_some_and(|i| self.editors[i].model.is_dirty()),
                    });
                    self.entries.push(TreeEntry::File(si, m.rel_path.clone()));
                }
            }
        }
        self.tree_model.set_vec(rows);
    }

    /// The context menu of a project node.
    pub fn project_menu(&mut self, window: &MainWindow, row: usize, action: i32) {
        let Some(TreeEntry::Project(si)) = self.entries.get(row) else { return };
        let id = self.sessions[*si].id.clone();
        match action {
            0 => self.start_analysis(window, &id, false),
            1 => self.start_analysis(window, &id, true),
            2 => self.export_to_dat(window, &id),
            3 => self.open_output_folder(&id),
            4 => self.edit_project_settings(window, &id),
            5 => {
                self.close_project(window, &id);
            }
            _ => {}
        }
    }

    pub fn tree_activated(&mut self, window: &MainWindow, index: usize) {
        let Some(entry) = self.entries.get(index) else { return };
        match entry {
            TreeEntry::Project(si) => {
                let si = *si;
                self.sessions[si].expanded = !self.sessions[si].expanded;
                self.rebuild_tree(window);
            }
            TreeEntry::Group(si, g) => {
                let (si, g) = (*si, *g);
                self.sessions[si].groups[g] = !self.sessions[si].groups[g];
                self.rebuild_tree(window);
            }
            TreeEntry::File(si, rel) => {
                let (id, rel) = (self.sessions[*si].id.clone(), rel.clone());
                self.open_file(window, &id, &rel);
            }
        }
    }
}
