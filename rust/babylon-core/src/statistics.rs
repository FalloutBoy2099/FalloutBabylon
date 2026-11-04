//! Progress numbers of message files, and the word counting they rely on.

use crate::stemmer::is_cjk;

/// Progress numbers of one message file (or the sum of several).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct FileStats {
    pub rel_path: String,
    /// Character / file name ("Abel" for dialog/abel.msg).
    pub display_name: String,
    /// 0: game, 1: dialog, 2: other (see `FileKind`).
    pub kind: i32,
    pub lines: i32,
    pub translated: i32,
    pub review: i32,
    pub untranslated: i32,
    /// Words of the source text.
    pub words: i64,
    pub words_remaining: i64,
    pub chars: i64,
    pub chars_remaining: i64,
}

impl FileStats {
    /// Share of lines that have a translation (records marked "to review" count: they are translated).
    pub fn percent(&self) -> f64 {
        if self.lines > 0 {
            100.0 * (self.lines - self.untranslated) as f64 / self.lines as f64
        } else {
            100.0
        }
    }
    pub fn is_untouched(&self) -> bool {
        self.lines > 0 && self.translated == 0 && self.review == 0
    }
    pub fn is_complete(&self) -> bool {
        self.untranslated == 0 && self.review == 0
    }
    pub fn is_partial(&self) -> bool {
        !self.is_complete() && !self.is_untouched()
    }
    pub fn add(&mut self, o: &FileStats) {
        self.lines += o.lines;
        self.translated += o.translated;
        self.review += o.review;
        self.untranslated += o.untranslated;
        self.words += o.words;
        self.words_remaining += o.words_remaining;
        self.chars += o.chars;
        self.chars_remaining += o.chars_remaining;
    }
}

/// Number of words (CJK: number of characters).
pub fn count_words(text: &str) -> i32 {
    let mut words = 0;
    let mut in_word = false;
    for c in text.chars() {
        if is_cjk(c) {
            words += 1;
            in_word = false;
        } else if c.is_alphanumeric() {
            if !in_word {
                words += 1;
            }
            in_word = true;
        } else if !(c == '\'' || c == '\u{2019}' || c == '-') {
            in_word = false;
        }
    }
    words
}

/// True when the text contains something worth translating (at least one letter).
pub fn is_translatable(text: &str) -> bool {
    text.chars().any(|c| c.is_alphabetic())
}

/// "dialog/abel.msg" -> "Abel", "game/pro_item.msg" -> "pro_item"
pub fn display_name_for(rel_path: &str) -> String {
    let mut name = rel_path.rsplit('/').next().unwrap_or("").to_string();
    if name.to_lowercase().ends_with(".msg") {
        name.truncate(name.len() - 4);
    }
    if rel_path.starts_with("dialog/") && !name.is_empty() {
        name = name.replace('_', " ");
        let mut chars = name.chars();
        if let Some(first) = chars.next() {
            name = first.to_uppercase().collect::<String>() + chars.as_str();
        }
    }
    name
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn words() {
        assert_eq!(count_words("Hello there, Gizmo's friend - well-known!"), 5);
        assert_eq!(count_words("你好世界"), 4);
        assert_eq!(count_words("..."), 0);
        assert_eq!(count_words("I need 3 caps"), 4);
    }

    #[test]
    fn translatable() {
        assert!(is_translatable("Hello"));
        assert!(!is_translatable("..."));
        assert!(!is_translatable("12 ... 5"));
        assert!(is_translatable("%d caps"));
    }

    #[test]
    fn names() {
        assert_eq!(display_name_for("dialog/abel.msg"), "Abel");
        assert_eq!(display_name_for("dialog/vault_city.msg"), "Vault city");
        assert_eq!(display_name_for("game/pro_item.msg"), "pro_item");
    }

    #[test]
    fn stats() {
        let mut a = FileStats { lines: 10, translated: 4, review: 1, untranslated: 5, ..Default::default() };
        assert!((a.percent() - 50.0).abs() < 1e-9);
        assert!(a.is_partial());
        let b = FileStats { lines: 2, translated: 2, ..Default::default() };
        a.add(&b);
        assert_eq!(a.lines, 12);
        assert!(FileStats { lines: 3, untranslated: 3, ..Default::default() }.is_untouched());
        assert!(FileStats { lines: 3, translated: 3, ..Default::default() }.is_complete());
    }
}
