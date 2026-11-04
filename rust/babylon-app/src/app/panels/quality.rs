//! The quality report: checks of the saved files (tokens, spacing, encoding, length, duplicates, glossary).

use super::{table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::quality_check::{self, Cache, Issue, Kind, Options};
use babylon_core::statistics;
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

const MAX_ROWS: usize = 5000;
const KINDS: [Kind; 6] = [Kind::Tokens, Kind::Spacing, Kind::Encoding, Kind::Length, Kind::Duplicates, Kind::Glossary];

pub(crate) struct QualityState {
    issues: Vec<Issue>,
    shown: Vec<usize>,
    generation: u64,
    cancel: Arc<AtomicBool>,
    cache_hits: usize,
}

impl Default for QualityState {
    fn default() -> Self {
        QualityState { issues: Vec::new(), shown: Vec::new(), generation: 0, cancel: Arc::new(AtomicBool::new(false)), cache_hits: 0 }
    }
}

fn csv(s: &str) -> String {
    format!("\"{}\"", s.replace('"', "\"\"").replace('\n', " "))
}

pub(super) fn localize(host: &Host) {
    let mut names = vec![SharedString::from(tr("All problems"))];
    names.extend(KINDS.iter().map(|k| SharedString::from(k.name())));
    host.quality().set_filter_names(ModelRc::new(VecModel::from(names)));
}

pub(super) fn wire(host: &Host) {
    let data = host.quality();
    data.on_run(|| {
        with_app(|app, w| app.quality_run(w));
    });
    data.on_filter_changed(|| {
        with_app(|app, w| app.quality_show(w));
    });
    data.on_export_report(|| {
        with_app(|app, w| app.quality_export(w));
    });
    data.on_activated(|i| {
        with_app(|app, w| {
            let Some(si) = app.active_session() else { return };
            let Some(issue) = app.quality.shown.get(i as usize).and_then(|&k| app.quality.issues.get(k)).cloned() else { return };
            let id = app.sessions[si].id.clone();
            app.open_file(w, &id, &issue.rel_path);
            app.select_id(w, issue.id);
        });
    });
}

fn options_of(data: &crate::QualityData<'_>) -> Options {
    let flags = [
        data.get_tokens(),
        data.get_spacing(),
        data.get_encoding(),
        data.get_length(),
        data.get_duplicates(),
        data.get_glossary(),
    ];
    let mut kinds = 0u32;
    for (flag, kind) in flags.iter().zip(KINDS) {
        if *flag {
            kinds |= kind as u32;
        }
    }
    Options { kinds, max_chars: data.get_max_chars().max(0) as usize, ..Default::default() }
}

impl App {
    pub fn refresh_quality_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::QUALITY, window);
        let data = host.quality();
        if self.active_session().is_none() {
            data.set_status(tr("No project open.").into());
            data.set_can_run(false);
        } else if self.quality.issues.is_empty() && self.quality.generation == 0 {
            data.set_status(tr("Press \"Run the checks\".").into());
            data.set_can_run(true);
        }
    }

    fn quality_run(&mut self, window: &MainWindow) {
        let host = self.host_of(super::QUALITY, window);
        let data = host.quality();
        let Some(si) = self.active_session() else { return };
        let options = options_of(&data);
        if options.kinds == 0 {
            data.set_status(tr("Select at least one check.").into());
            return;
        }
        self.quality.cancel.store(true, Ordering::Relaxed);
        self.quality.cancel = Arc::new(AtomicBool::new(false));
        self.quality.generation += 1;
        let generation = self.quality.generation;
        let cancel = self.quality.cancel.clone();
        data.set_can_run(false);
        data.set_status(tr("Checking...").into());
        let project = self.sessions[si].project.clone();
        let cache_file = self.store.qa_cache_file(&self.sessions[si].id);
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let glossary = project.glossary();
            let mut cache = Cache::open(&cache_file, &quality_check::cache_key(&project, Some(&glossary), &options));
            let mut hits = 0usize;
            let progress_weak = weak.clone();
            let progress_cancel = cancel.clone();
            let mut progress = move |done: usize, total: usize| {
                if progress_cancel.load(Ordering::Relaxed) {
                    return;
                }
                let _ = progress_weak.upgrade_in_event_loop(move |_| {
                    with_app(|app, w| {
                        app.host_of(super::QUALITY, w)
                            .quality()
                            .set_status(trf("Checking... %1 / %2 files", &[&done.to_string(), &total.to_string()]).into());
                    });
                });
            };
            let issues =
                quality_check::run(&project, Some(&glossary), &options, Some(&mut progress), Some(&cancel), Some(&mut cache), Some(&mut hits));
            cache.save();
            if cancel.load(Ordering::Relaxed) {
                return;
            }
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.quality_results(w, generation, issues, hits));
            });
        });
    }

    fn quality_results(&mut self, window: &MainWindow, generation: u64, issues: Vec<Issue>, cache_hits: usize) {
        if generation != self.quality.generation {
            return;
        }
        let host = self.host_of(super::QUALITY, window);
        let data = host.quality();
        data.set_can_run(true);
        data.set_can_export(!issues.is_empty());
        self.quality.issues = issues;
        self.quality.cache_hits = cache_hits;
        self.quality_show(window);
    }

    fn quality_show(&mut self, window: &MainWindow) {
        let host = self.host_of(super::QUALITY, window);
        let data = host.quality();
        let filter = data.get_filter();
        let kind = if filter <= 0 { None } else { KINDS.get(filter as usize - 1).copied() };
        let shown: Vec<usize> =
            self.quality.issues.iter().enumerate().filter(|(_, i)| kind.is_none_or(|k| i.kind == k)).map(|(n, _)| n).collect();
        let rows = shown
            .iter()
            .take(MAX_ROWS)
            .map(|&n| {
                let i = &self.quality.issues[n];
                table_row(vec![
                    i.kind.name(),
                    statistics::display_name_for(&i.rel_path),
                    i.id.to_string(),
                    i.message.clone(),
                    i.target.replace('\n', " ⏎ "),
                ])
            })
            .collect();
        data.set_rows(table_model(rows));
        let mut status = if self.quality.issues.is_empty() {
            tr("No problem found.")
        } else {
            trf("%1 problem(s)", &[&self.quality.issues.len().to_string()])
        };
        if kind.is_some() {
            status.push_str(&trf(" — %1 shown", &[&shown.len().to_string()]));
        }
        if shown.len() > MAX_ROWS {
            status.push_str("  ⚠ ");
            status.push_str(&trf("only the first %1 are listed (use the export for all)", &[&MAX_ROWS.to_string()]));
        }
        if self.quality.cache_hits > 0 {
            status.push_str("  ·  ");
            status.push_str(&trf("%1 file(s) taken from the cache", &[&self.quality.cache_hits.to_string()]));
        }
        data.set_status(status.into());
        self.quality.shown = shown;
    }

    fn quality_export(&mut self, window: &MainWindow) {
        let Some(path) = self.picker(window)
            .set_title(tr("Export the report"))
            .set_file_name("quality-report.csv")
            .add_filter(tr("CSV files"), &["csv"])
            .save_file()
        else {
            return;
        };
        let mut out = String::from("\u{feff}check,file,id,problem,source,translation\r\n");
        for i in &self.quality.issues {
            out.push_str(&format!(
                "{},{},{},{},{},{}\r\n",
                csv(&i.kind.name()),
                csv(&i.rel_path),
                i.id,
                csv(&i.message),
                csv(&i.source),
                csv(&i.target)
            ));
        }
        if std::fs::write(&path, out).is_err() {
            self.set_status(window, trf("Cannot write %1.", &[&path.to_string_lossy()]));
        }
    }
}
