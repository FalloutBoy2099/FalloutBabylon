//! Coloured / underlined spans of the line editors, rendered through a `StyledText` laid over the text input.

use babylon_core::glossary::Glossary;
use babylon_core::grammar::{GrammarIssue, IssueKind};
use slint::StyledText;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Span {
    /// Byte offsets, on character boundaries.
    pub start: usize,
    pub end: usize,
    pub color: &'static str,
}

/// Colours of the indicators (the ones of the Qt version).
pub struct Palette {
    pub term: &'static str,
    pub fuzzy: &'static str,
    pub issue: &'static str,
    pub spelling: &'static str,
    pub grammar: &'static str,
}

pub const LIGHT: Palette =
    Palette { term: "#0b57d0", fuzzy: "#3d8be8", issue: "#d97706", spelling: "#d93025", grammar: "#7b3fb8" };
pub const DARK: Palette =
    Palette { term: "#5cb0ff", fuzzy: "#8cc6ff", issue: "#ffa83d", spelling: "#ff6b6b", grammar: "#c28cff" };

pub fn palette(dark: bool) -> &'static Palette {
    if dark {
        &DARK
    } else {
        &LIGHT
    }
}

fn escape(out: &mut String, text: &str) {
    for c in text.chars() {
        match c {
            // a backslash before the line end is a hard break (the HTML <br> is not supported)
            '\n' => out.push_str("\\\n"),
            '\r' => {}
            c if c.is_ascii_punctuation() => {
                out.push('\\');
                out.push(c);
            }
            c => out.push(c),
        }
    }
}

/// What the tooltip of an editor says: the glossary terms of the text with their translations, and the problems
/// the checker found. `None` when there is nothing to say.
pub fn tooltip(glossary: &Glossary, text: &str, in_source: bool, issues: &[GrammarIssue]) -> Option<StyledText> {
    let mut lines: Vec<String> = Vec::new();
    for issue in issues {
        let mut line = format!("\u{26a0} {}", markup(&issue.message, &[]));
        if !issue.suggestions.is_empty() {
            let shown: Vec<String> = issue.suggestions.iter().take(4).map(|s| markup(s, &[])).collect();
            line.push_str(&format!(" \u{2192} {}", shown.join(" / ")));
        }
        lines.push(line);
    }
    let hits = if in_source { glossary.find_in_source(text) } else { glossary.find_in_target(text) };
    let mut seen = std::collections::HashSet::new();
    for hit in hits {
        if !seen.insert(hit.term) {
            continue;
        }
        let term = &glossary.terms()[hit.term];
        let targets = if term.targets.is_empty() {
            "\u{2026}".to_string()
        } else {
            term.targets.iter().map(|t| markup(t, &[])).collect::<Vec<_>>().join(" / ")
        };
        lines.push(format!("**{}** ({}) \u{2192} {}", markup(&term.source, &[]), markup(&term.category, &[]), targets));
    }
    if lines.is_empty() {
        return None;
    }
    StyledText::from_markdown(&lines.join("\n\n")).ok()
}

/// Markup of `text` with the spans coloured and underlined. Overlapping spans: the first one wins.
pub fn markup(text: &str, spans: &[Span]) -> String {
    let mut ordered: Vec<Span> = spans
        .iter()
        .copied()
        .filter(|s| s.start < s.end && s.end <= text.len() && text.is_char_boundary(s.start) && text.is_char_boundary(s.end))
        .collect();
    ordered.sort_by_key(|s| (s.start, std::cmp::Reverse(s.end)));
    let mut out = String::with_capacity(text.len() + spans.len() * 40);
    let mut pos = 0;
    for s in ordered {
        if s.start < pos {
            continue;
        }
        escape(&mut out, &text[pos..s.start]);
        out.push_str("<font color=\"");
        out.push_str(s.color);
        out.push_str("\"><u>");
        escape(&mut out, &text[s.start..s.end]);
        out.push_str("</u></font>");
        pos = s.end;
    }
    escape(&mut out, &text[pos..]);
    out
}

pub fn styled(text: &str, spans: &[Span]) -> StyledText {
    StyledText::from_markdown(&markup(text, spans)).unwrap_or_default()
}

/// Glossary terms of the source text, and the ones whose translation is missing from the target.
pub fn source_spans(glossary: &Glossary, source: &str, target: &str, p: &Palette) -> Vec<Span> {
    let mut spans = Vec::new();
    for issue in glossary.check_consistency(source, target) {
        spans.push(Span { start: issue.source_match.start, end: issue.source_match.start + issue.source_match.length, color: p.issue });
    }
    for hit in glossary.find_in_source(source) {
        let color = if hit.m.fuzzy { p.fuzzy } else { p.term };
        spans.push(Span { start: hit.m.start, end: hit.m.start + hit.m.length, color });
    }
    spans
}

/// Glossary terms of the translation, and what the grammar checker found.
pub fn target_spans(glossary: &Glossary, target: &str, issues: &[GrammarIssue], p: &Palette) -> Vec<Span> {
    let mut spans = Vec::new();
    for issue in issues {
        let color = if issue.kind == IssueKind::Spelling { p.spelling } else { p.grammar };
        spans.push(Span { start: issue.start, end: issue.start + issue.length, color });
    }
    for hit in glossary.find_in_target(target) {
        let color = if hit.m.fuzzy { p.fuzzy } else { p.term };
        spans.push(Span { start: hit.m.start, end: hit.m.start + hit.m.length, color });
    }
    spans
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn markup_escapes_and_colours() {
        let text = "a*b Stimpak.\nnext";
        let spans = [Span { start: 4, end: 11, color: "#111111" }];
        let m = markup(text, &spans);
        assert_eq!(m, "a\\*b <font color=\"#111111\"><u>Stimpak</u></font>\\.\\\nnext");
    }

    #[test]
    fn overlapping_spans_keep_the_first() {
        let spans = [Span { start: 0, end: 5, color: "#1" }, Span { start: 3, end: 8, color: "#2" }];
        let m = markup("abcdefghij", &spans);
        assert!(m.contains("#1") && !m.contains("#2"));
    }
}

#[cfg(test)]
mod render_tests {
    use super::*;

    #[test]
    fn multiline_markup_parses() {
        let spans = [Span { start: 2, end: 5, color: "#123456" }];
        let st = StyledText::from_markdown(&markup("a \nbcd\n\nef", &spans));
        println!("{:?}", st);
        assert!(st.is_ok());
    }
}
