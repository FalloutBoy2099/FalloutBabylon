//! Suggestions from the translation memory of the project for the current line.

use super::{table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::statistics;

const MAX_SUGGESTIONS: usize = 8;

fn flat(text: &str) -> String {
    text.replace('\n', " ⏎ ")
}

pub(super) fn wire(host: &Host) {
    let data = host.suggestions();
    data.on_minimum_changed(|v| {
        with_app(|app, w| {
            app.settings.suggestion_minimum = v;
            app.refresh_suggestions_panel(w);
        });
    });
    data.on_activated(|i| {
        with_app(|app, w| {
            if let Some(m) = app.suggestions.get(i as usize).cloned() {
                app.apply_suggestion(w, &m.target, m.score);
            }
        });
    });
}

impl App {
    pub fn refresh_suggestions_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::SUGGESTIONS, window);
        let data = host.suggestions();
        data.set_minimum(self.settings.suggestion_minimum.clamp(40, 100));
        self.suggestions.clear();
        data.set_rows(table_model(Vec::new()));

        let Some(ed) = self.current_editor() else {
            data.set_status(tr("Select a line to see what was already translated for the same or a similar text.").into());
            return;
        };
        let Some(row) = ed.current.map(|r| ed.model.row(r)) else {
            data.set_status(tr("Select a line to see what was already translated for the same or a similar text.").into());
            return;
        };
        let Some(si) = self.session_index(&ed.session) else { return };
        let Some(index) = self.sessions[si].index.clone() else {
            data.set_status(tr("Building the translation memory of the project...").into());
            return;
        };
        if !statistics::is_translatable(&row.source) {
            data.set_status(tr("This line has nothing to translate.").into());
            return;
        }
        let minimum = self.settings.suggestion_minimum.clamp(40, 100) as f64 / 100.0;
        let matches = index.find(&row.source, MAX_SUGGESTIONS, minimum, ed.rel_path(), row.id);
        let rows = matches
            .iter()
            .map(|m| {
                let mut origin = format!("{}#{}", statistics::display_name_for(&m.rel_path), m.id);
                if m.count > 1 {
                    origin.push_str(&format!(" (×{})", m.count));
                }
                table_row(vec![
                    (if m.score >= 1.0 { 100 } else { (m.score * 100.0) as i32 }).to_string(),
                    flat(&m.target),
                    flat(&m.source),
                    origin,
                ])
            })
            .collect();
        data.set_status(
            if matches.is_empty() {
                tr("No similar translated text.")
            } else {
                trf("%1 suggestion(s): double-click to use one.", &[&matches.len().to_string()])
            }
            .into(),
        );
        data.set_rows(table_model(rows));
        self.suggestions = matches;
    }
}
