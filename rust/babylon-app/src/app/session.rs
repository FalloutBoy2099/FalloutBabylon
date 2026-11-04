//! Opening projects and the background analysis (sources, statistics, glossary).

use super::{with_app, App, Session};
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::project::{Project, ProjectConfig};
use babylon_core::statistics::FileStats;
use babylon_core::translation_index::TranslationIndex;
use slint::ComponentHandle;
use std::sync::Arc;

impl App {
    /// The "Saved projects" menu: the projects that are not open.
    pub fn refresh_saved_menu(&mut self, window: &MainWindow) {
        let configs: Vec<ProjectConfig> = self.store.load_all().into_iter().filter(|c| self.session_index(&c.id).is_none()).collect();
        self.saved_ids = configs.iter().map(|c| c.id.clone()).collect();
        let recent: Vec<slint::SharedString> = self.settings.recent_folders.iter().map(|f| f.as_str().into()).collect();
        window.set_recent_folders(slint::ModelRc::new(slint::VecModel::from(recent)));
        let names: Vec<slint::SharedString> = configs
            .iter()
            .map(|c| format!("{}  ({} → {})", c.name, c.source_language, c.target_language).into())
            .collect();
        window.set_saved_projects(slint::ModelRc::new(slint::VecModel::from(names)));
    }

    /// Opens a saved project (by id); does nothing when it is already open.
    pub fn open_saved_project(&mut self, window: &MainWindow, id: &str) {
        if self.session_index(id).is_some() {
            return;
        }
        let Some(config) = self.store.load_all().into_iter().find(|c| c.id == id) else { return };
        self.open_project(window, config);
    }

    pub fn open_project(&mut self, window: &MainWindow, config: ProjectConfig) {
        if self.session_index(&config.id).is_some() {
            return;
        }
        let mut project = Project::new(config.clone());
        // an existing output archive restores the working folder on a new computer / after switching to an archive
        if let Err(e) = babylon_core::output_archive::seed_working_folder(&config) {
            self.set_status(window, e.to_string());
        }
        project.set_state_file(self.store.state_file(&config.id));
        project.set_stats_cache_file(self.store.stats_cache_file(&config.id));
        self.sessions.push(Session {
            id: config.id.clone(),
            project: Arc::new(project),
            stats: Vec::new(),
            expanded: true,
            groups: [true, true, false],
            generation: 0,
            busy: false,
            glossary_terms: 0,
            index: None,
        });
        if !self.settings.open_projects.contains(&config.id) {
            self.settings.open_projects.push(config.id.clone());
        }
        self.rebuild_tree(window);
        self.refresh_saved_menu(window);
        self.start_analysis(window, &config.id, true);
    }

