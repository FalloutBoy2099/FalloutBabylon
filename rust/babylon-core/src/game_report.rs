//! Lines noticed in the game ("this text is wrong / cut / mistranslated"), written by the player or by a script
//! into a plain text file, one line per remark:
//!
//! ```text
//! dialog/abel.msg 102 the name is not the one of the quest
//! abel.msg#102
//! text/french/game/pro_item.msg:200 too long for the inventory
//! Vous n'avez pas assez de capsules      <- a text seen on screen, found in the project
//! ```
//!
//! Empty lines and lines starting with '#' or ';' are ignored. Every line found is marked "to review" with the
//! remark (and "Game report") as a comment.

use crate::i18n::tr;
use crate::project::Project;
use regex::Regex;
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::OnceLock;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Remark {
    /// Empty for a text to look for.
    pub rel_path: String,
    pub id: i32,
    /// The text to look for (when `rel_path` is empty).
    pub text: String,
    pub note: String,
    /// The line of the report.
    pub raw: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Resolved {
    pub rel_path: String,
    pub id: i32,
    pub comment: String,
    pub raw: String,
}

#[derive(Debug, Clone, Default)]
pub struct Outcome {
    pub found: Vec<Resolved>,
    /// Lines of the report that matched nothing.
    pub not_found: Vec<String>,
    /// Texts that match too many lines to choose.
    pub ambiguous: Vec<String>,
}

/// The comment put on a marked line.
pub fn comment_for(note: &str) -> String {
    let prefix = tr("Game report");
    if note.trim().is_empty() {
        prefix
    } else {
        format!("{}: {}", prefix, note.trim())
    }
}

pub fn parse(report: &str) -> Vec<Remark> {
    // [text/<language>/]<path>.msg, then a separator (space, #, :, ',' or ';'), the id, then a free note
    static LOCATED: OnceLock<Regex> = OnceLock::new();
    let located = LOCATED
        .get_or_init(|| Regex::new(r"(?i)^(?:text[/\\][^/\\]+[/\\])?([\w .\\/-]+?\.msg)\s*[#:,;\s]\s*(\d+)\b\s*(.*)$").unwrap());

    let mut remarks = Vec::new();
    let text = report.strip_prefix('\u{FEFF}').unwrap_or(report);
    for raw_line in text.split('\n') {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        let mut r = Remark { raw: line.to_string(), ..Default::default() };
        if let Some(m) = located.captures(line) {
            r.rel_path = m[1].replace('\\', "/").trim().to_lowercase();
            r.id = m[2].parse().unwrap_or(0);
            r.note = m[3].trim().to_string();
        } else {
            r.text = line.to_string();
        }
        remarks.push(r);
    }
    remarks
}

/// Finds the lines of the project the remarks designate. A text is looked for in the translations, then in the
/// sources; a text found in more than `max_matches` lines is reported as ambiguous.
pub fn resolve(project: &Project, remarks: &[Remark], max_matches: usize, cancel: Option<&AtomicBool>) -> Outcome {
    let mut out = Outcome::default();
    let files = project.message_files();

    // path of a remark: complete ("dialog/abel.msg"), or only a name ("abel.msg")
    let file_for = |path: &str| -> Option<String> {
        if let Some(f) = files.iter().find(|f| f.eq_ignore_ascii_case(path)) {
            return Some(f.clone());
        }
        let name = path.rsplit('/').next().unwrap_or(path);
        let mut found: Option<&String> = None;
        for f in &files {
            if f.rsplit('/').next().unwrap_or("").eq_ignore_ascii_case(name) {
                if found.is_some() {
                    return None; // the name is ambiguous
                }
                found = Some(f);
            }
        }
        found.cloned()
    };

    let mut wanted: Vec<(usize, String)> = Vec::new(); // remark index, simplified lower case text
    for (i, r) in remarks.iter().enumerate() {
        if !r.rel_path.is_empty() {
            let mut exists = false;
            if let Some(file) = file_for(&r.rel_path) {
                if let Ok(doc) = project.load_document(&file) {
                    exists = doc.rows.iter().any(|row| row.id == r.id);
                    if exists {
                        out.found.push(Resolved { rel_path: file, id: r.id, comment: comment_for(&r.note), raw: r.raw.clone() });
                    }
                }
            }
            if !exists {
                out.not_found.push(r.raw.clone());
            }
        } else {
            wanted.push((i, r.text.split_whitespace().collect::<Vec<_>>().join(" ").to_lowercase()));
        }
    }

    if !wanted.is_empty() {
        let mut in_translation: HashMap<usize, Vec<Resolved>> = HashMap::new();
        let mut in_source: HashMap<usize, Vec<Resolved>> = HashMap::new();
        for file in &files {
            if cancel.is_some_and(|c| c.load(Ordering::Relaxed)) {
                break;
            }
            let Ok(doc) = project.load_document(file) else { continue };
            for row in &doc.rows {
                let target = row.target.split_whitespace().collect::<Vec<_>>().join(" ").to_lowercase();
                let source = row.source.split_whitespace().collect::<Vec<_>>().join(" ").to_lowercase();
                for (i, key) in &wanted {
                    if key.is_empty() {
                        continue;
                    }
                    let hit_target = !target.is_empty() && target.contains(key.as_str());
                    let hit_source = !hit_target && source.contains(key.as_str());
                    if !hit_target && !hit_source {
                        continue;
                    }
                    let found = Resolved { rel_path: file.clone(), id: row.id, comment: comment_for(&remarks[*i].note), raw: remarks[*i].raw.clone() };
                    (if hit_target { &mut in_translation } else { &mut in_source }).entry(*i).or_default().push(found);
                }
            }
        }
        for (i, _) in &wanted {
            // what is seen on screen is the translation; the source is only a fallback
            let matches = in_translation.get(i).or_else(|| in_source.get(i)).cloned().unwrap_or_default();
            if matches.is_empty() {
                out.not_found.push(remarks[*i].raw.clone());
            } else if matches.len() > max_matches {
                out.ambiguous.push(remarks[*i].raw.clone());
            } else {
                out.found.extend(matches);
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::project::{ProjectConfig, ReviewMark};
    use std::path::Path;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    #[test]
    fn game_report_remarks() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("m/text/english/dialog/abel.msg"), b"{100}{}{Hello there.}\r\n{101}{}{You are not welcome.}\r\n");
        write(&root.join("m/text/french/dialog/abel.msg"), b"{100}{}{Bonjour.}\r\n{101}{}{Vous n'etes pas le bienvenu.}\r\n");
        write(&root.join("m/text/english/game/misc.msg"), b"{200}{}{Hello there.}\r\n");
        write(&root.join("m/text/french/game/misc.msg"), b"{200}{}{Salut.}\r\n");
        let config = ProjectConfig {
            id: "gr".into(),
            name: "gr".into(),
            sources: vec![root.join("m").to_string_lossy().replace('\\', "/")],
            output_dir: root.join("out").to_string_lossy().replace('\\', "/"),
            ..Default::default()
        };
        let make = || {
            let mut p = Project::new(config.clone());
            p.set_state_file(root.join("state.json"));
            p.reload();
            p
        };
        let p = make();

        let report = "# written by the game\r\n\
dialog/abel.msg 101 the sentence is too rude\r\n\
abel.msg#100\r\n\
text/french/game/misc.msg:200 wrong word\r\n\
Vous n'etes pas le bienvenu\r\n\
Hello there\r\n\
dialog/abel.msg 999 no such line\r\n\
A text that exists nowhere\r\n\
\r\n";
        let remarks = parse(report);
        assert_eq!(remarks.len(), 7);
        assert_eq!(remarks[0].rel_path, "dialog/abel.msg");
        assert_eq!(remarks[0].id, 101);
        assert_eq!(remarks[0].note, "the sentence is too rude");
        assert_eq!(remarks[1].rel_path, "abel.msg");
        assert_eq!(remarks[2].rel_path, "game/misc.msg"); // text/<language>/ prefix dropped
        assert!(remarks[3].rel_path.is_empty()); // free text

        let outcome = resolve(&p, &remarks, 1, None);
        let found: Vec<String> = outcome.found.iter().map(|r| format!("{}:{}", r.rel_path, r.id)).collect();
        // 101 and 100 by id, misc 200 by id, the rude sentence found by its text (translation)
        assert!(found.contains(&"dialog/abel.msg:101".to_string()));
        assert!(found.contains(&"dialog/abel.msg:100".to_string()));
        assert!(found.contains(&"game/misc.msg:200".to_string()));
        assert_eq!(outcome.not_found.len(), 2); // id 999, the unknown text
        assert_eq!(outcome.ambiguous.len(), 1); // "Hello there" is the source of two lines (> 1)

        // the marks only touch the saved state: no .msg file is written
        let marks: Vec<ReviewMark> =
            outcome.found.iter().map(|r| ReviewMark { rel_path: r.rel_path.clone(), id: r.id, comment: r.comment.clone() }).collect();
        assert!(p.mark_for_review(&marks) > 0);
        assert_eq!(p.mark_for_review(&marks), 0); // idempotent
        assert!(!root.join("out/text/french/dialog/abel.msg").exists());
        let doc = p.load_document("dialog/abel.msg").unwrap();
        assert!(doc.rows[1].review);
        assert!(doc.rows[1].comment.contains("too rude"));
        assert!(doc.rows[1].comment.contains("Game report"));
        let again = make();
        assert!(again.load_document("dialog/abel.msg").unwrap().rows[1].review); // persisted
    }
}
