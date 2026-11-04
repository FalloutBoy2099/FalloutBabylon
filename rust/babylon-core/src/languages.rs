//! Correspondence between the game's language directory names and the codes of DeepL / LanguageTool.

struct Entry {
    name: &'static str,
    deepl_source: &'static str,
    deepl_target: &'static str,
    language_tool: &'static str,
}

const TABLE: [Entry; 17] = [
    Entry { name: "english", deepl_source: "EN", deepl_target: "EN-US", language_tool: "en-US" },
    Entry { name: "french", deepl_source: "FR", deepl_target: "FR", language_tool: "fr" },
    Entry { name: "german", deepl_source: "DE", deepl_target: "DE", language_tool: "de-DE" },
    Entry { name: "spanish", deepl_source: "ES", deepl_target: "ES", language_tool: "es" },
    Entry { name: "italian", deepl_source: "IT", deepl_target: "IT", language_tool: "it" },
    Entry { name: "portuguese", deepl_source: "PT", deepl_target: "PT-PT", language_tool: "pt-PT" },
    Entry { name: "brazilian", deepl_source: "PT", deepl_target: "PT-BR", language_tool: "pt-BR" },
    Entry { name: "polish", deepl_source: "PL", deepl_target: "PL", language_tool: "pl-PL" },
    Entry { name: "czech", deepl_source: "CS", deepl_target: "CS", language_tool: "" },
    Entry { name: "russian", deepl_source: "RU", deepl_target: "RU", language_tool: "ru-RU" },
    Entry { name: "hungarian", deepl_source: "HU", deepl_target: "HU", language_tool: "" },
    Entry { name: "swedish", deepl_source: "SV", deepl_target: "SV", language_tool: "sv" },
    Entry { name: "japanese", deepl_source: "JA", deepl_target: "JA", language_tool: "ja-JP" },
    Entry { name: "korean", deepl_source: "KO", deepl_target: "KO", language_tool: "" },
    Entry { name: "chinese", deepl_source: "ZH", deepl_target: "ZH-HANS", language_tool: "zh-CN" },
    Entry { name: "chs", deepl_source: "ZH", deepl_target: "ZH-HANS", language_tool: "zh-CN" },
    Entry { name: "cht", deepl_source: "ZH", deepl_target: "ZH-HANT", language_tool: "" },
];

fn entry(language: &str) -> Option<&'static Entry> {
    let l = language.to_lowercase();
    TABLE.iter().find(|e| e.name == l)
}

/// Code of a source language for DeepL ("" when DeepL does not support it).
pub fn deepl_source(game_language: &str) -> String {
    entry(game_language).map(|e| e.deepl_source.to_string()).unwrap_or_default()
}

pub fn deepl_target(game_language: &str) -> String {
    entry(game_language).map(|e| e.deepl_target.to_string()).unwrap_or_default()
}

/// Glossaries use the lower-case source code.
pub fn deepl_glossary(game_language: &str) -> String {
    deepl_source(game_language).to_lowercase()
}

pub fn language_tool(game_language: &str) -> String {
    entry(game_language).map(|e| e.language_tool.to_string()).unwrap_or_default()
}

pub fn is_french(game_language: &str) -> bool {
    game_language.eq_ignore_ascii_case("french")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn codes() {
        assert_eq!(deepl_source("English"), "EN");
        assert_eq!(deepl_target("english"), "EN-US");
        assert_eq!(deepl_target("portuguese"), "PT-PT");
        assert_eq!(deepl_target("brazilian"), "PT-BR");
        assert_eq!(deepl_glossary("french"), "fr");
        assert_eq!(language_tool("german"), "de-DE");
        assert_eq!(language_tool("czech"), "");
        assert_eq!(deepl_source("klingon"), "");
        assert!(is_french("French"));
    }
}
