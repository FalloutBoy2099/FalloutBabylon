//! Game texts hold tokens that must reach the translation untouched (@Name@, %s, %d...).
//!
//! `protect` turns them into `<x>n</x>` markers (and XML-escapes the rest) for a translation service used in
//! XML mode with `<x>` ignored; `restore` puts the original tokens back.

use crate::glossary::Glossary;
use regex::Regex;
use std::collections::HashSet;
use std::sync::OnceLock;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Protected {
    pub xml: String,
    pub tokens: Vec<String>,
}

fn escape_xml(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
}

fn unescape_xml(s: &str) -> String {
    s.replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&apos;", "'")
        .replace("&amp;", "&")
}

struct Span {
    start: usize,
    end: usize,
    replacement: String,
}

fn at_sentence_start(text: &str, start: usize) -> bool {
    let mut prev = text[..start].chars().rev().skip_while(|c| c.is_whitespace() && *c != '\n');
    match prev.next() {
        None => true,
        Some(c) => matches!(c, '\n' | '.' | '!' | '?' | ':' | '"' | '(' | '\'' | '-' | '\u{2026}'),
    }
}

fn glossary_spans(text: &str, glossary: &Glossary) -> Vec<Span> {
    let mut spans = Vec::new();
    for h in glossary.find_in_source(text) {
        let term = &glossary.terms()[h.term];
        if h.m.fuzzy || !h.m.case_match || term.targets.len() != 1 || term.targets[0].is_empty() {
            continue;
        }
        let found = &text[h.m.start..h.m.start + h.m.length];
        if found.to_lowercase() != term.source.to_lowercase() {
            continue; // plural / possessive forms are left to the service
        }
        let multi_word = term.source.contains(' ') || term.source.contains('-');
        let capitalised = term.source.chars().next().is_some_and(|c| c.is_uppercase());
        if !term.custom && !multi_word && !(capitalised && !at_sentence_start(text, h.m.start)) {
            continue; // a common word: only a safe match is worth forcing
        }
        spans.push(Span { start: h.m.start, end: h.m.start + h.m.length, replacement: term.targets[0].clone() });
    }
    spans
}

fn token_regex() -> &'static Regex {
    // @Name@ style script variables, printf conversions, and {braced} fields.
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"@[^@\s][^@\n]{0,40}@|%[-+0# ]?\d*[sdiuxXc]|\{[^{}\n]{1,40}\}").unwrap())
}

/// Protects the tokens of the game and, when a glossary is given, the glossary terms found in `text` as if they
/// were tokens whose "original text" is the project's translation: the translation service leaves them alone and
/// `restore` writes the translation of the project. Only safe matches are used (exact form, not a common word at
/// the start of a sentence, a single translation). This stands in for a DeepL glossary when none can be used.
pub fn protect_with(text: &str, glossary: Option<&Glossary>) -> Protected {
    let mut spans: Vec<Span> = token_regex()
        .find_iter(text)
        .map(|m| Span { start: m.start(), end: m.end(), replacement: m.as_str().to_string() })
        .collect();
    if let Some(g) = glossary.filter(|g| !g.is_empty()) {
        let mut terms = glossary_spans(text, g);
        terms.sort_by(|a, b| (b.end - b.start).cmp(&(a.end - a.start))); // longest first (stable)
        for t in terms {
            if spans.iter().all(|s| t.end <= s.start || t.start >= s.end) {
                spans.push(t);
            }
        }
        spans.sort_by_key(|s| s.start);
    }

    let mut p = Protected::default();
    let mut last = 0;
    for s in spans {
        p.xml.push_str(&escape_xml(&text[last..s.start]));
        p.xml.push_str(&format!("<x>{}</x>", p.tokens.len()));
        p.tokens.push(s.replacement);
        last = s.end;
    }
    p.xml.push_str(&escape_xml(&text[last..]));
    p
}

pub fn protect(text: &str) -> Protected {
    protect_with(text, None)
}

