//! A translation project: sources, languages, glossary and the per-segment state.

use crate::custom_glossary::CustomTerm;
use crate::error::{Error, Result};
use crate::fsutil;
use crate::glossary::Glossary;
use crate::msg_file::{MsgEntry, MsgFile};
use crate::msg_repair;
use crate::paths;
use crate::statistics::{self, FileStats};
use crate::text_codec;
use crate::vfs::VirtualFileSystem;
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, RwLock};

fn default_source_language() -> String {
    "english".into()
}
fn default_target_language() -> String {
    "french".into()
}
fn default_encoding() -> String {
    "cp1252".into()
}
fn default_archive_format() -> String {
    "dat2".into()
}
fn default_true() -> bool {
    true
}

/// The definition of a project, as stored in the project file (`<id>.fbproj`).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProjectConfig {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub name: String,
    /// .dat files and/or folders, lowest priority first.
    #[serde(default)]
    pub sources: Vec<String>,
    /// Where translated files are written (loose files).
    #[serde(default)]
    pub output_dir: String,
    /// Optional: a .dat archive the translation is delivered in. `output_dir` is then a working folder of the
    /// application and the archive is rebuilt from it after each save (see `output_archive`).
    #[serde(default)]
    pub output_archive: String,
    /// "dat2" (Fallout 2, zlib) or "dat1" (Fallout 1).
    #[serde(default = "default_archive_format")]
    pub output_archive_format: String,
    /// Also store the original texts in the archive, as reference files.
    #[serde(default = "default_true")]
    pub archive_with_source: bool,
    #[serde(default = "default_source_language")]
    pub source_language: String,
    #[serde(default = "default_target_language")]
    pub target_language: String,
    #[serde(default = "default_encoding")]
    pub source_encoding: String,
    #[serde(default = "default_encoding")]
    pub target_encoding: String,
}

impl Default for ProjectConfig {
    fn default() -> Self {
        ProjectConfig {
            id: String::new(),
            name: String::new(),
            sources: Vec::new(),
            output_dir: String::new(),
            output_archive: String::new(),
            output_archive_format: default_archive_format(),
            archive_with_source: true,
            source_language: default_source_language(),
            target_language: default_target_language(),
            source_encoding: default_encoding(),
            target_encoding: default_encoding(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SegmentStatus {
    Untranslated,
    Translated,
    ToReview,
}

/// One line of a translation: the source record and the translator's text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TranslationRow {
    pub id: i32,
    pub audio: String,
    pub source: String,
    pub target: String,
    /// File trivia kept for the round trip.
    pub leading: String,
    /// False: record only present in the target file.
    pub in_source: bool,
    /// Translation accepted even though it equals the source text.
    pub confirmed: bool,
    /// Marked "to review".
    pub review: bool,
    /// Translator's note on this line.
    pub comment: String,
    /// Hash of the translation when it was last validated (0 = never).
    pub validated: u32,
    /// Translation before the last import / change of the source (for comparison).
    pub previous: String,
}

impl Default for TranslationRow {
    fn default() -> Self {
        TranslationRow {
            id: 0,
            audio: String::new(),
            source: String::new(),
            target: String::new(),
            leading: String::new(),
            in_source: true,
            confirmed: false,
            review: false,
            comment: String::new(),
            validated: 0,
            previous: String::new(),
        }
    }
}

impl TranslationRow {
    pub fn changed_since_validation(&self) -> bool {
        self.validated != 0 && Project::text_hash(&self.target) != self.validated
    }
}

#[derive(Debug, Clone, Default)]
pub struct TranslationDoc {
    /// Below text/<lang>/, e.g. "dialog/abel.msg".
    pub rel_path: String,
    pub rows: Vec<TranslationRow>,
    pub trailing: String,
    pub crlf: bool,
    pub target_file_exists: bool,
    /// Malformed records of the source and of the translation.
    pub warnings: Vec<String>,
    /// The part of `warnings` that comes from the translation file.
    pub target_warnings: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FileKind {
    Game = 0,
    Dialog = 1,
    Other = 2,
}

/// A segment marked "to review".
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TodoItem {
    pub rel_path: String,
    pub id: i32,
    pub source: String,
    pub target: String,
}

/// A text written in the code of a script and its translation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScriptTranslation {
    /// "abel" (scripts/abel.int)
    pub script: String,
    pub operand: u32,
    /// The original text, to recognise the string when the script changes.
    pub source: String,
    pub target: String,
}

/// A line to put in "to review" with a comment.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ReviewMark {
    pub rel_path: String,
    pub id: i32,
    pub comment: String,
}

/// Message files of /game, in the order in which translating them is most useful for the glossary.
const GAME_ORDER: [&str; 12] = [
    "pro_crit", "pro_item", "pro_scen", "pro_wall", "pro_misc", "skill", "trait", "perk", "stat", "worldmap", "map",
    "quests",
];

fn stem_of(rel: &str) -> String {
    let name = rel.rsplit('/').next().unwrap_or("");
    name.strip_suffix(".msg").unwrap_or(name).to_lowercase()
}

fn category_for(stem: &str) -> String {
    match stem {
        "pro_crit" => "Creature",
        "pro_item" => "Item",
        "pro_scen" => "Scenery",
        "pro_wall" => "Wall",
        "pro_misc" => "Misc",
        "skill" => "Skill",
        "trait" => "Trait",
        "perk" => "Perk",
        "stat" => "Stat",
        "worldmap" | "map" => "Location",
        "quests" => "Quest",
        other => other,
    }
    .to_string()
}

fn looks_like_name(text: &str) -> bool {
    let t = text.trim();
    let len = t.chars().count();
    if !(3..=40).contains(&len) || t.contains('\n') {
        return false;
    }
    if !t.chars().next().is_some_and(|c| c.is_uppercase()) {
        return false;
    }
    if statistics::count_words(t) > 5 {
        return false;
    }
    if matches!(t.chars().last(), Some('.' | '!' | '?' | ':')) {
        return false;
    }
    !t.chars().any(|c| c.is_ascii_digit() || c == '%' || c == '@' || c == '#')
}

/// Is this record the *name* of something (as opposed to a description or a sentence)?
fn is_name_record(stem: &str, id: i32, text: &str) -> bool {
    if !statistics::is_translatable(text) {
        return false;
    }
    if stem.starts_with("pro_") {
        // Fallout prototypes: id = 100 * n for the name, +1 for the description.
        if id < 100 || id % 100 != 0 || text.trim().chars().count() > 60 || text.contains('\n') {
            return false;
        }
        // Internal placeholders ("test (don't use)", "spare", "encinodude") are not worth a glossary entry.
        let t = text.trim();
        let lower = t.to_lowercase();
        if lower.contains("don't use") || lower.contains("unused") {
            return false;
        }
        let first = t.chars().next().unwrap();
        return first.is_uppercase() || first.is_ascii_digit() || first == '.' || first == '"';
    }
    match stem {
        "skill" | "trait" => (100..200).contains(&id) && looks_like_name(text),
        "perk" | "stat" => id < 1000 && looks_like_name(text),
        "worldmap" | "map" | "quests" => looks_like_name(text),
        _ => false,
    }
}

fn prefix_for(language: &str) -> String {
    format!("text/{}/", language.to_lowercase())
}

#[derive(Default, Clone)]
struct FileFlags {
    review: HashSet<i32>,
    confirmed: HashSet<i32>,
    comments: HashMap<i32, String>,
    previous: HashMap<i32, String>,
    validated: HashMap<i32, u32>,
}

impl FileFlags {
    fn is_empty(&self) -> bool {
        self.review.is_empty()
            && self.confirmed.is_empty()
            && self.comments.is_empty()
            && self.previous.is_empty()
            && self.validated.is_empty()
    }
}

#[derive(Default)]
struct Inner {
    vfs: VirtualFileSystem,
    flags: HashMap<String, FileFlags>,
    custom_terms: Vec<CustomTerm>,
    script_translations: Vec<ScriptTranslation>,
}

#[derive(Default)]
struct StatsCache {
    loaded: bool,
    dirty: bool,
    hits: usize,
    entries: HashMap<String, (String, FileStats)>,
}

pub struct Project {
    config: ProjectConfig,
    state_file: Option<PathBuf>,
    stats_cache_file: Option<PathBuf>,
    /// Readers: load_document/message_files/statistics (any thread). Writer: save_document/reload.
    inner: RwLock<Inner>,
    glossary: RwLock<Arc<Glossary>>,
    stats: Mutex<StatsCache>,
}

impl Project {
    pub fn new(mut config: ProjectConfig) -> Project {
        config.source_language = config.source_language.to_lowercase();
        config.target_language = config.target_language.to_lowercase();
        Project {
            config,
            state_file: None,
            stats_cache_file: None,
            inner: RwLock::new(Inner::default()),
            glossary: RwLock::new(Arc::new(Glossary::new())),
            stats: Mutex::new(StatsCache::default()),
        }
    }

    pub fn config(&self) -> &ProjectConfig {
        &self.config
    }

    pub fn set_state_file(&mut self, file: impl Into<PathBuf>) {
        self.state_file = Some(file.into());
    }

    pub fn state_file(&self) -> Option<&Path> {
        self.state_file.as_deref()
    }

    pub fn set_stats_cache_file(&mut self, file: impl Into<PathBuf>) {
        self.stats_cache_file = Some(file.into());
    }

    fn read(&self) -> std::sync::RwLockReadGuard<'_, Inner> {
        self.inner.read().unwrap_or_else(|e| e.into_inner())
    }

    fn write(&self) -> std::sync::RwLockWriteGuard<'_, Inner> {
        self.inner.write().unwrap_or_else(|e| e.into_inner())
    }

