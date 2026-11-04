//! Search and replace over every message file of a project.

use crate::project::{Project, TranslationDoc};
use fancy_regex::Regex;
use std::collections::HashSet;
use std::sync::atomic::{AtomicBool, Ordering};

#[derive(Debug, Clone)]
pub struct Options {
    pub text: String,
    /// Look in the source texts.
    pub in_source: bool,
    /// Look in the translations.
    pub in_target: bool,
    /// `text` is a regular expression (replacement may use \1, \2...).
    pub regex: bool,
    pub case_sensitive: bool,
    pub whole_word: bool,
}

impl Default for Options {
    fn default() -> Self {
        Options { text: String::new(), in_source: false, in_target: true, regex: false, case_sensitive: false, whole_word: false }
    }
}

impl Options {
    /// The compiled pattern; an error message when the pattern is empty or malformed.
    pub fn expression(&self) -> Result<Regex, String> {
        if self.text.is_empty() {
            return Err("empty pattern".into());
        }
        let mut pattern = if self.regex { self.text.clone() } else { fancy_regex::escape(&self.text).into_owned() };
        if self.whole_word {
            pattern = format!(r"(?<![\p{{L}}\p{{N}}_])(?:{pattern})(?![\p{{L}}\p{{N}}_])");
        }
        if !self.case_sensitive {
            pattern = format!("(?i){pattern}");
        }
        Regex::new(&pattern).map_err(|e| e.to_string())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Hit {
    pub rel_path: String,
    pub id: i32,
    /// Which text matched.
    pub in_target: bool,
    /// The text that matched.
    pub text: String,
    /// The text after replacement (when a replacement was given).
    pub after: String,
    pub occurrences: usize,
}

fn count_matches(re: &Regex, text: &str) -> usize {
    re.find_iter(text).filter(|m| m.is_ok()).count()
}

/// Replacement inside one text; returns the number of replacements made.
pub fn replace_in(text: &mut String, options: &Options, replacement: &str) -> usize {
    let Ok(re) = options.expression() else { return 0 };
    let mut count = 0;
    let mut result = String::with_capacity(text.len());
    let mut last = 0;
    for caps in re.captures_iter(text) {
        let Ok(caps) = caps else { continue };
        let whole = caps.get(0).unwrap();
        result.push_str(&text[last..whole.start()]);
        if options.regex {
            // \1..\9 insert a captured group; a plain search never interprets the replacement
            let chars: Vec<char> = replacement.chars().collect();
            let mut i = 0;
            while i < chars.len() {
                let ch = chars[i];
                if ch == '\\' && i + 1 < chars.len() {
                    let next = chars[i + 1];
                    if let Some(d) = next.to_digit(10) {
                        if let Some(g) = caps.get(d as usize) {
                            result.push_str(g.as_str());
                        }
                        i += 2;
                        continue;
                    }
                    if next == '\\' {
                        result.push('\\');
                        i += 2;
                        continue;
                    }
                }
                result.push(ch);
                i += 1;
            }
        } else {
            result.push_str(replacement);
        }
        last = whole.end();
        count += 1;
    }
    if count == 0 {
        return 0;
    }
    result.push_str(&text[last..]);
    *text = result;
    count
}

/// Applies the replacement to the translation of the rows of `doc` listed in `ids` (all rows when empty).
/// Returns the number of replacements.
pub fn replace_in_document(doc: &mut TranslationDoc, options: &Options, replacement: &str, ids: &HashSet<i32>) -> usize {
    let mut total = 0;
    for row in doc.rows.iter_mut() {
        if !ids.is_empty() && !ids.contains(&row.id) {
            continue;
        }
        let mut text = row.target.clone();
        let n = replace_in(&mut text, options, replacement);
        if n > 0 && text != row.target {
            row.confirmed = !text.is_empty() && text == row.source;
            row.target = text;
            total += n;
        }
    }
    total
}

/// All matching records (at most `max_hits`); the flag tells when the limit was reached.
/// With `replacement` the hits of translations carry the text after replacement.
pub fn search(
    project: &Project,
    options: &Options,
    replacement: Option<&str>,
    mut progress: Option<&mut dyn FnMut(usize, usize)>,
    cancel: Option<&AtomicBool>,
    max_hits: usize,
) -> (Vec<Hit>, bool) {
    let mut hits = Vec::new();
    let Ok(re) = options.expression() else { return (hits, false) };

    let files = project.message_files();
    for (i, rel) in files.iter().enumerate() {
        if cancel.is_some_and(|c| c.load(Ordering::Relaxed)) {
            break;
        }
        if i % 20 == 0 {
            if let Some(p) = progress.as_deref_mut() {
                p(i, files.len());
            }
        }
        let Ok(doc) = project.load_document(rel) else { continue };
        for row in &doc.rows {
            for pass in 0..2 {
                let target = pass == 1;
                if if target { !options.in_target } else { !options.in_source } {
                    continue;
                }
                let text = if target { &row.target } else { &row.source };
                if text.is_empty() || !re.is_match(text).unwrap_or(false) {
                    continue;
                }
                let mut hit = Hit {
                    rel_path: doc.rel_path.clone(),
                    id: row.id,
                    in_target: target,
                    text: text.clone(),
                    after: String::new(),
                    occurrences: 0,
                };
                match replacement {
                    Some(r) if target => {
                        hit.after = text.clone();
                        hit.occurrences = replace_in(&mut hit.after, options, r);
                    }
                    _ => hit.occurrences = count_matches(&re, text),
                }
                hits.push(hit);
                if hits.len() >= max_hits {
                    return (hits, true);
                }
            }
        }
    }
    if let Some(p) = progress.as_deref_mut() {
        p(files.len(), files.len());
    }
    (hits, false)
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
    fn project_search_and_replace() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("m/text/english/dialog/a.msg"), b"{1}{}{The Vault Dweller}\r\n{2}{}{Vault 13}\r\n{3}{}{Nothing}\r\n");
        write(
            &root.join("m/text/french/dialog/a.msg"),
            b"{1}{}{L'Habitant de l'Abri}\r\n{2}{}{Abri 13 et abri 8}\r\n{3}{}{Rien du tout}\r\n",
        );
        write(&root.join("m/text/english/dialog/b.msg"), b"{1}{}{Cat}\r\n");
        write(&root.join("m/text/french/dialog/b.msg"), b"{1}{}{Concatener le chat}\r\n");
        let fwd = |p: std::path::PathBuf| p.to_string_lossy().replace('\\', "/");
        let config = ProjectConfig {
            id: "search".into(),
            name: "search".into(),
            sources: vec![fwd(root.join("m"))],
            output_dir: fwd(root.join("out")),
            ..Default::default()
        };
        let make = || {
            let mut p = Project::new(config.clone());
            p.set_state_file(root.join("state.json"));
            p.reload();
            p
        };
        let p = make();

        let mut o = Options { text: "abri".into(), ..Default::default() };
        // case-insensitive by default: three occurrences in two lines of a.msg
        let (hits, _) = search(&p, &o, Some("Refuge"), None, None, 5000);
        assert_eq!(hits.len(), 2);
        let mut occurrences = 0;
        for h in &hits {
            assert!(h.in_target);
            assert!(!h.after.to_lowercase().contains("bri"));
            occurrences += h.occurrences;
        }
        assert_eq!(occurrences, 3);

        o.case_sensitive = true;
        assert_eq!(search(&p, &o, None, None, None, 5000).0.len(), 1); // only the lowercase one

        // whole words
        let mut w = Options { text: "chat".into(), ..Default::default() };
        assert_eq!(search(&p, &w, None, None, None, 5000).0.len(), 1);
        w.whole_word = true;
        assert_eq!(search(&p, &w, None, None, None, 5000).0.len(), 1);
        w.text = "cat".into();
        w.whole_word = false;
        w.in_source = true;
        w.in_target = true;
        assert_eq!(search(&p, &w, None, None, None, 5000).0.len(), 2); // "Cat" (source) and "Concatener" (target)
        w.whole_word = true;
        let words = search(&p, &w, None, None, None, 5000).0;
        assert_eq!(words.len(), 1);
        assert!(!words[0].in_target);

        // regex with a back-reference, and a plain replacement that contains a backslash and a dollar
        let r = Options { text: r"(\w+) (\d+)".into(), regex: true, ..Default::default() };
        let mut text = "Abri 13".to_string();
        assert_eq!(replace_in(&mut text, &r, r"\2 \1"), 1);
        assert_eq!(text, "13 Abri");
        let plain = Options { text: "13".into(), ..Default::default() };
        let mut text = "Abri 13".to_string();
        replace_in(&mut text, &plain, r"\1$1");
        assert_eq!(text, r"Abri \1$1");
        let bad = Options { text: "(".into(), regex: true, ..Default::default() };
        assert!(bad.expression().is_err());
        assert!(search(&p, &bad, None, None, None, 5000).0.is_empty());

        // replacing in a document changes only the requested records
        let mut doc = p.load_document("dialog/a.msg").unwrap();
        let again = Options { text: "abri".into(), ..Default::default() };
        let only_2: HashSet<i32> = [2].into_iter().collect();
        assert_eq!(replace_in_document(&mut doc, &again, "Refuge", &only_2), 2);
        for row in &doc.rows {
            if row.id == 1 {
                assert_eq!(row.target, "L'Habitant de l'Abri");
            }
            if row.id == 2 {
                assert_eq!(row.target, "Refuge 13 et Refuge 8");
            }
        }
        p.save_document(&doc, true).unwrap();
        let q = make();
        assert_eq!(search(&q, &again, None, None, None, 5000).0.len(), 1);

        // truncation
        let any = Options { text: "e".into(), in_source: true, ..Default::default() };
        let (hits, truncated) = search(&q, &any, None, None, None, 2);
        assert_eq!(hits.len(), 2);
        assert!(truncated);
    }
}
