//! Import of an existing translation (another version of the mod) into the project.

use super::{with_app, App};
use crate::{ImportDialog, MainWindow};
use babylon_core::i18n::{tr, trf};
use babylon_core::paths;
use babylon_core::text_codec;
use babylon_core::translation_memory::{self, Memory, Options, Report};
use babylon_core::vfs::VirtualFileSystem;
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

pub(crate) struct ImportState {
    pub(super) dialog: Option<ImportDialog>,
    session: String,
    sources: Vec<String>,
    languages: Vec<String>,
    stop: Arc<AtomicBool>,
    imported: bool,
}

impl Default for ImportState {
    fn default() -> Self {
        ImportState { dialog: None, session: String::new(), sources: Vec::new(), languages: Vec::new(), stop: Arc::new(AtomicBool::new(false)), imported: false }
    }
}

impl App {
    pub fn import_translation(&mut self, window: &MainWindow) {
        let Some(si) = self.active_session() else { return };
        let id = self.sessions[si].id.clone();
        // the files are rewritten on disk: the open ones are closed first (modified ones are saved)
        if self.editors.iter().any(|e| e.session == id) {
            let question = tr("The files of this project that are open will be closed first. Continue?");
            self.ask(&tr("Import an existing translation"), &question, move |app, w| {
                let uids: Vec<u64> = app.editors.iter().filter(|e| e.session == id).map(|e| e.uid).collect();
                let id = id.clone();
                app.confirm_close(w, uids, move |app, w| {
                    app.close_editors_of(w, &id);
                    app.show_import_dialog(w, &id);
                });
            });
            return;
        }
        self.show_import_dialog(window, &id);
    }

    fn close_editors_of(&mut self, window: &MainWindow, id: &str) {
        for i in 0..self.editors.len() {
            if self.editors[i].session == id && self.editors[i].model.is_dirty() {
                self.save_editor(i);
            }
        }
        let uid = self.current.and_then(|c| self.editors.get(c)).map(|e| e.uid);
        self.editors.retain(|e| e.session != id);
        self.current = uid.and_then(|u| self.editors.iter().position(|e| e.uid == u)).or(if self.editors.is_empty() { None } else { Some(0) });
        self.show_editor(window);
        self.rebuild_tree(window);
    }

    fn show_import_dialog(&mut self, _window: &MainWindow, id: &str) {
        let Some(si) = self.session_index(id) else { return };
        if let Some(d) = &self.import.dialog {
            if d.window().is_visible() {
                self.present(d);
                return;
            }
        }
        let Ok(dialog) = ImportDialog::new() else { return };
        let config = self.sessions[si].project.config().clone();
        dialog.set_intro(
            trf(
                "Reuse the translation of another version of the mod (for instance an older installation translated into %1). Only untranslated lines are filled in unless you ask to replace.",
                &[&config.target_language],
            )
            .into(),
        );
        self.import = ImportState { dialog: None, session: id.to_string(), ..Default::default() };

        dialog.on_browse(|| {
            with_app(|app, w| {
                let mut picker = app.picker(w).set_title(tr("Folder holding the translation"));
                if let Some(start) = app.settings.recent_folders.first() {
                    picker = picker.set_directory(start);
                }
                if let Some(dir) = picker.pick_folder() {
                    if let Some(d) = &app.import.dialog {
                        d.set_folder(dir.to_string_lossy().replace('\\', "/").into());
                    }
                    app.import_scan();
                }
            });
        });
        dialog.on_scan(|| {
            with_app(|app, _| app.import_scan());
        });
        dialog.on_start(|| {
            with_app(|app, w| app.import_start(w));
        });
        dialog.on_closed(|| {
            with_app(|app, w| app.import_close(w));
        });
        self.present(&dialog);
        self.import.dialog = Some(dialog);
    }

