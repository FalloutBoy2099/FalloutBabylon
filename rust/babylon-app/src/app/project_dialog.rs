//! The dialog that creates a project or edits its settings, and the opening / closing of projects.

use super::{with_app, App};
use crate::{MainWindow, ProjectDialog};
use babylon_core::i18n::{tr, trf};
use babylon_core::output_archive;
use babylon_core::paths;
use babylon_core::project::ProjectConfig;
use babylon_core::project_store::ProjectStore;
use babylon_core::text_codec::{self, Info};
use babylon_core::vfs::VirtualFileSystem;
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

const LANGUAGES: [&str; 10] =
    ["english", "french", "german", "spanish", "italian", "portuguese", "polish", "czech", "russian", "chinese"];

pub(crate) struct ProjectDialogState {
    pub dialog: ProjectDialog,
    base: ProjectConfig,
    is_new: bool,
    sources: Vec<String>,
    source_index: i32,
    output_edited: bool,
    found: Vec<String>,
    encodings: Vec<Info>,
}

fn shared(list: &[String]) -> ModelRc<SharedString> {
    ModelRc::new(VecModel::from(list.iter().map(|s| SharedString::from(s.as_str())).collect::<Vec<_>>()))
}

/// Where translated files go by default: the game's data folder, else the folder of a mod, else a folder in
/// Documents. An extracted master.dat / critter.dat folder stands for an archive and is never used.
pub fn default_output_for(sources: &[String], project_name: &str) -> String {
    for s in sources.iter().rev() {
        if paths::is_dir(s) && paths::file_name(s).eq_ignore_ascii_case("data") {
            return paths::clean_path(s);
        }
    }
    for s in sources {
        if paths::is_dir(s) && !paths::file_name(s).to_lowercase().ends_with(".dat") {
            return VirtualFileSystem::resolve_folder_root(&paths::clean_path(s));
        }
    }
    let docs = dirs::document_dir().map(|d| d.to_string_lossy().replace('\\', "/")).unwrap_or_default();
    let name = if project_name.trim().is_empty() { "translation" } else { project_name.trim() };
    paths::join(&docs, &format!("FalloutBabylon/{name}/data"))
}

fn default_archive_for(sources: &[String], name: &str, language: &str) -> String {
    let folder = default_output_for(sources, name);
    let base = if paths::file_name(&folder).eq_ignore_ascii_case("data") { paths::parent_dir(&folder) } else { folder };
    let language = language.trim().to_lowercase();
    paths::join(&base, &format!("{}.dat", if language.is_empty() { "translation".to_string() } else { language }))
}

impl App {
    /// `existing`: the project to edit (`None`: a new one); `folder`: an installation to add at once.
    pub fn open_project_dialog(&mut self, window: &MainWindow, existing: Option<ProjectConfig>, folder: Option<String>) {
        // one dialog at a time: an open one is brought forward
        if let Some(s) = &self.project_dialog {
            if s.dialog.window().is_visible() {
                self.present(&s.dialog);
                return;
            }
        }
        let Ok(dialog) = ProjectDialog::new() else { return };
        let is_new = existing.is_none();
        let config = existing.unwrap_or_default();
        let encodings = text_codec::available();
        let archive_mode = output_archive::enabled(&config);

        dialog.set_new_project(is_new);
        dialog.set_project_name(config.name.as_str().into());
        dialog.set_source_language(config.source_language.as_str().into());
        dialog.set_target_language(config.target_language.as_str().into());
        dialog.set_encoding_names(shared(&encodings.iter().map(|e| e.label.clone()).collect::<Vec<_>>()));
        let index_of = |id: &str| encodings.iter().position(|e| e.id == id).unwrap_or(0) as i32;
        dialog.set_source_encoding(index_of(&config.source_encoding));
        dialog.set_target_encoding(index_of(&config.target_encoding));
        dialog.set_output_mode(i32::from(archive_mode));
        dialog.set_output_folder(if archive_mode { String::new() } else { config.output_dir.clone() }.into());
        dialog.set_output_archive(config.output_archive.as_str().into());
        dialog.set_archive_format(i32::from(config.output_archive_format == "dat1"));
        dialog.set_archive_with_source(config.archive_with_source);
        dialog.set_recent_folders(shared(&self.settings.recent_folders));

        self.project_dialog = Some(ProjectDialogState {
            dialog,
            sources: config.sources.clone(),
            output_edited: !is_new && !archive_mode,
            base: config,
            is_new,
            source_index: -1,
            found: Vec::new(),
            encodings,
        });
        self.pd_wire(window);
        if let Some(folder) = folder {
            self.pd_add_installation(&folder);
        }
        self.pd_sync_sources();
        let Some(state) = &self.project_dialog else { return };
        self.present(&state.dialog);
    }

