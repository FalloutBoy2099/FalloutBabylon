//! Terms added by hand to the glossary of a project (names, jargon, "do not translate" words...), and their
//! exchange as CSV.

use std::collections::HashMap;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CustomTerm {
    pub source: String,
    /// Equals `source` when `keep` is set.
    pub target: String,
    /// Do not translate: the term must appear unchanged in the translation.
    pub keep: bool,
}

pub fn key_of(source: &str) -> String {
    source.split_whitespace().collect::<Vec<_>>().join(" ").to_lowercase()
}

fn quote(s: &str) -> String {
    if !s.contains('"') && !s.contains(';') && !s.contains('\n') {
        return s.to_string();
    }
    format!("\"{}\"", s.replace('"', "\"\""))
}

/// Splits CSV text into rows of cells (quotes may hold separators and line breaks).
fn parse(text: &str, sep: char) -> Vec<Vec<String>> {
    let mut rows: Vec<Vec<String>> = Vec::new();
    let mut row: Vec<String> = Vec::new();
    let mut cell = String::new();
    let mut quoted = false;
    let mut any = false;

    let chars: Vec<char> = text.chars().collect();
    let mut i = 0;
    macro_rules! end_cell {
        () => {{
            row.push(cell.trim().to_string());
            cell.clear();
        }};
    }
    macro_rules! end_row {
        () => {{
            end_cell!();
            if any || row.len() > 1 || !row[0].is_empty() {
                rows.push(std::mem::take(&mut row));
            } else {
                row.clear();
            }
            #[allow(unused_assignments)]
            {
                any = false;
            }
        }};
    }
    while i < chars.len() {
        let ch = chars[i];
        if quoted {
            if ch == '"' {
                if i + 1 < chars.len() && chars[i + 1] == '"' {
                    cell.push('"');
                    i += 1;
                } else {
                    quoted = false;
                }
            } else {
                cell.push(ch);
            }
        } else if ch == '"' && cell.trim().is_empty() {
            quoted = true;
            any = true;
        } else if ch == sep {
            end_cell!();
            any = true;
        } else if ch == '\n' {
            end_row!();
        } else if ch != '\r' {
            cell.push(ch);
        }
        i += 1;
    }
    if !cell.is_empty() || !row.is_empty() {
        end_row!();
    }
    rows
}

fn is_true(s: &str) -> bool {
    matches!(
        s.trim().to_lowercase().as_str(),
        "1" | "x" | "yes" | "oui" | "true" | "keep" | "si" | "ja"
    )
}

/// "term;translation;keep" lines (UTF-8 with a header), as exchanged with spreadsheets.
pub fn to_csv(terms: &[CustomTerm]) -> String {
    let mut out = String::from("term;translation;keep\r\n");
    for t in terms {
        out.push_str(&quote(&t.source));
        out.push(';');
        out.push_str(&quote(if t.keep { "" } else { &t.target }));
        out.push(';');
        out.push_str(if t.keep { "1" } else { "0" });
        out.push_str("\r\n");
    }
    out
}

/// Accepts comma, semicolon or tab separated text, with or without a header; the third column
/// ("keep", "1", "yes", "x"...) marks a do-not-translate term. A line with only a term is a do-not-translate term.
/// Returns the terms and the number of lines ignored.
pub fn from_csv(text: &str) -> (Vec<CustomTerm>, usize) {
    // The separator is the most frequent of tab, semicolon and comma on the first lines.
    let head: String = text.chars().take(4000).collect();
    let (mut tabs, mut semis, mut commas) = (0, 0, 0);
    for line in head.split('\n').take(5) {
        tabs += line.matches('\t').count();
        semis += line.matches(';').count();
        commas += line.matches(',').count();
    }
    let sep = if tabs >= semis && tabs >= commas && tabs > 0 {
        '\t'
    } else if commas > semis {
        ','
    } else {
        ';'
    };

    let mut terms = Vec::new();
    let mut skipped = 0;
    for (i, r) in parse(text, sep).iter().enumerate() {
        let cell = |n: usize| r.get(n).map(String::as_str).unwrap_or("");
        let source = cell(0).split_whitespace().collect::<Vec<_>>().join(" ");
        if i == 0 {
            let h = source.to_lowercase();
            if matches!(h.as_str(), "term" | "source" | "terme" | "original") {
                continue;
            }
        }
        if source.is_empty() {
            skipped += 1;
            continue;
        }
        let target = cell(1).split_whitespace().collect::<Vec<_>>().join(" ");
        let keep = target.is_empty() || is_true(cell(2)) || target == source;
        terms.push(CustomTerm { target: if keep { source.clone() } else { target }, source, keep });
    }
    (terms, skipped)
}

/// Imported terms replace those with the same term (compared ignoring case and extra spaces).
pub fn merge(existing: &[CustomTerm], imported: &[CustomTerm]) -> Vec<CustomTerm> {
    let mut out = existing.to_vec();
    let mut index: HashMap<String, usize> = out.iter().enumerate().map(|(i, t)| (key_of(&t.source), i)).collect();
    for t in imported {
        let key = key_of(&t.source);
        match index.get(&key) {
            Some(&at) => out[at] = t.clone(),
            None => {
                index.insert(key, out.len());
                out.push(t.clone());
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn term(s: &str, t: &str, keep: bool) -> CustomTerm {
        CustomTerm { source: s.into(), target: t.into(), keep }
    }

    #[test]
    fn csv_round_trip() {
        let terms = vec![
            term("Vault Dweller", "Habitant de l'Abri", false),
            term("Brotherhood; of Steel", "Confrérie \"Acier\"", false),
            term("Pip-Boy", "Pip-Boy", true),
        ];
        let csv = to_csv(&terms);
        let (back, skipped) = from_csv(&csv);
        assert_eq!(back.len(), 3);
        assert_eq!(skipped, 0);
        assert_eq!(back[1].source, "Brotherhood; of Steel");
        assert_eq!(back[1].target, "Confrérie \"Acier\"");
        assert!(back[2].keep);
    }

    #[test]
    fn other_separators_and_headers() {
        let (tabs, _) = from_csv("Nuka-Cola\tNuka-Cola\nStimpak\tStimpak\n\nCaps\tcapsules\nRad-X");
        assert_eq!(tabs.len(), 4);
        assert!(tabs[0].keep);
        assert!(!tabs[2].keep);
        assert!(tabs[3].keep);
        let (commas, _) = from_csv("term,translation\nCaps,capsules\n");
        assert_eq!(commas.len(), 1);
    }

    #[test]
    fn merging_replaces_by_term() {
        let existing = vec![term("Vault Dweller", "Habitant de l'Abri", false), term("Pip-Boy", "Pip-Boy", true)];
        let merged = merge(&existing, &[term("vault  dweller", "Habitant du Vault", false), term("Ghoul", "Goule", false)]);
        assert_eq!(merged.len(), 3);
        assert_eq!(merged[0].target, "Habitant du Vault");
    }
}
