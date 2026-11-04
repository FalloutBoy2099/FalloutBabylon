//! Quality report over every translated line of a project.

use crate::fsutil;
use crate::glossary::Glossary;
use crate::i18n::{tr, trf};
use crate::placeholders;
use crate::project::{Project, ProjectConfig, SegmentStatus, TranslationDoc, TranslationRow};
use crate::text_codec;
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Kind {
    /// @Name@, %s, %d, {field} lost or invented by the translation.
    Tokens = 1,
    /// Leading / trailing / doubled spaces, text made of spaces only.
    Spacing = 2,
    /// Characters the target encoding cannot hold (they would become '?').
    Encoding = 4,
    /// Translation much longer than the source.
    Length = 8,
    /// Same source text translated differently elsewhere in the project.
    Duplicates = 16,
    /// Glossary term not respected.
    Glossary = 32,
}

pub const ALL_KINDS: u32 = 1 | 2 | 4 | 8 | 16 | 32;

impl Kind {
    pub fn from_bits(bits: i64) -> Kind {
        match bits {
            2 => Kind::Spacing,
            4 => Kind::Encoding,
            8 => Kind::Length,
            16 => Kind::Duplicates,
            32 => Kind::Glossary,
            _ => Kind::Tokens,
        }
    }
    pub fn name(self) -> String {
        match self {
            Kind::Tokens => tr("Tokens"),
            Kind::Spacing => tr("Spacing"),
            Kind::Encoding => tr("Encoding"),
            Kind::Length => tr("Length"),
            Kind::Duplicates => tr("Inconsistent duplicate"),
            Kind::Glossary => tr("Glossary"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct Options {
    pub kinds: u32,
    /// Length: target longer than ratio x source (and over `min_length_chars`).
    pub max_ratio: f64,
    pub min_length_chars: usize,
    /// Length: absolute limit of the translation (0 = none).
    pub max_chars: usize,
    /// Duplicates: ignore shorter sources ("Yes", "No"...).
    pub min_duplicate_chars: usize,
}

impl Default for Options {
    fn default() -> Self {
        Options { kinds: ALL_KINDS, max_ratio: 2.5, min_length_chars: 40, max_chars: 0, min_duplicate_chars: 10 }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Issue {
    pub rel_path: String,
    pub id: i32,
    pub kind: Kind,
    pub message: String,
    pub source: String,
    pub target: String,
}

fn is_checked(row: &TranslationRow) -> bool {
    // Lines still to translate are reported by the statistics, not here.
    row.in_source && !row.target.is_empty() && Project::status_of(row) != SegmentStatus::Untranslated
}

fn shorten(s: &str) -> String {
    let s = s.replace('\n', " ");
    if s.chars().count() > 40 {
        format!("{}…", s.chars().take(40).collect::<String>())
    } else {
        s
    }
}

fn sorted_tokens(text: &str) -> Vec<String> {
    let mut tokens = placeholders::protect(text).tokens;
    tokens.sort();
    tokens
}

fn simplified(s: &str) -> String {
    s.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn issue(doc: &TranslationDoc, row: &TranslationRow, kind: Kind, message: String) -> Issue {
    Issue {
        rel_path: doc.rel_path.clone(),
        id: row.id,
        kind,
        message,
        source: row.source.clone(),
        target: row.target.clone(),
    }
}

/// Checks of one file (without the project-wide duplicate check).
pub fn check_document(doc: &TranslationDoc, config: &ProjectConfig, glossary: Option<&Glossary>, options: &Options) -> Vec<Issue> {
    let mut out = Vec::new();
    let has = |k: Kind| options.kinds & k as u32 != 0;
    for row in &doc.rows {
        if !is_checked(row) {
            continue;
        }
        let (src, tgt) = (row.source.as_str(), row.target.as_str());

        if has(Kind::Tokens) {
            let (a, b) = (sorted_tokens(src), sorted_tokens(tgt));
            if a != b {
                let mut missing = a.clone();
                let mut extra = b.clone();
                for t in &b {
                    if let Some(i) = missing.iter().position(|m| m == t) {
                        missing.remove(i);
                    }
                }
                for t in &a {
                    if let Some(i) = extra.iter().position(|m| m == t) {
                        extra.remove(i);
                    }
                }
                let mut parts = Vec::new();
                if !missing.is_empty() {
                    parts.push(trf("missing: %1", &[&missing.join(" ")]));
                }
                if !extra.is_empty() {
                    parts.push(trf("unexpected: %1", &[&extra.join(" ")]));
                }
                out.push(issue(doc, row, Kind::Tokens, parts.join("; ")));
            }
        }

        if has(Kind::Spacing) {
            let mut problems = Vec::new();
            if tgt.trim().is_empty() {
                problems.push(tr("the translation only contains spaces"));
            } else {
                if src.trim() == src && tgt.trim() != tgt {
                    problems.push(tr("spaces at the start or the end"));
                } else if src.trim() != src && tgt.trim() == tgt {
                    problems.push(tr("the source has spaces at the start or the end"));
                }
                if tgt.contains("  ") && !src.contains("  ") {
                    problems.push(tr("doubled spaces"));
                }
            }
            if !problems.is_empty() {
                out.push(issue(doc, row, Kind::Spacing, problems.join("; ")));
            }
        }

        if has(Kind::Encoding) {
            let mut bad = String::new();
            text_codec::encode(tgt, &config.target_encoding, Some(&mut bad));
            if !bad.is_empty() {
                out.push(issue(doc, row, Kind::Encoding, trf("cannot be written as %1: %2", &[&config.target_encoding, &bad])));
            }
        }

        if has(Kind::Length) {
            let (tl, sl) = (tgt.chars().count(), src.chars().count());
            if options.max_chars > 0 && tl > options.max_chars {
                out.push(issue(doc, row, Kind::Length, trf("%1 characters (limit %2)", &[&tl.to_string(), &options.max_chars.to_string()])));
            } else if doc.rel_path.starts_with("game/") && sl <= 24 && sl > 0 && tl as f64 > (1.6 * sl as f64).max(sl as f64 + 8.0) {
                out.push(issue(
                    doc,
                    row,
                    Kind::Length,
                    trf("short label of the interface: %1 characters for %2 in the source", &[&tl.to_string(), &sl.to_string()]),
                ));
            } else if tl > options.min_length_chars && sl > 0 && tl as f64 > options.max_ratio * sl as f64 {
                out.push(issue(doc, row, Kind::Length, trf("%1 characters for %2 in the source", &[&tl.to_string(), &sl.to_string()])));
            }
        }

        if has(Kind::Glossary) {
            if let Some(g) = glossary {
                for i in g.check_consistency(src, tgt) {
                    out.push(issue(doc, row, Kind::Glossary, i.message));
                }
            }
        }
    }
    out
}

fn hash64(text: &str) -> u64 {
    let mut h: u64 = 14695981039346656037; // FNV-1a over the UTF-16 units
    for u in text.encode_utf16() {
        h = (h ^ u as u64).wrapping_mul(1099511628211);
    }
    if h == 0 {
        1
    } else {
        h
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DupRecord {
    pub id: i32,
    /// Hash of the simplified source text.
    pub source: u64,
    /// Hash of the simplified translation.
    pub target: u64,
}

#[derive(Debug, Clone, Default)]
pub struct CacheEntry {
    pub signature: String,
    /// Every check but the duplicates.
    pub issues: Vec<Issue>,
    pub dups: Vec<DupRecord>,
}

/// Results kept from one run to the next, per file: a file whose content, marks, glossary and checks did not
/// change is not read again. Persisted as JSON next to the project.
#[derive(Default)]
pub struct Cache {
    path: Option<PathBuf>,
    key: String,
    files: HashMap<String, CacheEntry>,
    dirty: bool,
}

impl Cache {
    /// Entries are only valid for the same `key` (checks, limits, glossary...); another key starts empty.
    pub fn open(path: impl AsRef<Path>, key: &str) -> Cache {
        let mut cache = Cache { path: Some(path.as_ref().to_path_buf()), key: key.to_string(), ..Default::default() };
        let Ok(bytes) = std::fs::read(path.as_ref()) else { return cache };
        let Ok(root) = serde_json::from_slice::<Value>(&bytes) else { return cache };
        if root["key"].as_str() != Some(key) {
            return cache;
        }
        let Some(files) = root["files"].as_object() else { return cache };
        for (rel, o) in files {
            let mut entry = CacheEntry { signature: o["sig"].as_str().unwrap_or("").to_string(), ..Default::default() };
            for v in o["issues"].as_array().cloned().unwrap_or_default() {
                let Some(a) = v.as_array() else { continue };
                if a.len() < 5 {
                    continue;
                }
                entry.issues.push(Issue {
                    rel_path: rel.clone(),
                    id: a[0].as_i64().unwrap_or(0) as i32,
                    kind: Kind::from_bits(a[1].as_i64().unwrap_or(1)),
                    message: a[2].as_str().unwrap_or("").to_string(),
                    source: a[3].as_str().unwrap_or("").to_string(),
                    target: a[4].as_str().unwrap_or("").to_string(),
                });
            }
            for v in o["dups"].as_array().cloned().unwrap_or_default() {
                let Some(a) = v.as_array() else { continue };
                if a.len() < 3 {
                    continue;
                }
                entry.dups.push(DupRecord {
                    id: a[0].as_i64().unwrap_or(0) as i32,
                    source: u64::from_str_radix(a[1].as_str().unwrap_or("0"), 16).unwrap_or(0),
                    target: u64::from_str_radix(a[2].as_str().unwrap_or("0"), 16).unwrap_or(0),
                });
            }
            cache.files.insert(rel.clone(), entry);
        }
        cache
    }

    pub fn save(&mut self) {
        let Some(path) = &self.path else { return };
        if !self.dirty {
            return;
        }
        let mut files = serde_json::Map::new();
        for (rel, e) in &self.files {
            let issues: Vec<Value> = e.issues.iter().map(|i| json!([i.id, i.kind as i32, i.message, i.source, i.target])).collect();
            let dups: Vec<Value> = e.dups.iter().map(|d| json!([d.id, format!("{:x}", d.source), format!("{:x}", d.target)])).collect();
            files.insert(rel.clone(), json!({"sig": e.signature, "issues": issues, "dups": dups}));
        }
        let root = json!({"key": self.key, "files": files});
        if let Ok(bytes) = serde_json::to_vec(&root) {
            if fsutil::write_atomic(path, &bytes).is_ok() {
                self.dirty = false;
            }
        }
    }

    pub fn find(&self, rel: &str, signature: &str) -> Option<&CacheEntry> {
        self.files.get(rel).filter(|e| e.signature == signature)
    }

    pub fn set(&mut self, rel: &str, entry: CacheEntry) {
        self.files.insert(rel.to_string(), entry);
        self.dirty = true;
    }

    /// Forgets files that no longer exist.
    pub fn keep_only(&mut self, files: &[String]) {
        let keep: HashSet<&String> = files.iter().collect();
        let before = self.files.len();
        self.files.retain(|k, _| keep.contains(k));
        if self.files.len() != before {
            self.dirty = true;
        }
    }

    pub fn len(&self) -> usize {
        self.files.len()
    }

    pub fn is_empty(&self) -> bool {
        self.files.is_empty()
    }
}

/// What the cached results of a run depend on besides the files themselves.
pub fn cache_key(project: &Project, glossary: Option<&Glossary>, o: &Options) -> String {
    let c = project.config();
    format!(
        "v1|{}|{}|{}|{}|{}|{}|{}|{}|{}",
        o.kinds & !(Kind::Duplicates as u32),
        o.max_ratio,
        o.min_length_chars,
        o.max_chars,
        o.min_duplicate_chars,
        glossary.map(|g| g.signature()).unwrap_or(""),
        c.target_encoding,
        c.source_language,
        c.target_language
    )
}

struct Where {
    rel_path: String,
    id: i32,
}

/// The whole project. With a `cache` (opened by the caller with `cache_key`), unchanged files are not read
/// again; `cache_hits` receives the number of files answered from it.
pub fn run(
    project: &Project,
    glossary: Option<&Glossary>,
    options: &Options,
    mut progress: Option<&mut dyn FnMut(usize, usize)>,
    cancel: Option<&AtomicBool>,
    mut cache: Option<&mut Cache>,
    cache_hits: Option<&mut usize>,
) -> Vec<Issue> {
    let cancelled = || cancel.is_some_and(|c| c.load(Ordering::Relaxed));
    let mut hits = 0usize;
    let mut issues: Vec<Issue> = Vec::new();
    // source hash -> translation hash -> where
    let mut seen: HashMap<u64, HashMap<u64, Vec<Where>>> = HashMap::new();
    let duplicates = options.kinds & Kind::Duplicates as u32 != 0;

    let files = project.message_files();
    for (i, rel) in files.iter().enumerate() {
        if cancelled() {
            break;
        }
        if i % 10 == 0 {
            if let Some(p) = progress.as_deref_mut() {
                p(i, files.len());
            }
        }
        let signature = if cache.is_some() { project.content_signature(rel) } else { String::new() };
        let cached: Option<CacheEntry> = cache.as_deref().and_then(|c| c.find(rel, &signature)).cloned();
        let entry = match cached {
            Some(e) => {
                hits += 1;
                e
            }
            None => {
                let Ok(doc) = project.load_document(rel) else { continue };
                let mut entry = CacheEntry { signature: signature.clone(), ..Default::default() };
                entry.issues = check_document(&doc, project.config(), glossary, options);
                for row in &doc.rows {
                    if !is_checked(row) {
                        continue;
                    }
                    let key = simplified(&row.source);
                    if key.chars().count() < options.min_duplicate_chars {
                        continue;
                    }
                    entry.dups.push(DupRecord { id: row.id, source: hash64(&key), target: hash64(&simplified(&row.target)) });
                }
                if let Some(c) = cache.as_deref_mut() {
                    c.set(rel, entry.clone());
                }
                entry
            }
        };
        issues.extend(entry.issues.iter().cloned());
        if duplicates {
            for d in &entry.dups {
                seen.entry(d.source).or_default().entry(d.target).or_default().push(Where { rel_path: rel.clone(), id: d.id });
            }
        }
    }
    if let Some(c) = cache.as_deref_mut() {
        if !cancelled() {
            c.keep_only(&files);
            c.save();
        }
    }
    if let Some(h) = cache_hits {
        *h = hits;
    }

    if duplicates && !cancelled() {
        // Groups of the same source translated in several ways: the most used translation is the reference.
        struct Conflict {
            best_count: usize,
        }
        let mut conflicts: HashMap<u64, Conflict> = HashMap::new();
        let mut wanted: HashMap<String, Vec<(i32, u64)>> = HashMap::new(); // file -> (id, source hash) of the rows to report
        let mut best_where: HashMap<u64, (String, i32)> = HashMap::new();
        for (source, by_target) in &seen {
            if by_target.len() < 2 {
                continue;
            }
            let mut best = 0u64;
            let mut best_count = 0usize;
            for (t, places) in by_target {
                if places.len() > best_count || (places.len() == best_count && *t < best) {
                    best = *t;
                    best_count = places.len();
                }
            }
            conflicts.insert(*source, Conflict { best_count });
            let first = &by_target[&best][0];
            best_where.insert(*source, (first.rel_path.clone(), first.id));
            for (t, places) in by_target {
                if *t != best {
                    for w in places {
                        wanted.entry(w.rel_path.clone()).or_default().push((w.id, *source));
                    }
                }
            }
        }
        // The texts of the reference translations: one row of each group.
        let mut best_text: HashMap<u64, String> = HashMap::new();
        {
            let mut need: HashMap<String, Vec<(i32, u64)>> = HashMap::new();
            for (source, (rel, id)) in &best_where {
                need.entry(rel.clone()).or_default().push((*id, *source));
            }
            for (rel, list) in &need {
                if cancelled() {
                    break;
                }
                let Ok(doc) = project.load_document(rel) else { continue };
                for (id, source) in list {
                    if let Some(row) = doc.rows.iter().find(|r| r.id == *id) {
                        best_text.insert(*source, row.target.clone());
                    }
                }
            }
        }
        for (rel, list) in &wanted {
            if cancelled() {
                break;
            }
            let Ok(doc) = project.load_document(rel) else { continue };
            for (id, source) in list {
                let Some(row) = doc.rows.iter().find(|r| r.id == *id) else { continue };
                let count = conflicts.get(source).map(|c| c.best_count).unwrap_or(0);
                issues.push(Issue {
                    rel_path: rel.clone(),
                    id: row.id,
                    kind: Kind::Duplicates,
                    source: row.source.clone(),
                    target: row.target.clone(),
                    message: trf("elsewhere (%1 times): %2", &[&count.to_string(), &shorten(best_text.get(source).map(String::as_str).unwrap_or(""))]),
                });
            }
        }
    }
    if let Some(p) = progress.as_deref_mut() {
        p(files.len(), files.len());
    }
    issues.sort_by(|a, b| {
        if a.rel_path != b.rel_path {
            let (pa, pb) = (Project::game_file_priority(&a.rel_path), Project::game_file_priority(&b.rel_path));
            return if pa != pb { pa.cmp(&pb) } else { a.rel_path.cmp(&b.rel_path) };
        }
        a.id.cmp(&b.id).then(a.kind.cmp(&b.kind))
    });
    issues
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    fn setup(root: &Path) -> (ProjectConfig, impl Fn() -> Project + '_) {
        write(
            &root.join("m/text/english/dialog/a.msg"),
            b"{1}{}{Hello @Name@, you have %d caps}\r\n\
{2}{}{Plain sentence here}\r\n\
{3}{}{Short}\r\n\
{4}{}{This quest is really difficult to finish}\r\n\
{5}{}{This quest is really difficult to finish}\r\n\
{6}{}{Untouched line}\r\n",
        );
        write(
            &root.join("m/text/french/dialog/a.msg"),
            b"{1}{}{Bonjour, vous avez %d capsules}\r\n\
{2}{}{Phrase  simple ici }\r\n\
{3}{}{Court}\r\n\
{4}{}{Cette qu\xEAte est vraiment difficile \xE0 terminer}\r\n\
{5}{}{Cette mission est vraiment difficile \xE0 terminer}\r\n",
        );
        let fwd = |p: PathBuf| p.to_string_lossy().replace('\\', "/");
        let config = ProjectConfig {
            id: "qa".into(),
            name: "qa".into(),
            sources: vec![fwd(root.join("m"))],
            output_dir: fwd(root.join("out")),
            ..Default::default()
        };
        let c2 = config.clone();
        let state = root.join("state.json");
        let make = move || {
            let mut p = Project::new(c2.clone());
            p.set_state_file(&state);
            p.reload();
            p
        };
        (config, make)
    }

    #[test]
    fn quality_checks() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        let (_, make) = setup(root);
        let p = make();
        let o = Options::default();
        let issues = run(&p, None, &o, None, None, None, None);
        let has = |id: i32, kind: Kind| issues.iter().any(|i| i.id == id && i.kind == kind);
        assert!(has(1, Kind::Tokens)); // @Name@ lost
        assert!(!has(2, Kind::Tokens));
        assert!(has(2, Kind::Spacing)); // double and trailing space
        assert!(!has(3, Kind::Encoding));
        assert_ne!(has(4, Kind::Duplicates), has(5, Kind::Duplicates)); // one of the two is the odd one

        // a character typed in the editor that cp1252 cannot hold
        let mut doc = p.load_document("dialog/a.msg").unwrap();
        for row in doc.rows.iter_mut() {
            if row.id == 3 {
                row.target = "Court \u{2260}".into(); // not equal sign
            }
        }
        assert!(check_document(&doc, p.config(), None, &o).iter().any(|i| i.id == 3 && i.kind == Kind::Encoding));
        // untranslated lines are not reported here
        assert!(issues.iter().all(|i| i.id != 6));

        // the cache: a second run reads nothing, and gives the same answer
        let cache_file = root.join("qa.cache.json");
        let key = cache_key(&p, None, &o);
        let mut hits = 99;
        let mut first = Cache::open(&cache_file, &key);
        let fresh = run(&p, None, &o, None, None, Some(&mut first), Some(&mut hits));
        assert_eq!(hits, 0);
        assert_eq!(fresh.len(), issues.len());

        let mut second = Cache::open(&cache_file, &key);
        assert!(!second.is_empty());
        let cached = run(&p, None, &o, None, None, Some(&mut second), Some(&mut hits));
        assert_eq!(hits, 1); // the project has a single message file
        assert_eq!(cached, fresh);

        // a changed file is read again
        let mut edited = p.load_document("dialog/a.msg").unwrap();
        edited.rows[1].target = "Phrase simple ici".into();
        p.save_document(&edited, true).unwrap();
        let mut third = Cache::open(&cache_file, &key);
        run(&p, None, &o, None, None, Some(&mut third), Some(&mut hits));
        assert_eq!(hits, 0);

        // other checks, other cache
        let narrow = Options { kinds: Kind::Tokens as u32, ..Default::default() };
        let other = Cache::open(&cache_file, &cache_key(&p, None, &narrow));
        assert_eq!(other.len(), 0);

        // a restricted selection
        for i in run(&p, None, &narrow, None, None, None, None) {
            assert_eq!(i.kind, Kind::Tokens);
        }
    }

    #[test]
    fn glossary_check() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("m/text/english/dialog/a.msg"), b"{1}{}{Wear the Leather Armor now}\r\n");
        write(&root.join("m/text/french/dialog/a.msg"), b"{1}{}{Mets la cuirasse maintenant}\r\n");
        let config = ProjectConfig {
            id: "g".into(),
            sources: vec![root.join("m").to_string_lossy().replace('\\', "/")],
            output_dir: root.join("out").to_string_lossy().replace('\\', "/"),
            ..Default::default()
        };
        let mut p = Project::new(config);
        p.set_state_file(root.join("state.json"));
        p.reload();
        let mut g = Glossary::new();
        g.add("Leather Armor", "Armure en cuir", "Item", "x");
        g.finalize("english", "french");
        let issues = run(&p, Some(&g), &Options::default(), None, None, None, None);
        assert!(issues.iter().any(|i| i.kind == Kind::Glossary && i.message.contains("Armure en cuir")));
    }
}