    fn pd_wire(&mut self, _window: &MainWindow) {
        let Some(state) = &self.project_dialog else { return };
        let d = &state.dialog;
        d.on_confirmed(|| {
            with_app(|app, w| app.pd_confirm(w));
        });
        d.on_dismissed(|| {
            with_app(|app, _| {
                if let Some(s) = app.project_dialog.take() {
                    let _ = s.dialog.hide();
                }
            });
        });
        d.on_add_install(|| {
            with_app(|app, w| {
                if let Some(dir) = app.picker(w).set_title(tr("Fallout folder")).pick_folder() {
                    app.pd_add_installation(&dir.to_string_lossy());
                    app.pd_sync_sources();
                }
            });
        });
        d.on_add_folder(|| {
            with_app(|app, w| {
                if let Some(dir) = app.picker(w).set_title(tr("Mod folder (containing text/ or data/)")).pick_folder() {
                    app.pd_add_source(&dir.to_string_lossy());
                    app.pd_sync_sources();
                }
            });
        });
        d.on_add_dat(|| {
            with_app(|app, w| {
                if let Some(files) = app.picker(w).set_title(tr("DAT archives")).add_filter(tr("DAT archives"), &["dat"]).pick_files() {
                    for f in files {
                        app.pd_add_source(&f.to_string_lossy());
                    }
                    app.pd_sync_sources();
                }
            });
        });
        d.on_add_recent(|i| {
            with_app(|app, _| {
                if let Some(folder) = usize::try_from(i).ok().and_then(|i| app.settings.recent_folders.get(i).cloned()) {
                    app.pd_add_installation(&folder);
                    app.pd_sync_sources();
                }
            });
        });
        d.on_select_source(|i| {
            with_app(|app, _| {
                if let Some(s) = &mut app.project_dialog {
                    s.source_index = i;
                    s.dialog.set_source_index(i);
                }
            });
        });
        d.on_move_source(|delta| {
            with_app(|app, _| {
                if let Some(s) = &mut app.project_dialog {
                    let r = s.source_index;
                    let to = r + delta;
                    if r >= 0 && to >= 0 && (to as usize) < s.sources.len() {
                        s.sources.swap(r as usize, to as usize);
                        s.source_index = to;
                    }
                }
                app.pd_sync_sources();
            });
        });
        d.on_remove_source(|| {
            with_app(|app, _| {
                if let Some(s) = &mut app.project_dialog {
                    if s.source_index >= 0 && (s.source_index as usize) < s.sources.len() {
                        s.sources.remove(s.source_index as usize);
                        s.source_index = -1;
                    }
                }
                app.pd_sync_sources();
            });
        });
        d.on_browse_output(|| {
            with_app(|app, _| {
                let Some(s) = &mut app.project_dialog else { return };
                let start = s.dialog.get_output_folder().to_string();
                let mut dialog = super::modal::picker(s.dialog.window()).set_title(tr("Output folder"));
                if !start.is_empty() {
                    dialog = dialog.set_directory(&start);
                }
                if let Some(dir) = dialog.pick_folder() {
                    s.dialog.set_output_folder(dir.to_string_lossy().replace('\\', "/").into());
                    s.output_edited = true;
                }
                app.pd_update_hints();
            });
        });
        d.on_browse_archive(|| {
            with_app(|app, _| {
                let Some(s) = &mut app.project_dialog else { return };
                let current = s.dialog.get_output_archive().to_string();
                let start = if current.is_empty() {
                    default_archive_for(&s.sources, &s.dialog.get_project_name(), &s.dialog.get_target_language())
                } else {
                    current
                };
                let mut dialog = super::modal::picker(s.dialog.window()).set_title(tr("Output archive")).add_filter(tr("DAT archives"), &["dat"]);
                if let Some(dir) = std::path::Path::new(&start).parent() {
                    dialog = dialog.set_directory(dir);
                }
                if let Some(name) = std::path::Path::new(&start).file_name() {
                    dialog = dialog.set_file_name(name.to_string_lossy());
                }
                if let Some(file) = dialog.save_file() {
                    let mut text = file.to_string_lossy().replace('\\', "/");
                    if !text.to_lowercase().ends_with(".dat") {
                        text.push_str(".dat");
                    }
                    s.dialog.set_output_archive(text.into());
                }
                app.pd_update_hints();
            });
        });
        d.on_name_edited(|| {
            with_app(|app, _| {
                app.pd_update_output_default();
                app.pd_update_hints();
            });
        });
        d.on_languages_edited(|| {
            with_app(|app, _| {
                let Some(s) = &mut app.project_dialog else { return };
                // the encodings follow the languages
                let target = s.dialog.get_target_language().to_string();
                let source = s.dialog.get_source_language().to_string();
                let index_of = |encodings: &[Info], id: &str| encodings.iter().position(|e| e.id == id).unwrap_or(0) as i32;
                s.dialog.set_target_encoding(index_of(&s.encodings, text_codec::default_for(&target)));
                s.dialog.set_source_encoding(index_of(&s.encodings, text_codec::default_for(&source)));
                app.pd_update_hints();
            });
        });
        d.on_output_edited(|| {
            with_app(|app, _| {
                if let Some(s) = &mut app.project_dialog {
                    if s.dialog.get_output_mode() == 0 {
                        s.output_edited = true;
                    } else if s.dialog.get_output_archive().trim().is_empty() {
                        let archive = default_archive_for(&s.sources, &s.dialog.get_project_name(), &s.dialog.get_target_language());
                        s.dialog.set_output_archive(archive.into());
                        let id = output_archive::format_id(output_archive::detect_format(&s.sources));
                        s.dialog.set_archive_format(i32::from(id == "dat1"));
                    }
                }
                app.pd_update_hints();
            });
        });
    }

