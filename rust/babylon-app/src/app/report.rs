//! Game reports: remarks written by the game (or a tester) that mark lines "to review". A report can be
//! imported once, or followed while the game appends to it.

use super::{with_app, App};
use crate::MainWindow;
use babylon_core::app_settings::ReportWatch;
use babylon_core::game_report::{self, Outcome};
use babylon_core::i18n::{tr, trf};
use babylon_core::project::ReviewMark;
use babylon_core::text_codec;
use slint::{ComponentHandle, TimerMode};
use std::io::{Read, Seek, SeekFrom};
use std::time::Duration;

#[derive(Default)]
pub(crate) struct ReportState {
    timer: slint::Timer,
}

impl App {
    /// Starts the polling of followed report files.
    pub fn start_report_polling(&mut self) {
        self.report.timer.start(TimerMode::Repeated, Duration::from_secs(2), || {
            with_app(|app, w| app.poll_reports(w));
        });
    }

    fn poll_reports(&mut self, window: &MainWindow) {
        let watches: Vec<(String, ReportWatch)> = self
            .settings
            .report_watches
            .iter()
            .filter(|(id, _)| self.session_index(id).is_some())
            .map(|(id, w)| (id.clone(), w.clone()))
            .collect();
        for (id, watch) in watches {
            let size = std::fs::metadata(&watch.path).map(|m| m.len()).unwrap_or(0);
            // a smaller file was emptied: a new report starts
            if size != watch.offset {
                self.process_report(window, &id, &watch.path, false);
            }
        }
    }

    pub fn update_report_flag(&self, window: &MainWindow) {
        let watching = self.active_session().is_some_and(|si| self.settings.report_watches.contains_key(&self.sessions[si].id));
        window.set_report_watching(watching);
    }

    pub fn import_game_report(&mut self, window: &MainWindow) {
        let Some(si) = self.active_session() else { return };
        let id = self.sessions[si].id.clone();
        if let Some(path) = self.picker(window).set_title(tr("Import a game report")).add_filter(tr("Text files"), &["txt", "log"]).pick_file() {
            self.process_report(window, &id, &path.to_string_lossy(), true);
        }
    }

    pub fn toggle_report_watch(&mut self, window: &MainWindow) {
        let Some(si) = self.active_session() else { return };
        let id = self.sessions[si].id.clone();
        if self.settings.report_watches.remove(&id).is_some() {
            self.update_report_flag(window);
            return;
        }
        let Some(path) = self.picker(window)
            .set_title(tr("File the game writes its report to"))
            .set_file_name("babylon-report.txt")
            .add_filter(tr("Text files"), &["txt", "log"])
            .save_file()
        else {
            return;
        };
        let path = path.to_string_lossy().replace('\\', "/");
        // only what is written from now on is followed
        let offset = std::fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
        self.settings.report_watches.insert(id, ReportWatch { path: path.clone(), offset });
        self.set_status(window, trf("Following %1: lines written there are marked \"to review\".", &[&path]));
        self.update_report_flag(window);
    }

    /// Reads the report (all of it, or what was added since the last time) and marks the lines it designates.
    fn process_report(&mut self, window: &MainWindow, id: &str, path: &str, whole: bool) {
        let Some(si) = self.session_index(id) else { return };
        let mut file = match std::fs::File::open(path) {
            Ok(f) => f,
            Err(_) => {
                if whole {
                    self.notify(&tr("Game report"), &trf("Cannot read %1.", &[path]));
                }
                return;
            }
        };
        let size = file.metadata().map(|m| m.len()).unwrap_or(0);
        let followed = self.settings.report_watches.get(id).filter(|w| w.path == path).map(|w| w.offset).unwrap_or(0);
        let mut from = if whole { 0 } else { followed };
        if size < from {
            from = 0; // the file was emptied: a new report
        }
        let mut bytes = Vec::new();
        if file.seek(SeekFrom::Start(from)).is_err() || file.read_to_end(&mut bytes).is_err() {
            return;
        }
        if !whole && !bytes.is_empty() && bytes.last() != Some(&b'\n') {
            // the game is still writing the last line
            let cut = bytes.iter().rposition(|&b| b == b'\n').map_or(0, |p| p + 1);
            bytes.truncate(cut);
        }
        if !whole {
            if let Some(w) = self.settings.report_watches.get_mut(id) {
                w.offset = from + bytes.len() as u64;
            }
        }
        if bytes.is_empty() {
            if whole {
                self.notify(&tr("Game report"), &tr("The report is empty."));
            }
            return;
        }
        let text = String::from_utf8(bytes.clone()).unwrap_or_else(|_| text_codec::decode(&bytes, "cp1252"));
        let remarks = game_report::parse(&text);
        if remarks.is_empty() {
            if whole {
                self.notify(&tr("Game report"), &tr("No remark found in this file."));
            }
            return;
        }
        let project = self.sessions[si].project.clone();
        let (weak, id) = (window.as_weak(), id.to_string());
        std::thread::spawn(move || {
            let outcome = game_report::resolve(&project, &remarks, 5, None);
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.report_resolved(w, &id, outcome, !whole));
            });
        });
    }

    fn report_resolved(&mut self, window: &MainWindow, id: &str, outcome: Outcome, silent: bool) {
        let Some(si) = self.session_index(id) else { return };
        let open: std::collections::HashSet<String> =
            self.editors.iter().filter(|e| e.session == id).map(|e| e.rel_path().to_string()).collect();
        let (mut open_marks, mut closed_marks) = (Vec::new(), Vec::new());
        for r in &outcome.found {
            let mark = ReviewMark { rel_path: r.rel_path.clone(), id: r.id, comment: r.comment.clone() };
            if open.contains(&r.rel_path) {
                open_marks.push(mark);
            } else {
                closed_marks.push(mark);
            }
        }
        let mut changed = 0;
        for ci in 0..self.editors.len() {
            if self.editors[ci].session != id {
                continue;
            }
            let n = self.editors[ci].model.mark_for_review(&open_marks);
            if n > 0 {
                changed += n;
                self.editors[ci].refresh_all_items();
                if Some(ci) == self.current {
                    self.document_changed(window);
                    self.show_current_row(window);
                } else {
                    self.update_tabs();
                }
            }
        }
        let closed_changed = self.sessions[si].project.mark_for_review(&closed_marks);
        changed += closed_changed;
        if closed_changed > 0 {
            self.start_analysis(window, id, false);
        }
        self.refresh_panel(window);

        let missing = outcome.not_found.len() + outcome.ambiguous.len();
        if silent {
            self.set_status(window, trf("Game report: %1 line(s) marked \"to review\", %2 not found.", &[&changed.to_string(), &missing.to_string()]));
            return;
        }
        let mut text = trf("%1 line(s) marked \"to review\".", &[&changed.to_string()]);
        if !outcome.not_found.is_empty() {
            text.push_str("\n\n");
            text.push_str(&tr("Not found:"));
            text.push('\n');
            text.push_str(&outcome.not_found.iter().take(8).cloned().collect::<Vec<_>>().join("\n"));
        }
        if !outcome.ambiguous.is_empty() {
            text.push_str("\n\n");
            text.push_str(&tr("Too many matches (give the file and the id):"));
            text.push('\n');
            text.push_str(&outcome.ambiguous.iter().take(8).cloned().collect::<Vec<_>>().join("\n"));
        }
        self.notify(&tr("Game report"), &text);
    }
}
