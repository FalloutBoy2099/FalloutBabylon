//! Tokenisation and light normalisation (case, possessives, plural forms) used to recognise glossary terms in
//! text even when they do not match exactly.
//!
//! Offsets are byte offsets in the original text (what the UI toolkit uses for cursor positions).

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Token {
    /// Byte offset in the original text.
    pub start: usize,
    /// Length in bytes.
    pub length: usize,
    pub normalized: String,
}

pub fn is_cjk(c: char) -> bool {
    let u = c as u32;
    (0x3040..=0x30FF).contains(&u)
        || (0x3400..=0x4DBF).contains(&u)
        || (0x4E00..=0x9FFF).contains(&u)
        || (0xF900..=0xFAFF).contains(&u)
        || (0xAC00..=0xD7AF).contains(&u)
}

fn is_apostrophe(c: char) -> bool {
    c == '\'' || c == '\u{2019}'
}

fn ends_with_any(w: &[char], suffixes: &[&str]) -> bool {
    suffixes.iter().any(|s| ends_with(w, s))
}

fn ends_with(w: &[char], s: &str) -> bool {
    let sc: Vec<char> = s.chars().collect();
    w.len() >= sc.len() && w[w.len() - sc.len()..] == sc[..]
}

fn left(w: &[char], n: usize) -> String {
    w[..n].iter().collect()
}

/// Lower-cases, drops possessive 's and strips plural endings according to the language
/// (game language directory name: "english", "french"...).
pub fn normalize(word: &str, language: &str) -> String {
    let mut w: Vec<char> = word.to_lowercase().chars().collect();
    // possessive: "gizmo's", "raiders'"
    if ends_with(&w, "'s") || ends_with(&w, "\u{2019}s") {
        w.truncate(w.len() - 2);
    } else if w.last().is_some_and(|&c| is_apostrophe(c)) {
        w.pop();
    }

    let n = w.len();
    if n <= 3 {
        return w.iter().collect();
    }

    match language {
        "english" => {
            if ends_with(&w, "ies") && n > 4 {
                return left(&w, n - 3) + "y";
            }
            if ends_with_any(&w, &["sses", "xes", "zes", "ches", "shes"]) {
                return left(&w, n - 2);
            }
            if ends_with(&w, "s") && !ends_with_any(&w, &["ss", "us", "is"]) {
                return left(&w, n - 1);
            }
        }
        "french" => {
            if ends_with(&w, "aux") && n > 4 {
                return left(&w, n - 3) + "al";
            }
            if ends_with(&w, "s") || ends_with(&w, "x") {
                return left(&w, n - 1);
            }
        }
        "spanish" | "portuguese" => {
            if ends_with(&w, "es") && n > 4 {
                return left(&w, n - 2);
            }
            if ends_with(&w, "s") {
                return left(&w, n - 1);
            }
        }
        "italian" => {
            if ends_with(&w, "i") || ends_with(&w, "e") {
                return left(&w, n - 1);
            }
        }
        "german" => {
            if n > 5 && ends_with_any(&w, &["en", "er", "es"]) {
                return left(&w, n - 2);
            }
            if n > 4 && (ends_with(&w, "e") || ends_with(&w, "n") || ends_with(&w, "s")) {
                return left(&w, n - 1);
            }
        }
        "polish" | "czech" | "russian" => {
            // Inflected languages: drop one trailing vowel/consonant ending so declined forms still meet.
            if n > 5 {
                return left(&w, n - 1);
            }
        }
        _ => {
            if ends_with(&w, "s") && !ends_with(&w, "ss") {
                return left(&w, n - 1);
            }
        }
    }
    w.iter().collect()
}

pub fn tokenize(text: &str, language: &str) -> Vec<Token> {
    let chars: Vec<(usize, char)> = text.char_indices().collect();
    let n = chars.len();
    let byte_at = |i: usize| if i < n { chars[i].0 } else { text.len() };
    // French and Italian elide articles with an apostrophe (l'armure, dell'arma): it separates words there.
    let elision = language == "french" || language == "italian";
    let mut tokens = Vec::new();
    let mut i = 0;
    while i < n {
        let c = chars[i].1;
        if is_cjk(c) {
            tokens.push(Token { start: chars[i].0, length: c.len_utf8(), normalized: c.to_string() });
            i += 1;
            continue;
        }
        if !c.is_alphanumeric() {
            i += 1;
            continue;
        }
        let mut j = i + 1;
        while j < n {
            let d = chars[j].1;
            if is_cjk(d) {
                break;
            }
            if d.is_alphanumeric() {
                j += 1;
            } else if !elision && is_apostrophe(d) && j + 1 < n && chars[j + 1].1.is_alphabetic() {
                j += 1; // inner apostrophe: gizmo's
            } else if is_apostrophe(d) && j + 1 >= n {
                j += 1;
            } else {
                break;
            }
        }
        let start = chars[i].0;
        let end = byte_at(j);
        tokens.push(Token { start, length: end - start, normalized: normalize(&text[start..end], language) });
        i = j;
    }
    tokens
}

/// True when a and b differ by at most one insertion, deletion or substitution.
pub fn within_one_edit(a: &str, b: &str) -> bool {
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let (la, lb) = (a.len(), b.len());
    if la.abs_diff(lb) > 1 {
        return false;
    }
    let mut i = 0;
    while i < la && i < lb && a[i] == b[i] {
        i += 1;
    }
    if i == la || i == lb {
        return la.abs_diff(lb) <= 1;
    }
    if la == lb {
        return a[i + 1..] == b[i + 1..];
    }
    if la > lb {
        return a[i + 1..] == b[i..];
    }
    a[i..] == b[i + 1..]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plural_forms_by_language() {
        assert_eq!(normalize("Stimpaks", "english"), "stimpak");
        assert_eq!(normalize("Batteries", "english"), "battery");
        assert_eq!(normalize("Boxes", "english"), "box");
        assert_eq!(normalize("Gizmo's", "english"), "gizmo");
        assert_eq!(normalize("Raiders'", "english"), "raider");
        assert_eq!(normalize("chevaux", "french"), "cheval");
        assert_eq!(normalize("armures", "french"), "armure");
        assert_eq!(normalize("gas", "english"), "gas"); // short words are left alone
        assert_eq!(normalize("virus", "english"), "virus");
    }

    #[test]
    fn tokens_have_byte_offsets() {
        let t = tokenize("L'armure d'Abel, les Stimpaks!", "french");
        let words: Vec<&str> = t.iter().map(|x| x.normalized.as_str()).collect();
        assert_eq!(words, vec!["l", "armure", "d", "abel", "les", "stimpak"]);
        let text = "Déjà vu: Stimpaks";
        let t = tokenize(text, "english");
        let last = t.last().unwrap();
        assert_eq!(&text[last.start..last.start + last.length], "Stimpaks");
        // an inner apostrophe stays in English
        assert_eq!(tokenize("gizmo's", "english").len(), 1);
    }

    #[test]
    fn cjk_is_split_per_character() {
        let t = tokenize("你好abc", "chinese");
        assert_eq!(t.len(), 3);
        assert_eq!(t[0].normalized, "你");
    }

    #[test]
    fn one_edit() {
        assert!(within_one_edit("stimpak", "stimpack"));
        assert!(within_one_edit("stimpak", "stimpax"));
        assert!(within_one_edit("stimpak", "timpak"));
        assert!(!within_one_edit("stimpak", "stimpaks!!"));
        assert!(!within_one_edit("stimpak", "stampik"));
    }
}
