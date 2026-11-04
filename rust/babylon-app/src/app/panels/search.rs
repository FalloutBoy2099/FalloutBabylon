//! Search and replace in the whole project.

use super::{one_line, table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::project_search::{self, Hit, Options};
use babylon_core::statistics;
use slint::ComponentHandle;
use std::collections::{BTreeMap, HashSet};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

const MAX_HITS: usize = 5000;

pub(crate) struct SearchState {
    hits: Vec<Hit>,
    checked: Vec<bool>,
    generation: u64,
    cancel: Arc<AtomicBool>,
}

impl Default for SearchState {
    fn default() -> Self {
        SearchState { hits: Vec::new(), checked: Vec::new(), generation: 0, cancel: Arc::new(AtomicBool::new(false)) }
    }
}

fn flat(text: &str) -> String {
    text.replace('\n', " ⏎ ")
}

pub(super) fn wire(host: &Host) {
    let data = host.search();
    data.on_search(|| {
        with_app(|app, w| app.search_now(w));
    });
    data.on_toggled(|i| {
        with_app(|app, w| {
            let i = i as usize;
            if app.search.hits.get(i).is_some_and(|h| h.in_target) {
                app.search.checked[i] = !app.search.checked[i];
                app.show_search_rows(w);
            }
        });
    });
    data.on_replace_checked(|| {
        with_app(|app, w| app.replace_requested(w, false));
    });
    data.on_replace_all(|| {
        with_app(|app, w| app.replace_requested(w, true));
    });
    data.on_activated(|i| {
        with_app(|app, w| {
            let (Some(si), Some(hit)) = (app.active_session(), app.search.hits.get(i as usize).cloned()) else { return };
            let id = app.sessions[si].id.clone();
            app.open_file(w, &id, &hit.rel_path);
            app.select_id(w, hit.id);
        });
    });
}

fn options_of(data: &crate::SearchData<'_>) -> Options {
    let scope = data.get_scope();
    Options {
        text: data.get_find().to_string(),
        in_target: scope == 0 || scope == 2,
        in_source: scope == 1 || scope == 2,
        regex: data.get_regex(),
        case_sensitive: data.get_match_case(),
        whole_word: data.get_whole_word(),
    }
}

impl App {
    fn search_now(&mut self, window: &MainWindow) {
        let host = self.host_of(super::SEARCH, window);
        let data = host.search();
        let Some(si) = self.active_session() else { return };
        let options = options_of(&data);
        if options.text.is_empty() {
            data.set_status(tr("Type a text to search.").into());
            return;
        }
        if let Err(e) = options.expression() {
            data.set_status(trf("Invalid regular expression: %1", &[&e]).into());
            return;
        }
        self.search.cancel.store(true, Ordering::Relaxed);
        self.search.cancel = Arc::new(AtomicBool::new(false));
        self.search.generation += 1;
        let generation = self.search.generation;
        let cancel = self.search.cancel.clone();
        data.set_status(tr("Searching...").into());
        data.set_busy(true);
        let project = self.sessions[si].project.clone();
        let replacement = data.get_replace().to_string();
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let (hits, truncated) = project_search::search(
                &project,
                &options,
                if replacement.is_empty() { None } else { Some(replacement.as_str()) },
                None,
                Some(&cancel),
                MAX_HITS,
            );
            if cancel.load(Ordering::Relaxed) {
                return;
            }
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.search_results(w, generation, hits, truncated));
            });
        });
    }

    fn search_results(&mut self, window: &MainWindow, generation: u64, hits: Vec<Hit>, truncated: bool) {
        if generation != self.search.generation {
            return;
        }
        let host = self.host_of(super::SEARCH, window);
        let data = host.search();
        data.set_busy(false);
        self.search.checked = hits.iter().map(|h| h.in_target).collect();
        let occurrences: usize = hits.iter().map(|h| h.occurrences).sum();
        let target_hits = hits.iter().filter(|h| h.in_target).count();
        let mut status = if hits.is_empty() {
            tr("No result.")
        } else {
            trf("%1 line(s), %2 occurrence(s)", &[&hits.len().to_string(), &occurrences.to_string()])
        };
        if truncated {
            status.push_str("  ⚠ ");
            status.push_str(&tr("only the first results are shown"));
        }
        data.set_status(status.into());
        data.set_can_replace(target_hits > 0);
        self.search.hits = hits;
        self.show_search_rows(window);
    }

    fn show_search_rows(&self, window: &MainWindow) {
        let rows = self
            .search
            .hits
            .iter()
            .enumerate()
            .map(|(i, h)| {
                let mut file = statistics::display_name_for(&h.rel_path);
                if !h.in_target {
                    file.push_str(&tr(" (source)"));
                }
                let mut row = table_row(vec![String::new(), file, h.id.to_string(), one_line(&flat(&h.text), 200), one_line(&flat(&h.after), 200)]);
                row.checkable = h.in_target;
                row.checked = self.search.checked[i];
                row.muted = !h.in_target;
                row
            })
            .collect();
        self.host_of(super::SEARCH, window).search().set_rows(table_model(rows));
    }

    fn replace_requested(&mut self, window: &MainWindow, all: bool) {
        let Some(si) = self.active_session() else { return };
        let host = self.host_of(super::SEARCH, window);
        let options = options_of(&host.search());
        let replacement = host.search().get_replace().to_string();
        let mut by_file: BTreeMap<String, HashSet<i32>> = BTreeMap::new();
        let mut lines = 0;
        for (i, h) in self.search.hits.iter().enumerate() {
            if h.in_target && (all || self.search.checked[i]) {
                by_file.entry(h.rel_path.clone()).or_default().insert(h.id);
                lines += 1;
            }
        }
        if by_file.is_empty() {
            return;
        }
        let question = trf(
            "Replace \"%1\" by \"%2\" in the translation of %3 line(s) of %4 file(s)?\n\nFiles that are not open are saved immediately.",
            &[&options.text, &replacement, &lines.to_string(), &by_file.len().to_string()],
        );
        let session_id = self.sessions[si].id.clone();
        self.ask(&tr("Replace"), &question, move |app, w| app.do_replace(w, &session_id, &options, &replacement, by_file));
    }

    fn do_replace(
        &mut self,
        window: &MainWindow,
        session_id: &str,
        options: &Options,
        replacement: &str,
        by_file: BTreeMap<String, HashSet<i32>>,
    ) {
        let Some(si) = self.session_index(session_id) else { return };
        let project = self.sessions[si].project.clone();
        let (mut total, mut files) = (0, 0);
        let mut problems: Vec<String> = Vec::new();
        for (rel, ids) in &by_file {
            if let Some(n) = self.replace_in_open_editor(window, session_id, rel, options, replacement, ids) {
                total += n;
                files += usize::from(n > 0);
                continue;
            }
            let mut doc = match project.load_document(rel) {
                Ok(d) => d,
                Err(e) => {
                    problems.push(format!("{rel}: {e}"));
                    continue;
                }
            };
            let n = project_search::replace_in_document(&mut doc, options, replacement, ids);
            if n == 0 {
                continue;
            }
            if let Err(e) = project.save_document(&doc, true) {
                problems.push(format!("{rel}: {e}"));
                continue;
            }
            total += n;
            files += 1;
            let stats = babylon_core::project::Project::statistics_of(&doc);
            if let Some(slot) = self.sessions[si].stats.iter_mut().find(|s| s.rel_path == stats.rel_path) {
                *slot = stats;
            }
            if let Some(index) = &self.sessions[si].index {
                index.update_file(&doc);
            }
        }
        self.rebuild_tree(window);
        self.update_summary(window);
        self.refresh_panel(window);
        let mut text = trf("%1 replacement(s) in %2 file(s).", &[&total.to_string(), &files.to_string()]);
        if !problems.is_empty() {
            text.push_str("\n\n");
            text.push_str(&problems.join("\n"));
        }
        self.notify(&tr("Replace"), &text);
        self.search_now(window);
    }
}