    /// Re-reads the sources (call after the configuration changed). Returns the sources that could not be read.
    pub fn reload(&self) -> Vec<String> {
        let mut sources = self.config.sources.clone();
        if !self.config.output_dir.is_empty() {
            let out = paths::clean_path(&self.config.output_dir);
            let known = sources.iter().any(|s| paths::clean_path(s) == out);
            if !known && paths::is_dir(&out) {
                sources.push(out);
            }
        }
        let mut errors = Vec::new();
        let mut inner = self.write();
        inner.vfs.set_sources(&sources, Some(&mut errors));
        self.load_state_locked(&mut inner);
        errors
    }

    pub fn kind_of(rel: &str) -> FileKind {
        if rel.starts_with("game/") {
            FileKind::Game
        } else if rel.starts_with("dialog/") {
            FileKind::Dialog
        } else {
            FileKind::Other
        }
    }

    pub fn game_file_priority(rel: &str) -> usize {
        let stem = stem_of(rel);
        GAME_ORDER.iter().position(|s| *s == stem).unwrap_or(1000)
    }

    pub fn available_languages(&self) -> Vec<String> {
        self.read().vfs.languages()
    }

    /// Message files of the project (source + target), relative to text/<lang>/, in the recommended
    /// translation order: /game first, then /dialog, then the rest.
    pub fn message_files(&self) -> Vec<String> {
        let inner = self.read();
        let mut set: HashSet<String> = HashSet::new();
        for lang in [&self.config.source_language, &self.config.target_language] {
            let prefix = prefix_for(lang);
            for path in inner.vfs.list(&prefix, ".msg") {
                set.insert(path[prefix.len()..].to_string());
            }
        }
        let mut files: Vec<String> = set.into_iter().collect();
        files.sort_by(|a, b| {
            let (ka, kb) = (Self::kind_of(a) as i32, Self::kind_of(b) as i32);
            if ka != kb {
                return ka.cmp(&kb);
            }
            if ka == FileKind::Game as i32 {
                let (pa, pb) = (Self::game_file_priority(a), Self::game_file_priority(b));
                if pa != pb {
                    return pa.cmp(&pb);
                }
            }
            a.cmp(b)
        });
        files
    }

    pub fn source_path(&self, rel: &str) -> String {
        prefix_for(&self.config.source_language) + rel
    }

    /// Normalised path inside text/.
    pub fn target_path(&self, rel: &str) -> String {
        prefix_for(&self.config.target_language) + rel
    }

