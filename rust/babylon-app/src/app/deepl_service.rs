//! DeepL: the project glossary on the account, translating the current line, and the settings dialog that
//! holds the key and the correction servers.

use super::{with_app, App};
use crate::{MainWindow, SettingsDialog};
use babylon_core::deepl::{self, DeepLClient, Options};
use babylon_core::doc_model::DocModel;
use babylon_core::grammar::{Engine, GrammarClient};
use babylon_core::i18n::{tr, trf};
use babylon_core::languages;
use babylon_core::project::{Project, TranslationDoc};
use babylon_core::statistics;
use slint::ComponentHandle;
use std::collections::HashMap;

type Then = Box<dyn FnOnce(&mut App, &MainWindow, String, String)>;

#[derive(Default)]
pub(crate) struct DeepLState {
    pending: HashMap<u64, Then>,
    next_token: u64,
    pub translating: bool,
    settings_dialog: Option<SettingsDialog>,
}

/// What a worker thread needs to create the glossary of a project on the DeepL account.
#[derive(Clone)]
struct GlossaryJob {
    key: String,
    project_id: String,
    name: String,
    source: String,
    target: String,
    entries: Vec<(String, String)>,
    signature: String,
    old_id: String,
}

enum GlossaryOutcome {
    Created(String),
    /// The free plan holds one glossary and the account already has some.
    Limit { existing: Vec<(String, String)>, message: String },
    Failed(String),
}

fn create_on_account(client: &DeepLClient, job: &GlossaryJob, delete_existing: Option<&[(String, String)]>) -> GlossaryOutcome {
    if let Some(existing) = delete_existing {
        for (id, _) in existing {
            if let Err(e) = client.delete_glossary(&job.key, id) {
                return GlossaryOutcome::Failed(e.to_string());
            }
        }
    }
    match client.create_glossary(&job.key, &job.name, &job.source, &job.target, &job.entries) {
        Ok(id) => GlossaryOutcome::Created(id),
        Err(e) => {
            let message = e.to_string();
            if message == deepl::glossary_limit_message() && delete_existing.is_none() {
                match client.list_glossaries(&job.key) {
                    Ok(existing) if !existing.is_empty() => GlossaryOutcome::Limit { existing, message },
                    _ => GlossaryOutcome::Failed(message),
                }
            } else {
                GlossaryOutcome::Failed(message)
            }
        }
    }
}

/// The context sent along with a line: its neighbours (not translated, not billed).
pub fn context_for(doc: &TranslationDoc, row: usize) -> String {
    let line = |i: isize| -> String {
        if i < 0 {
            return String::new();
        }
        doc.rows.get(i as usize).map(|r| r.source.split_whitespace().collect::<Vec<_>>().join(" ").chars().take(160).collect()).unwrap_or_default()
    };
    let r = row as isize;
    let mut parts = vec![format!("Dialogue line from the video game Fallout ({}).", statistics::display_name_for(&doc.rel_path))];
    let before = format!("{} {}", line(r - 2), line(r - 1)).trim().to_string();
    let after = line(r + 1);
    if !before.is_empty() {
        parts.push(format!("Previous lines: {before}"));
    }
    if !after.is_empty() {
        parts.push(format!("Next line: {after}"));
    }
    parts.join("\n").chars().take(700).collect()
}

impl DeepLState {
    pub fn settings_snapshot(&self) -> Option<slint::SharedPixelBuffer<slint::Rgba8Pixel>> {
        self.settings_dialog.as_ref().and_then(|d| d.window().take_snapshot().ok())
    }
}

impl App {
    pub fn deepl_configured(&self) -> bool {
        !self.settings.deepl_key.trim().is_empty()
    }

    pub fn deepl_options(&self, project: &Project) -> Options {
        Options {
            api_key: self.settings.deepl_key.clone(),
            source_language: project.config().source_language.clone(),
            target_language: project.config().target_language.clone(),
            glossary_id: String::new(),
            formality: self.settings.deepl_formality.clone(),
            // without a DeepL glossary (free plan limit, unsupported language pair...) the terms are substituted locally
            local_glossary: if self.settings.deepl_use_glossary { Some(project.glossary()) } else { None },
        }
    }

