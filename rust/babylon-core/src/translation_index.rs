//! Translation memory of the project itself: every translated line (source -> translation), searchable by
//! similarity, so the translator is offered what was already done for the same or a close text.
//! `find` and `update_file` may be called from different threads.

use crate::project::{Project, SegmentStatus, TranslationDoc, TranslationRow};
use crate::statistics;
use regex::Regex;
use std::collections::{HashMap, HashSet};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{OnceLock, RwLock};

#[derive(Debug, Clone, PartialEq)]
pub struct Match {
    pub rel_path: String,
    pub id: i32,
    pub source: String,
    pub target: String,
    /// 1.0: same text.
    pub score: f64,
    /// How many lines carry this very pair.
    pub count: usize,
}

#[derive(Clone)]
struct Entry {
    rel_path: String,
    id: i32,
    source: String,
    target: String,
    /// Simplified, lower case source.
    key: String,
    tokens: usize,
    count: usize,
    alive: bool,
}

#[derive(Default)]
struct Data {
    entries: Vec<Entry>,
    postings: HashMap<String, Vec<usize>>, // token -> entries
    by_file: HashMap<String, Vec<usize>>,  // file -> entries
    dead: usize,
}

#[derive(Default)]
pub struct TranslationIndex {
    data: RwLock<Data>,
}

fn tokens_of(key: &str) -> Vec<String> {
    static WORD: OnceLock<Regex> = OnceLock::new();
    let word = WORD.get_or_init(|| Regex::new(r"[\p{L}\p{N}]{2,}").unwrap());
    let set: HashSet<String> = word.find_iter(key).map(|m| m.as_str().to_string()).collect();
    set.into_iter().collect()
}

