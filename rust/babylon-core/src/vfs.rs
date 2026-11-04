//! A merged, case-insensitive view of several game sources (DAT archives and loose folders).
//!
//! Sources are given from lowest to highest priority; a later source overrides an earlier one, like the game
//! does with master.dat < critter.dat < patch000.dat < data/. Only the "text/" subtree, the interface fonts and
//! scripts/*.int are indexed: this is a translation tool.

use crate::dat_archive::{normalize_path, DatArchive};
use crate::error::{Error, Result};
use crate::paths;
use std::collections::BTreeMap;
use std::sync::Arc;

const TEXT_PREFIX: &str = "text/";

/// Indexed paths: everything under text/ (messages), interface fonts (read to measure Pip-Boy text) and the
/// compiled scripts under scripts/ (dialogue trees).
fn is_indexed(path: &str) -> bool {
    let font = path.strip_prefix("font").and_then(|r| r.strip_suffix(".aaf")).is_some_and(|d| {
        !d.is_empty() && d.chars().all(|c| c.is_ascii_digit())
    });
    path.starts_with(TEXT_PREFIX) || font || (path.starts_with("scripts/") && path.ends_with(".int"))
}

fn is_dat_file(path: &str) -> bool {
    path.to_lowercase().ends_with(".dat")
}

/// Fallout installs ship master.dat then critter.dat, then patches in name order.
fn dat_rank(file_name: &str) -> i32 {
    let n = file_name.to_lowercase();
    if n.starts_with("master") {
        0
    } else if n.starts_with("critter") {
        1
    } else if n.starts_with("f2_res") || n.starts_with("f1_res") {
        2
    } else {
        3
    }
}

/// Names of the entries of a folder (`dirs` and/or `files`), sorted by name ignoring case.
fn list_dir(folder: &str, dirs: bool, files: bool) -> Vec<String> {
    let mut names = Vec::new();
    if let Ok(rd) = std::fs::read_dir(folder) {
        for e in rd.flatten() {
            let Ok(t) = e.file_type() else { continue };
            // symlinks to folders count as folders
            let is_dir = t.is_dir() || (t.is_symlink() && e.path().is_dir());
            if (is_dir && dirs) || (!is_dir && files) {
                names.push(e.file_name().to_string_lossy().into_owned());
            }
        }
    }
    names.sort_by_key(|n| n.to_lowercase());
    names
}

/// Name of the sub-directory matching `name` whatever its case.
fn child_dir(dir: &str, name: &str) -> Option<String> {
    list_dir(dir, true, false).into_iter().find(|n| n.eq_ignore_ascii_case(name))
}

#[derive(Clone)]
pub struct VfsFile {
    /// Normalised: lower case, '/' separators, e.g. "text/english/game/skill.msg".
    pub path: String,
    pub layer: usize,
    /// Entry in the archive, or `None` for a loose file.
    pub dat_index: Option<usize>,
    /// For loose files.
    pub absolute_path: String,
}

struct Layer {
    source: String,
    dat: Option<Arc<DatArchive>>,
    stamp: String,
}

#[derive(Default)]
pub struct VirtualFileSystem {
    layers: Vec<Layer>,
    files: BTreeMap<String, VfsFile>,
}

impl VirtualFileSystem {
    pub fn new() -> Self {
        Self::default()
    }

    /// Root of a mod folder: the folder itself when it contains "text/", else "data/" when that does.
    pub fn resolve_folder_root(folder: &str) -> String {
        let dir = paths::absolute_file_path(folder);
        if child_dir(&dir, "text").is_some() {
            return dir;
        }
        if let Some(data) = child_dir(&dir, "data") {
            let data_dir = paths::join(&dir, &data);
            if child_dir(&data_dir, "text").is_some() {
                return paths::absolute_file_path(&data_dir);
            }
        }
        dir
    }