    /// Makes sure the glossary of the project is on the DeepL account, then calls `then(glossary id, warning)`
    /// (the id is empty when no glossary is used).
    pub fn with_deepl_glossary(
        &mut self,
        window: &MainWindow,
        si: usize,
        then: impl FnOnce(&mut App, &MainWindow, String, String) + 'static,
    ) {
        let project = self.sessions[si].project.clone();
        let config = project.config();
        let entries = deepl::glossary_entries(&project.glossary());
        if !self.settings.deepl_use_glossary
            || entries.is_empty()
            || languages::deepl_glossary(&config.source_language).is_empty()
            || languages::deepl_glossary(&config.target_language).is_empty()
        {
            then(self, window, String::new(), String::new());
            return;
        }
        let signature = deepl::glossary_signature(&config.source_language, &config.target_language, &entries);
        let saved = self.settings.deepl_glossaries.get(&config.id).cloned().unwrap_or_default();
        let mut parts = saved.splitn(2, '|');
        let (saved_signature, saved_id) = (parts.next().unwrap_or(""), parts.next().unwrap_or(""));
        if !saved_id.is_empty() && saved_signature == signature {
            then(self, window, saved_id.to_string(), String::new());
            return;
        }
        let job = GlossaryJob {
            key: self.settings.deepl_key.clone(),
            project_id: config.id.clone(),
            name: format!("FalloutBabylon {}", config.name),
            source: config.source_language.clone(),
            target: config.target_language.clone(),
            entries,
            signature,
            old_id: saved_id.to_string(),
        };
        let token = self.deepl.next_token;
        self.deepl.next_token += 1;
        self.deepl.pending.insert(token, Box::new(then));
        self.run_glossary_job(window, token, job, None);
    }

