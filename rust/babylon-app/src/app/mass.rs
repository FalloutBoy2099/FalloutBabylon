//! Mass translation with DeepL: a dialog that lists the proposals, translated three at a time in the
//! background; the translator checks / edits them and applies the chosen ones as one undoable step.

use super::deepl_service::context_for;
use super::{with_app, App};
use crate::{MainWindow, MassTranslateDialog, TableRow};
use babylon_core::deepl::DeepLClient;
use babylon_core::i18n::{tr, trf};
use babylon_core::project::SegmentStatus;
use babylon_core::statistics;
use slint::{ComponentHandle, Model, ModelRc, SharedString, VecModel};
use std::collections::VecDeque;
use std::rc::Rc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

const CONCURRENCY: usize = 3;

struct Job {
    slot: usize,
    source: String,
    context: String,
}

pub(crate) struct MassState {
    dialog: Option<MassTranslateDialog>,
    editor_uid: u64,
    /// Rows selected in the table when the dialog was opened.
    selected: Vec<usize>,
    /// Rows being translated (document rows), and what came back for each.
    queue: Vec<usize>,
    proposals: Vec<String>,
    checked: Vec<bool>,
    notes: Vec<String>,
    rows: Rc<VecModel<TableRow>>,
    running: bool,
    stop: Arc<AtomicBool>,
    generation: u64,
    done: usize,
    failed: usize,
    workers_left: usize,
    quota_left: i64,
    current: Option<usize>,
}

impl Default for MassState {
    fn default() -> Self {
        MassState {
            dialog: None,
            editor_uid: 0,
            selected: Vec::new(),
            queue: Vec::new(),
            proposals: Vec::new(),
            checked: Vec::new(),
            notes: Vec::new(),
            rows: Rc::new(VecModel::default()),
            running: false,
            stop: Arc::new(AtomicBool::new(false)),
            generation: 0,
            done: 0,
            failed: 0,
            workers_left: 0,
            quota_left: -1,
            current: None,
        }
    }
}

impl MassState {
    pub fn snapshot(&self) -> Option<slint::SharedPixelBuffer<slint::Rgba8Pixel>> {
        self.dialog.as_ref().and_then(|d| d.window().take_snapshot().ok())
    }
}

impl App {
    /// The lines the chosen scope covers.
    fn mass_candidates(&self, dialog: &MassTranslateDialog) -> Vec<usize> {
        let Some(ed) = self.editors.iter().find(|e| e.uid == self.mass.editor_uid) else { return Vec::new() };
        let scope = dialog.get_scope();
        let overwrite = dialog.get_overwrite() && scope != 0;
        let base: Vec<usize> = if scope == 1 { self.mass.selected.clone() } else { (0..ed.model.len()).collect() };
        let mut rows: Vec<usize> = base
            .into_iter()
            .filter(|&r| r < ed.model.len())
            .filter(|&r| {
                let row = ed.model.row(r);
                row.in_source
                    && statistics::is_translatable(&row.source)
                    && (overwrite || ed.model.status(r) == SegmentStatus::Untranslated)
            })
            .collect();
        rows.sort_unstable();
        rows
    }

    fn mass_chars(&self, rows: &[usize]) -> i64 {
        let Some(ed) = self.editors.iter().find(|e| e.uid == self.mass.editor_uid) else { return 0 };
        rows.iter().map(|&r| ed.model.row(r).source.chars().count() as i64).sum()
    }

    fn mass_update_estimate(&self) {
        let Some(dialog) = &self.mass.dialog else { return };
        if self.mass.running {
            return;
        }
        dialog.set_can_overwrite(dialog.get_scope() != 0);
        let rows = self.mass_candidates(dialog);
        let chars = self.mass_chars(&rows);
        let mut text = trf("%1 line(s), about %2 characters to send to DeepL.", &[&rows.len().to_string(), &chars.to_string()]);
        if self.mass.quota_left >= 0 {
            text.push_str(&trf(" Remaining quota: %L1 characters.", &[&self.mass.quota_left.to_string()]));
            if chars > self.mass.quota_left {
                text.push(' ');
                text.push_str(&tr("Not enough quota for everything."));
            }
        }
        dialog.set_estimate(text.into());
    }