    /// Sources recognised in an installation folder: *.dat files in a sensible priority order, then data/.
    pub fn discover_sources(folder: &str) -> Vec<String> {
        let mut result: Vec<String> = Vec::new();
        let dir = paths::absolute_file_path(folder);
        let push = |result: &mut Vec<String>, p: String| {
            if !result.contains(&p) {
                result.push(p);
            }
        };
        // Archives: master.dat / critter.dat / patches as files, or extracted into a folder of the same name.
        let mut dats: Vec<String> = list_dir(&dir, true, true).into_iter().filter(|n| is_dat_file(n)).collect();
        dats.sort_by(|a, b| {
            dat_rank(a).cmp(&dat_rank(b)).then_with(|| a.to_lowercase().cmp(&b.to_lowercase()))
        });
        for name in dats {
            push(&mut result, paths::join(&dir, &name));
        }
        if let Some(data) = child_dir(&dir, "data") {
            push(&mut result, paths::join(&dir, &data));
        }

        // sfall mod loader: mods/mods_order.txt lists the mods from the lowest to the highest priority;
        // mods that are not listed come after.
        if let Some(mods_name) = child_dir(&dir, "mods") {
            let mods = paths::join(&dir, &mods_name);
            let mut ordered: Vec<String> = Vec::new();
            if let Ok(bytes) = std::fs::read(paths::join(&mods, "mods_order.txt")) {
                let text = String::from_utf8_lossy(&bytes);
                for line in text.lines() {
                    let line = line.trim();
                    if !line.is_empty() && !line.starts_with('#') && !line.starts_with(';') {
                        ordered.push(line.to_string());
                    }
                }
            }
            let is_mod = |name: &str| {
                let p = paths::join(&mods, name);
                paths::exists(&p) && (paths::is_dir(&p) || is_dat_file(name))
            };
            for name in &ordered {
                if is_mod(name) {
                    push(&mut result, paths::join(&mods, name));
                }
            }
            for name in list_dir(&mods, true, true) {
                if is_mod(&name) {
                    push(&mut result, paths::join(&mods, &name));
                }
            }
        }
        if result.is_empty() {
            result.push(dir);
        }
        result
    }

    /// Resolves each path (a .dat file, or a folder that contains "text/" or "data/text/").
    /// Paths that cannot be read are reported in `errors`.
    pub fn set_sources(&mut self, sources: &[String], mut errors: Option<&mut Vec<String>>) {
        self.layers.clear();
        self.files.clear();

        for source in sources {
            let layer_index = self.layers.len();
            let mut layer = Layer { source: source.clone(), dat: None, stamp: String::new() };

            if paths::is_file(source) && is_dat_file(source) {
                match DatArchive::open(source) {
                    Ok(archive) => {
                        let (size, mtime) = paths::size_and_mtime(source);
                        layer.stamp = format!("{size}|{mtime}");
                        for (i, entry) in archive.entries().iter().enumerate() {
                            if !is_indexed(&entry.path) {
                                continue;
                            }
                            self.files.insert(
                                entry.path.clone(),
                                VfsFile { path: entry.path.clone(), layer: layer_index, dat_index: Some(i), absolute_path: String::new() },
                            );
                        }
                        layer.dat = Some(Arc::new(archive));
                    }
                    Err(e) => {
                        if let Some(errs) = errors.as_deref_mut() {
                            errs.push(format!("{source}: {e}"));
                        }
                        continue;
                    }
                }
            } else if paths::is_dir(source) {
                let root = Self::resolve_folder_root(source);
                for name in list_dir(&root, false, true) {
                    let path = normalize_path(&name);
                    if !is_indexed(&path) {
                        continue;
                    }
                    self.files.insert(
                        path.clone(),
                        VfsFile { path, layer: layer_index, dat_index: None, absolute_path: paths::join(&root, &name) },
                    );
                }
                for sub in ["text", "scripts"] {
                    let Some(name) = child_dir(&root, sub) else { continue };
                    let base = paths::join(&root, &name);
                    walk_files(&base, &mut |abs: &str| {
                        let relative = abs.strip_prefix(&format!("{root}/")).unwrap_or(abs);
                        let path = normalize_path(relative);
                        if is_indexed(&path) {
                            self.files.insert(
                                path.clone(),
                                VfsFile { path, layer: layer_index, dat_index: None, absolute_path: abs.to_string() },
                            );
                        }
                    });
                }
            } else {
                if let Some(errs) = errors.as_deref_mut() {
                    errs.push(format!("{source}: not found"));
                }
                continue;
            }
            self.layers.push(layer);
        }
    }

