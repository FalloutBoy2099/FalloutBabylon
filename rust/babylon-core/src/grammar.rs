//! Client of a checker server reached over HTTP(S): Grammalecte (French) or LanguageTool.
//! Blocking: call it from a worker thread.

use crate::error::{Error, Result};
use crate::http;
use crate::i18n::trf;
use crate::languages;
use serde_json::Value;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IssueKind {
    Spelling,
    Grammar,
    Style,
}

/// One problem found by a grammar / spelling checker, as a range of the checked text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GrammarIssue {
    pub kind: IssueKind,
    /// Byte offset in the checked text.
    pub start: usize,
    /// Length in bytes.
    pub length: usize,
    pub message: String,
    pub rule_id: String,
    pub suggestions: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Engine {
    Grammalecte,
    LanguageTool,
}

/// Which server serves a target language: Grammalecte for French when it is configured, LanguageTool
/// otherwise. `None` when no configured server handles the language.
pub fn engine_for(game_language: &str, grammalecte_url: &str, language_tool_url: &str) -> Option<(Engine, String)> {
    if languages::is_french(game_language) && !grammalecte_url.trim().is_empty() {
        return Some((Engine::Grammalecte, grammalecte_url.trim().to_string()));
    }
    if !language_tool_url.trim().is_empty() && !languages::language_tool(game_language).is_empty() {
        return Some((Engine::LanguageTool, language_tool_url.trim().to_string()));
    }
    None
}

fn join_url(base: &str, path: &str) -> String {
    format!("{}{}", base.trim().trim_end_matches('/'), path)
}

/// Byte offset of a UTF-16 offset in `text` (clamped to the end of the text).
pub fn utf16_to_byte(text: &str, offset16: usize) -> usize {
    let mut units = 0;
    for (i, c) in text.char_indices() {
        if units >= offset16 {
            return i;
        }
        units += c.len_utf16();
    }
    text.len()
}

pub struct GrammarClient {
    agent: ureq::Agent,
}

impl Default for GrammarClient {
    fn default() -> Self {
        Self::new()
    }
}

impl GrammarClient {
    pub fn new() -> Self {
        GrammarClient { agent: http::agent(15) }
    }

    fn post(&self, url: &str, body: &str) -> Result<Vec<u8>> {
        let outcome = self.agent.post(url).set("Content-Type", "application/x-www-form-urlencoded").send_string(body);
        match outcome {
            Ok(resp) => {
                let mut data = Vec::new();
                use std::io::Read;
                resp.into_reader().read_to_end(&mut data)?;
                Ok(data)
            }
            Err(ureq::Error::Status(code, _)) => Err(Error::new(trf("Server error %1", &[&code.to_string()]))),
            Err(e) => Err(Error::new(e.to_string())),
        }
    }

    pub fn check(&self, engine: Engine, server_url: &str, game_language: &str, text: &str) -> Result<Vec<GrammarIssue>> {
        if text.trim().is_empty() {
            return Ok(Vec::new());
        }
        // application/x-www-form-urlencoded, built by hand: every byte of the text is percent-encoded ('+' included).
        let mut form = format!("text={}", http::percent_encode(text));
        if engine == Engine::Grammalecte {
            let data = self.post(&join_url(server_url, "/gc_text/fr"), &form)?;
            return Ok(parse_grammalecte(&data, text));
        }
        let code = languages::language_tool(game_language);
        if code.is_empty() {
            return Err(Error::new(trf("LanguageTool does not support %1.", &[game_language])));
        }
        form.push_str(&format!("&language={}", http::percent_encode(&code)));
        let data = self.post(&join_url(server_url, "/v2/check"), &form)?;
        Ok(parse_language_tool(&data, text))
    }
}

/// Offsets of Grammalecte are relative to the paragraph (a line of the text).
pub fn parse_grammalecte(json: &[u8], text: &str) -> Vec<GrammarIssue> {
    let mut issues = Vec::new();
    let Ok(root) = serde_json::from_slice::<Value>(json) else { return issues };

    // start of each line, in UTF-16 units
    let mut line_start: Vec<usize> = Vec::new();
    let mut pos = 0usize;
    for line in text.split('\n') {
        line_start.push(pos);
        pos += line.encode_utf16().count() + 1;
    }

    let range = |base: usize, e: &Value| -> Option<(usize, usize)> {
        let n_start = e["nStart"].as_i64().unwrap_or(0).max(0) as usize;
        let n_end = e["nEnd"].as_i64().unwrap_or(0).max(0) as usize;
        if n_end <= n_start {
            return None;
        }
        let start = utf16_to_byte(text, base + n_start);
        let end = utf16_to_byte(text, base + n_end);
        Some((start, end - start))
    };

    for paragraph in root["data"].as_array().cloned().unwrap_or_default() {
        let index = (paragraph["iParagraph"].as_i64().unwrap_or(1) - 1).max(0) as usize;
        let base = line_start.get(index).copied().unwrap_or(0);

        for e in paragraph["lGrammarErrors"].as_array().cloned().unwrap_or_default() {
            let Some((start, length)) = range(base, &e) else { continue };
            issues.push(GrammarIssue {
                kind: if e["sType"].as_str() == Some("typo") { IssueKind::Style } else { IssueKind::Grammar },
                start,
                length,
                message: e["sMessage"].as_str().unwrap_or("").to_string(),
                rule_id: e["sRuleId"].as_str().unwrap_or("").to_string(),
                suggestions: e["aSuggestions"]
                    .as_array()
                    .map(|a| a.iter().filter_map(|s| s.as_str().map(String::from)).collect())
                    .unwrap_or_default(),
            });
        }
        for e in paragraph["lSpellingErrors"].as_array().cloned().unwrap_or_default() {
            let Some((start, length)) = range(base, &e) else { continue };
            issues.push(GrammarIssue {
                kind: IssueKind::Spelling,
                start,
                length,
                message: trf("Unknown word: %1", &[e["sValue"].as_str().unwrap_or("")]),
                rule_id: "spelling".into(),
                suggestions: Vec::new(),
            });
        }
    }
    issues
}

