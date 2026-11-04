//! Minimal client of the DeepL REST API: translation (with context, glossary and protected placeholders),
//! glossaries (v3 endpoints) and usage. Blocking: call it from a worker thread.

use crate::error::{Error, Result};
use crate::glossary::Glossary;
use crate::http;
use crate::i18n::{tr, trf};
use crate::languages;
use crate::placeholders;
use serde_json::{json, Value};
use std::sync::Arc;
use std::time::Duration;

const MAX_ATTEMPTS: u32 = 4;

#[derive(Clone, Default)]
pub struct Options {
    pub api_key: String,
    /// Game language folder names ("english"); converted to DeepL codes.
    pub source_language: String,
    pub target_language: String,
    /// Optional.
    pub glossary_id: String,
    /// "", "prefer_more" or "prefer_less".
    pub formality: String,
    /// Used when there is no DeepL glossary: the terms of this glossary are replaced locally (see `placeholders`).
    pub local_glossary: Option<Arc<Glossary>>,
}

/// A translation, with a warning when something looks wrong in it (a placeholder was altered).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Translation {
    pub text: String,
    pub warning: Option<String>,
}

#[derive(Debug)]
struct HttpError {
    status: u16,
    message: String,
}

pub struct DeepLClient {
    /// Overrides the host derived from the key (free keys end with ":fx"); used by the tests.
    base_url: Option<String>,
    agent: ureq::Agent,
}

impl Default for DeepLClient {
    fn default() -> Self {
        Self::new()
    }
}

/// The error text `create_glossary` reports when the account's glossary allowance is used up.
pub fn glossary_limit_message() -> String {
    tr("DeepL refused the glossary: the free plan allows a single glossary (delete the existing one in your \
        DeepL account to use the project glossary).")
}

pub fn base_url_for(api_key: &str) -> &'static str {
    if api_key.trim().ends_with(":fx") {
        "https://api-free.deepl.com"
    } else {
        "https://api.deepl.com"
    }
}

impl DeepLClient {
    pub fn new() -> Self {
        DeepLClient { base_url: None, agent: http::agent(30) }
    }

    pub fn set_base_url(&mut self, url: impl Into<String>) {
        self.base_url = Some(url.into());
    }

    fn send(&self, api_key: &str, method: &str, path: &str, body: Option<&Value>) -> std::result::Result<Value, HttpError> {
        let key = api_key.trim();
        if key.is_empty() {
            return Err(HttpError { status: 0, message: tr("No DeepL API key. Enter it in Tools > Settings.") });
        }
        let base = self.base_url.clone().unwrap_or_else(|| base_url_for(key).to_string());
        let url = format!("{base}{path}");

        let mut attempt = 0;
        loop {
            let request = self
                .agent
                .request(method, &url)
                .set("Authorization", &format!("DeepL-Auth-Key {key}"))
                .set("Content-Type", "application/json");
            let outcome = match (method, body) {
                ("GET" | "DELETE", _) => request.call(),
                (_, Some(b)) => request.send_string(&b.to_string()),
                (_, None) => request.send_string("{}"),
            };
            let (status, text) = match outcome {
                Ok(resp) => (resp.status(), resp.into_string().unwrap_or_default()),
                Err(ureq::Error::Status(code, resp)) => (code, resp.into_string().unwrap_or_default()),
                Err(e) => return Err(HttpError { status: 0, message: e.to_string() }),
            };
            // Rate limit / transient server trouble: wait and try again.
            if matches!(status, 429 | 503 | 529) && attempt + 1 < MAX_ATTEMPTS {
                attempt += 1;
                std::thread::sleep(Duration::from_millis(600 * attempt as u64));
                continue;
            }
            let json: Value = serde_json::from_str(&text).unwrap_or(Value::Null);
            if (200..300).contains(&status) {
                return Ok(json);
            }
            let mut message = json["message"].as_str().unwrap_or("").to_string();
            if status == 403 {
                message = tr("DeepL rejected the API key.");
            } else if status == 456 {
                message = tr("The DeepL quota is exhausted.");
            } else if message.is_empty() {
                message = trf("DeepL error %1", &[&status.to_string()]);
            }
            return Err(HttpError { status, message });
        }
    }