    pub fn find(&self, path: &str) -> Option<&VfsFile> {
        self.files.get(&normalize_path(path))
    }

    pub fn exists(&self, path: &str) -> bool {
        self.find(path).is_some()
    }

    pub fn read(&self, path: &str) -> Result<Vec<u8>> {
        let f = self.find(path).ok_or_else(|| Error::new(format!("File not found: {path}")))?;
        if let (Some(index), Some(layer)) = (f.dat_index, self.layers.get(f.layer)) {
            if let Some(dat) = &layer.dat {
                return dat.read(index);
            }
        }
        std::fs::read(&f.absolute_path).map_err(|e| Error::new(e.to_string()))
    }

    /// Files whose path starts with `prefix` and ends with `suffix` (both normalised).
    pub fn list(&self, prefix: &str, suffix: &str) -> Vec<String> {
        self.files
            .range(prefix.to_string()..)
            .take_while(|(k, _)| k.starts_with(prefix))
            .filter(|(k, _)| suffix.is_empty() || k.ends_with(suffix))
            .map(|(k, _)| k.clone())
            .collect()
    }

    /// Language directory names below text/.
    pub fn languages(&self) -> Vec<String> {
        let mut langs: Vec<String> = Vec::new();
        for (k, _) in self.files.range(TEXT_PREFIX.to_string()..).take_while(|(k, _)| k.starts_with(TEXT_PREFIX)) {
            let Some(slash) = k[TEXT_PREFIX.len()..].find('/') else { continue };
            let lang = &k[TEXT_PREFIX.len()..TEXT_PREFIX.len() + slash];
            if !langs.iter().any(|l| l == lang) {
                langs.push(lang.to_string());
            }
        }
        langs
    }

    /// Registers (or replaces) a loose file written on disk after the sources were scanned.
    pub fn add_loose_file(&mut self, path: &str, absolute_path: &str) {
        let path = normalize_path(path);
        // above every scanned source
        self.files.insert(
            path.clone(),
            VfsFile { path, layer: self.layers.len(), dat_index: None, absolute_path: absolute_path.to_string() },
        );
    }

    /// Cheap identity of a file's content source (archive + entry, or loose file size/date): equal signatures
    /// mean the content did not change. Empty when the file does not exist.
    pub fn signature(&self, path: &str) -> String {
        let Some(f) = self.find(path) else { return String::new() };
        if let (Some(index), Some(layer)) = (f.dat_index, self.layers.get(f.layer)) {
            if let Some(dat) = &layer.dat {
                let e = &dat.entries()[index];
                return format!("d|{}|{}|{}|{}|{}", layer.source, layer.stamp, e.offset, e.packed_size, e.unpacked_size);
            }
        }
        let (size, mtime) = paths::size_and_mtime(&f.absolute_path);
        format!("f|{}|{}|{}", f.absolute_path, size, mtime)
    }

    /// Where a file comes from, for display.
    pub fn describe(&self, path: &str) -> String {
        let Some(f) = self.find(path) else { return String::new() };
        if f.absolute_path.is_empty() {
            if let Some(layer) = self.layers.get(f.layer) {
                return paths::file_name(&layer.source);
            }
        }
        f.absolute_path.clone()
    }
}

