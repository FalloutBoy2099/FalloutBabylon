//! Texts written in the code of the scripts (.int): scan, translate, write the translated scripts.

use super::{one_line, table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::project::ScriptTranslation;
use babylon_core::script_strings::{self, Text};
use babylon_core::text_codec;
use slint::ComponentHandle;
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

pub(crate) struct ScriptsState {
    texts: Vec<Text>,
    /// "script#operand" -> translation
    targets: HashMap<String, String>,
    shown: Vec<usize>,
    selected: Option<usize>,
    generation: u64,
    cancel: Arc<AtomicBool>,
    /// The project whose translations `targets` holds.
    project: String,
}

impl Default for ScriptsState {
    fn default() -> Self {
        ScriptsState {
            texts: Vec::new(),
            targets: HashMap::new(),
            shown: Vec::new(),
            selected: None,
            generation: 0,
            cancel: Arc::new(AtomicBool::new(false)),
            project: String::new(),
        }
    }
}

fn key_of(script: &str, operand: u32) -> String {
    format!("{script}#{operand}")
}

pub(super) fn wire(host: &Host) {
    let data = host.scripts();
    data.on_scan(|| {
        with_app(|app, w| app.scripts_scan(w));
    });
    data.on_apply(|| {
        with_app(|app, w| app.scripts_apply(w));
    });
    data.on_enlarge_toggled(|| {
        with_app(|app, w| {
            app.settings.script_enlarge = app.host_of(super::SCRIPTS, w).scripts().get_enlarge();
            app.scripts_show(w);
        });
    });
    data.on_filter_changed(|| {
        with_app(|app, w| app.scripts_show(w));
    });
    data.on_selected(|i| {
        with_app(|app, w| app.scripts_select(w, i));
    });
    data.on_translation_edited(|text| {
        with_app(|app, w| app.scripts_edited(w, &text));
    });
}

impl App {
    /// Translations of the project, as they are known without scanning.
    fn scripts_translations(&self) -> Vec<ScriptTranslation> {
        let mut out = Vec::new();
        for t in &self.scripts.texts {
            if let Some(target) = self.scripts.targets.get(&key_of(&t.script, t.operand)).filter(|s| !s.is_empty()) {
                out.push(ScriptTranslation { script: t.script.clone(), operand: t.operand, source: t.source.clone(), target: target.clone() });
            }
        }
        out
    }

    pub fn refresh_scripts_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::SCRIPTS, window);
        let data = host.scripts();
        let Some(si) = self.active_session() else {
            data.set_status(tr("No project open.").into());
            data.set_can_scan(false);
            data.set_can_apply(false);
            return;
        };
        let session = &self.sessions[si];
        if self.scripts.project != session.id {
            // another project: what was scanned does not apply
            self.scripts = Default::default();
            self.scripts.project = session.id.clone();
            for t in session.project.script_translations() {
                self.scripts.targets.insert(key_of(&t.script, t.operand), t.target);
            }
            data.set_rows(table_model(Vec::new()));
            data.set_enlarge(self.settings.script_enlarge);
            data.set_status(
                if session.project.script_names().is_empty() {
                    tr("The sources of this project contain no compiled script.")
                } else {
                    tr("Press \"Scan the scripts\".")
                }
                .into(),
            );
        }
        data.set_can_scan(true);
        data.set_can_apply(true);
    }

    fn scripts_scan(&mut self, window: &MainWindow) {
        let host = self.host_of(super::SCRIPTS, window);
        let data = host.scripts();
        let Some(si) = self.active_session() else { return };
        self.scripts.cancel.store(true, Ordering::Relaxed);
        self.scripts.cancel = Arc::new(AtomicBool::new(false));
        self.scripts.generation += 1;
        let generation = self.scripts.generation;
        let cancel = self.scripts.cancel.clone();
        data.set_can_scan(false);
        data.set_status(tr("Scanning the scripts...").into());
        let project = self.sessions[si].project.clone();
        let weak = window.as_weak();
        std::thread::spawn(move || {
            let progress_weak = weak.clone();
            let mut progress = move |done: usize, total: usize| {
                let _ = progress_weak.upgrade_in_event_loop(move |_| {
                    with_app(|app, w| {
                        app.host_of(super::SCRIPTS, w)
                            .scripts()
                            .set_status(trf("Scanning the scripts... %1 / %2", &[&done.to_string(), &total.to_string()]).into());
                    });
                });
            };
            let texts = script_strings::scan(&project, Some(&mut progress), Some(&cancel));
            if cancel.load(Ordering::Relaxed) {
                return;
            }
            let _ = weak.upgrade_in_event_loop(move |_| {
                with_app(|app, w| {
                    if generation != app.scripts.generation {
                        return;
                    }
                    app.scripts.texts = texts;
                    app.host_of(super::SCRIPTS, w).scripts().set_can_scan(true);
                    app.scripts_show(w);
                });
            });
        });
    }

    fn room_text(&self, t: &Text, encoding: &str, enlarge: bool) -> (String, bool) {
        let target = self.scripts.targets.get(&key_of(&t.script, t.operand)).cloned().unwrap_or_default();
        if target.is_empty() {
            return (trf("%1 max", &[&t.capacity.to_string()]), false);
        }
        let length = text_codec::encode(&target, encoding, None).len();
        if length <= t.capacity {
            (format!("{length} / {}", t.capacity), false)
        } else if enlarge {
            (format!("{length} / {} · {}", t.capacity, tr("will be appended")), false)
        } else {
            (format!("{length} / {} · {}", t.capacity, tr("too long")), true)
        }
    }

    fn scripts_show(&mut self, window: &MainWindow) {
        let host = self.host_of(super::SCRIPTS, window);
        let data = host.scripts();
        let Some(si) = self.active_session() else { return };
        let encoding = self.sessions[si].project.config().target_encoding.clone();
        let enlarge = data.get_enlarge();
        let needle = data.get_filter().trim().to_lowercase();
        let shown: Vec<usize> = (0..self.scripts.texts.len())
            .filter(|&i| {
                let t = &self.scripts.texts[i];
                needle.is_empty()
                    || t.script.to_lowercase().contains(&needle)
                    || t.source.to_lowercase().contains(&needle)
                    || self.scripts.targets.get(&key_of(&t.script, t.operand)).is_some_and(|x| x.to_lowercase().contains(&needle))
            })
            .collect();
        let rows = shown
            .iter()
            .map(|&i| {
                let t = &self.scripts.texts[i];
                let target = self.scripts.targets.get(&key_of(&t.script, t.operand)).cloned().unwrap_or_default();
                let (room, too_long) = self.room_text(t, &encoding, enlarge);
                let mut row = table_row(vec![t.script.clone(), one_line(&t.source, 200), one_line(&target, 200), room]);
                row.warn = too_long;
                row
            })
            .collect();
        data.set_rows(table_model(rows));
        let translated = self.scripts.texts.iter().filter(|t| self.scripts.targets.contains_key(&key_of(&t.script, t.operand))).count();
        data.set_status(trf("%1 text(s) in the scripts, %2 translated.", &[&self.scripts.texts.len().to_string(), &translated.to_string()]).into());
        self.scripts.shown = shown;
    }

    fn scripts_select(&mut self, window: &MainWindow, row: i32) {
        let host = self.host_of(super::SCRIPTS, window);
        let data = host.scripts();
        let Some(&index) = usize::try_from(row).ok().and_then(|r| self.scripts.shown.get(r)) else { return };
        let t = &self.scripts.texts[index];
        self.scripts.selected = Some(index);
        data.set_current(row);
        data.set_original(t.source.as_str().into());
        data.set_translation(self.scripts.targets.get(&key_of(&t.script, t.operand)).cloned().unwrap_or_default().into());
        data.set_editing(true);
    }

    fn scripts_edited(&mut self, window: &MainWindow, text: &str) {
        let Some(index) = self.scripts.selected else { return };
        let t = &self.scripts.texts[index];
        let key = key_of(&t.script, t.operand);
        let text = text.trim();
        if text.is_empty() {
            self.scripts.targets.remove(&key);
        } else {
            self.scripts.targets.insert(key, text.to_string());
        }
        let translations = self.scripts_translations();
        if let Some(si) = self.active_session() {
            self.sessions[si].project.set_script_translations(translations);
        }
        // refresh the rows without losing the selection
        let current = self.host_of(super::SCRIPTS, window).scripts().get_current();
        let editing = self.scripts.selected;
        self.scripts_show(window);
        self.host_of(super::SCRIPTS, window).scripts().set_current(current);
        self.scripts.selected = editing;
    }

    fn scripts_apply(&mut self, window: &MainWindow) {
        let Some(si) = self.active_session() else { return };
        let enlarge = self.host_of(super::SCRIPTS, window).scripts().get_enlarge();
        // before any scan, what the project already holds is what there is
        let translations = if self.scripts.texts.is_empty() {
            self.sessions[si].project.script_translations()
        } else {
            self.scripts_translations()
        };
        let project = self.sessions[si].project.clone();
        project.set_script_translations(translations.clone());
        let id = self.sessions[si].id.clone();
        let text = match script_strings::apply(&project, &translations, enlarge) {
            Ok(result) => {
                let mut text = trf(
                    "%1 script(s) written: %2 text(s) in place, %3 lengthened.",
                    &[&result.scripts.to_string(), &result.in_place.to_string(), &result.enlarged.to_string()],
                );
                if !result.problems.is_empty() {
                    text.push_str("\n\n");
                    text.push_str(&tr("Not written:"));
                    text.push('\n');
                    text.push_str(&result.problems.iter().take(10).cloned().collect::<Vec<_>>().join("\n"));
                }
                text
            }
            Err(e) => e.to_string(),
        };
        self.schedule_archive(&id);
        self.notify(&tr("Texts in the scripts"), &text);
    }
}
