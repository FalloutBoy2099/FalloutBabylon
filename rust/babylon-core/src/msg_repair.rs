//! Repairs the usual damage of hand-edited .msg files, which the game answers by silently dropping every record
//! after the first broken one: a record with two fields, a missing closing or opening brace, a record that
//! swallowed the next one, a record whose id is not a number (put in a comment so the file stays readable).

use regex::Regex;
use std::sync::OnceLock;

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Fix {
    /// 1-based line of the record in the original text.
    pub line: usize,
    /// "two fields", "missing }", "missing {", "invalid id", "unrepaired"
    pub kind: String,
    pub before: String,
    pub after: String,
}

#[derive(Debug, Clone, Default)]
pub struct Repair {
    /// The repaired text (same line ending style as the input).
    pub text: String,
    pub fixes: Vec<Fix>,
    /// Records that could not be fixed automatically.
    pub unrepaired: usize,
    pub original: String,
}

impl Repair {
    pub fn changed(&self) -> bool {
        self.text != self.original
    }
}

struct Chunk {
    lines: Vec<String>,
    first_line: usize, // 1-based
    record: bool,      // starts with '{'
}

fn one_line(s: &str) -> String {
    let s = s.replace('\n', " ⏎ ");
    if s.chars().count() > 120 {
        format!("{}…", s.chars().take(120).collect::<String>())
    } else {
        s
    }
}

struct Patterns {
    valid: Regex,
    two_fields: Regex,
    no_close: Regex,
    no_open: Regex,
    bad_id: Regex,
}

fn patterns() -> &'static Patterns {
    static P: OnceLock<Patterns> = OnceLock::new();
    P.get_or_init(|| Patterns {
        valid: Regex::new(r"(?s)^\s*\{\s*-?\d+\s*\}\s*\{[^{}]*\}\s*\{[^{}]*\}").unwrap(),
        two_fields: Regex::new(r"(?s)^(\s*)\{\s*(-?\d+)\s*\}\s*\{([^{}]*)\}(\s*)$").unwrap(),
        no_close: Regex::new(r"(?s)^(\s*)\{\s*(-?\d+)\s*\}\s*\{([^{}]*)\}\s*\{([^{}]*?)(\s*)$").unwrap(),
        no_open: Regex::new(r"(?s)^(\s*)\{\s*(-?\d+)\s*\}\s*\{([^{}]*)\}\s*([^{}\s][^{}]*?)\}(\s*)$").unwrap(),
        bad_id: Regex::new(r"^\s*\{([^{}]*)\}").unwrap(),
    })
}