    pub fn target_disk_path(&self, rel: &str) -> String {
        paths::join(&self.config.output_dir, &self.target_path(rel))
    }

    /// Compiled script (scripts/<stem>.int) that drives a dialogue file. dialog/abel.msg -> scripts/abel.int
    pub fn read_dialog_script(&self, rel: &str) -> Result<Vec<u8>> {
        let inner = self.read();
        let path = format!("scripts/{}.int", stem_of(rel));
        if !inner.vfs.exists(&path) {
            return Err(Error::new(format!("scripts/{}.int not found in the sources", stem_of(rel))));
        }
        inner.vfs.read(&path)
    }

    /// Interface font (font1.aaf is the Pip-Boy's): text/<target language>/, text/<source language>/, then the root.
    pub fn read_interface_font(&self, index: u32) -> Option<Vec<u8>> {
        let inner = self.read();
        let name = format!("font{index}.aaf");
        for path in [prefix_for(&self.config.target_language) + &name, prefix_for(&self.config.source_language) + &name, name] {
            if inner.vfs.exists(&path) {
                return inner.vfs.read(&path).ok();
            }
        }
        None
    }

    /// Raw bytes of a message file of the source language (for the reference copies stored in an output archive).
    pub fn read_source_text(&self, rel: &str) -> Result<Vec<u8>> {
        self.read().vfs.read(&self.source_path(rel))
    }

    /// Everything the virtual file system knows under a prefix (see `VirtualFileSystem::list`).
    pub fn list_files(&self, prefix: &str, suffix: &str) -> Vec<String> {
        self.read().vfs.list(prefix, suffix)
    }

    pub fn file_signature(&self, path: &str) -> String {
        self.read().vfs.signature(path)
    }

    pub fn describe_file(&self, path: &str) -> String {
        self.read().vfs.describe(path)
    }

    pub fn file_exists(&self, path: &str) -> bool {
        self.read().vfs.exists(path)
    }

    pub fn read_file(&self, path: &str) -> Result<Vec<u8>> {
        self.read().vfs.read(path)
    }

    pub fn status_of(row: &TranslationRow) -> SegmentStatus {
        if row.review {
            return SegmentStatus::ToReview;
        }
        if !row.in_source || !statistics::is_translatable(&row.source) {
            return SegmentStatus::Translated;
        }
        if row.target.trim().is_empty() {
            return SegmentStatus::Untranslated;
        }
        if row.target == row.source && !row.confirmed {
            return SegmentStatus::Untranslated;
        }
        SegmentStatus::Translated
    }

    /// Stable 32-bit hash of a text (FNV-1a over the UTF-16 units, so it matches the Qt version's state files).
    /// 0 means "never validated".
    pub fn text_hash(text: &str) -> u32 {
        let mut h: u32 = 2166136261;
        for unit in text.encode_utf16() {
            h = (h ^ unit as u32).wrapping_mul(16777619);
        }
        if h == 0 {
            1
        } else {
            h
        }
    }

    // ------------------------------------------------------------------------- documents

    /// Fixes the malformed records of the translation file of `rel` (see `msg_repair`) and writes the corrected
    /// file in the output folder (a loose file already there is kept as <name>.msg.bak). Nothing is written
    /// when the file needs no repair. The returned result lists the fixes.
    pub fn repair_target_file(&self, rel: &str) -> Result<msg_repair::Repair> {
        let (bytes, existing) = {
            let inner = self.read();
            let f = inner
                .vfs
                .find(&self.target_path(rel))
                .ok_or_else(|| Error::new(format!("{rel} does not exist in the translation")))?;
            (inner.vfs.read(&self.target_path(rel))?, f.absolute_path.clone())
        };
        let result = msg_repair::repair(&text_codec::decode(&bytes, &self.config.target_encoding));
        if !result.changed() {
            return Ok(result);
        }
        let path = self.target_disk_path(rel);
        if !existing.is_empty() && paths::same_path(&existing, &path) {
            let backup = format!("{path}.bak");
            if !paths::exists(&backup) {
                let _ = std::fs::copy(&path, &backup);
            }
        }
        let fixed = text_codec::encode(&result.text, &self.config.target_encoding, None);
        fsutil::write_atomic(&path, &fixed)?;
        self.write().vfs.add_loose_file(&self.target_path(rel), &path);
        Ok(result)
    }

    pub fn load_document(&self, rel: &str) -> Result<TranslationDoc> {
        let inner = self.read();
        let mut doc = TranslationDoc { rel_path: rel.to_string(), ..Default::default() };

        let mut source = MsgFile::default();
        let mut target = MsgFile::default();
        let mut have_source = false;
        if inner.vfs.exists(&self.source_path(rel)) {
            let bytes = inner.vfs.read(&self.source_path(rel))?;
            source = MsgFile::parse(&text_codec::decode(&bytes, &self.config.source_encoding));
            have_source = true;
        }
        if inner.vfs.exists(&self.target_path(rel)) {
            let bytes = inner.vfs.read(&self.target_path(rel))?;
            target = MsgFile::parse(&text_codec::decode(&bytes, &self.config.target_encoding));
            doc.target_file_exists = true;
        }
        doc.warnings = source.warnings.iter().chain(target.warnings.iter()).cloned().collect();
        doc.target_warnings = target.warnings.clone();
        doc.crlf = if have_source { source.crlf } else { target.crlf };
        doc.trailing = if have_source { source.trailing.clone() } else { target.trailing.clone() };

        let target_index = target.index_by_id();
        let flags = inner.flags.get(rel);

        let mut seen: HashSet<i32> = HashSet::new();
        for s in &source.entries {
            let mut row = TranslationRow {
                id: s.id,
                audio: s.audio.clone(),
                source: s.text.clone(),
                leading: s.leading.clone(),
                ..Default::default()
            };
            if let Some(&ti) = target_index.get(&s.id) {
                row.target = target.entries[ti].text.clone();
            }
            if let Some(f) = flags {
                row.review = f.review.contains(&s.id);
                row.confirmed = f.confirmed.contains(&s.id);
                row.comment = f.comments.get(&s.id).cloned().unwrap_or_default();
                row.previous = f.previous.get(&s.id).cloned().unwrap_or_default();
                row.validated = f.validated.get(&s.id).copied().unwrap_or(0);
            }
            seen.insert(s.id);
            doc.rows.push(row);
        }
        for (i, t) in target.entries.iter().enumerate() {
            if seen.contains(&t.id) || target_index.get(&t.id) != Some(&i) {
                continue;
            }
            doc.rows.push(TranslationRow {
                id: t.id,
                audio: t.audio.clone(),
                target: t.text.clone(),
                leading: t.leading.clone(),
                in_source: false,
                review: flags.is_some_and(|f| f.review.contains(&t.id)),
                ..Default::default()
            });
        }
        Ok(doc)
    }