/// Returns the restored text and whether every token came back (false when a token was lost or invented by the
/// translation: the text is still restored as far as possible).
pub fn restore(xml: &str, tokens: &[String]) -> (String, bool) {
    static MARKER: OnceLock<Regex> = OnceLock::new();
    static STRAY: OnceLock<Regex> = OnceLock::new();
    let marker = MARKER.get_or_init(|| Regex::new(r"<x>\s*(\d+)\s*</x>").unwrap());
    let stray = STRAY.get_or_init(|| Regex::new(r"</?x\s*/?>").unwrap());

    let mut out = String::new();
    let mut seen: HashSet<usize> = HashSet::new();
    let mut ok = true;
    let mut last = 0;
    for m in marker.captures_iter(xml) {
        let whole = m.get(0).unwrap();
        out.push_str(&unescape_xml(&xml[last..whole.start()]));
        match m[1].parse::<usize>() {
            Ok(n) if n < tokens.len() => {
                out.push_str(&tokens[n]);
                seen.insert(n);
            }
            _ => ok = false,
        }
        last = whole.end();
    }
    out.push_str(&unescape_xml(&xml[last..]));
    if seen.len() != tokens.len() {
        ok = false;
    }
    // Any other tag left by the service is not part of the text.
    if stray.is_match(&out) {
        out = stray.replace_all(&out, "").into_owned();
        ok = false;
    }
    (out, ok)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tokens_are_protected_and_restored() {
        let p = protect("Hello @Name@, you have %d caps & {gold}!");
        assert_eq!(p.tokens, vec!["@Name@", "%d", "{gold}"]);
        assert_eq!(p.xml, "Hello <x>0</x>, you have <x>1</x> caps &amp; <x>2</x>!");
        let (back, ok) = restore(&p.xml, &p.tokens);
        assert!(ok);
        assert_eq!(back, "Hello @Name@, you have %d caps & {gold}!");
        // a lost token is reported
        let (_, ok) = restore("Bonjour <x>0</x>", &p.tokens);
        assert!(!ok);
        // a stray tag is dropped
        let (text, ok) = restore("a <x/> b", &[]);
        assert_eq!(text, "a  b");
        assert!(!ok);
    }

    #[test]
    fn local_glossary_substitution() {
        let mut g = Glossary::new();
        g.add("Vault Dweller", "Habitant de l'Abri", "Misc", "a#1");
        g.add("Stimpak", "Stimpak", "Item", "b#1");
        g.add("Water", "Eau", "Item", "b#2");
        g.add("Chip", "Puce", "Item", "b#3");
        g.add("Chip", "Jeton", "Item", "b#4"); // two translations: never forced
        g.add_custom("caps", "capsules", false, "Custom");
        g.finalize("english", "french");

        // multi-word names, capitalised names (not at the start of a sentence) and custom terms are protected
        let p = protect_with("The Vault Dweller found a Stimpak and 5 caps for @Name@. Water is rare.", Some(&g));
        assert!(p.tokens.contains(&"Habitant de l'Abri".to_string()));
        assert!(p.tokens.contains(&"Stimpak".to_string()));
        assert!(p.tokens.contains(&"capsules".to_string()));
        assert!(p.tokens.contains(&"@Name@".to_string()));
        assert!(!p.tokens.contains(&"Eau".to_string())); // common word at the start of a sentence
        assert!(!p.tokens.contains(&"Puce".to_string()) && !p.tokens.contains(&"Jeton".to_string()));
        assert!(p.xml.contains("Water is rare"));

        // what the service sends back is restored with the translations of the project
        let (restored, ok) = restore(&p.xml, &p.tokens);
        assert!(ok);
        assert!(restored.contains("Habitant de l'Abri"));
        assert!(restored.contains("5 capsules"));
        assert!(restored.contains("@Name@"));

        // without a glossary only the real tokens are protected
        assert_eq!(protect("The Vault Dweller has %d caps").tokens, vec!["%d"]);
        // exact form only: a plural is left to the service
        assert!(protect_with("Two Stimpaks here", Some(&g)).tokens.is_empty());
    }
}