    /// `context` is extra text that influences the translation but is not translated (may be empty).
    pub fn translate(&self, text: &str, context: &str, o: &Options) -> Result<Translation> {
        let source = languages::deepl_source(&o.source_language);
        let target = languages::deepl_target(&o.target_language);
        if source.is_empty() || target.is_empty() {
            let name = if source.is_empty() { &o.source_language } else { &o.target_language };
            return Err(Error::new(trf("DeepL does not support %1.", &[name])));
        }
        let local = if o.glossary_id.is_empty() { o.local_glossary.as_deref() } else { None };
        let p = placeholders::protect_with(text, local);

        let mut body = json!({
            "text": [p.xml],
            "source_lang": source,
            "target_lang": target,
            "tag_handling": "xml",
            "ignore_tags": ["x"],
            "split_sentences": "nonewlines",
            "preserve_formatting": true,
        });
        if !context.trim().is_empty() {
            body["context"] = json!(context);
        }
        if !o.glossary_id.is_empty() {
            body["glossary_id"] = json!(o.glossary_id);
        }
        if !o.formality.is_empty() {
            body["formality"] = json!(o.formality);
        }
        let answer = self.send(&o.api_key, "POST", "/v2/translate", Some(&body)).map_err(|e| Error::new(e.message))?;
        let first = answer["translations"].as_array().and_then(|l| l.first());
        let Some(first) = first else {
            return Err(Error::new(tr("Empty answer from DeepL.")));
        };
        let (result, intact) = placeholders::restore(first["text"].as_str().unwrap_or(""), &p.tokens);
        Ok(Translation {
            text: result,
            warning: if intact { None } else { Some(tr("A placeholder (@...@, %s...) was altered: check this line.")) },
        })
    }

    /// (characters used, character limit)
    pub fn usage(&self, api_key: &str) -> Result<(i64, i64)> {
        let json = self.send(api_key, "GET", "/v2/usage", None).map_err(|e| Error::new(e.message))?;
        Ok((json["character_count"].as_f64().unwrap_or(0.0) as i64, json["character_limit"].as_f64().unwrap_or(0.0) as i64))
    }

    /// Returns the id of the new glossary.
    pub fn create_glossary(
        &self,
        api_key: &str,
        name: &str,
        source_language: &str,
        target_language: &str,
        entries: &[(String, String)],
    ) -> Result<String> {
        let source = languages::deepl_glossary(source_language);
        let target = languages::deepl_glossary(target_language);
        if source.is_empty() || target.is_empty() || entries.is_empty() {
            return Err(Error::new(tr("No glossary can be created for this language pair.")));
        }
        let lines: Vec<String> = entries.iter().map(|(a, b)| format!("{a}\t{b}")).collect();
        let body = json!({
            "name": name,
            "dictionaries": [{
                "source_lang": source,
                "target_lang": target,
                "entries": lines.join("\n"),
                "entries_format": "tsv",
            }],
        });
        match self.send(api_key, "POST", "/v3/glossaries", Some(&body)) {
            Ok(json) => Ok(json["glossary_id"].as_str().unwrap_or("").to_string()),
            // 456 on this call means the glossary allowance is used up (the free plan holds a single glossary).
            Err(e) if e.status == 456 => Err(Error::new(glossary_limit_message())),
            Err(e) => Err(Error::new(e.message)),
        }
    }

