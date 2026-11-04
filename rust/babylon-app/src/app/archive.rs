//! Delivery of the translation as a .dat archive: rebuilt from the working folder a few seconds after the
//! last save, and once more when the application closes.

use super::{with_app, App};
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::output_archive::{self, BuildResult};
use slint::{ComponentHandle, TimerMode};
use std::collections::HashSet;
use std::time::Duration;

const DELAY: Duration = Duration::from_secs(4);

#[derive(Default)]
pub(crate) struct ArchiveState {
    pending: HashSet<String>,
    running: HashSet<String>,
    failed: HashSet<String>,
}

impl App {
    /// A project whose output is an archive has changed: rebuild it soon.
    pub fn schedule_archive(&mut self, session_id: &str) {
        let Some(si) = self.session_index(session_id) else { return };
        if !output_archive::enabled(self.sessions[si].project.config()) {
            return;
        }
        self.archive.pending.insert(session_id.to_string());
        self.archive_timer.start(TimerMode::SingleShot, DELAY, || {
            with_app(|app, w| app.archive_timer_fired(w));
        });
    }

    fn archive_timer_fired(&mut self, window: &MainWindow) {
        let ids: Vec<String> = self.archive.pending.iter().cloned().collect();
        for id in ids {
            self.build_archive(window, &id);
        }
    }

    /// Writes the archive now (in the background).
    pub fn build_archive(&mut self, window: &MainWindow, session_id: &str) {
        let Some(si) = self.session_index(session_id) else { return };
        let project = self.sessions[si].project.clone();
        if !output_archive::enabled(project.config()) {
            return;
        }
        if self.archive.running.contains(session_id) {
            return; // changed while being written: the pending mark makes it run once more afterwards
        }
        self.archive.pending.remove(session_id);
        self.archive.running.insert(session_id.to_string());
        self.set_status(window, tr("Updating the DAT archive..."));
        let id = session_id.to_string();
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let result = output_archive::build(&project).map_err(|e| e.to_string());
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.archive_built(w, &id, result));
            });
        });
    }

    fn archive_built(&mut self, window: &MainWindow, id: &str, result: Result<BuildResult, String>) {
        self.archive.running.remove(id);
        match result {
            Ok(r) => {
                self.archive.failed.remove(id);
                self.set_status(
                    window,
                    trf("DAT archive updated: %1 translated file(s), %2 original(s).", &[&r.translated.to_string(), &r.originals.to_string()]),
                );
                if self.archive.pending.contains(id) {
                    self.archive_timer.start(TimerMode::SingleShot, DELAY, || {
                        with_app(|app, w| app.archive_timer_fired(w));
                    });
                }
            }
            Err(e) => {
                self.archive.failed.insert(id.to_string());
                self.set_status(window, trf("DAT archive NOT updated: %1", &[&e]));
            }
        }
    }

    /// Final state of the archives when the application closes.
    pub fn flush_archives(&mut self) {
        let ids: Vec<String> = self.sessions.iter().map(|s| s.id.clone()).collect();
        for id in ids {
            self.archive_flush_one(&id);
        }
    }

    /// Writes the archive of a project now if it is out of date (blocking).
    pub fn archive_flush_one(&mut self, id: &str) {
        if !self.archive.pending.contains(id) && !self.archive.failed.contains(id) {
            return;
        }
        let Some(si) = self.session_index(id) else { return };
        let project = self.sessions[si].project.clone();
        if !output_archive::enabled(project.config()) {
            return;
        }
        self.archive.pending.remove(id);
        if let Err(e) = output_archive::build(&project) {
            eprintln!("{}: {e}", tr("The DAT archive could not be written:"));
        }
    }
}
