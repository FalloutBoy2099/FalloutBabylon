//! Finds known terms (single or multi word) in a text, tolerating plural forms, possessives, case differences
//! and, for long words, a single typo.

use crate::stemmer::{self, Token};
use std::collections::HashMap;

const MIN_FUZZY_LENGTH: usize = 6;

const STOP_WORDS: [&str; 25] = [
    "the", "and", "you", "for", "yes", "not", "are", "his", "her", "with", "that", "this", "from", "have", "will",
    "what", "who", "how", "why", "but", "all", "any", "can", "out", "one",
];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Match {
    /// Byte offset in the text.
    pub start: usize,
    /// Length in bytes.
    pub length: usize,
    pub term_id: usize,
    /// Matched through a one-character difference.
    pub fuzzy: bool,
    /// First letter capitalisation equals the term's one.
    pub case_match: bool,
}

struct Compiled {
    tokens: Vec<String>,
    term_id: usize,
    upper_first: bool,
}

pub struct TermMatcher {
    language: String,
    terms: Vec<Compiled>,
    by_first: HashMap<String, Vec<usize>>,
    fuzzy_keys: HashMap<String, Vec<String>>,
    built: bool,
}

fn gap_is_separator(text: &str, from: usize, to: usize) -> bool {
    text[from..to].chars().all(|c| c.is_whitespace() || c == '-' || c == '\u{2013}' || c == '\u{2014}')
}

fn deletions(word: &str) -> Vec<String> {
    let chars: Vec<char> = word.chars().collect();
    (0..chars.len())
        .map(|i| chars[..i].iter().chain(chars[i + 1..].iter()).collect())
        .collect()
}

fn token_equals(a: &str, b: &str, fuzzy: &mut bool) -> bool {
    if a == b {
        return true;
    }
    if a.chars().count() >= MIN_FUZZY_LENGTH && b.chars().count() >= MIN_FUZZY_LENGTH && stemmer::within_one_edit(a, b) {
        *fuzzy = true;
        return true;
    }
    false
}

impl TermMatcher {
    pub fn new(language: &str) -> Self {
        TermMatcher {
            language: language.to_string(),
            terms: Vec::new(),
            by_first: HashMap::new(),
            fuzzy_keys: HashMap::new(),
            built: false,
        }
    }

    pub fn add(&mut self, term: &str, term_id: usize) {
        let tokens: Vec<Token> = stemmer::tokenize(term, &self.language);
        if tokens.is_empty() {
            return;
        }
        if tokens.len() == 1 {
            let t = &tokens[0].normalized;
            if t.chars().count() < 3 || STOP_WORDS.contains(&t.as_str()) {
                return;
            }
        }
        let upper_first = term[tokens[0].start..].chars().next().is_some_and(|c| c.is_uppercase());
        self.terms.push(Compiled {
            tokens: tokens.into_iter().map(|t| t.normalized).collect(),
            term_id,
            upper_first,
        });
        self.built = false;
    }

    pub fn build(&mut self) {
        self.by_first.clear();
        self.fuzzy_keys.clear();
        for (i, t) in self.terms.iter().enumerate() {
            self.by_first.entry(t.tokens[0].clone()).or_default().push(i);
        }
        let terms = &self.terms;
        for (key, list) in self.by_first.iter_mut() {
            // longest term first (stable)
            list.sort_by(|&a, &b| terms[b].tokens.len().cmp(&terms[a].tokens.len()));
            if key.chars().count() >= MIN_FUZZY_LENGTH {
                self.fuzzy_keys.entry(key.clone()).or_default().push(key.clone());
                for d in deletions(key) {
                    self.fuzzy_keys.entry(d).or_default().push(key.clone());
                }
            }
        }
        self.built = true;
    }

