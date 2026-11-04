//! A project can deliver its translation as a .dat archive instead of a folder of loose files. The saved files
//! stay in a working folder (`ProjectConfig::output_dir`, kept by the application) and the archive is rebuilt
//! from it.
//!
//! The archive holds only text/<target language>/..., plus (option, on by default) the original texts of the
//! same files under text/<source language>/... so that the reference files are known, plus the translated
//! scripts (scripts/<name>.int) when there are some.

use crate::dat_archive::{DatArchive, Format};
use crate::dat_writer::{self, DatWriteItem};
use crate::error::{Error, Result};
use crate::i18n::tr;
use crate::paths;
use crate::project::{Project, ProjectConfig};
use std::path::Path;

pub fn enabled(config: &ProjectConfig) -> bool {
    !config.output_archive.trim().is_empty()
}

pub fn format_of(config: &ProjectConfig) -> Format {
    if config.output_archive_format == "dat1" {
        Format::Fallout1
    } else {
        Format::Fallout2
    }
}

/// "dat1" / "dat2"
pub fn format_id(format: Format) -> &'static str {
    match format {
        Format::Fallout1 => "dat1",
        Format::Fallout2 => "dat2",
    }
}

/// Format of the first .dat file among `sources` (Fallout 2 when there is none, or only folders).
pub fn detect_format(sources: &[String]) -> Format {
    for s in sources {
        if paths::is_file(s) {
            if let Ok(dat) = DatArchive::open(s) {
                return dat.format();
            }
        }
    }
    Format::Fallout2
}

fn language_prefix(language: &str) -> String {
    format!("text/{}/", language.to_lowercase())
}

/// Why the archive cannot be written there (`None` when it can): it must not be one of the sources.
pub fn problem_with(config: &ProjectConfig) -> Option<String> {
    if !enabled(config) {
        return None;
    }
    if config.sources.iter().any(|s| paths::same_path(s, &config.output_archive)) {
        return Some(tr("The output archive cannot be one of the sources of the project."));
    }
    if !config.output_archive.to_lowercase().ends_with(".dat") {
        return Some(tr("The output archive must be a .dat file."));
    }
    None
}

/// An archive already exists but the working folder does not hold its files yet (new computer, switch to an
/// archive output): copies the text/<target language>/ files of the archive into the working folder. Existing
/// files are never overwritten. Returns the number of files copied.
pub fn seed_working_folder(config: &ProjectConfig) -> Result<usize> {
    if !enabled(config) || !paths::is_file(&config.output_archive) {
        return Ok(0);
    }
    let dat = DatArchive::open(&config.output_archive)?;
    let prefix = language_prefix(&config.target_language);
    let mut copied = 0;
    for (i, entry) in dat.entries().iter().enumerate() {
        if !entry.path.starts_with(&prefix) {
            continue;
        }
        let target = paths::join(&config.output_dir, &entry.path);
        if paths::exists(&target) {
            continue;
        }
        let data = dat.read(i)?;
        if let Some(parent) = Path::new(&target).parent() {
            std::fs::create_dir_all(parent)?;
        }
        if std::fs::write(&target, data).is_ok() {
            copied += 1;
        }
    }
    Ok(copied)
}

#[derive(Debug, Clone, Default)]
pub struct BuildResult {
    /// Files text/<target>/...
    pub translated: usize,
    /// Files text/<source>/...
    pub originals: usize,
    /// Translated scripts/<name>.int
    pub scripts: usize,
}

fn list_files(folder: &str) -> Vec<String> {
    let mut out = Vec::new();
    fn walk(dir: &str, out: &mut Vec<String>) {
        let Ok(rd) = std::fs::read_dir(dir) else { return };
        let mut entries: Vec<_> = rd.flatten().collect();
        entries.sort_by_key(|e| e.file_name().to_string_lossy().to_lowercase());
        for e in entries {
            let path = paths::join(dir, &e.file_name().to_string_lossy());
            if e.file_type().map(|t| t.is_dir()).unwrap_or(false) {
                walk(&path, out);
            } else {
                out.push(path);
            }
        }
    }
    walk(folder, &mut out);
    out
}