    fn pd_add_source(&mut self, path: &str) {
        let clean = paths::clean_path(&paths::absolute_file_path(path));
        let Some(s) = &mut self.project_dialog else { return };
        if s.sources.iter().any(|x| paths::same_path(x, &clean)) {
            return;
        }
        s.sources.push(clean.clone());
        self.settings.add_recent_folder(&clean);
    }

    fn pd_add_installation(&mut self, folder: &str) {
        let clean = paths::clean_path(folder);
        for s in VirtualFileSystem::discover_sources(&clean) {
            self.pd_add_source(&s);
        }
        if let Some(s) = &self.project_dialog {
            if s.dialog.get_project_name().is_empty() {
                s.dialog.set_project_name(paths::file_name(&clean).into());
            }
        }
    }

    /// The list of sources changed: languages are detected again and the defaults follow.
    fn pd_sync_sources(&mut self) {
        let Some(s) = &mut self.project_dialog else { return };
        s.dialog.set_sources(shared(&s.sources));
        s.dialog.set_source_index(s.source_index);
        if s.sources.is_empty() {
            s.found.clear();
            s.dialog.set_hint(tr("Add the game folder or the archives of the mod to translate.").into());
        } else {
            let mut vfs = VirtualFileSystem::new();
            let mut errors = Vec::new();
            vfs.set_sources(&s.sources, Some(&mut errors));
            s.found = vfs.languages();
            let mut text = if s.found.is_empty() {
                tr("No text/<language>/ folder found in these sources.")
            } else {
                trf("Languages found: %1.", &[&s.found.join(", ")])
            };
            if !errors.is_empty() {
                text.push('\n');
                text.push_str(&errors.join("\n"));
            }
            s.dialog.set_hint(text.into());
            if s.is_new && !s.found.is_empty() && !s.found.contains(&s.dialog.get_source_language().to_lowercase()) {
                let pick = if s.found.iter().any(|l| l == "english") { "english".to_string() } else { s.found[0].clone() };
                s.dialog.set_source_language(pick.into());
            }
        }
        // the detected languages are offered first
        let mut choices = s.found.clone();
        for l in LANGUAGES {
            if !choices.iter().any(|c| c == l) {
                choices.push(l.to_string());
            }
        }
        s.dialog.set_language_choices(shared(&choices));
        self.pd_update_output_default();
        self.pd_update_hints();
    }

