//! Terms extracted from the /game message files (and entered by hand), plus the matchers that spot them in
//! dialogue text.

use crate::stemmer;
use crate::term_matcher::{Match, TermMatcher};
use sha1::{Digest, Sha1};
use std::collections::{HashMap, HashSet};

/// One glossary term: a source expression with its translation(s) found in the game files.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct GlossaryTerm {
    pub source: String,
    /// Distinct translations, most frequent first (empty = not translated yet).
    pub targets: Vec<String>,
    /// "Item", "Skill", "Location"...
    pub category: String,
    /// "pro_item.msg#100"
    pub origins: Vec<String>,
    /// Entered by hand (it overrides what the game files say).
    pub custom: bool,
    /// Custom term that must stay unchanged in the translation.
    pub keep: bool,
}

impl GlossaryTerm {
    pub fn conflict(&self) -> bool {
        self.targets.len() > 1
    }
}

#[derive(Debug, Clone)]
pub struct Hit {
    pub m: Match,
    /// Index in `Glossary::terms()`.
    pub term: usize,
}

#[derive(Debug, Clone)]
pub struct Issue {
    pub term: usize,
    pub source_match: Match,
    pub message: String,
}

struct Raw {
    source: String,
    target: String,
    category: String,
    origin: String,
    custom: bool,
    keep: bool,
}

#[derive(Default)]
pub struct Glossary {
    raw: Vec<Raw>,
    terms: Vec<GlossaryTerm>,
    signature: String,
    source_matcher: Option<TermMatcher>,
    target_matcher: Option<TermMatcher>,
}

fn simplified(s: &str) -> String {
    s.split_whitespace().collect::<Vec<_>>().join(" ")
}

impl Glossary {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn clear(&mut self) {
        *self = Glossary::default();
    }

    pub fn add(&mut self, source: &str, target: &str, category: &str, origin: &str) {
        let s = simplified(source);
        if s.is_empty() {
            return;
        }
        self.raw.push(Raw {
            source: s,
            target: simplified(target),
            category: category.to_string(),
            origin: origin.to_string(),
            custom: false,
            keep: false,
        });
    }

    /// A term entered by hand: its translation wins over the ones found in the game files.
    pub fn add_custom(&mut self, source: &str, target: &str, keep: bool, category: &str) {
        let s = simplified(source);
        if s.is_empty() {
            return;
        }
        let target = if keep { s.clone() } else { simplified(target) };
        self.raw.push(Raw {
            source: s,
            target,
            category: category.to_string(),
            origin: "(manual)".to_string(),
            custom: true,
            keep,
        });
    }

    /// Groups duplicates and builds the matchers.
    pub fn finalize(&mut self, source_language: &str, target_language: &str) {
        struct Group {
            term: GlossaryTerm,
            counts: HashMap<String, i32>,          // lower-cased target -> occurrences
            spelling: HashMap<String, String>,     // lower-cased target -> first spelling seen
            custom_counts: HashMap<String, i32>,   // same, for the terms entered by hand
        }
        let mut groups: Vec<Group> = Vec::new();
        let mut by_key: HashMap<String, usize> = HashMap::new();

        for r in &self.raw {
            let key: Vec<String> = stemmer::tokenize(&r.source, source_language).into_iter().map(|t| t.normalized).collect();
            if key.is_empty() {
                continue;
            }
            let k = key.join(" ");
            let idx = *by_key.entry(k).or_insert_with(|| {
                groups.push(Group {
                    term: GlossaryTerm { source: r.source.clone(), category: r.category.clone(), ..Default::default() },
                    counts: HashMap::new(),
                    spelling: HashMap::new(),
                    custom_counts: HashMap::new(),
                });
                groups.len() - 1
            });
            let g = &mut groups[idx];
            g.term.origins.push(r.origin.clone());
            if r.custom {
                if !g.term.custom {
                    g.term.custom = true;
                    g.term.source = r.source.clone();
                    g.term.category = r.category.clone();
                }
                g.term.keep = r.keep;
            }
            if !r.target.is_empty() {
                let lower = r.target.to_lowercase();
                *(if r.custom { &mut g.custom_counts } else { &mut g.counts }).entry(lower.clone()).or_insert(0) += 1;
                if !g.spelling.contains_key(&lower) || r.custom {
                    g.spelling.insert(lower, r.target.clone());
                }
            }
        }

        self.terms.clear();
        for mut g in groups {
            // What the user typed is the reference: the translations of the game files are ignored for that term.
            if g.term.custom {
                g.counts = g.custom_counts.clone();
            }
            let mut keys: Vec<&String> = g.counts.keys().collect();
            keys.sort_by(|a, b| {
                let (ca, cb) = (g.counts[*a], g.counts[*b]);
                cb.cmp(&ca).then_with(|| a.cmp(b))
            });
            for k in keys {
                g.term.targets.push(g.spelling[k].clone());
            }
            self.terms.push(g.term);
        }
        self.terms.sort_by_key(|t| t.source.to_lowercase());

        let mut hash = Sha1::new();
        hash.update(source_language.as_bytes());
        hash.update(target_language.as_bytes());
        for t in &self.terms {
            hash.update(t.source.as_bytes());
            hash.update(t.targets.join("\t").as_bytes());
            hash.update(if t.custom { b"c" } else { b"g" });
        }
        self.signature = hash.finalize().iter().map(|b| format!("{b:02x}")).collect();

        let mut source_matcher = TermMatcher::new(source_language);
        let mut target_matcher = TermMatcher::new(target_language);
        for (i, t) in self.terms.iter().enumerate() {
            source_matcher.add(&t.source, i);
            for tr in &t.targets {
                target_matcher.add(tr, i);
            }
        }
        source_matcher.build();
        target_matcher.build();
        self.source_matcher = Some(source_matcher);
        self.target_matcher = Some(target_matcher);
        self.raw.clear();
        self.raw.shrink_to_fit();
    }