    /// Glossaries of the account (the free plan holds a single one): (id, name).
    pub fn list_glossaries(&self, api_key: &str) -> Result<Vec<(String, String)>> {
        let json = self.send(api_key, "GET", "/v3/glossaries", None).map_err(|e| Error::new(e.message))?;
        Ok(json["glossaries"]
            .as_array()
            .map(|a| {
                a.iter()
                    .map(|g| (g["glossary_id"].as_str().unwrap_or("").to_string(), g["name"].as_str().unwrap_or("").to_string()))
                    .collect()
            })
            .unwrap_or_default())
    }

    pub fn delete_glossary(&self, api_key: &str, glossary_id: &str) -> Result<()> {
        self.send(api_key, "DELETE", &format!("/v3/glossaries/{glossary_id}"), None)
            .map(|_| ())
            .map_err(|e| Error::new(e.message))
    }
}

/// The (term, translation) pairs of a glossary that DeepL can hold (no tab or line break in an entry).
pub fn glossary_entries(glossary: &Glossary) -> Vec<(String, String)> {
    glossary
        .terms()
        .iter()
        .filter_map(|t| {
            let target = t.targets.first()?;
            let bad = |s: &str| s.contains('\t') || s.contains('\n');
            if bad(&t.source) || bad(target) {
                None
            } else {
                Some((t.source.clone(), target.clone()))
            }
        })
        .collect()
}