    /// Writes the translation into the output folder and remembers the marks of the rows. Returns the characters
    /// the target encoding cannot hold (they were written as '?').
    /// `persist_state` = false lets a bulk operation write the state file once at the end (call `save_state`).
    pub fn save_document(&self, doc: &TranslationDoc, persist_state: bool) -> Result<String> {
        let mut file = MsgFile { crlf: doc.crlf, trailing: doc.trailing.clone(), ..Default::default() };
        for row in &doc.rows {
            file.entries.push(MsgEntry {
                id: row.id,
                audio: row.audio.clone(),
                // An untranslated record keeps the source text so the game never shows an empty line.
                text: if row.target.is_empty() && row.in_source { row.source.clone() } else { row.target.clone() },
                leading: row.leading.clone(),
            });
        }
        let mut unencodable = String::new();
        let bytes = text_codec::encode(&file.serialize(), &self.config.target_encoding, Some(&mut unencodable));
        let path = self.target_disk_path(&doc.rel_path);
        fsutil::write_atomic(&path, &bytes)?;

        let mut inner = self.write();
        inner.vfs.add_loose_file(&self.target_path(&doc.rel_path), &path);
        let mut flags = FileFlags::default();
        for row in &doc.rows {
            if row.review {
                flags.review.insert(row.id);
            }
            if row.confirmed {
                flags.confirmed.insert(row.id);
            }
            if !row.comment.is_empty() {
                flags.comments.insert(row.id, row.comment.clone());
            }
            if !row.previous.is_empty() {
                flags.previous.insert(row.id, row.previous.clone());
            }
            if row.validated != 0 {
                flags.validated.insert(row.id, row.validated);
            }
        }
        if flags.is_empty() {
            inner.flags.remove(&doc.rel_path);
        } else {
            inner.flags.insert(doc.rel_path.clone(), flags);
        }
        if persist_state {
            self.save_state_locked(&inner);
        }
        Ok(unencodable)
    }

    pub fn statistics_of(doc: &TranslationDoc) -> FileStats {
        let mut s = FileStats {
            rel_path: doc.rel_path.clone(),
            display_name: statistics::display_name_for(&doc.rel_path),
            kind: Self::kind_of(&doc.rel_path) as i32,
            ..Default::default()
        };
        for row in &doc.rows {
            if !row.in_source {
                continue;
            }
            s.lines += 1;
            let words = statistics::count_words(&row.source) as i64;
            let chars = row.source.encode_utf16().count() as i64;
            s.words += words;
            s.chars += chars;
            match Self::status_of(row) {
                SegmentStatus::Translated => s.translated += 1,
                SegmentStatus::ToReview => s.review += 1,
                SegmentStatus::Untranslated => {
                    s.untranslated += 1;
                    s.words_remaining += words;
                    s.chars_remaining += chars;
                }
            }
        }
        s
    }

    // ------------------------------------------------------------------------- glossary

    pub fn glossary(&self) -> Arc<Glossary> {
        self.glossary.read().unwrap_or_else(|e| e.into_inner()).clone()
    }

    pub fn set_glossary(&self, glossary: Glossary) {
        *self.glossary.write().unwrap_or_else(|e| e.into_inner()) = Arc::new(glossary);
    }

    /// Builds the glossary from the /game files and the terms entered by hand (thread safe: call it from a worker).
    pub fn build_glossary(&self, cancel: Option<&AtomicBool>) -> Glossary {
        let mut glossary = Glossary::new();
        for rel in self.message_files() {
            if cancel.is_some_and(|c| c.load(Ordering::Relaxed)) {
                return glossary;
            }
            if Self::kind_of(&rel) != FileKind::Game {
                continue;
            }
            let stem = stem_of(&rel);
            let Ok(doc) = self.load_document(&rel) else { continue };

            // A name kept as is (Stimpak -> Stimpak) is a real translation when the file itself is translated;
            // in a file that is still a copy of the source it only means "not done yet".
            let (mut differing, mut translatable) = (0, 0);
            for row in &doc.rows {
                if row.in_source && statistics::is_translatable(&row.source) && !row.target.is_empty() {
                    translatable += 1;
                    if row.target != row.source {
                        differing += 1;
                    }
                }
            }
            let file_is_translated = doc.target_file_exists && translatable > 0 && differing * 10 >= translatable;

            for row in &doc.rows {
                if !row.in_source || !is_name_record(&stem, row.id, &row.source) {
                    continue;
                }
                let translated = Self::status_of(row) != SegmentStatus::Untranslated
                    || (file_is_translated && !row.target.trim().is_empty());
                let file_name = rel.rsplit('/').next().unwrap_or(&rel);
                glossary.add(
                    &row.source,
                    if translated { &row.target } else { "" },
                    &category_for(&stem),
                    &format!("{}#{}", file_name, row.id),
                );
            }
        }
        for t in self.custom_terms() {
            glossary.add_custom(&t.source, &t.target, t.keep, "Custom");
        }
        glossary.finalize(&self.config.source_language, &self.config.target_language);
        glossary
    }

    // ------------------------------------------------------------------------- statistics

    /// Identity of what a file's analysis depends on (source and translation files, languages, encodings, review
    /// and confirmation marks): equal signatures mean the same results. Used by the caches.
    pub fn content_signature(&self, rel: &str) -> String {
        let inner = self.read();
        self.signature_locked(&inner, rel)
    }