    /// Re-reads the sources and recomputes the statistics (and, when asked, the glossary) of a project.
    pub fn start_analysis(&mut self, window: &MainWindow, id: &str, rebuild_glossary: bool) {
        let Some(si) = self.session_index(id) else { return };
        let session = &mut self.sessions[si];
        session.generation += 1;
        session.busy = true;
        let generation = session.generation;
        let project = session.project.clone();
        let id = session.id.clone();
        window.set_busy(true);
        window.set_progress(0.0);
        self.set_status(window, tr("Reading the game files..."));

        let weak = window.as_weak();
        std::thread::spawn(move || {
            let errors = project.reload();
            let progress_weak = weak.clone();
            let mut last = 0usize;
            let mut progress = |done: usize, total: usize, _: &FileStats| {
                if done == total || done >= last + 25 {
                    last = done;
                    let _ = progress_weak.upgrade_in_event_loop(move |w| {
                        w.set_progress(done as f32 / total.max(1) as f32);
                        w.set_status_text(trf("Analysing %1 / %2", &[&done.to_string(), &total.to_string()]).into());
                    });
                }
            };
            let stats = project.compute_statistics(Some(&mut progress), None);
            {
                let id = id.clone();
                let _ = weak.upgrade_in_event_loop(move |_| {
                    with_app(|app, w| app.statistics_ready(w, &id, generation, stats, errors, rebuild_glossary));
                });
            }
            if rebuild_glossary {
                let glossary = project.build_glossary(None);
                let count = glossary.terms().len();
                project.set_glossary(glossary);
                let id = id.clone();
                let _ = weak.upgrade_in_event_loop(move |_| {
                    with_app(|app, w| app.glossary_ready(w, &id, generation, count));
                });
            }
            // the translation memory of the project: every translated line, for the suggestions
            let index = Arc::new(TranslationIndex::build(&project, None, None));
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.index_ready(w, &id, generation, index));
            });
        });
    }

    fn statistics_ready(
        &mut self,
        window: &MainWindow,
        id: &str,
        generation: u64,
        stats: Vec<FileStats>,
        errors: Vec<String>,
        glossary_follows: bool,
    ) {
        let Some(si) = self.session_index(id) else { return };
        if self.sessions[si].generation != generation {
            return;
        }
        self.sessions[si].stats = stats;
        // open files show their own, possibly unsaved, numbers
        for ed in &self.editors {
            if ed.session == id {
                let fresh = ed.model.stats();
                if let Some(slot) = self.sessions[si].stats.iter_mut().find(|s| s.rel_path == fresh.rel_path) {
                    *slot = fresh;
                }
            }
        }
        self.rebuild_tree(window);
        self.update_summary(window);
        self.refresh_panel(window);
        window.set_progress(0.0);
        if glossary_follows {
            self.set_status(window, tr("Building the glossary..."));
        } else {
            self.sessions[si].busy = false;
            window.set_busy(self.sessions.iter().any(|s| s.busy));
            self.set_status(window, "");
        }
        if !errors.is_empty() {
            let text = format!("{}\n\n{}", tr("Some sources could not be read:"), errors.join("\n"));
            self.notify(&tr("Project"), &text);
        }
        if self.sessions[si].stats.is_empty() {
            let config = self.sessions[si].project.config();
            let text = trf(
                "No .msg file found under text/%1 or text/%2 in the sources of \"%3\".",
                &[&config.source_language, &config.target_language, &config.name],
            );
            self.notify(&tr("Project"), &text);
        }
        self.restore_files(window, id);
        self.open_files_from_env(window);
    }

    fn glossary_ready(&mut self, window: &MainWindow, id: &str, generation: u64, count: usize) {
        let Some(si) = self.session_index(id) else { return };
        if self.sessions[si].generation != generation {
            return;
        }
        self.sessions[si].busy = false;
        self.sessions[si].glossary_terms = count;
        window.set_busy(self.sessions.iter().any(|s| s.busy));
        self.set_status(window, trf("Glossary: %1 terms", &[&count.to_string()]));
        let glossary = self.sessions[si].project.glossary();
        for ed in self.editors.iter_mut().filter(|e| e.session == id) {
            ed.model.refresh_issues(glossary.clone());
        }
        self.refresh_all_row_items();
        self.show_current_row(window);
        self.refresh_panel(window);
    }

    fn index_ready(&mut self, window: &MainWindow, id: &str, generation: u64, index: Arc<TranslationIndex>) {
        let Some(si) = self.session_index(id) else { return };
        if self.sessions[si].generation != generation {
            return;
        }
        self.sessions[si].index = Some(index);
        self.refresh_panel(window);
    }

    /// Summary line of the active project (the one of the current tab, else the first).
    pub fn update_summary(&self, window: &MainWindow) {
        let active = self
            .current
            .and_then(|c| self.editors.get(c))
            .and_then(|e| self.session_index(&e.session))
            .or(if self.sessions.is_empty() { None } else { Some(0) });
        let Some(si) = active else {
            window.set_summary_text("".into());
            window.set_window_title("Fallout Babylon".into());
            return;
        };
        let session = &self.sessions[si];
        let mut total = FileStats::default();
        for s in &session.stats {
            total.add(s);
        }
        self.update_report_flag(window);
        window.set_window_title(format!("{} — Fallout Babylon", session.project.config().name).into());
        window.set_summary_text(
            format!(
                "{} — {}",
                session.project.config().name,
                trf(
                    "%1 files — %2 lines, %3 untranslated, %4 to review (%5%)",
                    &[
                        &session.stats.len().to_string(),
                        &total.lines.to_string(),
                        &total.untranslated.to_string(),
                        &total.review.to_string(),
                        &format!("{:.0}", total.percent()),
                    ],
                )
            )
            .into(),
        );
    }

    /// Opens the projects / files named by `BABYLON_OPEN_PROJECT`, `BABYLON_OPEN_FILE` and `BABYLON_OPEN_ROW`
    /// (tests and screenshots).
    fn open_files_from_env(&mut self, window: &MainWindow) {
        let Ok(rel) = std::env::var("BABYLON_OPEN_FILE") else { return };
        std::env::remove_var("BABYLON_OPEN_FILE");
        let Some(session) = self.sessions.iter().find(|s| s.stats.iter().any(|f| f.rel_path == rel)) else { return };
        let id = session.id.clone();
        self.open_file(window, &id, &rel);
        if let Ok(row) = std::env::var("BABYLON_OPEN_ROW") {
            if let Ok(row) = row.parse::<usize>() {
                self.select_position(window, row);
            }
        }
    }
}
