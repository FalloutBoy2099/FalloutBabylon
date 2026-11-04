//! Lines marked "to review", in every file of the project.

use super::{one_line, table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::project::TodoItem;
use babylon_core::statistics;
use std::collections::HashSet;

pub(super) fn wire(host: &Host) {
    host.todo().on_activated(|i| {
        with_app(|app, w| {
            let (Some(si), Some(item)) = (app.active_session(), app.todo_items.get(i as usize).cloned()) else { return };
            let id = app.sessions[si].id.clone();
            app.open_file(w, &id, &item.rel_path);
            app.select_id(w, item.id);
        });
    });
}

impl App {
    pub fn refresh_todo_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::TODO, window);
        let data = host.todo();
        let Some(si) = self.active_session() else {
            self.todo_items.clear();
            data.set_rows(table_model(Vec::new()));
            data.set_summary(tr("No project open.").into());
            return;
        };
        let session = &self.sessions[si];
        // open files hold their own, possibly unsaved, marks
        let mut open: HashSet<String> = HashSet::new();
        let mut items: Vec<TodoItem> = Vec::new();
        for ed in self.editors.iter().filter(|e| e.session == session.id) {
            open.insert(ed.rel_path().to_string());
            for row in ed.model.rows().iter().filter(|r| r.review) {
                items.push(TodoItem {
                    rel_path: ed.rel_path().to_string(),
                    id: row.id,
                    source: row.source.clone(),
                    target: row.target.clone(),
                });
            }
        }
        items.extend(session.project.review_items(&open));
        items.sort_by(|a, b| a.rel_path.cmp(&b.rel_path).then(a.id.cmp(&b.id)));
        data.set_summary(
            if items.is_empty() {
                tr("Nothing to review. Mark lines with Ctrl+R.")
            } else {
                trf("%1 line(s) to review — double-click to open", &[&items.len().to_string()])
            }
            .into(),
        );
        let rows = items
            .iter()
            .map(|t| {
                table_row(vec![
                    statistics::display_name_for(&t.rel_path),
                    t.id.to_string(),
                    one_line(&t.source, 160),
                    one_line(&t.target, 160),
                ])
            })
            .collect();
        data.set_rows(table_model(rows));
        self.todo_items = items;
    }
}