    fn import_scan(&mut self) {
        let Some(si) = self.session_index(&self.import.session) else { return };
        let config = self.sessions[si].project.config().clone();
        let Some(d) = &self.import.dialog else { return };
        let folder = d.get_folder().trim().to_string();
        d.set_can_start(false);
        if folder.is_empty() {
            return;
        }
        self.import.sources = VirtualFileSystem::discover_sources(&paths::clean_path(&folder));
        let mut vfs = VirtualFileSystem::new();
        let mut errors = Vec::new();
        vfs.set_sources(&self.import.sources, Some(&mut errors));
        let languages = vfs.languages();
        let index_of = |l: &str| languages.iter().position(|x| x == l).map(|i| i as i32);
        d.set_languages(ModelRc::new(VecModel::from(languages.iter().map(|l| SharedString::from(l.as_str())).collect::<Vec<_>>())));
        d.set_source_language(index_of(&config.source_language).or_else(|| index_of("english")).unwrap_or(0));
        d.set_target_language(index_of(&config.target_language).unwrap_or(0));
        let mut text = if languages.is_empty() {
            tr("No text/<language>/ folder found here.")
        } else {
            trf("%1 source(s) found; languages: %2.", &[&self.import.sources.len().to_string(), &languages.join(", ")])
        };
        if !errors.is_empty() {
            text.push('\n');
            text.push_str(&errors.join("\n"));
        }
        d.set_found(text.into());
        d.set_can_start(languages.iter().any(|l| *l == config.target_language));
        self.import.languages = languages;
    }

    fn import_start(&mut self, window: &MainWindow) {
        let Some(si) = self.session_index(&self.import.session) else { return };
        let Some(d) = &self.import.dialog else { return };
        let language = |i: i32| usize::try_from(i).ok().and_then(|i| self.import.languages.get(i)).cloned().unwrap_or_default();
        let (source_language, target_language) = (language(d.get_source_language()), language(d.get_target_language()));
        let memory = Memory {
            sources: self.import.sources.clone(),
            source_encoding: text_codec::default_for(&source_language).to_string(),
            target_encoding: text_codec::default_for(&target_language).to_string(),
            source_language,
            target_language,
        };
        let options = Options { match_by_text: d.get_by_text(), changed_as_review: d.get_changed_as_review(), overwrite: d.get_overwrite() };
        if options.overwrite {
            let question = tr("Replace the translations the project already has? This rewrites the files of the output folder.");
            self.ask(&tr("Import an existing translation"), &question, move |app, w| app.import_run(w, si, memory, options));
        } else {
            self.import_run(window, si, memory, options);
        }
    }

    fn import_run(&mut self, window: &MainWindow, si: usize, memory: Memory, options: Options) {
        let project = self.sessions[si].project.clone();
        let Some(d) = &self.import.dialog else { return };
        d.set_running(true);
        d.set_result(tr("Importing...").into());
        self.import.stop = Arc::new(AtomicBool::new(false));
        let stop = self.import.stop.clone();
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let report = translation_memory::import(&project, &memory, &options, None, Some(&stop));
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.import_finished(w, report));
            });
        });
    }

    fn import_finished(&mut self, _window: &MainWindow, report: Report) {
        self.import.imported = true;
        let Some(d) = &self.import.dialog else { return };
        d.set_running(false);
        let mut text = trf(
            "%1 file(s) examined, %2 written.\n%3 line(s) with the same text, %4 with a changed source (to review), %5 found under another id; %6 already translated and kept.",
            &[
                &report.files.to_string(),
                &report.saved.to_string(),
                &report.exact.to_string(),
                &report.changed.to_string(),
                &report.by_text.to_string(),
                &report.kept.to_string(),
            ],
        );
        if !report.errors.is_empty() {
            text.push_str("\n⚠ ");
            text.push_str(&report.errors.iter().take(5).cloned().collect::<Vec<_>>().join("\n⚠ "));
        }
        d.set_result(text.into());
    }

    fn import_close(&mut self, window: &MainWindow) {
        if self.import.dialog.as_ref().is_some_and(|d| d.get_running()) {
            self.import.stop.store(true, Ordering::Relaxed);
            if let Some(d) = &self.import.dialog {
                d.set_result(tr("Cancelling...").into());
            }
            return;
        }
        let imported = self.import.imported;
        let id = self.import.session.clone();
        if let Some(d) = &self.import.dialog {
            let _ = d.hide();
        }
        if imported {
            if let Some(si) = self.session_index(&id) {
                self.sessions[si].project.reload(); // pick up the files written in the output folder
                self.schedule_archive(&id);
                self.start_analysis(window, &id, true);
            }
        }
        self.import.imported = false;
    }
}