/// Offsets of LanguageTool are UTF-16 units of the checked text.
pub fn parse_language_tool(json: &[u8], text: &str) -> Vec<GrammarIssue> {
    let mut issues = Vec::new();
    let Ok(root) = serde_json::from_slice::<Value>(json) else { return issues };
    for m in root["matches"].as_array().cloned().unwrap_or_default() {
        let rule = &m["rule"];
        let kind = match rule["issueType"].as_str().unwrap_or("") {
            "misspelling" => IssueKind::Spelling,
            "style" | "typographical" => IssueKind::Style,
            _ => IssueKind::Grammar,
        };
        let start16 = m["offset"].as_i64().unwrap_or(0).max(0) as usize;
        let length16 = m["length"].as_i64().unwrap_or(0).max(0) as usize;
        if length16 == 0 {
            continue;
        }
        let start = utf16_to_byte(text, start16);
        let end = utf16_to_byte(text, start16 + length16);
        let suggestions: Vec<String> = m["replacements"]
            .as_array()
            .map(|a| a.iter().filter_map(|r| r["value"].as_str().map(String::from)).take(6).collect())
            .unwrap_or_default();
        issues.push(GrammarIssue {
            kind,
            start,
            length: end - start,
            message: m["message"].as_str().unwrap_or("").to_string(),
            rule_id: rule["id"].as_str().unwrap_or("").to_string(),
            suggestions,
        });
    }
    issues
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::http::mock::{MockServer, Response};

    #[test]
    fn grammar_parsers() {
        // answers captured from real Grammalecte 2.3.0 / LanguageTool 6.8 servers
        let gram = r##"{ "program": "grammalecte-fr", "version": "2.3.0", "lang": "fr", "error": "", "data" : [
{"iParagraph": 1, "lGrammarErrors": [{"nStart": 8, "nEnd": 13, "sLineId": "#33033", "sRuleId": "r1", "sType": "ppas", "sMessage": "Après être, un participe passé.", "aSuggestions": ["allé", "allée"], "URL": ""}], "lSpellingErrors": []},
{"iParagraph": 2, "lGrammarErrors": [], "lSpellingErrors": [{"nStart": 3, "nEnd": 8, "sValue": "marchè", "sType": "WORD"}]}
]}"##;
        let text = "Je suis aller au marché.\nUn marchè.";
        let g = parse_grammalecte(gram.as_bytes(), text);
        assert_eq!(g.len(), 2);
        assert_eq!(&text[g[0].start..g[0].start + g[0].length], "aller");
        assert_eq!(g[0].suggestions, vec!["allé", "allée"]);
        assert_eq!(g[1].kind, IssueKind::Spelling);
        assert_eq!(&text[g[1].start..g[1].start + g[1].length], "march"); // second paragraph offset

        let lt = r#"{"matches":[{"message":"Un participe passé semble plus approprié.","replacements":[{"value":"allé"},{"value":"allés"}],"offset":8,"length":5,"rule":{"id":"ETRE_VPPA_OU_ADJ","issueType":"uncategorized"}},{"message":"Faute","replacements":[],"offset":0,"length":2,"rule":{"id":"MORFOLOGIK","issueType":"misspelling"}}]}"#;
        let l = parse_language_tool(lt.as_bytes(), text);
        assert_eq!(l.len(), 2);
        assert_eq!(l[0].rule_id, "ETRE_VPPA_OU_ADJ");
        assert_eq!(l[0].suggestions[0], "allé");
        assert_eq!(l[1].kind, IssueKind::Spelling);
    }

    #[test]
    fn utf16_offsets_become_byte_offsets() {
        let text = "é😀x"; // 1, 2 UTF-16 units, 1
        assert_eq!(utf16_to_byte(text, 0), 0);
        assert_eq!(utf16_to_byte(text, 1), 2);
        assert_eq!(utf16_to_byte(text, 3), 6);
        assert_eq!(utf16_to_byte(text, 99), text.len());
    }

    #[test]
    fn engine_choice() {
        assert_eq!(engine_for("french", "http://g", "http://l").unwrap().0, Engine::Grammalecte);
        assert_eq!(engine_for("french", "", "http://l").unwrap().0, Engine::LanguageTool);
        assert_eq!(engine_for("german", "http://g", "http://l").unwrap().0, Engine::LanguageTool);
        assert!(engine_for("german", "http://g", "").is_none());
        assert!(engine_for("czech", "", "http://l").is_none()); // LanguageTool has no Czech
    }

    #[test]
    fn client_against_a_mock_server() {
        let server = MockServer::start(|_| {
            Response::json(r#"{"matches":[{"message":"m","replacements":[{"value":"x"}],"offset":1,"length":2,"rule":{"id":"R","issueType":"grammar"}}]}"#)
        });
        let client = GrammarClient::new();
        let issues = client.check(Engine::LanguageTool, &format!("{}/", server.url), "german", "a+b & c é").unwrap();
        assert_eq!(issues.len(), 1);
        let req = server.requests.lock().unwrap()[0].clone();
        assert_eq!(req.path, "/v2/check");
        assert_eq!(req.body, "text=a%2Bb%20%26%20c%20%C3%A9&language=de-DE");
        // an empty text needs no request
        assert!(client.check(Engine::LanguageTool, &server.url, "german", "  ").unwrap().is_empty());
        assert_eq!(server.count(), 1);
    }
}