    fn pd_update_output_default(&mut self) {
        let Some(s) = &mut self.project_dialog else { return };
        if s.output_edited {
            return;
        }
        s.dialog.set_output_folder(default_output_for(&s.sources, &s.dialog.get_project_name()).into());
    }

    /// The language selectors show the language that is typed (when it is one of the choices).
    fn pd_sync_language_indexes(&self) {
        let Some(s) = &self.project_dialog else { return };
        let choices = s.dialog.get_language_choices();
        let find = |name: String| (0..slint::Model::row_count(&choices)).find(|&i| slint::Model::row_data(&choices, i).is_some_and(|c| c.eq_ignore_ascii_case(name.trim())));
        if let Some(i) = find(s.dialog.get_source_language().to_string()) {
            s.dialog.set_source_language_index(i as i32);
        }
        if let Some(i) = find(s.dialog.get_target_language().to_string()) {
            s.dialog.set_target_language_index(i as i32);
        }
    }

    fn pd_update_hints(&mut self) {
        self.pd_sync_language_indexes();
        let Some(s) = &self.project_dialog else { return };
        let language = s.dialog.get_target_language().trim().to_lowercase();
        let shown = if language.is_empty() { "<language>".to_string() } else { language };
        let text = if s.dialog.get_output_mode() == 1 {
            let mut text = trf(
                "The translation is delivered in %1, rebuilt after each save. It contains text/%2/ with the translated files",
                &[s.dialog.get_output_archive().trim(), &shown],
            );
            if s.dialog.get_archive_with_source() {
                text.push_str(&trf(", and the original texts under text/%1/.", &[&s.dialog.get_source_language().trim().to_lowercase()]));
            } else {
                text.push('.');
            }
            text
        } else {
            trf(
                "Translated files are written to %1 (this folder overrides the sources).",
                &[&paths::join(s.dialog.get_output_folder().trim(), &format!("text/{shown}/"))],
            )
        };
        s.dialog.set_output_hint(text.into());
    }

    fn pd_config(&self) -> Option<ProjectConfig> {
        let s = self.project_dialog.as_ref()?;
        let d = &s.dialog;
        let mut c = s.base.clone();
        c.name = d.get_project_name().trim().to_string();
        c.sources = s.sources.clone();
        if c.id.is_empty() {
            c.id = ProjectStore::new_id();
        }
        if d.get_output_mode() == 1 {
            // the files are kept in a working folder of the application; the archive is built from it
            c.output_archive = d.get_output_archive().trim().replace('\\', "/");
            c.output_archive_format = if d.get_archive_format() == 1 { "dat1" } else { "dat2" }.into();
            c.archive_with_source = d.get_archive_with_source();
            c.output_dir = self.store.staging_dir(&c.id).to_string_lossy().replace('\\', "/");
        } else {
            c.output_archive.clear();
            c.output_dir = d.get_output_folder().trim().replace('\\', "/");
        }
        c.source_language = d.get_source_language().trim().to_lowercase();
        c.target_language = d.get_target_language().trim().to_lowercase();
        c.source_encoding = s.encodings.get(d.get_source_encoding().max(0) as usize)?.id.clone();
        c.target_encoding = s.encodings.get(d.get_target_encoding().max(0) as usize)?.id.clone();
        Some(c)
    }

