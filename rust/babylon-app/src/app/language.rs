//! The language of the interface: the texts of the Slint files come from the bundled .po files, the ones of
//! the Rust code from the same translations as JSON tables.

use super::panels::{localize_panels, panel_names};
use super::App;
use crate::MainWindow;
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};
use std::collections::HashMap;

/// (code, name in its own language)
pub const LANGUAGES: [(&str, &str); 10] = [
    ("en", "English"),
    ("fr", "Français"),
    ("es", "Español"),
    ("it", "Italiano"),
    ("pt", "Português"),
    ("de", "Deutsch"),
    ("pl", "Polski"),
    ("cs", "Čeština"),
    ("ru", "Русский"),
    ("zh", "中文"),
];

fn table(code: &str) -> Option<&'static str> {
    Some(match code {
        "fr" => include_str!("../../lang/fr.json"),
        "es" => include_str!("../../lang/es.json"),
        "it" => include_str!("../../lang/it.json"),
        "pt" => include_str!("../../lang/pt.json"),
        "de" => include_str!("../../lang/de.json"),
        "pl" => include_str!("../../lang/pl.json"),
        "cs" => include_str!("../../lang/cs.json"),
        "ru" => include_str!("../../lang/ru.json"),
        "zh" => include_str!("../../lang/zh.json"),
        _ => return None,
    })
}

/// The language code of the system ("fr-FR" -> "fr"), "en" when it is not one of the interface languages.
pub fn system_language() -> String {
    let locale = sys_locale::get_locale().unwrap_or_default().to_lowercase();
    let code = locale.split(['-', '_']).next().unwrap_or("en");
    if LANGUAGES.iter().any(|(c, _)| *c == code) {
        code.to_string()
    } else {
        "en".to_string()
    }
}

/// Selects the language of the interface: "auto" follows the system.
pub fn select_language(setting: &str) {
    let code = if setting == "auto" || !LANGUAGES.iter().any(|(c, _)| *c == setting) { system_language() } else { setting.to_string() };
    let _ = slint::select_bundled_translation(&code);
    match table(&code).and_then(|t| serde_json::from_str::<HashMap<String, String>>(t).ok()) {
        Some(map) => babylon_core::i18n::set_translations(map),
        None => babylon_core::i18n::clear_translations(),
    }
}

impl App {
    /// The entries of the language menu ("auto" first, the current one marked).
    pub fn language_names(&self) -> Vec<SharedString> {
        let mut names = vec![SharedString::from(babylon_core::i18n::tr("Automatic (system)"))];
        names.extend(LANGUAGES.iter().map(|(_, name)| SharedString::from(*name)));
        names
    }

    /// The choices of the View menu (languages, themes, panels). The models are replaced each time so that the
    /// check marks of the menu items are built again from the current state.
    pub fn sync_menu_choices(&self, window: &MainWindow) {
        let current = &self.settings.ui_language;
        let index = LANGUAGES.iter().position(|(c, _)| c == current).map_or(0, |i| i + 1);
        window.set_language_names(ModelRc::new(VecModel::from(self.language_names())));
        window.set_language_index(index as i32);
        let themes: Vec<SharedString> =
            ["System", "Light", "Dark"].iter().map(|n| SharedString::from(babylon_core::i18n::tr(n))).collect();
        window.set_theme_names(ModelRc::new(VecModel::from(themes)));
        window.set_panel_names(ModelRc::new(VecModel::from(panel_names())));
    }

    pub fn set_ui_language(&mut self, window: &MainWindow, index: usize) {
        self.settings.ui_language = if index == 0 { "auto".to_string() } else { LANGUAGES[index - 1].0.to_string() };
        select_language(&self.settings.ui_language);
        self.refresh_texts(window);
    }

    /// Texts that the Rust code produces are built again after a change of language.
    pub fn refresh_texts(&mut self, window: &MainWindow) {
        self.sync_menu_choices(window);
        window.set_filter_names(ModelRc::new(VecModel::from(super::editor::filter_names())));
        window.set_tree_filter_names(ModelRc::new(VecModel::from(super::editor::tree_filter_names())));
        localize_panels(window);
        for f in self.floats.values() {
            super::panels::localize_host(&super::host::Host::Float(f.clone_strong()));
        }
        self.rebuild_tree(window);
        self.show_editor(window);
        self.update_summary(window);
        self.refresh_panel(window);
        self.refresh_saved_menu(window);
    }
}