/// Rewrites the archive from the working folder (atomically; the previous archive stays if anything fails).
/// Safe to call from a worker thread.
pub fn build(project: &Project) -> Result<BuildResult> {
    let config = project.config();
    if let Some(problem) = problem_with(config) {
        return Err(Error::new(problem));
    }
    let mut result = BuildResult::default();
    if !enabled(config) {
        return Ok(result);
    }

    let target_prefix = language_prefix(&config.target_language);
    let translated_root = paths::join(&config.output_dir, &target_prefix);
    let translated_root = translated_root.trim_end_matches('/').to_string();

    let mut items: Vec<DatWriteItem> = Vec::new();
    let mut relatives: Vec<String> = Vec::new(); // path below text/<target>/, for the reference copies
    if paths::is_dir(&translated_root) {
        for abs in list_files(&translated_root) {
            let data = std::fs::read(&abs).map_err(|_| Error::new(crate::i18n::trf("Cannot read %1.", &[&abs])))?;
            let rel = abs[translated_root.len()..].trim_start_matches('/').to_string();
            items.push(DatWriteItem::from_data(format!("{target_prefix}{rel}"), data));
            relatives.push(rel);
            result.translated += 1;
        }
    }

    // translated scripts (texts written in their code), if any
    let scripts_root = paths::join(&config.output_dir, "scripts");
    if paths::is_dir(&scripts_root) {
        for abs in list_files(&scripts_root) {
            if !abs.to_lowercase().ends_with(".int") {
                continue;
            }
            let Ok(data) = std::fs::read(&abs) else { continue };
            items.push(DatWriteItem::from_data(format!("scripts/{}", paths::file_name(&abs)), data));
            result.scripts += 1;
        }
    }

    if config.archive_with_source {
        let source_prefix = language_prefix(&config.source_language);
        for rel in &relatives {
            if !rel.to_lowercase().ends_with(".msg") {
                continue;
            }
            let Ok(data) = project.read_source_text(rel) else { continue };
            if data.is_empty() {
                continue; // the file only exists in the translation
            }
            items.push(DatWriteItem::from_data(format!("{source_prefix}{rel}"), data));
            result.originals += 1;
        }
    }

    dat_writer::write(&config.output_archive, format_of(config), &items)?;
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    fn fwd(p: std::path::PathBuf) -> String {
        p.to_string_lossy().replace('\\', "/")
    }

    #[test]
    fn output_archive_build() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("m/text/english/dialog/a.msg"), b"{1}{}{Hello}\r\n{2}{}{Bye}\r\n");
        write(&root.join("m/text/english/dialog/b.msg"), b"{1}{}{Other}\r\n");
        let c = ProjectConfig {
            id: "oa".into(),
            name: "oa".into(),
            sources: vec![fwd(root.join("m"))],
            output_dir: fwd(root.join("work")),
            output_archive: fwd(root.join("out/french.dat")),
            ..Default::default()
        };
        let mut p = Project::new(c.clone());
        p.set_state_file(root.join("state.json"));
        p.reload();

        assert!(problem_with(&c).is_none());
        let mut doc = p.load_document("dialog/a.msg").unwrap();
        doc.rows[0].target = "Bonjour".into();
        doc.rows[1].target = "Au revoir".into();
        p.save_document(&doc, true).unwrap();

        // with the original texts (default): translated file + its reference, but not the untouched b.msg
        let r = build(&p).unwrap();
        assert_eq!((r.translated, r.originals), (1, 1));
        {
            let dat = DatArchive::open(&c.output_archive).unwrap();
            assert_eq!(dat.format(), Format::Fallout2);
            assert_eq!(dat.entries().len(), 2);
            let fr = dat.index_of("text/french/dialog/a.msg").unwrap();
            let en = dat.index_of("text/english/dialog/a.msg").unwrap();
            assert!(String::from_utf8_lossy(&dat.read(fr).unwrap()).contains("{1}{}{Bonjour}"));
            assert!(String::from_utf8_lossy(&dat.read(en).unwrap()).contains("{1}{}{Hello}"));
            assert!(dat.index_of("text/english/dialog/b.msg").is_none());
        }

        // without the originals, in the Fallout 1 format
        let c1 = ProjectConfig {
            archive_with_source: false,
            output_archive_format: "dat1".into(),
            output_archive: fwd(root.join("out/french1.dat")),
            ..c.clone()
        };
        let mut p1 = Project::new(c1.clone());
        p1.set_state_file(root.join("state1.json"));
        p1.reload();
        let r = build(&p1).unwrap();
        assert_eq!(r.originals, 0);
        {
            let dat = DatArchive::open(&c1.output_archive).unwrap();
            assert_eq!(dat.format(), Format::Fallout1);
            assert_eq!(dat.entries().len(), 1);
            let fr = dat.index_of("text/french/dialog/a.msg").unwrap();
            assert!(String::from_utf8_lossy(&dat.read(fr).unwrap()).contains("{2}{}{Au revoir}"));
        }
        assert_eq!(detect_format(&[c1.output_archive.clone()]), Format::Fallout1);
        assert_eq!(detect_format(&[fwd(root.join("m"))]), Format::Fallout2);

        // a new computer: the working folder is restored from the archive (translations only, never overwritten)
        let again = ProjectConfig { output_dir: fwd(root.join("work2")), ..c.clone() };
        assert_eq!(seed_working_folder(&again).unwrap(), 1);
        assert!(root.join("work2/text/french/dialog/a.msg").exists());
        assert!(!root.join("work2/text/english").exists());
        assert_eq!(seed_working_folder(&again).unwrap(), 0);

        // the archive can never be one of the sources
        let bad = ProjectConfig { sources: vec![c.output_archive.clone()], ..c.clone() };
        assert!(problem_with(&bad).is_some());
        let bad = ProjectConfig { output_archive: fwd(root.join("out/french.txt")), ..c.clone() };
        assert!(problem_with(&bad).is_some());
    }
}
