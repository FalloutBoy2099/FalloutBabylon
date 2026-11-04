//! The statistics panel: progress by category, the current file, and the files left to do.

use super::{table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::project::FileKind;
use babylon_core::statistics::FileStats;

const MAX_FILES: usize = 300;

fn percent_text(s: &FileStats) -> String {
    let p = s.percent();
    format!("{:.*}%", if !(10.0..=90.0).contains(&p) { 1 } else { 0 }, p)
}

pub(super) fn wire(host: &Host) {
    let data = host.stats();
    data.on_toggled(|| {
        with_app(|app, w| app.refresh_stats_panel(w));
    });
    data.on_file_activated(|i| {
        with_app(|app, w| {
            let (Some(si), Some(rel)) = (app.active_session(), app.stats_files.get(i as usize).cloned()) else { return };
            let id = app.sessions[si].id.clone();
            app.open_file(w, &id, &rel);
        });
    });
}

impl App {
    pub fn refresh_stats_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::STATS, window);
        let data = host.stats();
        let Some(si) = self.active_session() else {
            data.set_title(tr("No project open.").into());
            data.set_categories(table_model(Vec::new()));
            data.set_files(table_model(Vec::new()));
            data.set_current_summary("".into());
            self.stats_files.clear();
            return;
        };
        let session = &self.sessions[si];
        let known = !session.stats.is_empty();
        data.set_title(
            if known {
                session.project.config().name.clone()
            } else {
                trf("%1 — computing...", &[&session.project.config().name])
            }
            .into(),
        );

        let mut by_kind = [FileStats::default(), FileStats::default(), FileStats::default()];
        let mut total = FileStats::default();
        for s in &session.stats {
            by_kind[s.kind.clamp(0, 2) as usize].add(s);
            total.add(s);
        }
        let labels = [tr("Game files"), tr("Dialogues"), tr("Other"), tr("Total")];
        let rows = [&by_kind[0], &by_kind[1], &by_kind[2], &total]
            .iter()
            .zip(labels)
            .map(|(s, label)| {
                let mut row = table_row(vec![
                    label,
                    s.lines.to_string(),
                    if s.lines > 0 { percent_text(s) } else { "-".into() },
                    s.review.to_string(),
                    s.words_remaining.to_string(),
                    s.chars_remaining.to_string(),
                ]);
                row.bold = false;
                row
            })
            .collect();
        data.set_categories(table_model(rows));

        let current = self
            .current_editor()
            .filter(|e| e.session == session.id)
            .and_then(|e| session.stats.iter().find(|s| s.rel_path == e.rel_path()));
        data.set_current_summary(
            current
                .map(|s| {
                    trf(
                        "%1 — %2 done\n%3 lines (%4 untranslated, %5 to review)\n%6 words and %7 characters left (of %8 words)",
                        &[
                            &s.display_name,
                            &percent_text(s),
                            &s.lines.to_string(),
                            &s.untranslated.to_string(),
                            &s.review.to_string(),
                            &s.words_remaining.to_string(),
                            &s.chars_remaining.to_string(),
                            &s.words.to_string(),
                        ],
                    )
                })
                .unwrap_or_default()
                .into(),
        );

        let only_incomplete = data.get_only_incomplete();
        let mut list: Vec<&FileStats> = session.stats.iter().filter(|s| !only_incomplete || !s.is_complete()).collect();
        list.sort_by(|a, b| b.words_remaining.cmp(&a.words_remaining).then_with(|| a.rel_path.cmp(&b.rel_path)));
        list.truncate(MAX_FILES);
        let files = list
            .iter()
            .map(|s| table_row(vec![s.display_name.clone(), percent_text(s), s.untranslated.to_string(), s.words_remaining.to_string()]))
            .collect();
        self.stats_files = list.iter().map(|s| s.rel_path.clone()).collect();
        data.set_files(table_model(files));
        let _ = FileKind::Game;
    }
}