fn simplified(s: &str) -> String {
    s.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// Only translated lines with real text are worth remembering.
pub fn usable(row: &TranslationRow) -> bool {
    row.in_source
        && !row.target.is_empty()
        && statistics::is_translatable(&row.source)
        && simplified(&row.source).chars().count() >= 3
        && Project::status_of(row) == SegmentStatus::Translated
}

/// 0..1, on simplified lower-case text (Levenshtein ratio).
pub fn similarity(a: &str, b: &str) -> f64 {
    if a == b {
        return 1.0;
    }
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let (la, lb) = (a.len(), b.len());
    let longest = la.max(lb);
    if longest == 0 {
        return 1.0;
    }
    // The ratio can never beat the length ratio: no need to compute when that is already too low.
    if (la.min(lb) as f64) / (longest as f64) < 0.3 {
        return (la.min(lb) as f64) / (longest as f64) * 0.5;
    }
    let mut prev: Vec<usize> = (0..=lb).collect();
    let mut cur = vec![0usize; lb + 1];
    for i in 1..=la {
        cur[0] = i;
        for j in 1..=lb {
            let cost = if a[i - 1] == b[j - 1] { 0 } else { 1 };
            cur[j] = (prev[j] + 1).min(cur[j - 1] + 1).min(prev[j - 1] + cost);
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    1.0 - prev[lb] as f64 / longest as f64
}

impl Data {
    fn add_rows(&mut self, doc: &TranslationDoc) {
        let mut first_of: HashMap<String, usize> = HashMap::new(); // the same pair twice in one file is kept once, with a count
        let mut added: Vec<usize> = Vec::new();
        for row in &doc.rows {
            if !usable(row) {
                continue;
            }
            let key = simplified(&row.source).to_lowercase();
            let pair = format!("{}\u{1F}{}", key, simplified(&row.target));
            if let Some(&i) = first_of.get(&pair) {
                self.entries[i].count += 1;
                continue;
            }
            let tokens = tokens_of(&key);
            let index = self.entries.len();
            self.entries.push(Entry {
                rel_path: doc.rel_path.clone(),
                id: row.id,
                source: row.source.clone(),
                target: row.target.clone(),
                key,
                tokens: tokens.len(),
                count: 1,
                alive: true,
            });
            first_of.insert(pair, index);
            added.push(index);
            for t in tokens {
                self.postings.entry(t).or_default().push(index);
            }
        }
        if !added.is_empty() {
            self.by_file.entry(doc.rel_path.clone()).or_default().extend(added);
        }
    }

    fn compact(&mut self) {
        let old = std::mem::take(&mut self.entries);
        self.postings.clear();
        self.by_file.clear();
        self.dead = 0;
        for e in old.into_iter().filter(|e| e.alive) {
            let index = self.entries.len();
            self.by_file.entry(e.rel_path.clone()).or_default().push(index);
            for t in tokens_of(&e.key) {
                self.postings.entry(t).or_default().push(index);
            }
            self.entries.push(e);
        }
    }
}

impl TranslationIndex {
    /// Reads every message file of the project (saved state). Returns an empty index when cancelled.
    pub fn build(
        project: &Project,
        mut progress: Option<&mut dyn FnMut(usize, usize)>,
        cancel: Option<&AtomicBool>,
    ) -> TranslationIndex {
        let index = TranslationIndex::default();
        let files = project.message_files();
        {
            let mut data = index.data.write().unwrap();
            for (i, rel) in files.iter().enumerate() {
                if cancel.is_some_and(|c| c.load(Ordering::Relaxed)) {
                    return TranslationIndex::default();
                }
                if let Ok(doc) = project.load_document(rel) {
                    data.add_rows(&doc);
                }
                if i % 10 == 0 {
                    if let Some(p) = progress.as_deref_mut() {
                        p(i, files.len());
                    }
                }
            }
        }
        if let Some(p) = progress.as_deref_mut() {
            p(files.len(), files.len());
        }
        index
    }

    /// Replaces what the index knows of one file (after it was saved or imported).
    pub fn update_file(&self, doc: &TranslationDoc) {
        let mut data = self.data.write().unwrap();
        if let Some(old) = data.by_file.remove(&doc.rel_path) {
            for i in old {
                if data.entries[i].alive {
                    data.entries[i].alive = false;
                    data.dead += 1;
                }
            }
        }
        data.add_rows(doc);
        if data.dead > 1000 && data.dead * 3 > data.entries.len() {
            data.compact();
        }
    }

    /// Lines currently indexed.
    pub fn len(&self) -> usize {
        let data = self.data.read().unwrap();
        data.entries.len() - data.dead
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// The closest translated lines for `source`, best first (at most `max`, at least `min_score` similar).
    /// The line (`exclude_rel`, `exclude_id`) itself is not offered.
    pub fn find(&self, source: &str, max: usize, min_score: f64, exclude_rel: &str, exclude_id: i32) -> Vec<Match> {
        let key = simplified(source).to_lowercase();
        let tokens = tokens_of(&key);
        if tokens.is_empty() {
            return Vec::new();
        }
        let data = self.data.read().unwrap();
        // candidates: entries sharing words with the query (very common words are skipped when others exist)
        let mut overlap: HashMap<usize, usize> = HashMap::new();
        for t in &tokens {
            let Some(list) = data.postings.get(t) else { continue };
            if list.len() > 4000 && tokens.len() > 1 {
                continue;
            }
            for &i in list {
                *overlap.entry(i).or_insert(0) += 1;
            }
        }
        let floor = (min_score - 0.35).max(0.3);
        let mut candidates: Vec<(usize, f64)> = overlap
            .into_iter()
            .filter_map(|(i, n)| {
                let e = &data.entries[i];
                if !e.alive {
                    return None;
                }
                let dice = 2.0 * n as f64 / (tokens.len() + e.tokens) as f64;
                (dice >= floor).then_some((i, dice))
            })
            .collect();
        candidates.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal).then(a.0.cmp(&b.0)));
        candidates.truncate(80);

        let mut result: Vec<Match> = Vec::new();
        let mut by_target: HashMap<String, usize> = HashMap::new(); // the same translation found several times: one line
        for (i, _) in candidates {
            let e = &data.entries[i];
            if e.rel_path == exclude_rel && e.id == exclude_id && e.count == 1 {
                continue;
            }
            let score = similarity(&key, &e.key);
            if score < min_score {
                continue;
            }
            let target_key = simplified(&e.target);
            if let Some(&at) = by_target.get(&target_key) {
                let m = &mut result[at];
                m.count += e.count;
                if score > m.score {
                    m.score = score;
                    m.rel_path = e.rel_path.clone();
                    m.id = e.id;
                    m.source = e.source.clone();
                }
                continue;
            }
            by_target.insert(target_key, result.len());
            result.push(Match {
                rel_path: e.rel_path.clone(),
                id: e.id,
                source: e.source.clone(),
                target: e.target.clone(),
                score,
                count: e.count,
            });
        }
        result.sort_by(|a, b| b.score.partial_cmp(&a.score).unwrap_or(std::cmp::Ordering::Equal).then(b.count.cmp(&a.count)));
        result.truncate(max);
        result
    }
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
    fn translation_index_suggestions() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(
            &root.join("m/text/english/dialog/a.msg"),
            b"{1}{}{You need a Stimpak to heal your wounds.}\r\n{2}{}{The Vault Dweller returns home.}\r\n{3}{}{Goodbye.}\r\n{4}{}{Nothing yet.}\r\n",
        );
        write(
            &root.join("m/text/french/dialog/a.msg"),
            b"{1}{}{Il vous faut un Stimpak pour soigner vos blessures.}\r\n{2}{}{L'Habitant de l'Abri rentre chez lui.}\r\n{3}{}{Au revoir.}\r\n",
        );
        write(
            &root.join("m/text/english/dialog/b.msg"),
            b"{1}{}{You need a Stimpak to heal your wounds.}\r\n{2}{}{You need a Super Stimpak to heal your wounds.}\r\n{3}{}{Goodbye.}\r\n",
        );
        write(&root.join("m/text/french/dialog/b.msg"), b"{1}{}{Il vous faut un Stimpak pour soigner vos blessures.}\r\n");
        let config = ProjectConfig {
            id: "ti".into(),
            name: "ti".into(),
            sources: vec![root.join("m").to_string_lossy().replace('\\', "/")],
            output_dir: root.join("out").to_string_lossy().replace('\\', "/"),
            ..Default::default()
        };
        let mut p = Project::new(config);
        p.set_state_file(root.join("state.json"));
        p.reload();

        let index = TranslationIndex::build(&p, None, None);
        assert_eq!(index.len(), 4); // a.msg 1-3 and b.msg 1 (untranslated lines are not indexed)

        // the same text elsewhere: an exact suggestion; the line itself is not offered
        let found = index.find("You need a Stimpak to heal your wounds.", 5, 0.7, "dialog/b.msg", 2);
        assert!(!found.is_empty());
        assert_eq!(found[0].score, 1.0);
        assert_eq!(found[0].target, "Il vous faut un Stimpak pour soigner vos blessures.");
        assert_eq!(found[0].count, 2); // translated the same way in two files

        // a close text: a fuzzy suggestion below 100 %
        let found = index.find("You need a Super Stimpak to heal your wounds.", 5, 0.7, "dialog/b.msg", 2);
        assert!(!found.is_empty());
        assert!(found[0].score < 1.0 && found[0].score > 0.7);
        assert_eq!(found[0].source, "You need a Stimpak to heal your wounds.");

        // nothing similar, and a line asking about itself
        assert!(index.find("Completely different sentence about robots.", 5, 0.7, "", -1).is_empty());
        assert!(index.find("The Vault Dweller returns home.", 5, 0.7, "dialog/a.msg", 2).is_empty());

        // a saved file is taken into account without rebuilding everything
        let mut doc = p.load_document("dialog/a.msg").unwrap();
        doc.rows[3].target = "Rien pour l'instant.".into();
        index.update_file(&doc);
        assert_eq!(index.len(), 5);
        let found = index.find("Nothing yet.", 5, 0.7, "", -1);
        assert!(!found.is_empty());
        assert_eq!(found[0].target, "Rien pour l'instant.");
        doc.rows[3].target.clear();
        index.update_file(&doc);
        assert!(index.find("Nothing yet.", 5, 0.7, "", -1).is_empty());

        assert_eq!(similarity("abc", "abc"), 1.0);
        assert!(similarity("kitten", "sitting") < 0.6);
    }
}