    fn run_glossary_job(&mut self, window: &MainWindow, token: u64, job: GlossaryJob, delete_existing: Option<Vec<(String, String)>>) {
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let client = DeepLClient::new();
            let outcome = create_on_account(&client, &job, delete_existing.as_deref());
            if let GlossaryOutcome::Created(_) = &outcome {
                if !job.old_id.is_empty() {
                    let _ = client.delete_glossary(&job.key, &job.old_id);
                }
            }
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| app.glossary_job_done(w, token, job, outcome));
            });
        });
    }

    fn glossary_job_done(&mut self, window: &MainWindow, token: u64, job: GlossaryJob, outcome: GlossaryOutcome) {
        match outcome {
            GlossaryOutcome::Created(id) => {
                self.settings.deepl_glossaries.insert(job.project_id.clone(), format!("{}|{id}", job.signature));
                self.finish_glossary(window, token, id, String::new());
            }
            GlossaryOutcome::Failed(message) => {
                self.finish_glossary(window, token, String::new(), trf("DeepL glossary not used: %1", &[&message]));
            }
            GlossaryOutcome::Limit { existing, message } => {
                let names: Vec<String> = existing.iter().map(|g| format!("• {}", g.1)).collect();
                let question = trf(
                    "Your DeepL account already contains a glossary:\n\n%1\n\nThe free plan allows a single glossary. Delete it and replace it with the glossary of this project?\n\nThis cannot be undone.",
                    &[&names.join("\n")],
                );
                let (yes_job, no_message) = (job, message);
                self.ask_choice(
                    &tr("DeepL glossary"),
                    &question,
                    move |app, w| app.run_glossary_job(w, token, yes_job, Some(existing)),
                    move |app, w| app.finish_glossary(w, token, String::new(), trf("DeepL glossary not used: %1", &[&no_message])),
                );
            }
        }
    }

    fn finish_glossary(&mut self, window: &MainWindow, token: u64, id: String, warning: String) {
        if let Some(then) = self.deepl.pending.remove(&token) {
            then(self, window, id, warning);
        }
    }

    // ------------------------------------------------------------------------- one line

    pub fn translate_current(&mut self, window: &MainWindow) {
        let Some(ed) = self.current_editor() else { return };
        let Some(row) = ed.current else { return };
        if self.deepl.translating {
            return;
        }
        if !self.deepl_configured() {
            self.notify(&tr("DeepL"), &tr("Enter your DeepL API key in Tools > Settings first."));
            return;
        }
        let source = ed.model.row(row).source.clone();
        if !statistics::is_translatable(&source) {
            return;
        }
        let (uid, row_id) = (ed.uid, ed.model.row(row).id);
        let Some(si) = self.session_index(&ed.session) else { return };
        let context = if self.settings.deepl_use_context { context_for(ed.model.doc(), row) } else { String::new() };
        self.deepl.translating = true;
        self.set_status(window, tr("Translating with DeepL..."));
        self.with_deepl_glossary(window, si, move |app, w, glossary_id, warning| {
            let project = app.sessions[si].project.clone();
            let mut options = app.deepl_options(&project);
            options.glossary_id = glossary_id;
            let weak = w.as_weak();
            std::thread::spawn(move || {
                let result = DeepLClient::new().translate(&source, &context, &options).map_err(|e| e.to_string());
                let _ = weak.upgrade_in_event_loop(move |_| {
                    with_app(|app, w| app.line_translated(w, uid, row_id, result, warning));
                });
            });
        });
    }

    fn line_translated(&mut self, window: &MainWindow, uid: u64, row_id: i32, result: Result<deepl::Translation, String>, warning: String) {
        self.deepl.translating = false;
        let mark_review = self.settings.deepl_mark_review;
        let Some(ci) = self.editors.iter().position(|e| e.uid == uid) else { return };
        match result {
            Err(e) => self.set_status(window, format!("✖ {e}")),
            Ok(t) => {
                let ed = &mut self.editors[ci];
                if let Some(row) = ed.model.row_for_id(row_id) {
                    ed.model.operation(&tr("DeepL translation"), |m: &mut DocModel| {
                        m.set_target(row, &t.text);
                        if mark_review {
                            m.set_review(row, true);
                        }
                    });
                    ed.refresh_row(row);
                }
                if Some(ci) == self.current {
                    self.document_changed(window);
                    self.show_current_row(window);
                }
                let note = t.warning.unwrap_or(warning);
                self.set_status(window, if note.is_empty() { String::new() } else { format!("⚠ {note}") });
            }
        }
    }

    // ------------------------------------------------------------------------- settings dialog

    pub fn open_settings_dialog(&mut self, _window: &MainWindow) {
        if let Some(d) = &self.deepl.settings_dialog {
            if d.window().is_visible() {
                self.present(d);
                return;
            }
        }
        let Ok(dialog) = SettingsDialog::new() else { return };
        let s = &self.settings;
        dialog.set_deepl_key(s.deepl_key.as_str().into());
        dialog.set_formality(match s.deepl_formality.as_str() {
            "prefer_more" => 1,
            "prefer_less" => 2,
            _ => 0,
        });
        dialog.set_use_glossary(s.deepl_use_glossary);
        dialog.set_use_context(s.deepl_use_context);
        dialog.set_mark_review(s.deepl_mark_review);
        dialog.set_check_enabled(s.check_enabled);
        dialog.set_grammalecte_url(s.grammalecte_url.as_str().into());
        dialog.set_language_tool_url(s.language_tool_url.as_str().into());

        let weak = dialog.as_weak();
        dialog.on_confirmed(move || {
            let Some(d) = weak.upgrade() else { return };
            let _ = d.hide();
            with_app(|app, w| {
                let s = &mut app.settings;
                s.deepl_key = d.get_deepl_key().trim().to_string();
                s.deepl_formality = match d.get_formality() {
                    1 => "prefer_more",
                    2 => "prefer_less",
                    _ => "",
                }
                .into();
                s.deepl_use_glossary = d.get_use_glossary();
                s.deepl_use_context = d.get_use_context();
                s.deepl_mark_review = d.get_mark_review();
                s.check_enabled = d.get_check_enabled();
                s.grammalecte_url = d.get_grammalecte_url().trim().to_string();
                s.language_tool_url = d.get_language_tool_url().trim().to_string();
                let _ = s.save(&app.store.settings_file());
                // the current line is checked again with the new servers
                app.schedule_check(w, 150);
            });
        });
        let weak = dialog.as_weak();
        dialog.on_dismissed(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
        });
        let weak = dialog.as_weak();
        dialog.on_test_deepl(move || {
            let Some(d) = weak.upgrade() else { return };
            d.set_key_status(tr("Contacting DeepL...").into());
            let key = d.get_deepl_key().trim().to_string();
            let weak = weak.clone();
            std::thread::spawn(move || {
                let message = match DeepLClient::new().usage(&key) {
                    Ok((used, limit)) => trf("✔ Key accepted. Usage: %L1 / %L2 characters this period.", &[&used.to_string(), &limit.to_string()]),
                    Err(e) => format!("✖ {e}"),
                };
                let _ = weak.upgrade_in_event_loop(move |d| d.set_key_status(message.into()));
            });
        });
        for grammalecte in [true, false] {
            let weak = dialog.as_weak();
            let handler = move || {
                let Some(d) = weak.upgrade() else { return };
                let url = if grammalecte { d.get_grammalecte_url() } else { d.get_language_tool_url() }.trim().to_string();
                let set = {
                    let weak = weak.clone();
                    move |text: String| {
                        let _ = weak.upgrade_in_event_loop(move |d| {
                            if grammalecte {
                                d.set_grammalecte_status(text.into())
                            } else {
                                d.set_language_tool_status(text.into())
                            }
                        });
                    }
                };
                if url.is_empty() {
                    set(tr("Enter the server address first."));
                    return;
                }
                set(tr("Contacting the server..."));
                std::thread::spawn(move || {
                    let engine = if grammalecte { Engine::Grammalecte } else { Engine::LanguageTool };
                    let text = match GrammarClient::new().check(engine, &url, "french", "Je suis aller au marché.") {
                        Ok(issues) => trf("✔ The server answers (%1 issue(s) found in the test sentence).", &[&issues.len().to_string()]),
                        Err(e) => format!("✖ {e}"),
                    };
                    set(text);
                });
            };
            if grammalecte {
                dialog.on_test_grammalecte(handler);
            } else {
                dialog.on_test_language_tool(handler);
            }
        }
        self.present(&dialog);
        self.deepl.settings_dialog = Some(dialog);
    }
}