    fn signature_locked(&self, inner: &Inner, rel: &str) -> String {
        let c = &self.config;
        let mut sig = format!("{}|{}|{}|{}|", c.source_language, c.target_language, c.source_encoding, c.target_encoding);
        sig.push_str(&inner.vfs.signature(&self.source_path(rel)));
        sig.push('#');
        sig.push_str(&inner.vfs.signature(&self.target_path(rel)));
        if let Some(f) = inner.flags.get(rel) {
            let mut r: Vec<i32> = f.review.iter().copied().collect();
            let mut cf: Vec<i32> = f.confirmed.iter().copied().collect();
            r.sort_unstable();
            cf.sort_unstable();
            let mut flags = String::new();
            for id in r {
                flags.push_str(&format!("{id},"));
            }
            flags.push(';');
            for id in cf {
                flags.push_str(&format!("{id},"));
            }
            sig.push_str(&format!("#{}", Self::text_hash(&flags)));
        }
        sig
    }

    fn load_stats_cache(&self, cache: &mut StatsCache) {
        if cache.loaded {
            return;
        }
        cache.loaded = true;
        let Some(file) = &self.stats_cache_file else { return };
        let Ok(bytes) = std::fs::read(file) else { return };
        let Ok(root) = serde_json::from_slice::<Value>(&bytes) else { return };
        if root["version"].as_i64() != Some(1) {
            return;
        }
        let Some(files) = root["files"].as_object() else { return };
        for (rel, entry) in files {
            let Some(a) = entry["s"].as_array() else { continue };
            if a.len() < 8 {
                continue;
            }
            let n = |i: usize| a[i].as_f64().unwrap_or(0.0);
            let stats = FileStats {
                rel_path: rel.clone(),
                display_name: statistics::display_name_for(rel),
                kind: Self::kind_of(rel) as i32,
                lines: n(0) as i32,
                translated: n(1) as i32,
                review: n(2) as i32,
                untranslated: n(3) as i32,
                words: n(4) as i64,
                words_remaining: n(5) as i64,
                chars: n(6) as i64,
                chars_remaining: n(7) as i64,
            };
            cache.entries.insert(rel.clone(), (entry["sig"].as_str().unwrap_or("").to_string(), stats));
        }
    }

    fn save_stats_cache(&self, cache: &mut StatsCache) {
        let Some(file) = &self.stats_cache_file else { return };
        if !cache.dirty {
            return;
        }
        let mut files = Map::new();
        for (rel, (sig, s)) in &cache.entries {
            files.insert(
                rel.clone(),
                json!({"sig": sig, "s": [s.lines, s.translated, s.review, s.untranslated, s.words, s.words_remaining, s.chars, s.chars_remaining]}),
            );
        }
        let root = json!({"version": 1, "files": files});
        if let Ok(bytes) = serde_json::to_vec(&root) {
            if fsutil::write_atomic(file, &bytes).is_ok() {
                cache.dirty = false;
            }
        }
    }

    /// Number of files of the last `compute_statistics` answered from the cache.
    pub fn stats_cache_hits(&self) -> usize {
        self.stats.lock().unwrap_or_else(|e| e.into_inner()).hits
    }

    /// Statistics of every message file; files that did not change since the last run come from the cache.
    pub fn compute_statistics(
        &self,
        mut progress: Option<&mut dyn FnMut(usize, usize, &FileStats)>,
        cancel: Option<&AtomicBool>,
    ) -> Vec<FileStats> {
        let files = self.message_files();
        {
            let mut cache = self.stats.lock().unwrap_or_else(|e| e.into_inner());
            self.load_stats_cache(&mut cache);
            cache.hits = 0;
        }
        let mut all = Vec::with_capacity(files.len());
        for (i, rel) in files.iter().enumerate() {
            if cancel.is_some_and(|c| c.load(Ordering::Relaxed)) {
                break;
            }
            let sig = self.content_signature(rel);
            let cached = {
                let mut cache = self.stats.lock().unwrap_or_else(|e| e.into_inner());
                match cache.entries.get(rel) {
                    Some((s, stats)) if *s == sig => {
                        let stats = stats.clone();
                        cache.hits += 1;
                        Some(stats)
                    }
                    _ => None,
                }
            };
            let stats = match cached {
                Some(s) => s,
                None => {
                    let s = match self.load_document(rel) {
                        Ok(doc) => Self::statistics_of(&doc),
                        Err(_) => FileStats {
                            rel_path: rel.clone(),
                            display_name: statistics::display_name_for(rel),
                            kind: Self::kind_of(rel) as i32,
                            ..Default::default()
                        },
                    };
                    let mut cache = self.stats.lock().unwrap_or_else(|e| e.into_inner());
                    cache.entries.insert(rel.clone(), (sig, s.clone()));
                    cache.dirty = true;
                    s
                }
            };
            if let Some(p) = progress.as_deref_mut() {
                p(i + 1, files.len(), &stats);
            }
            all.push(stats);
        }
        let mut cache = self.stats.lock().unwrap_or_else(|e| e.into_inner());
        self.save_stats_cache(&mut cache);
        all
    }

    /// Segments marked "to review" in the saved state, skipping the files in `skip` (e.g. those open in an editor).
    pub fn review_items(&self, skip: &HashSet<String>) -> Vec<TodoItem> {
        let mut files: Vec<String> = {
            let inner = self.read();
            inner
                .flags
                .iter()
                .filter(|(k, f)| !f.review.is_empty() && !skip.contains(*k))
                .map(|(k, _)| k.clone())
                .collect()
        };
        files.sort();
        let mut items = Vec::new();
        for rel in files {
            if let Ok(doc) = self.load_document(&rel) {
                for row in &doc.rows {
                    if row.review {
                        items.push(TodoItem { rel_path: rel.clone(), id: row.id, source: row.source.clone(), target: row.target.clone() });
                    }
                }
            }
        }
        items
    }

