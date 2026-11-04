//! Persistence of project definitions (one JSON file each in the application data folder).
//!
//! The files are the ones of the Qt version (`<id>.fbproj`, `<id>.state.json`, caches), in the same folder
//! (`%APPDATA%/FalloutBabylon/FalloutBabylon/projects`): both applications can open the same projects.

use crate::error::Result;
use crate::fsutil;
use crate::project::ProjectConfig;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct ProjectStore {
    base: PathBuf,
}

impl ProjectStore {
    pub fn new(base: impl Into<PathBuf>) -> Self {
        ProjectStore { base: base.into() }
    }

    /// The application data folder of the user (`BABYLON_DATA_DIR` overrides it, for tests).
    pub fn default_location() -> Self {
        if let Ok(dir) = std::env::var("BABYLON_DATA_DIR") {
            if !dir.is_empty() {
                return ProjectStore::new(dir);
            }
        }
        let base = dirs::data_dir().unwrap_or_else(|| PathBuf::from(".")).join("FalloutBabylon").join("FalloutBabylon");
        ProjectStore::new(base)
    }

    pub fn base_directory(&self) -> &Path {
        &self.base
    }

    fn projects_dir(&self) -> PathBuf {
        self.base.join("projects")
    }

    pub fn new_id() -> String {
        uuid::Uuid::new_v4().simple().to_string()[..8].to_string()
    }

    pub fn state_file(&self, id: &str) -> PathBuf {
        self.projects_dir().join(format!("{id}.state.json"))
    }

    pub fn stats_cache_file(&self, id: &str) -> PathBuf {
        self.projects_dir().join(format!("{id}.stats.json"))
    }

    pub fn qa_cache_file(&self, id: &str) -> PathBuf {
        self.projects_dir().join(format!("{id}.qa.json"))
    }

    /// Working folder of a project whose output is a .dat archive (see `output_archive`).
    pub fn staging_dir(&self, id: &str) -> PathBuf {
        self.projects_dir().join(format!("{id}.output"))
    }

    pub fn settings_file(&self) -> PathBuf {
        crate::app_settings::AppSettings::file_in(&self.base)
    }

    pub fn load_all(&self) -> Vec<ProjectConfig> {
        let mut names: Vec<PathBuf> = std::fs::read_dir(self.projects_dir())
            .map(|rd| {
                rd.flatten()
                    .map(|e| e.path())
                    .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("fbproj")))
                    .collect()
            })
            .unwrap_or_default();
        names.sort();
        let mut list = Vec::new();
        for path in names {
            let Ok(bytes) = std::fs::read(&path) else { continue };
            let Ok(config) = serde_json::from_slice::<ProjectConfig>(&bytes) else { continue };
            if !config.id.is_empty() {
                list.push(config);
            }
        }
        list
    }

    pub fn save(&self, config: &ProjectConfig) -> Result<()> {
        let path = self.projects_dir().join(format!("{}.fbproj", config.id));
        fsutil::write_atomic(path, &serde_json::to_vec_pretty(config)?)
    }

    pub fn remove(&self, id: &str) {
        let _ = std::fs::remove_file(self.projects_dir().join(format!("{id}.fbproj")));
        let _ = std::fs::remove_file(self.state_file(id));
        let _ = std::fs::remove_file(self.stats_cache_file(id));
        let _ = std::fs::remove_file(self.qa_cache_file(id));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn save_load_remove() {
        let dir = tempfile::tempdir().unwrap();
        let store = ProjectStore::new(dir.path());
        let id = ProjectStore::new_id();
        assert_eq!(id.len(), 8);
        let config = ProjectConfig { id: id.clone(), name: "RPU — French".into(), sources: vec!["C:/F2/master.dat".into()], ..Default::default() };
        store.save(&config).unwrap();
        let all = store.load_all();
        assert_eq!(all, vec![config.clone()]);
        std::fs::write(store.state_file(&id), b"{}").unwrap();
        store.remove(&id);
        assert!(store.load_all().is_empty());
        assert!(!store.state_file(&id).exists());
        assert!(store.staging_dir(&id).ends_with(format!("{id}.output")));
    }

    #[test]
    fn reads_the_qt_project_file() {
        let dir = tempfile::tempdir().unwrap();
        let store = ProjectStore::new(dir.path());
        std::fs::create_dir_all(dir.path().join("projects")).unwrap();
        std::fs::write(
            dir.path().join("projects/abcd1234.fbproj"),
            "{\n    \"id\": \"abcd1234\",\n    \"name\": \"Old\",\n    \"outputDir\": \"C:/x\",\n    \"sources\": [\"C:\\\\Fallout2\\\\master.dat\"],\n    \"sourceLanguage\": \"english\",\n    \"targetLanguage\": \"german\",\n    \"sourceEncoding\": \"cp1252\",\n    \"targetEncoding\": \"cp1252\"\n}\n",
        )
        .unwrap();
        let all = store.load_all();
        assert_eq!(all.len(), 1);
        assert_eq!(all[0].target_language, "german");
        assert_eq!(all[0].sources[0], "C:\\Fallout2\\master.dat");
    }
}