    fn pd_confirm(&mut self, window: &MainWindow) {
        let Some(c) = self.pd_config() else { return };
        let Some(state) = &self.project_dialog else { return };
        let archive = state.dialog.get_output_mode() == 1;
        let problem = if c.name.is_empty() {
            Some(tr("Give the project a name."))
        } else if c.sources.is_empty() {
            Some(tr("Add at least one source."))
        } else if c.source_language.is_empty() || c.target_language.is_empty() {
            Some(tr("Choose the source and the target language."))
        } else if c.source_language == c.target_language {
            Some(tr("The source and target languages must differ."))
        } else if !archive && c.output_dir.is_empty() {
            Some(tr("Choose an output folder."))
        } else if archive && c.output_archive.is_empty() {
            Some(tr("Choose the output archive."))
        } else {
            output_archive::problem_with(&c)
        };
        if let Some(problem) = problem {
            state.dialog.set_error(problem.into());
            return;
        }
        let is_new = state.is_new;
        let id = c.id.clone();
        if let Some(s) = self.project_dialog.take() {
            let _ = s.dialog.hide();
        }
        if let Err(e) = self.store.save(&c) {
            self.notify(&tr("Project"), &e.to_string());
            return;
        }
        if !is_new {
            // the project is reopened with its new settings
            if !self.close_project(window, &id) {
                return;
            }
        }
        self.open_project(window, c);
    }

    pub fn new_project(&mut self, window: &MainWindow) {
        self.open_project_dialog(window, None, None);
    }

    pub fn open_game_folder(&mut self, window: &MainWindow) {
        let mut dialog = self.picker(window).set_title(tr("Open a Fallout game or mod folder"));
        if let Some(start) = self.settings.recent_folders.first() {
            dialog = dialog.set_directory(start);
        }
        if let Some(folder) = dialog.pick_folder() {
            self.open_project_dialog(window, None, Some(folder.to_string_lossy().replace('\\', "/")));
        }
    }

    pub fn edit_project_settings(&mut self, window: &MainWindow, id: &str) {
        let Some(si) = self.session_index(id) else { return };
        if self.editors.iter().any(|e| e.session == id && e.model.is_dirty()) {
            self.notify(&tr("Project settings"), &tr("Save or close the modified files of this project first."));
            return;
        }
        let config = self.sessions[si].project.config().clone();
        self.open_project_dialog(window, Some(config), None);
    }

    /// Closes a project and its files, after asking about the modified ones. Returns true when it was closed at
    /// once (false: a question is pending, or the project is not open).
    pub fn close_project(&mut self, window: &MainWindow, id: &str) -> bool {
        if self.session_index(id).is_none() {
            return true;
        }
        let uids: Vec<u64> = self.editors.iter().filter(|e| e.session == id).map(|e| e.uid).collect();
        if self.editors.iter().any(|e| uids.contains(&e.uid) && e.model.is_dirty()) {
            let id = id.to_string();
            self.confirm_close(window, uids, move |app, w| {
                app.close_project_now(w, &id);
            });
            return false;
        }
        self.close_project_now(window, id)
    }

    fn close_project_now(&mut self, window: &MainWindow, id: &str) -> bool {
        let Some(si) = self.session_index(id) else { return true };
        let current_uid = self.current.and_then(|c| self.editors.get(c)).map(|e| e.uid);
        self.editors.retain(|e| e.session != id);
        self.current = current_uid
            .and_then(|uid| self.editors.iter().position(|e| e.uid == uid))
            .or(if self.editors.is_empty() { None } else { Some(self.editors.len() - 1) });
        self.sessions[si].generation += 1; // results of its background jobs are dropped
        self.sessions[si].project.save_state();
        self.archive_flush_one(id);
        self.sessions.remove(si);
        self.settings.open_projects.retain(|p| p != id);
        self.rebuild_tree(window);
        self.refresh_saved_menu(window);
        self.show_editor(window);
        self.update_summary(window);
        self.refresh_panel(window);
        true
    }
}