/// Identity of the glossary sent to DeepL: it is created again only when this changes.
pub fn glossary_signature(source_language: &str, target_language: &str, entries: &[(String, String)]) -> String {
    use sha1::{Digest, Sha1};
    let mut hash = Sha1::new();
    hash.update(source_language.as_bytes());
    hash.update(target_language.as_bytes());
    for (a, b) in entries {
        hash.update(a.as_bytes());
        hash.update(b"\t");
        hash.update(b.as_bytes());
        hash.update(b"\n");
    }
    hash.finalize().iter().map(|b| format!("{b:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::http::mock::{MockServer, Response};

    fn options(key: &str) -> Options {
        Options { api_key: key.into(), source_language: "english".into(), target_language: "french".into(), ..Default::default() }
    }

    #[test]
    fn translate_sends_everything_the_api_needs() {
        let server = MockServer::start(|r| {
            if r.path != "/v2/translate" {
                return Response::status(404);
            }
            if r.headers.get("authorization").map(String::as_str) != Some("DeepL-Auth-Key test-key:fx") {
                return Response::status(403);
            }
            let body: Value = serde_json::from_str(&r.body).unwrap();
            let text = body["text"][0].as_str().unwrap();
            Response::json(&json!({"translations": [{"text": format!("[FR] {text}")}]}).to_string())
        });
        let mut client = DeepLClient::new();
        client.set_base_url(&server.url);
        let mut o = options("test-key:fx");
        o.glossary_id = "g-1".into();
        o.formality = "prefer_more".into();

        let out = client.translate("Take a Stimpak, @Name@ & go.", "Previous line: Hello.", &o).unwrap();
        assert_eq!(out.text, "[FR] Take a Stimpak, @Name@ & go.");
        assert!(out.warning.is_none());
        let sent: Value = serde_json::from_str(&server.requests.lock().unwrap()[0].body).unwrap();
        assert_eq!(sent["source_lang"], "EN");
        assert_eq!(sent["target_lang"], "FR");
        assert_eq!(sent["glossary_id"], "g-1");
        assert_eq!(sent["context"], "Previous line: Hello.");
        assert_eq!(sent["tag_handling"], "xml");
        assert_eq!(sent["ignore_tags"][0], "x");
        assert_eq!(sent["formality"], "prefer_more");
        assert!(sent["text"][0].as_str().unwrap().contains("<x>0</x>"));

        // wrong key
        assert!(client.translate("Hi", "", &options("bad:fx")).is_err());

        // unsupported language: reported without any request
        let before = server.count();
        let mut o = options("test-key:fx");
        o.target_language = "vietnamese".into();
        assert!(client.translate("Hi", "", &o).is_err());
        assert_eq!(server.count(), before);
    }

    #[test]
    fn a_lost_placeholder_is_a_warning() {
        let server = MockServer::start(|_| Response::json(r#"{"translations":[{"text":"Bonjour"}]}"#));
        let mut client = DeepLClient::new();
        client.set_base_url(&server.url);
        let out = client.translate("Hello @Name@", "", &options("k:fx")).unwrap();
        assert_eq!(out.text, "Bonjour");
        assert!(out.warning.is_some());
    }

    #[test]
    fn retries_on_rate_limit() {
        let attempts = Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let a = attempts.clone();
        let server = MockServer::start(move |_| {
            if a.fetch_add(1, std::sync::atomic::Ordering::SeqCst) < 1 {
                Response::status(429)
            } else {
                Response::json(r#"{"translations":[{"text":"ok"}]}"#)
            }
        });
        let mut client = DeepLClient::new();
        client.set_base_url(&server.url);
        let out = client.translate("x", "", &options("k:fx")).unwrap();
        assert_eq!(out.text, "ok");
        assert_eq!(attempts.load(std::sync::atomic::Ordering::SeqCst), 2);
    }

    #[test]
    fn usage_and_glossaries() {
        let server = MockServer::start(|r| match (r.path.as_str(), r.method.as_str()) {
            ("/v2/usage", _) => Response::json(r#"{"character_count":1234,"character_limit":500000}"#),
            ("/v3/glossaries", "GET") => Response::json(r#"{"glossaries":[{"glossary_id":"old-1","name":"Boutique"}]}"#),
            ("/v3/glossaries", "POST") => Response::json(r#"{"glossary_id":"abc-123","name":"n"}"#),
            ("/v3/glossaries/abc-123", "DELETE") => Response::status(204),
            _ => Response::status(404),
        });
        let mut client = DeepLClient::new();
        client.set_base_url(&server.url);
        assert_eq!(client.usage("k:fx").unwrap(), (1234, 500000));
        assert_eq!(client.list_glossaries("k:fx").unwrap(), vec![("old-1".to_string(), "Boutique".to_string())]);
        let id = client
            .create_glossary(
                "k:fx",
                "FalloutBabylon test",
                "english",
                "french",
                &[("Stimpak".into(), "Stimpak".into()), ("Leather Armor".into(), "Armure en cuir".into())],
            )
            .unwrap();
        assert_eq!(id, "abc-123");
        let posts: Vec<_> = server.requests.lock().unwrap().iter().filter(|r| r.method == "POST").cloned().collect();
        let sent: Value = serde_json::from_str(&posts[0].body).unwrap();
        let dict = &sent["dictionaries"][0];
        assert_eq!(dict["source_lang"], "en");
        assert_eq!(dict["target_lang"], "fr");
        assert_eq!(dict["entries"], "Stimpak\tStimpak\nLeather Armor\tArmure en cuir");
        assert_eq!(dict["entries_format"], "tsv");
        client.delete_glossary("k:fx", "abc-123").unwrap();
    }

    #[test]
    fn quota_on_glossary_creation_means_the_limit_is_reached() {
        let server = MockServer::start(|_| Response::status(456));
        let mut client = DeepLClient::new();
        client.set_base_url(&server.url);
        let err = client.create_glossary("k:fx", "n", "english", "french", &[("a".into(), "b".into())]).unwrap_err();
        assert_eq!(err.0, glossary_limit_message());
        // while a translation reports the quota
        let err = client.translate("x", "", &options("k:fx")).unwrap_err();
        assert!(err.0.contains("quota"));
    }

    #[test]
    fn missing_key() {
        let client = DeepLClient::new();
        assert!(client.usage("  ").is_err());
        assert_eq!(base_url_for("abc:fx"), "https://api-free.deepl.com");
        assert_eq!(base_url_for("abc"), "https://api.deepl.com");
    }
}