    /// Marks lines "to review" in the saved state and adds the comment (never twice), without rewriting any .msg
    /// file. Returns the number of lines whose state changed. Files open in an editor must be refreshed by the caller.
    pub fn mark_for_review(&self, marks: &[ReviewMark]) -> usize {
        let mut changed = 0;
        let mut inner = self.write();
        for mark in marks {
            let flags = inner.flags.entry(mark.rel_path.clone()).or_default();
            let mut touched = false;
            if flags.review.insert(mark.id) {
                touched = true;
            }
            if !mark.comment.is_empty() {
                let comment = flags.comments.entry(mark.id).or_default();
                if !comment.contains(&mark.comment) {
                    if !comment.is_empty() {
                        comment.push_str(" | ");
                    }
                    comment.push_str(&mark.comment);
                    touched = true;
                }
            }
            if touched {
                changed += 1;
            }
        }
        if changed > 0 {
            self.save_state_locked(&inner);
        }
        changed
    }

    // ------------------------------------------------------------------------- terms, scripts

    pub fn custom_terms(&self) -> Vec<CustomTerm> {
        self.read().custom_terms.clone()
    }

    /// Also writes the state file.
    pub fn set_custom_terms(&self, terms: Vec<CustomTerm>) {
        let mut inner = self.write();
        inner.custom_terms = terms;
        self.save_state_locked(&inner);
    }

    pub fn script_translations(&self) -> Vec<ScriptTranslation> {
        self.read().script_translations.clone()
    }

    /// Also writes the state file.
    pub fn set_script_translations(&self, translations: Vec<ScriptTranslation>) {
        let mut inner = self.write();
        inner.script_translations = translations;
        self.save_state_locked(&inner);
    }

    /// The compiled scripts of the sources.
    pub fn script_names(&self) -> Vec<String> {
        let inner = self.read();
        let mut names: Vec<String> = inner
            .vfs
            .list("scripts/", ".int")
            .into_iter()
            .map(|p| {
                let n = p.rsplit('/').next().unwrap_or("");
                n[..n.len() - 4].to_string()
            })
            .collect();
        names.sort();
        names
    }

    /// Where the untouched originals of the scripts written to the output are kept (empty without state file).
    pub fn pristine_scripts_dir(&self) -> Option<PathBuf> {
        let state = self.state_file.as_ref()?;
        let dir = state.parent().unwrap_or_else(|| Path::new("."));
        Some(dir.join(format!("{}.scripts", self.config.id)))
    }

    pub fn keep_pristine_script(&self, name: &str, original: &[u8]) {
        let Some(dir) = self.pristine_scripts_dir() else { return };
        let path = dir.join(format!("{name}.int"));
        if path.exists() {
            return;
        }
        let _ = fsutil::write_atomic(path, original);
    }

    /// The untouched original of a script (even after a translated copy was written in the output folder).
    pub fn read_script_original(&self, name: &str) -> Result<Vec<u8>> {
        if let Some(dir) = self.pristine_scripts_dir() {
            if let Ok(bytes) = std::fs::read(dir.join(format!("{name}.int"))) {
                return Ok(bytes);
            }
        }
        let inner = self.read();
        let path = format!("scripts/{}.int", name.to_lowercase());
        if !inner.vfs.exists(&path) {
            return Err(Error::new(format!("{path} not found in the sources")));
        }
        inner.vfs.read(&path)
    }

    pub fn register_script_file(&self, name: &str, absolute_path: &str) {
        self.write().vfs.add_loose_file(&format!("scripts/{}.int", name.to_lowercase()), absolute_path);
    }

    // ------------------------------------------------------------------------- persisted state

    pub fn load_state(&self) {
        let mut inner = self.write();
        self.load_state_locked(&mut inner);
    }

    pub fn save_state(&self) {
        let inner = self.read();
        self.save_state_locked(&inner);
    }

    fn load_state_locked(&self, inner: &mut Inner) {
        inner.flags.clear();
        inner.custom_terms.clear();
        inner.script_translations.clear();
        let Some(file) = &self.state_file else { return };
        let Ok(bytes) = std::fs::read(file) else { return };
        let Ok(root) = serde_json::from_slice::<Value>(&bytes) else { return };

        if let Some(list) = root["scriptStrings"].as_array() {
            for v in list {
                let script = v["script"].as_str().unwrap_or("").to_string();
                if script.is_empty() {
                    continue;
                }
                inner.script_translations.push(ScriptTranslation {
                    script,
                    operand: v["operand"].as_f64().unwrap_or(0.0) as u32,
                    source: v["source"].as_str().unwrap_or("").to_string(),
                    target: v["target"].as_str().unwrap_or("").to_string(),
                });
            }
        }
        if let Some(list) = root["customTerms"].as_array() {
            for v in list {
                let source = v["source"].as_str().unwrap_or("").to_string();
                if source.is_empty() {
                    continue;
                }
                inner.custom_terms.push(CustomTerm {
                    source,
                    target: v["target"].as_str().unwrap_or("").to_string(),
                    keep: v["keep"].as_bool().unwrap_or(false),
                });
            }
        }
        if let Some(files) = root["files"].as_object() {
            for (rel, entry) in files {
                let mut flags = FileFlags::default();
                let ids = |key: &str| -> Vec<i32> {
                    entry[key].as_array().map(|a| a.iter().filter_map(|v| v.as_i64()).map(|v| v as i32).collect()).unwrap_or_default()
                };
                flags.review = ids("review").into_iter().collect();
                flags.confirmed = ids("confirmed").into_iter().collect();
                let texts = |key: &str| -> HashMap<i32, String> {
                    entry[key]
                        .as_object()
                        .map(|o| {
                            o.iter()
                                .filter_map(|(k, v)| Some((k.parse::<i32>().ok()?, v.as_str()?.to_string())))
                                .collect()
                        })
                        .unwrap_or_default()
                };
                flags.comments = texts("comments");
                flags.previous = texts("previous");
                if let Some(o) = entry["validated"].as_object() {
                    flags.validated =
                        o.iter().filter_map(|(k, v)| Some((k.parse::<i32>().ok()?, v.as_f64()? as u32))).collect();
                }
                inner.flags.insert(rel.clone(), flags);
            }
        }
    }

