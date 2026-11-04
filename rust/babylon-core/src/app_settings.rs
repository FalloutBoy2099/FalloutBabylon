//! Settings of the application (translation and correction services, Pip-Boy layout, interface, session).
//!
//! Stored as one JSON file in the application data folder. The DeepL key is kept there as is: do not share the file.

use crate::error::Result;
use crate::fsutil;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

fn yes() -> bool {
    true
}
fn pipboy_width() -> i32 {
    374
}
fn pipboy_chars() -> i32 {
    53
}
fn suggestion_minimum() -> i32 {
    70
}
fn auto() -> String {
    "auto".into()
}

/// A file that was open in an editor tab.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct OpenFile {
    /// Project id.
    pub project: String,
    /// "dialog/abel.msg"
    pub rel_path: String,
    /// Id of the line that was selected (-1: none).
    pub row_id: i32,
}

/// A game report file followed for a project (see `game_report`).
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ReportWatch {
    pub path: String,
    /// Bytes of the file already processed.
    pub offset: u64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct AppSettings {
    pub deepl_key: String,
    #[serde(default = "yes")]
    pub deepl_use_glossary: bool,
    #[serde(default = "yes")]
    pub deepl_use_context: bool,
    /// Machine translations are marked "to review".
    #[serde(default = "yes")]
    pub deepl_mark_review: bool,
    /// "", "prefer_more", "prefer_less"
    pub deepl_formality: String,
    pub grammalecte_url: String,
    pub language_tool_url: String,
    #[serde(default = "yes")]
    pub check_enabled: bool,
    /// Pixels of the Pip-Boy text area (374 in the game).
    #[serde(default = "pipboy_width")]
    pub pipboy_max_width: i32,
    /// Characters per line when the game font is not available.
    #[serde(default = "pipboy_chars")]
    pub pipboy_fallback_chars: i32,
    /// Minimum similarity (percent) of the translation memory suggestions.
    #[serde(default = "suggestion_minimum")]
    pub suggestion_minimum: i32,
    /// Allow longer texts in the translated scripts (experimental).
    pub script_enlarge: bool,
    /// Size of the main window (0: the default size).
    pub window_width: i32,
    pub window_height: i32,
    /// Position of the window on the screen (pixels), when it was saved.
    pub window_x: Option<i32>,
    pub window_y: Option<i32>,
    /// Files open in tabs, and the tab that was in front.
    pub open_files: Vec<OpenFile>,
    pub current_file: i32,
    /// Layout: panel on screen, dock open, widths / heights (logical pixels; 0: default).
    pub panel_index: i32,
    pub dock_open: bool,
    /// Bit i is set when the panel i is shown (as a tab of the dock).
    pub panel_mask: u32,
    /// Bit i is set when the panel i lives in the bottom dock (otherwise in the right one).
    pub panel_bottom_mask: u32,
    /// Bit i is set when the panel i is detached in a window of its own.
    pub panel_float_mask: u32,
    pub bottom_index: i32,
    pub bottom_height: i32,
    pub dock_width: i32,
    pub tree_width: i32,
    pub editor_height: i32,
    /// The dialogue tree grows downwards (start at the top) instead of to the right.
    pub dialog_vertical: bool,
    /// "auto" (system language) or a language code ("fr", "en"...).
    #[serde(default = "auto")]
    pub ui_language: String,
    /// "system", "light" or "dark".
    pub theme: String,
    /// Projects open when the application was closed.
    pub open_projects: Vec<String>,
    pub recent_folders: Vec<String>,
    /// DeepL glossary of each project: id -> "signature|glossary id".
    pub deepl_glossaries: BTreeMap<String, String>,
    pub report_watches: BTreeMap<String, ReportWatch>,
}

impl Default for AppSettings {
    fn default() -> Self {
        AppSettings {
            deepl_key: String::new(),
            deepl_use_glossary: true,
            deepl_use_context: true,
            deepl_mark_review: true,
            deepl_formality: String::new(),
            grammalecte_url: String::new(),
            language_tool_url: String::new(),
            check_enabled: true,
            pipboy_max_width: 374,
            pipboy_fallback_chars: 53,
            suggestion_minimum: 70,
            script_enlarge: false,
            window_width: 0,
            window_height: 0,
            window_x: None,
            window_y: None,
            open_files: Vec::new(),
            current_file: -1,
            panel_index: 0,
            dock_open: true,
            panel_mask: 1,
            panel_bottom_mask: 0,
            panel_float_mask: 0,
            bottom_index: 0,
            bottom_height: 0,
            dock_width: 0,
            tree_width: 0,
            editor_height: 0,
            dialog_vertical: false,
            ui_language: "auto".into(),
            theme: "system".into(),
            open_projects: Vec::new(),
            recent_folders: Vec::new(),
            deepl_glossaries: BTreeMap::new(),
            report_watches: BTreeMap::new(),
        }
    }
}

const MAX_RECENT: usize = 12;

impl AppSettings {
    pub fn load(path: &Path) -> AppSettings {
        std::fs::read(path)
            .ok()
            .and_then(|b| serde_json::from_slice::<AppSettings>(&b).ok())
            .unwrap_or_default()
    }

    pub fn save(&self, path: &Path) -> Result<()> {
        fsutil::write_atomic(path, &serde_json::to_vec_pretty(self)?)
    }

    pub fn add_recent_folder(&mut self, path: &str) {
        self.recent_folders.retain(|p| p != path);
        self.recent_folders.insert(0, path.to_string());
        self.recent_folders.truncate(MAX_RECENT);
    }

    /// Settings file of a data folder.
    pub fn file_in(base: &Path) -> PathBuf {
        base.join("settings.json")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_and_round_trip() {
        let dir = tempfile::tempdir().unwrap();
        let file = AppSettings::file_in(dir.path());
        let mut s = AppSettings::load(&file);
        assert!(s.deepl_use_glossary && s.check_enabled);
        assert_eq!(s.pipboy_max_width, 374);
        assert_eq!(s.pipboy_fallback_chars, 53);
        s.deepl_key = "key".into();
        s.add_recent_folder("C:/a");
        s.add_recent_folder("C:/b");
        s.add_recent_folder("C:/a");
        s.save(&file).unwrap();
        let back = AppSettings::load(&file);
        assert_eq!(back.deepl_key, "key");
        assert_eq!(back.recent_folders, vec!["C:/a", "C:/b"]);
    }

    #[test]
    fn missing_keys_get_their_defaults() {
        let s: AppSettings = serde_json::from_str(r#"{"deeplKey":"x"}"#).unwrap();
        assert_eq!(s.deepl_key, "x");
        assert!(s.deepl_use_context);
        assert_eq!(s.suggestion_minimum, 70);
    }
}