fn walk_files(folder: &str, visit: &mut dyn FnMut(&str)) {
    let Ok(rd) = std::fs::read_dir(folder) else { return };
    let mut entries: Vec<_> = rd.flatten().collect();
    entries.sort_by_key(|e| e.file_name().to_string_lossy().to_lowercase());
    for e in entries {
        let path = paths::join(folder, &e.file_name().to_string_lossy());
        let Ok(t) = e.file_type() else { continue };
        if t.is_dir() || (t.is_symlink() && e.path().is_dir()) {
            walk_files(&path, visit);
        } else {
            visit(&path);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::dat_archive::Format;
    use crate::dat_writer::{write, DatWriteItem};

    fn put(path: &std::path::Path, data: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, data).unwrap();
    }

    #[test]
    fn later_sources_override_earlier_ones() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(
            root.join("master.dat"),
            Format::Fallout2,
            &[
                DatWriteItem::from_data("text/english/game/skill.msg", b"{1}{}{from master}".to_vec()),
                DatWriteItem::from_data("text/english/game/stat.msg", b"{1}{}{stat}".to_vec()),
                DatWriteItem::from_data("art/ignored.frm", b"x".to_vec()),
            ],
        )
        .unwrap();
        put(&root.join("data/Text/English/Game/Skill.msg"), b"{1}{}{from data}");
        let sources = VirtualFileSystem::discover_sources(root.to_str().unwrap());
        assert_eq!(sources.len(), 2);
        let mut vfs = VirtualFileSystem::new();
        let mut errors = Vec::new();
        vfs.set_sources(&sources, Some(&mut errors));
        assert!(errors.is_empty(), "{errors:?}");
        assert_eq!(vfs.read("text/english/game/skill.msg").unwrap(), b"{1}{}{from data}");
        assert_eq!(vfs.read("TEXT/English/game/STAT.msg").unwrap(), b"{1}{}{stat}");
        assert!(!vfs.exists("art/ignored.frm"));
        assert_eq!(vfs.languages(), vec!["english".to_string()]);
        assert_eq!(vfs.list("text/english/game/", ".msg").len(), 2);
        assert!(vfs.describe("text/english/game/stat.msg").contains("master.dat"));
        // a file registered later wins over every scanned source
        put(&root.join("out/skill.msg"), b"{1}{}{loose}");
        vfs.add_loose_file("text/english/game/skill.msg", root.join("out/skill.msg").to_str().unwrap());
        assert_eq!(vfs.read("text/english/game/skill.msg").unwrap(), b"{1}{}{loose}");
        assert!(vfs.signature("text/english/game/skill.msg").starts_with("f|"));
        assert!(vfs.signature("text/english/game/stat.msg").starts_with("d|"));
    }

    #[test]
    fn extracted_archive_folders_and_mod_order() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        put(&root.join("master.dat/text/english/a.msg"), b"{1}{}{a}");
        std::fs::create_dir_all(root.join("mods/zeta")).unwrap();
        std::fs::create_dir_all(root.join("mods/alpha")).unwrap();
        put(&root.join("mods/mods_order.txt"), b"# order\nzeta\n");
        let sources = VirtualFileSystem::discover_sources(root.to_str().unwrap());
        let names: Vec<String> = sources.iter().map(|s| paths::file_name(s)).collect();
        assert_eq!(names, vec!["master.dat", "zeta", "alpha"]);
        let mut vfs = VirtualFileSystem::new();
        vfs.set_sources(&sources, None);
        assert_eq!(vfs.read("text/english/a.msg").unwrap(), b"{1}{}{a}");
    }

    #[test]
    fn folder_root_resolution() {
        let dir = tempfile::tempdir().unwrap();
        put(&dir.path().join("Data/Text/english/x.msg"), b"{1}{}{x}");
        let root = VirtualFileSystem::resolve_folder_root(dir.path().to_str().unwrap());
        assert!(root.to_lowercase().ends_with("/data"));
    }

    #[test]
    fn unreadable_sources_are_reported() {
        let mut vfs = VirtualFileSystem::new();
        let mut errors = Vec::new();
        vfs.set_sources(&["Z:/does/not/exist".to_string()], Some(&mut errors));
        assert_eq!(errors.len(), 1);
    }
}