    fn save_state_locked(&self, inner: &Inner) {
        let Some(file) = &self.state_file else { return };
        let mut files = Map::new();
        for (rel, f) in &inner.flags {
            let mut r: Vec<i32> = f.review.iter().copied().collect();
            let mut c: Vec<i32> = f.confirmed.iter().copied().collect();
            r.sort_unstable();
            c.sort_unstable();
            let mut entry = Map::new();
            entry.insert("review".into(), json!(r));
            entry.insert("confirmed".into(), json!(c));
            let put_texts = |entry: &mut Map<String, Value>, key: &str, map: &HashMap<i32, String>| {
                if map.is_empty() {
                    return;
                }
                let o: Map<String, Value> = map.iter().map(|(k, v)| (k.to_string(), json!(v))).collect();
                entry.insert(key.into(), Value::Object(o));
            };
            put_texts(&mut entry, "comments", &f.comments);
            put_texts(&mut entry, "previous", &f.previous);
            if !f.validated.is_empty() {
                let o: Map<String, Value> = f.validated.iter().map(|(k, v)| (k.to_string(), json!(*v as f64))).collect();
                entry.insert("validated".into(), Value::Object(o));
            }
            files.insert(rel.clone(), Value::Object(entry));
        }
        let mut root = Map::new();
        root.insert("files".into(), Value::Object(files));
        if !inner.custom_terms.is_empty() {
            let terms: Vec<Value> = inner
                .custom_terms
                .iter()
                .map(|t| json!({"source": t.source, "target": t.target, "keep": t.keep}))
                .collect();
            root.insert("customTerms".into(), Value::Array(terms));
        }
        if !inner.script_translations.is_empty() {
            let scripts: Vec<Value> = inner
                .script_translations
                .iter()
                .map(|t| json!({"script": t.script, "operand": t.operand as f64, "source": t.source, "target": t.target}))
                .collect();
            root.insert("scriptStrings".into(), Value::Array(scripts));
        }
        if let Ok(bytes) = serde_json::to_vec(&Value::Object(root)) {
            let _ = fsutil::write_atomic(file, &bytes);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    fn config(root: &Path) -> ProjectConfig {
        ProjectConfig {
            id: "t1".into(),
            name: "Test".into(),
            sources: vec![root.join("mod").to_string_lossy().replace('\\', "/")],
            output_dir: root.join("out").to_string_lossy().replace('\\', "/"),
            source_language: "english".into(),
            target_language: "french".into(),
            ..Default::default()
        }
    }

    fn project(c: &ProjectConfig, root: &Path) -> Project {
        let mut p = Project::new(c.clone());
        p.set_state_file(root.join("state.json"));
        let errors = p.reload();
        assert!(errors.is_empty(), "{errors:?}");
        p
    }

    #[test]
    fn project_workflow() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(
            &root.join("mod/text/english/game/pro_item.msg"),
            b"{100}{}{Stimpak}\r\n{101}{}{A healing item.}\r\n{200}{}{Leather Armor}\r\n",
        );
        write(
            &root.join("mod/text/french/game/pro_item.msg"),
            b"{100}{}{Stimpak}\r\n{101}{}{Un objet de soin.}\r\n{200}{}{Armure en cuir}\r\n",
        );
        write(
            &root.join("mod/text/english/dialog/abel.msg"),
            b"# Abel\r\n{100}{}{Hello there!}\r\n{101}{}{Take a Stimpak.}\r\n{102}{}{...}\r\n",
        );
        let c = config(root);
        let p = project(&c, root);

        // /game first
        assert_eq!(p.message_files(), vec!["game/pro_item.msg", "dialog/abel.msg"]);
        assert!(p.available_languages().contains(&"french".to_string()));

        // glossary from /game: names only, descriptions skipped
        p.set_glossary(p.build_glossary(None));
        assert_eq!(p.glossary().terms().len(), 2);
        assert_eq!(p.glossary().check_consistency("Take a Leather Armor", "Prends une cuirasse").len(), 1);

        // statistics: "Stimpak" == "Stimpak" is the only ambiguous one
        let mut game = p.load_document("game/pro_item.msg").unwrap();
        let gs = Project::statistics_of(&game);
        assert_eq!(gs.lines, 3);
        assert_eq!(gs.untranslated, 1);
        game.rows[0].confirmed = true;
        assert_eq!(Project::statistics_of(&game).untranslated, 0);

        // dialog: nothing translated yet; "..." has nothing to translate
        let mut dlg = p.load_document("dialog/abel.msg").unwrap();
        assert!(!dlg.target_file_exists);
        let ds = Project::statistics_of(&dlg);
        assert_eq!(ds.lines, 3);
        assert_eq!(ds.untranslated, 2);
        assert_eq!(ds.words_remaining, 5);

        dlg.rows[0].target = "Bonjour à toi !".into();
        dlg.rows[1].review = true;
        dlg.rows[1].target = "Prends un Stimpak.".into();
        let bad = p.save_document(&dlg, true).unwrap();
        assert!(bad.is_empty());

        let written = std::fs::read(root.join("out/text/french/dialog/abel.msg")).unwrap();
        assert!(written.starts_with(b"# Abel\r\n{100}{}{Bonjour \xE0 toi !}"));
        assert!(String::from_utf8_lossy(&written).contains("{102}{}{...}")); // untranslated line keeps the source

        // reload from disk (+ persisted review flag)
        let q = project(&c, root);
        let again = q.load_document("dialog/abel.msg").unwrap();
        assert!(again.target_file_exists);
        assert_eq!(again.rows[0].target, "Bonjour à toi !");
        assert!(again.rows[1].review);
        assert_eq!(Project::status_of(&again.rows[1]), SegmentStatus::ToReview);
        let todo = q.review_items(&HashSet::new());
        assert_eq!(todo.len(), 1);
        assert_eq!(todo[0].id, 101);
        assert_eq!(todo[0].rel_path, "dialog/abel.msg");
        assert!(q.review_items(&["dialog/abel.msg".to_string()].into_iter().collect()).is_empty());
        assert_eq!(Project::status_of(&again.rows[0]), SegmentStatus::Translated);
        assert_eq!(Project::status_of(&again.rows[2]), SegmentStatus::Translated);

        assert_eq!(q.compute_statistics(None, None).len(), 2);
    }

    #[test]
    fn row_comments_and_validation() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("mod/text/english/dialog/a.msg"), b"{1}{}{One}\r\n{2}{}{Two}\r\n");
        let c = config(root);
        let p = project(&c, root);
        let mut doc = p.load_document("dialog/a.msg").unwrap();
        doc.rows[0].target = "Un".into();
        doc.rows[0].comment = "check the tone".into();
        doc.rows[0].validated = Project::text_hash("Un");
        doc.rows[1].target = "Deux".into();
        doc.rows[1].validated = Project::text_hash("Deux");
        assert!(!doc.rows[0].changed_since_validation());
        p.save_document(&doc, true).unwrap();

