//! Translation of the messages of the core. The English text is the key, like in the Qt version.
//!
//! The interface layer loads the table of the chosen language with `set_translations`; without it (English, or
//! a missing entry) the text is returned as is.

use std::collections::HashMap;
use std::sync::RwLock;

static TABLE: RwLock<Option<HashMap<String, String>>> = RwLock::new(None);

pub fn set_translations(table: HashMap<String, String>) {
    *TABLE.write().unwrap_or_else(|e| e.into_inner()) = Some(table);
}

pub fn clear_translations() {
    *TABLE.write().unwrap_or_else(|e| e.into_inner()) = None;
}

/// The translation of `text`, or `text` itself.
pub fn tr(text: &str) -> String {
    match TABLE.read().unwrap_or_else(|e| e.into_inner()).as_ref().and_then(|t| t.get(text)) {
        Some(t) if !t.is_empty() => t.clone(),
        _ => text.to_string(),
    }
}

/// Replaces %1, %2... of a template by the arguments (like `QString::arg` chained).
pub fn fill(template: &str, args: &[&str]) -> String {
    let mut out = String::with_capacity(template.len() + 16);
    let mut chars = template.char_indices().peekable();
    while let Some((i, c)) = chars.next() {
        if c == '%' {
            // longest run of digits after the percent sign
            let rest = &template[i + 1..];
            let digits: String = rest.chars().take_while(|d| d.is_ascii_digit()).collect();
            if !digits.is_empty() {
                // "%12" with only 9 arguments is "%1" followed by "2": try the longest valid number first
                let mut taken = digits.len().min(2);
                while taken > 0 {
                    let n: usize = digits[..taken].parse().unwrap_or(0);
                    if n >= 1 && n <= args.len() {
                        out.push_str(args[n - 1]);
                        for _ in 0..taken {
                            chars.next();
                        }
                        break;
                    }
                    taken -= 1;
                }
                if taken > 0 {
                    continue;
                }
            }
        }
        out.push(c);
    }
    out
}

/// `fill(&tr(text), args)`
pub fn trf(text: &str, args: &[&str]) -> String {
    fill(&tr(text), args)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn filling_templates() {
        assert_eq!(fill("%1 file(s) in %2", &["3", "x"]), "3 file(s) in x");
        assert_eq!(fill("100% sure %1", &["yes"]), "100% sure yes");
        assert_eq!(fill("%2 before %1", &["a", "b"]), "b before a");
        assert_eq!(fill("unused %3", &["a"]), "unused %3");
        assert_eq!(fill("%1%", &["5"]), "5%");
    }

    #[test]
    fn translations_fall_back_to_english() {
        // keys only used here: the table is global and the other tests run at the same time
        clear_translations();
        assert_eq!(tr("i18n test: Cancel"), "i18n test: Cancel");
        let mut t = HashMap::new();
        t.insert("i18n test: Cancel".to_string(), "Annuler".to_string());
        t.insert("i18n test: Empty".to_string(), String::new());
        set_translations(t);
        assert_eq!(tr("i18n test: Cancel"), "Annuler");
        assert_eq!(tr("i18n test: Empty"), "i18n test: Empty");
        assert_eq!(tr("i18n test: Other"), "i18n test: Other");
        clear_translations();
    }
}