pub fn repair(decoded: &str) -> Repair {
    let mut result = Repair { original: decoded.to_string(), ..Default::default() };
    let p = patterns();

    let crlf = decoded.contains("\r\n");
    let normalized = decoded.replace("\r\n", "\n");
    let (bom, text) = match normalized.strip_prefix('\u{FEFF}') {
        Some(rest) => ("\u{FEFF}", rest),
        None => ("", normalized.as_str()),
    };
    let lines: Vec<&str> = text.split('\n').collect();

    // --- split into chunks: one per record (a line starting with '{' and what follows until the next one)
    let mut chunks: Vec<Chunk> = Vec::new();
    for (i, line) in lines.iter().enumerate() {
        if line.trim().starts_with('{') {
            chunks.push(Chunk { lines: vec![line.to_string()], first_line: i + 1, record: true });
            continue;
        }
        // A line that is not a record continues an unfinished record, otherwise it is trivia.
        if let Some(last) = chunks.last_mut() {
            if last.record && last.lines.join("\n").matches('}').count() < 3 {
                last.lines.push(line.to_string());
                continue;
            }
        }
        if chunks.last().map(|c| c.record).unwrap_or(true) {
            chunks.push(Chunk { lines: Vec::new(), first_line: i + 1, record: false });
        }
        chunks.last_mut().unwrap().lines.push(line.to_string());
    }

    let mut out: Vec<String> = Vec::new();
    for c in &chunks {
        let whole = c.lines.join("\n");
        if !c.record || p.valid.is_match(&whole) {
            out.push(whole);
            continue;
        }
        // Blank lines and comments after a broken record are not part of it.
        let mut head: Vec<String> = c.lines.clone();
        let mut tail: Vec<String> = Vec::new();
        while head.len() > 1 && head.last().is_some_and(|l| l.trim().is_empty() || l.trim().starts_with('#')) {
            tail.insert(0, head.pop().unwrap());
        }
        let chunk_text = head.join("\n");
        let tail_text = if tail.is_empty() { String::new() } else { format!("\n{}", tail.join("\n")) };

        let mut fix = Fix { line: c.first_line, before: one_line(chunk_text.trim()), ..Default::default() };
        if let Some(m) = p.two_fields.captures(&chunk_text) {
            fix.kind = "two fields".into();
            fix.after = format!("{}{{{}}}{{}}{{{}}}{}", &m[1], &m[2], &m[3], &m[4]);
        } else if let Some(m) = p.no_close.captures(&chunk_text) {
            fix.kind = "missing }".into();
            fix.after = format!("{}{{{}}}{{{}}}{{{}}}{}", &m[1], &m[2], &m[3], &m[4], &m[5]);
        } else if let Some(m) = p.no_open.captures(&chunk_text) {
            fix.kind = "missing {".into();
            fix.after = format!("{}{{{}}}{{{}}}{{{}}}{}", &m[1], &m[2], &m[3], &m[4], &m[5]);
        } else if p.bad_id.is_match(&chunk_text) {
            // not a number: keep the text readable, but out of the game's way
            fix.kind = "invalid id".into();
            // braces would still be read by the parser, even inside a comment
            let commented: Vec<String> = head
                .iter()
                .map(|l| format!("# {}", l.replace('{', "[").replace('}', "]")))
                .collect();
            fix.after = commented.join("\n");
        } else {
            fix.kind = "unrepaired".into();
            fix.after = fix.before.clone();
            result.unrepaired += 1;
            result.fixes.push(fix);
            out.push(whole);
            continue;
        }
        out.push(format!("{}{}", fix.after, tail_text));
        result.fixes.push(fix);
    }

    let mut repaired = format!("{}{}", bom, out.join("\n"));
    if crlf {
        repaired = repaired.replace('\n', "\r\n");
    }
    result.text = if result.fixes.is_empty() { decoded.to_string() } else { repaired };
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::msg_file::MsgFile;

    #[test]
    fn msg_repair() {
        let broken = "# header\r\n{100}{}{Fine line}\r\n{101}{Two fields}\r\n{102}{}{Missing the closing brace\r\n\
{103}{}Missing the opening brace}\r\n{abc}{}{Not a number}\r\n\r\n{104}{}{Multi\r\nline is fine}\r\n{105}{}{Last one}\r\n";
        let before = MsgFile::parse(broken);
        assert!(!before.warnings.is_empty());

        let r = repair(broken);
        assert!(r.changed());
        assert_eq!(r.unrepaired, 0);
        assert_eq!(r.fixes.len(), 4);
        let after = MsgFile::parse(&r.text);
        assert!(after.warnings.is_empty(), "{:?}", after.warnings);
        let ids: Vec<i32> = after.entries.iter().map(|e| e.id).collect();
        assert_eq!(ids, vec![100, 101, 102, 103, 104, 105]);
        let text = |id: i32| after.entries.iter().find(|e| e.id == id).map(|e| e.text.clone()).unwrap_or_else(|| "<none>".into());
        assert_eq!(text(101), "Two fields");
        assert_eq!(text(102), "Missing the closing brace");
        assert_eq!(text(103), "Missing the opening brace");
        assert_eq!(text(104), "Multi\nline is fine");
        assert!(r.text.contains("# [abc][][Not a number]")); // kept, out of the game's way
        assert!(r.text.contains("\r\n")); // line endings preserved
        assert!(r.text.starts_with("# header"));

        // a healthy file is left alone, byte for byte
        let fine = "{1}{}{One}\r\n\r\n# note\r\n{2}{}{Two\r\nlines}\r\n";
        let same = repair(fine);
        assert!(!same.changed());
        assert!(same.fixes.is_empty());
        assert_eq!(same.text, fine);
    }
}