        let q = project(&c, root);
        let mut again = q.load_document("dialog/a.msg").unwrap();
        assert_eq!(again.rows[0].comment, "check the tone");
        assert!(!again.rows[0].changed_since_validation());
        again.rows[1].target = "Deux, modifié".into();
        assert!(again.rows[1].changed_since_validation());
        assert_ne!(Project::text_hash("a"), Project::text_hash("b"));
        assert_eq!(Project::text_hash("same"), Project::text_hash("same"));
    }

    #[test]
    fn text_hash_is_the_fnv_of_utf16() {
        // value computed by the Qt version for these texts
        assert_eq!(Project::text_hash(""), 2166136261);
        let expected = {
            let mut h: u32 = 2166136261;
            for u in "é".encode_utf16() {
                h = (h ^ u as u32).wrapping_mul(16777619);
            }
            h
        };
        assert_eq!(Project::text_hash("é"), expected);
    }

    #[test]
    fn statistics_cache() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("mod/text/english/dialog/a.msg"), b"{1}{}{Hello there}\r\n{2}{}{Bye}\r\n");
        write(&root.join("mod/text/english/dialog/b.msg"), b"{1}{}{Another one}\r\n");
        write(&root.join("mod/text/french/dialog/a.msg"), b"{1}{}{Bonjour}\r\n{2}{}{Salut}\r\n");
        let c = config(root);

        let make = |c: &ProjectConfig| {
            let mut p = Project::new(c.clone());
            p.set_state_file(root.join("state.json"));
            p.set_stats_cache_file(root.join("stats.json"));
            p.reload();
            p
        };
        let p = make(&c);
        let first = p.compute_statistics(None, None);
        assert_eq!(p.stats_cache_hits(), 0);
        assert!(root.join("stats.json").exists());

        let q = make(&c);
        let second = q.compute_statistics(None, None);
        assert_eq!(q.stats_cache_hits(), 2);
        assert_eq!(first, second);

        // changing a translation invalidates only that file
        let mut doc = q.load_document("dialog/b.msg").unwrap();
        doc.rows[0].target = "Un autre".into();
        q.save_document(&doc, true).unwrap();
        q.compute_statistics(None, None);
        assert_eq!(q.stats_cache_hits(), 1);
    }

    #[test]
    fn marks_without_rewriting_files() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("mod/text/english/dialog/abel.msg"), b"{100}{}{Hello}\r\n{101}{}{Bye}\r\n");
        let c = config(root);
        let p = project(&c, root);
        let marks = vec![ReviewMark { rel_path: "dialog/abel.msg".into(), id: 101, comment: "Game report: rude".into() }];
        assert_eq!(p.mark_for_review(&marks), 1);
        assert_eq!(p.mark_for_review(&marks), 0); // idempotent
        assert!(!root.join("out/text/french/dialog/abel.msg").exists());
        let doc = p.load_document("dialog/abel.msg").unwrap();
        assert!(doc.rows[1].review);
        assert!(doc.rows[1].comment.contains("rude"));
        let q = project(&c, root);
        assert!(q.load_document("dialog/abel.msg").unwrap().rows[1].review); // persisted
    }

    #[test]
    fn custom_terms_and_script_translations_persist() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("mod/text/english/game/pro_item.msg"), b"{100}{}{Stimpak}\r\n{101}{}{Super Stimpak}\r\n");
        write(&root.join("mod/text/french/game/pro_item.msg"), b"{100}{}{Stimpak}\r\n{101}{}{Super Stimpak}\r\n");
        let c = config(root);
        let p = project(&c, root);
        p.set_custom_terms(vec![
            CustomTerm { source: "Stimpak".into(), target: "Seringue".into(), keep: false },
            CustomTerm { source: "Radscorpion".into(), target: "Radscorpion".into(), keep: true },
        ]);
        p.set_script_translations(vec![ScriptTranslation {
            script: "sammy".into(),
            operand: 6,
            source: "You see Sammy.".into(),
            target: "C'est Sammy.".into(),
        }]);
        let q = project(&c, root);
        assert_eq!(q.custom_terms().len(), 2);
        assert_eq!(q.script_translations()[0].target, "C'est Sammy.");
        let g = q.build_glossary(None);
        let stimpak = g.terms().iter().find(|t| t.source == "Stimpak").unwrap();
        assert!(stimpak.custom);
        assert_eq!(stimpak.targets, vec!["Seringue".to_string()]);
        assert!(!stimpak.conflict());
        assert!(g.terms().iter().find(|t| t.source == "Radscorpion").unwrap().keep);
    }

    #[test]
    fn config_file_compatibility() {
        // a project file written by the Qt version
        let json = r#"{"id":"abc12345","name":"RPU","outputDir":"C:/Fallout2/data","sources":["C:/Fallout2/master.dat"],
            "sourceLanguage":"english","targetLanguage":"french","sourceEncoding":"cp1252","targetEncoding":"cp1252"}"#;
        let c: ProjectConfig = serde_json::from_str(json).unwrap();
        assert_eq!(c.id, "abc12345");
        assert_eq!(c.output_archive, "");
        assert!(c.archive_with_source);
        assert_eq!(c.output_archive_format, "dat2");
        let back = serde_json::to_value(&c).unwrap();
        assert_eq!(back["outputDir"], "C:/Fallout2/data");
        assert_eq!(back["archiveWithSource"], true);
    }
}