    pub fn find(&self, text: &str) -> Vec<Match> {
        let mut matches = Vec::new();
        if !self.built || self.terms.is_empty() {
            return matches;
        }
        let tokens = stemmer::tokenize(text, &self.language);

        let try_term = |token_index: usize, term: &Compiled| -> Option<Match> {
            let count = term.tokens.len();
            if token_index + count > tokens.len() {
                return None;
            }
            let mut fuzzy = false;
            for k in 0..count {
                let tok = &tokens[token_index + k];
                if !token_equals(&tok.normalized, &term.tokens[k], &mut fuzzy) {
                    return None;
                }
                if k > 0 {
                    let prev = &tokens[token_index + k - 1];
                    if !gap_is_separator(text, prev.start + prev.length, tok.start) {
                        return None;
                    }
                }
            }
            let first = &tokens[token_index];
            let last = &tokens[token_index + count - 1];
            let first_upper = text[first.start..].chars().next().is_some_and(|c| c.is_uppercase());
            Some(Match {
                start: first.start,
                length: last.start + last.length - first.start,
                term_id: term.term_id,
                fuzzy,
                case_match: first_upper == term.upper_first,
            })
        };

        let mut i = 0;
        while i < tokens.len() {
            let norm = &tokens[i].normalized;
            let mut found: Option<(Match, usize)> = None;

            // Exact first-token lookup (longest term first), then fuzzy lookup.
            if let Some(list) = self.by_first.get(norm) {
                for &idx in list {
                    if let Some(m) = try_term(i, &self.terms[idx]) {
                        found = Some((m, self.terms[idx].tokens.len()));
                        break;
                    }
                }
            }
            if found.is_none() && norm.chars().count() >= MIN_FUZZY_LENGTH {
                let mut keys: Vec<&String> = Vec::new();
                if let Some(direct) = self.fuzzy_keys.get(norm) {
                    keys.extend(direct.iter());
                }
                for d in deletions(norm) {
                    if let Some(list) = self.fuzzy_keys.get(&d) {
                        keys.extend(list.iter());
                    }
                }
                let mut seen: Vec<&String> = Vec::new();
                keys.retain(|k| {
                    if seen.contains(k) {
                        false
                    } else {
                        seen.push(k);
                        true
                    }
                });
                'keys: for key in keys {
                    if key == norm || !stemmer::within_one_edit(key, norm) {
                        continue;
                    }
                    if let Some(list) = self.by_first.get(key) {
                        for &idx in list {
                            if let Some(mut m) = try_term(i, &self.terms[idx]) {
                                m.fuzzy = true;
                                found = Some((m, self.terms[idx].tokens.len()));
                                break 'keys;
                            }
                        }
                    }
                }
            }
            match found {
                Some((m, consumed)) => {
                    matches.push(m);
                    i += consumed;
                }
                None => i += 1,
            }
        }
        matches
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stemmer_and_matcher() {
        let mut m = TermMatcher::new("english");
        m.add("Stimpak", 0);
        m.add("Leather Armor", 1);
        m.add("Vault City", 2);
        m.add("Vault", 3);
        m.add("the", 4); // stop word, ignored
        m.build();

        let ids = |t: &str| m.find(t).iter().map(|x| x.term_id).collect::<Vec<_>>();
        assert_eq!(ids("I need two Stimpaks and a stimpak."), vec![0, 0]);
        assert_eq!(ids("He wears the Leather Armor's collar"), vec![1]);
        assert_eq!(ids("leather-armor"), vec![1]);
        assert_eq!(ids("Welcome to Vault City, citizen of the Vault"), vec![2, 3]);
        assert_eq!(ids("a Stimpack here"), vec![0]); // one-letter typo
        assert!(m.find("a Stimpack here")[0].fuzzy);
        assert!(ids("leather and armor").is_empty());

        let text = "Take the Stimpaks!";
        let found = &m.find(text)[0];
        assert_eq!(&text[found.start..found.start + found.length], "Stimpaks");
    }

    #[test]
    fn case_match_flag() {
        let mut m = TermMatcher::new("english");
        m.add("Geckos", 0);
        m.build();
        assert!(m.find("the Geckos run")[0].case_match);
        assert!(!m.find("the geckos run")[0].case_match);
    }
}