    pub fn open_mass_dialog(&mut self, window: &MainWindow) {
        if let Some(d) = &self.mass.dialog {
            if d.window().is_visible() {
                self.present(d);
                return;
            }
        }
        let Some(ed) = self.current_editor() else { return };
        let uid = ed.uid;
        let selected: Vec<usize> =
            if ed.selection.is_empty() { ed.current.into_iter().collect() } else { ed.selection.iter().copied().collect() };
        let untranslated = (0..ed.model.len())
            .filter(|&i| ed.model.status(i) == SegmentStatus::Untranslated && statistics::is_translatable(&ed.model.row(i).source))
            .count();
        let total = ed.model.len();
        let Ok(dialog) = MassTranslateDialog::new() else { return };

        self.mass = Default::default();
        self.mass.editor_uid = uid;
        self.mass.selected = selected.clone();
        self.mass.quota_left = -1;
        let names: Vec<SharedString> = vec![
            trf("Untranslated lines (%1)", &[&untranslated.to_string()]).into(),
            trf("Selected lines (%1)", &[&selected.len().to_string()]).into(),
            trf("All lines (%1)", &[&total.to_string()]).into(),
        ];
        dialog.set_scope_names(ModelRc::new(VecModel::from(names)));
        dialog.set_rows(ModelRc::from(self.mass.rows.clone()));

        dialog.on_scope_changed(|| {
            with_app(|app, _| app.mass_update_estimate());
        });
        dialog.on_translate(|| {
            with_app(|app, w| app.mass_translate_clicked(w));
        });
        dialog.on_stop(|| {
            with_app(|app, w| app.mass_stop(w));
        });
        dialog.on_apply(|| {
            with_app(|app, w| app.mass_apply(w));
        });
        let weak = dialog.as_weak();
        dialog.on_dismissed(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
            with_app(|app, _| {
                app.mass.stop.store(true, Ordering::Relaxed);
                app.mass.generation += 1; // late answers are ignored
            });
        });
        dialog.on_selected(|i| {
            with_app(|app, _| app.mass_select(i));
        });
        dialog.on_toggled(|i| {
            with_app(|app, _| app.mass_toggle(i as usize));
        });
        dialog.on_proposal_edited(|text| {
            with_app(|app, _| app.mass_edited(&text));
        });

