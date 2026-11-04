//! Reuse of an existing translation (for instance the French of an older version of the mod) in a project.
//! The "memory" is another set of sources holding text/<language>/... files: it is only read.

use crate::msg_file::MsgFile;
use crate::project::{Project, SegmentStatus};
use crate::statistics;
use crate::text_codec;
use crate::vfs::VirtualFileSystem;
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Debug, Clone)]
pub struct Options {
    /// No record with this id: same source text elsewhere in the memory.
    pub match_by_text: bool,
    /// Same id but the source text changed: take the old translation, mark it "to review".
    pub changed_as_review: bool,
    /// Replace translations the project already has (default: untranslated lines only).
    pub overwrite: bool,
}

impl Default for Options {
    fn default() -> Self {
        Options { match_by_text: true, changed_as_review: true, overwrite: false }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Report {
    /// Files examined.
    pub files: usize,
    /// Records examined.
    pub examined: usize,
    /// Same id, same source text.
    pub exact: usize,
    /// Same id, source text changed (marked to review).
    pub changed: usize,
    /// Same source text found under another id.
    pub by_text: usize,
    /// Already translated, left alone.
    pub kept: usize,
    /// Files written.
    pub saved: usize,
    pub errors: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct Memory {
    /// Folders / .dat archives, lowest priority first.
    pub sources: Vec<String>,
    /// Language folder holding the memory's source texts...
    pub source_language: String,
    /// ... and its translations.
    pub target_language: String,
    pub source_encoding: String,
    pub target_encoding: String,
}

impl Default for Memory {
    fn default() -> Self {
        Memory {
            sources: Vec::new(),
            source_language: "english".into(),
            target_language: "french".into(),
            source_encoding: "cp1252".into(),
            target_encoding: "cp1252".into(),
        }
    }
}

fn key(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn load_msg(vfs: &VirtualFileSystem, path: &str, encoding: &str) -> Option<MsgFile> {
    if !vfs.exists(path) {
        return None;
    }
    Some(MsgFile::parse(&text_codec::decode(&vfs.read(path).unwrap_or_default(), encoding)))
}

/// Applies the memory to every message file of the project and writes the files that changed.
/// Open editors on this project must be closed first (the files are rewritten on disk).
pub fn import(
    project: &Project,
    memory: &Memory,
    options: &Options,
    mut progress: Option<&mut dyn FnMut(usize, usize)>,
    cancel: Option<&AtomicBool>,
) -> Report {
    let cancelled = || cancel.is_some_and(|c| c.load(Ordering::Relaxed));
    let mut report = Report::default();
    let mut mem = VirtualFileSystem::new();
    let mut errors = Vec::new();
    mem.set_sources(&memory.sources, Some(&mut errors));
    report.errors = errors;

    let src_prefix = format!("text/{}/", memory.source_language.to_lowercase());
    let dst_prefix = format!("text/{}/", memory.target_language.to_lowercase());

    // Text index of the whole memory: normalised source text -> translation (the most frequent one wins).
    let mut by_text: HashMap<String, HashMap<String, i32>> = HashMap::new();
    if options.match_by_text {
        for path in mem.list(&src_prefix, ".msg") {
            if cancelled() {
                return report;
            }
            let rel = &path[src_prefix.len()..];
            let Some(src) = load_msg(&mem, &path, &memory.source_encoding) else { continue };
            let Some(dst) = load_msg(&mem, &format!("{dst_prefix}{rel}"), &memory.target_encoding) else { continue };
            let dst_index = dst.index_by_id();
            for s in &src.entries {
                let Some(&di) = dst_index.get(&s.id) else { continue };
                let t = &dst.entries[di].text;
                if t.trim().is_empty() || *t == s.text || !statistics::is_translatable(&s.text) {
                    continue;
                }
                *by_text.entry(key(&s.text)).or_default().entry(t.clone()).or_insert(0) += 1;
            }
        }
    }

    let files = project.message_files();
    let profile = std::env::var("BABYLON_PROFILE").is_ok();
    let (mut t_load, mut t_mem, mut t_save) = (std::time::Duration::ZERO, std::time::Duration::ZERO, std::time::Duration::ZERO);
    for (fi, rel) in files.iter().enumerate() {
        if cancelled() {
            break;
        }
        if let Some(p) = progress.as_deref_mut() {
            p(fi, files.len());
        }
        let started = std::time::Instant::now();
        let Ok(mut doc) = project.load_document(rel) else { continue };
        t_load += started.elapsed();
        let started = std::time::Instant::now();
        let m_src = load_msg(&mem, &format!("{src_prefix}{rel}"), &memory.source_encoding);
        let m_dst = load_msg(&mem, &format!("{dst_prefix}{rel}"), &memory.target_encoding);
        let (have_src, have_dst) = (m_src.is_some(), m_dst.is_some());
        let m_src = m_src.unwrap_or_default();
        let m_dst = m_dst.unwrap_or_default();
        let src_index = m_src.index_by_id();
        let dst_index = m_dst.index_by_id();
        report.files += 1;
        t_mem += started.elapsed();

        let mut modified = false;
        for row in doc.rows.iter_mut() {
            if !row.in_source || !statistics::is_translatable(&row.source) {
                continue;
            }
            report.examined += 1;
            if !options.overwrite && Project::status_of(row) != SegmentStatus::Untranslated {
                report.kept += 1;
                continue;
            }

            let si = src_index.get(&row.id).copied();
            let di = dst_index.get(&row.id).copied();
            let old_source = si.map(|i| m_src.entries[i].text.clone()).unwrap_or_default();
            let old_target = di.map(|i| m_dst.entries[i].text.clone()).unwrap_or_default();
            let old_translated = !old_target.trim().is_empty() && old_target != old_source;

            if have_src && have_dst && si.is_some() && old_translated {
                if key(&old_source) == key(&row.source) {
                    row.target = old_target;
                    row.review = false;
                    report.exact += 1;
                    modified = true;
                    continue;
                }
                if options.changed_as_review {
                    row.target = old_target.clone();
                    row.review = true;
                    row.previous = old_target;
                    row.comment = format!("Imported translation: the source text has changed. Previous source: {}", key(&old_source));
                    report.changed += 1;
                    modified = true;
                    continue;
                }
            }
            if options.match_by_text {
                if let Some(candidates) = by_text.get(&key(&row.source)) {
                    let mut best = "";
                    let mut best_count = 0;
                    for (t, &count) in candidates {
                        if count > best_count || (count == best_count && t.as_str() < best) {
                            best = t;
                            best_count = count;
                        }
                    }
                    row.target = best.to_string();
                    row.review = false;
                    report.by_text += 1;
                    modified = true;
                }
            }
        }

        if modified {
            let started = std::time::Instant::now();
            let saved = project.save_document(&doc, false);
            t_save += started.elapsed();
            match saved {
                Ok(unencodable) => {
                    report.saved += 1;
                    if !unencodable.is_empty() {
                        report.errors.push(format!("{rel}: characters not representable in the target encoding: {unencodable}"));
                    }
                }
                Err(e) => report.errors.push(format!("{rel}: {e}")),
            }
        }
    }
    project.save_state();
    if profile {
        eprintln!("translation memory: load {t_load:?}, memory files {t_mem:?}, saving {t_save:?}");
    }
    if let Some(p) = progress.as_deref_mut() {
        p(files.len(), files.len());
    }
    report
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::project::ProjectConfig;
    use std::path::Path;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    #[test]
    fn translation_memory_import() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        // the new version of the mod (English only)
        write(
            &root.join("new/text/english/dialog/a.msg"),
            b"{1}{}{Hello there}\r\n{2}{}{Bye now}\r\n{5}{}{Thanks}\r\n{6}{}{Brand new line}\r\n{7}{}{Already done}\r\n",
        );
        write(&root.join("new/text/french/dialog/a.msg"), b"{7}{}{D\xE9j\xE0 fait}\r\n");
        // the old version, translated
        write(
            &root.join("old/text/english/dialog/a.msg"),
            b"{1}{}{Hello there}\r\n{2}{}{Bye}\r\n{9}{}{Thanks}\r\n{7}{}{Already done}\r\n",
        );
        write(
            &root.join("old/text/french/dialog/a.msg"),
            b"{1}{}{Bonjour}\r\n{2}{}{Salut}\r\n{9}{}{Merci}\r\n{7}{}{Ancienne version}\r\n",
        );

        let forward = |p: std::path::PathBuf| p.to_string_lossy().replace('\\', "/");
        let config = ProjectConfig {
            id: "tm".into(),
            name: "tm".into(),
            sources: vec![forward(root.join("new"))],
            output_dir: forward(root.join("out")),
            ..Default::default()
        };
        let make = || {
            let mut p = Project::new(config.clone());
            p.set_state_file(root.join("state.json"));
            p.reload();
            p
        };
        let p = make();
        let memory = Memory { sources: vec![forward(root.join("old"))], ..Default::default() };
        let report = import(&p, &memory, &Options::default(), None, None);
        assert!(report.errors.is_empty(), "{:?}", report.errors);
        assert_eq!(report.exact, 1); // id 1
        assert_eq!(report.changed, 1); // id 2: source changed
        assert_eq!(report.by_text, 1); // id 5 found under id 9
        assert_eq!(report.kept, 1); // id 7 already translated
        assert_eq!(report.saved, 1);

        let q = make();
        let doc = q.load_document("dialog/a.msg").unwrap();
        let row = |id: i32| doc.rows.iter().find(|r| r.id == id).unwrap().clone();
        assert_eq!(row(1).target, "Bonjour");
        assert!(!row(1).review);
        assert_eq!(row(2).target, "Salut");
        assert!(row(2).review);
        assert_eq!(row(2).previous, "Salut");
        assert!(row(2).comment.contains("Bye")); // the previous source is in the comment
        assert_eq!(row(5).target, "Merci");
        assert_eq!(Project::status_of(&row(6)), SegmentStatus::Untranslated); // nothing known: still to do
        assert_eq!(row(7).target, "Déjà fait"); // kept

        // overwrite mode replaces it
        let overwrite = Options { overwrite: true, ..Default::default() };
        let again = import(&p, &memory, &overwrite, None, None);
        assert_eq!(again.kept, 0);
        let r = make();
        let doc = r.load_document("dialog/a.msg").unwrap();
        assert_eq!(doc.rows.len(), 5);
        assert_eq!(doc.rows.iter().find(|x| x.id == 7).unwrap().target, "Ancienne version");
    }
}