    pub fn terms(&self) -> &[GlossaryTerm] {
        &self.terms
    }

    pub fn is_empty(&self) -> bool {
        self.terms.is_empty()
    }

    /// Stable identity of the content (terms and translations): caches of results that depend on the glossary use it.
    pub fn signature(&self) -> &str {
        &self.signature
    }

    pub fn conflict_count(&self) -> usize {
        self.terms.iter().filter(|t| t.conflict()).count()
    }

    pub fn find_in_source(&self, text: &str) -> Vec<Hit> {
        match &self.source_matcher {
            Some(m) => m.find(text).into_iter().map(|x| Hit { term: x.term_id, m: x }).collect(),
            None => Vec::new(),
        }
    }

    pub fn find_in_target(&self, text: &str) -> Vec<Hit> {
        match &self.target_matcher {
            Some(m) => m.find(text).into_iter().map(|x| Hit { term: x.term_id, m: x }).collect(),
            None => Vec::new(),
        }
    }

    /// Terms present in `source` whose known translation is absent from `target`.
    pub fn check_consistency(&self, source: &str, target: &str) -> Vec<Issue> {
        let mut issues = Vec::new();
        if target.trim().is_empty() || self.source_matcher.is_none() {
            return issues;
        }
        let present: HashSet<usize> = self.find_in_target(target).into_iter().map(|h| h.term).collect();
        let mut reported: HashSet<usize> = HashSet::new();
        for h in self.find_in_source(source) {
            let term = &self.terms[h.term];
            // Weak matches (a common word written with a different capitalisation) are not worth an alert.
            if !h.m.case_match || term.targets.is_empty() {
                continue;
            }
            if present.contains(&h.term) || reported.contains(&h.term) {
                continue;
            }
            reported.insert(h.term);
            issues.push(Issue {
                term: h.term,
                source_match: h.m,
                message: format!("{} → {}", term.source, term.targets.join(" / ")),
            });
        }
        issues
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Glossary {
        let mut g = Glossary::new();
        g.add("Stimpak", "Stimpak", "Item", "pro_item.msg#100");
        g.add("Leather Armor", "Armure en cuir", "Item", "pro_item.msg#200");
        g.add("Geckos", "Geckos", "Creature", "pro_crit.msg#100");
        g.add("Pip-Boy", "Pip-Boy", "Item", "pro_item.msg#300");
        g.add("Pip-Boy", "Pipboy", "Item", "misc#1");
        g.finalize("english", "french");
        g
    }

    #[test]
    fn glossary_consistency() {
        let g = sample();
        assert_eq!(g.terms().len(), 4);
        assert_eq!(g.conflict_count(), 1);

        assert!(g.check_consistency("Wear the Leather Armor.", "Mets l'armure en cuir.").is_empty());
        let bad = g.check_consistency("Wear the Leather Armor.", "Mets la cuirasse.");
        assert_eq!(bad.len(), 1);
        assert_eq!(g.terms()[bad[0].term].source, "Leather Armor");
        // untranslated target is not flagged
        assert!(g.check_consistency("Wear the Leather Armor.", "").is_empty());
        // either variant of a conflicting term is accepted
        assert!(g.check_consistency("My Pip-Boy", "Mon Pipboy").is_empty());
        assert!(g.check_consistency("My Pip-Boy", "Mon Pip-Boy").is_empty());
        // case-only match is a weak hint, never an alert
        assert!(g.check_consistency("the geckos run", "les lézards courent").is_empty());
        assert_eq!(g.find_in_target("Mon Pipboy").len(), 1);
    }

    #[test]
    fn custom_terms_override_the_game_files() {
        let mut g = Glossary::new();
        g.add("Stimpak", "Stimpak", "Item", "pro_item.msg#100");
        g.add("Super Stimpak", "Super Stimpak", "Item", "pro_item.msg#101");
        g.add_custom("Stimpak", "Seringue", false, "Custom");
        g.add_custom("Radscorpion", "", true, "Custom");
        g.finalize("english", "french");
        let stimpak = g.terms().iter().find(|t| t.source == "Stimpak").unwrap();
        assert!(stimpak.custom);
        assert_eq!(stimpak.targets, vec!["Seringue".to_string()]);
        assert!(!stimpak.conflict());
        let scorpion = g.terms().iter().find(|t| t.source == "Radscorpion").unwrap();
        assert!(scorpion.keep);
        assert_eq!(scorpion.targets, vec!["Radscorpion".to_string()]);
        assert_eq!(g.check_consistency("Use a Stimpak", "Utilisez un Stimpak").len(), 1);
        assert_eq!(g.check_consistency("Use a Stimpak", "Utilisez une seringue").len(), 0);
        assert_eq!(g.check_consistency("A Radscorpion", "Un scorpion").len(), 1);
    }

    #[test]
    fn signature_follows_the_content() {
        let a = sample();
        let b = sample();
        assert_eq!(a.signature(), b.signature());
        let mut c = Glossary::new();
        c.add("Stimpak", "Seringue", "Item", "x");
        c.finalize("english", "french");
        assert_ne!(a.signature(), c.signature());
        assert_eq!(a.signature().len(), 40);
    }
}