        if self.deepl_configured() {
            let key = self.settings.deepl_key.clone();
            let weak = window.as_weak();
            std::thread::spawn(move || {
                if let Ok((used, limit)) = DeepLClient::new().usage(&key) {
                    if limit > 0 {
                        let _ = weak.upgrade_in_event_loop(move |_| {
                            with_app(|app, _| {
                                app.mass.quota_left = limit - used;
                                app.mass_update_estimate();
                            });
                        });
                    }
                }
            });
        } else {
            dialog.set_status(tr("No DeepL API key: enter it in Tools > Settings.").into());
            dialog.set_can_translate(false);
        }
        self.present(&dialog);
        self.mass.dialog = Some(dialog);
        self.mass_update_estimate();
    }

    fn mass_translate_clicked(&mut self, _window: &MainWindow) {
        let Some(dialog) = &self.mass.dialog else { return };
        let rows = self.mass_candidates(dialog);
        if rows.is_empty() {
            dialog.set_status(tr("Nothing to translate with this choice.").into());
            return;
        }
        let chars = self.mass_chars(&rows);
        let question = trf("Send %1 line(s) (about %2 characters) to DeepL?", &[&rows.len().to_string(), &chars.to_string()]);
        self.ask(&tr("DeepL"), &question, move |app, w| app.mass_start(w, rows));
    }

    fn mass_start(&mut self, window: &MainWindow, rows: Vec<usize>) {
        let Some(ci) = self.editors.iter().position(|e| e.uid == self.mass.editor_uid) else { return };
        let Some(si) = self.session_index(&self.editors[ci].session) else { return };
        let Some(dialog) = &self.mass.dialog else { return };
        let table: Vec<TableRow> = rows
            .iter()
            .map(|&r| {
                let row = self.editors[ci].model.row(r);
                let mut t = super::panels::table_row(vec![String::new(), row.id.to_string(), super::panels::one_line(&row.source, 300), String::new(), String::new()]);
                t.checkable = true;
                t
            })
            .collect();
        self.mass.rows.set_vec(table);
        self.mass.queue = rows;
        self.mass.proposals = vec![String::new(); self.mass.queue.len()];
        self.mass.checked = vec![false; self.mass.queue.len()];
        self.mass.notes = vec![String::new(); self.mass.queue.len()];
        self.mass.done = 0;
        self.mass.failed = 0;
        self.mass.running = true;
        self.mass.stop = Arc::new(AtomicBool::new(false));
        self.mass.generation += 1;
        dialog.set_running(true);
        dialog.set_can_apply(false);
        dialog.set_progress(0.0);
        dialog.set_status(tr("Preparing the glossary...").into());
        let generation = self.mass.generation;
        self.with_deepl_glossary(window, si, move |app, w, glossary_id, warning| {
            if app.mass.generation != generation {
                return;
            }
            if let Some(d) = &app.mass.dialog {
                d.set_status(if warning.is_empty() { tr("Translating...") } else { warning }.into());
            }
            app.mass_pump(w, si, glossary_id);
        });
    }

    /// Starts the workers: three requests in flight at a time.
    fn mass_pump(&mut self, window: &MainWindow, si: usize, glossary_id: String) {
        let Some(ci) = self.editors.iter().position(|e| e.uid == self.mass.editor_uid) else { return };
        let project = self.sessions[si].project.clone();
        let mut options = self.deepl_options(&project);
        options.glossary_id = glossary_id;
        let use_context = self.settings.deepl_use_context;
        let queue: VecDeque<Job> = self
            .mass
            .queue
            .iter()
            .enumerate()
            .map(|(slot, &r)| {
                let ed = &self.editors[ci];
                Job {
                    slot,
                    source: ed.model.row(r).source.clone(),
                    context: if use_context { context_for(ed.model.doc(), r) } else { String::new() },
                }
            })
            .collect();
        let jobs = Arc::new(Mutex::new(queue));
        let stop = self.mass.stop.clone();
        let generation = self.mass.generation;
        let workers = CONCURRENCY.min(self.mass.queue.len()).max(1);
        self.mass.workers_left = workers;
        for _ in 0..workers {
            let (jobs, stop, options, weak) = (jobs.clone(), stop.clone(), options.clone(), window.as_weak());
            std::thread::spawn(move || {
                let client = DeepLClient::new();
                loop {
                    if stop.load(Ordering::Relaxed) {
                        break;
                    }
                    let Some(job) = jobs.lock().ok().and_then(|mut q| q.pop_front()) else { break };
                    let result = client.translate(&job.source, &job.context, &options).map_err(|e| e.to_string());
                    // a quota or key problem will hit every following line: stop instead of failing them all
                    if let Err(e) = &result {
                        let lower = e.to_lowercase();
                        if lower.contains("quota") || lower.contains("key") {
                            stop.store(true, Ordering::Relaxed);
                        }
                    }
                    let slot = job.slot;
                    let _ = weak.upgrade_in_event_loop(move |_| {
                        with_app(|app, w| app.mass_result(w, generation, slot, result));
                    });
                }
                let _ = weak.upgrade_in_event_loop(move |_| {
                    with_app(|app, w| app.mass_worker_done(w, generation));
                });
            });
        }
    }

    fn mass_row(&self, slot: usize) -> TableRow {
        let id = self
            .editors
            .iter()
            .find(|e| e.uid == self.mass.editor_uid)
            .map(|e| e.model.row(self.mass.queue[slot]).id.to_string())
            .unwrap_or_default();
        let source = self
            .editors
            .iter()
            .find(|e| e.uid == self.mass.editor_uid)
            .map(|e| super::panels::one_line(&e.model.row(self.mass.queue[slot]).source, 300))
            .unwrap_or_default();
        let mut t = super::panels::table_row(vec![
            String::new(),
            id,
            source,
            super::panels::one_line(&self.mass.proposals[slot], 300),
            self.mass.notes[slot].clone(),
        ]);
        t.checkable = true;
        t.checked = self.mass.checked[slot];
        t
    }

    fn mass_result(&mut self, _window: &MainWindow, generation: u64, slot: usize, result: Result<babylon_core::deepl::Translation, String>) {
        if generation != self.mass.generation || slot >= self.mass.queue.len() {
            return;
        }
        match result {
            Err(e) => {
                self.mass.failed += 1;
                self.mass.notes[slot] = e;
            }
            Ok(t) => {
                self.mass.proposals[slot] = t.text;
                self.mass.checked[slot] = t.warning.is_none();
                if let Some(w) = t.warning {
                    self.mass.notes[slot] = w; // altered placeholder: shown, left unchecked
                }
            }
        }
        self.mass.done += 1;
        self.mass.rows.set_row_data(slot, self.mass_row(slot));
        if let Some(d) = &self.mass.dialog {
            d.set_progress(self.mass.done as f32 / self.mass.queue.len().max(1) as f32);
        }
    }

    fn mass_worker_done(&mut self, _window: &MainWindow, generation: u64) {
        if generation != self.mass.generation {
            return;
        }
        self.mass.workers_left = self.mass.workers_left.saturating_sub(1);
        if self.mass.workers_left > 0 {
            return;
        }
        self.mass.running = false;
        let stopped = self.mass.stop.load(Ordering::Relaxed);
        let ok = self.mass.done - self.mass.failed;
        let any = self.mass.proposals.iter().any(|p| !p.is_empty());
        if let Some(d) = &self.mass.dialog {
            d.set_running(false);
            d.set_can_apply(any);
            d.set_status(
                if stopped {
                    trf(
                        "Stopped: %1 of %2 line(s) translated, %3 failed.",
                        &[&ok.to_string(), &self.mass.queue.len().to_string(), &self.mass.failed.to_string()],
                    )
                } else {
                    trf("Done: %1 line(s) translated, %2 failed. Check the proposals, then apply.", &[&ok.to_string(), &self.mass.failed.to_string()])
                }
                .into(),
            );
        }
        self.mass_update_estimate();
    }

    fn mass_stop(&mut self, _window: &MainWindow) {
        self.mass.stop.store(true, Ordering::Relaxed);
        if let Some(d) = &self.mass.dialog {
            d.set_status(tr("Stopping...").into());
        }
    }

    fn mass_select(&mut self, row: i32) {
        let Some(d) = &self.mass.dialog else { return };
        let Some(slot) = usize::try_from(row).ok().filter(|&s| s < self.mass.queue.len()) else { return };
        self.mass.current = Some(slot);
        d.set_current(row);
        d.set_proposal(self.mass.proposals[slot].as_str().into());
        d.set_source_of_current(
            self.editors
                .iter()
                .find(|e| e.uid == self.mass.editor_uid)
                .map(|e| e.model.row(self.mass.queue[slot]).source.clone())
                .unwrap_or_default()
                .into(),
        );
        d.set_editing(true);
    }

    fn mass_toggle(&mut self, slot: usize) {
        if slot >= self.mass.checked.len() {
            return;
        }
        self.mass.checked[slot] = !self.mass.checked[slot];
        self.mass.rows.set_row_data(slot, self.mass_row(slot));
    }

    fn mass_edited(&mut self, text: &str) {
        let Some(slot) = self.mass.current else { return };
        self.mass.proposals[slot] = text.to_string();
        self.mass.checked[slot] = !text.is_empty();
        self.mass.rows.set_row_data(slot, self.mass_row(slot));
    }

    fn mass_apply(&mut self, window: &MainWindow) {
        let review = self.settings.deepl_mark_review;
        let Some(ci) = self.editors.iter().position(|e| e.uid == self.mass.editor_uid) else { return };
        let todo: Vec<(usize, String)> = (0..self.mass.queue.len())
            .filter(|&i| self.mass.checked[i] && !self.mass.proposals[i].is_empty())
            .map(|i| (self.mass.queue[i], self.mass.proposals[i].clone()))
            .collect();
        let applied = todo.len();
        self.editors[ci].model.operation(&tr("Mass translation"), |m| {
            for (row, text) in &todo {
                m.set_target(*row, text);
                if review {
                    m.set_review(*row, true);
                }
            }
        });
        self.editors[ci].refresh_all_items();
        if Some(ci) == self.current {
            self.document_changed(window);
            self.show_current_row(window);
        } else {
            self.update_tabs();
        }
        if let Some(d) = self.mass.dialog.take() {
            let _ = d.hide();
        }
        self.mass.generation += 1;
        self.notify(&tr("DeepL"), &trf("%1 translation(s) applied. Save the file to keep them.", &[&applied.to_string()]));
    }
}
