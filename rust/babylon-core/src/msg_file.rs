//! A Fallout .msg file: a list of `{id}{audio}{text}` records. Everything outside the braces
//! (blank lines, '#' comments...) is ignored by the game; it is kept as "leading" trivia so an
//! unmodified file serialises back to the very same text.

use std::collections::HashMap;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct MsgEntry {
    pub id: i32,
    pub audio: String,
    /// Line breaks normalised to '\n'.
    pub text: String,
    /// Text found before this record.
    pub leading: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MsgFile {
    pub entries: Vec<MsgEntry>,
    pub trailing: String,
    /// Line ending used when serialising.
    pub crlf: bool,
    pub warnings: Vec<String>,
}

impl Default for MsgFile {
    fn default() -> Self {
        MsgFile { entries: Vec::new(), trailing: String::new(), crlf: true, warnings: Vec::new() }
    }
}

impl MsgFile {
    /// `raw` is the file content already converted to Unicode.
    pub fn parse(raw: &str) -> MsgFile {
        let mut file = MsgFile::default();

        let crlf_count = raw.matches("\r\n").count();
        let lf_count = raw.matches('\n').count();
        file.crlf = if lf_count == 0 { true } else { crlf_count * 2 >= lf_count };

        let normalized = raw.replace("\r\n", "\n");
        let text: &str = normalized.strip_prefix('\u{FEFF}').unwrap_or(&normalized);

        let n = text.len();
        let mut pos = 0usize;
        let mut leading = String::new();

        let line_of = |p: usize| text[..p].matches('\n').count() + 1;

        // Reads "{...}" starting at the first '{' at or after `from`; the content ends at the next '}'.
        let read_field = |from: usize, strict: bool| -> Option<(String, usize)> {
            let open = if strict {
                let mut open = from;
                let bytes = text.as_bytes();
                while open < n {
                    let c = text[open..].chars().next().unwrap();
                    if c.is_whitespace() {
                        open += c.len_utf8();
                    } else {
                        break;
                    }
                }
                if open >= n || bytes[open] != b'{' {
                    return None;
                }
                open
            } else {
                from + text[from..].find('{')?
            };
            let close = open + 1 + text[open + 1..].find('}')?;
            Some((text[open + 1..close].to_string(), close + 1))
        };

        while pos < n {
            let Some(rel) = text[pos..].find('{') else { break };
            let start = pos + rel;
            let record = read_field(start, false).and_then(|(id_field, after_id)| {
                let (audio, after_audio) = read_field(after_id, true)?;
                let (body, next) = read_field(after_audio, true)?;
                Some((id_field, audio, body, next))
            });
            let Some((id_field, audio, body, next)) = record else {
                file.warnings.push(format!("Malformed record near line {}", line_of(start)));
                break; // the game stops at the first broken record as well
            };
            let id = match id_field.trim().parse::<i32>() {
                Ok(id) => id,
                Err(_) => {
                    file.warnings.push(format!("Invalid number '{}' near line {}", id_field, line_of(start)));
                    leading.push_str(&text[pos..next]);
                    pos = next;
                    continue;
                }
            };
            let mut entry_leading = std::mem::take(&mut leading);
            entry_leading.push_str(&text[pos..start]);
            file.entries.push(MsgEntry { id, audio, text: body, leading: entry_leading });
            pos = next;
        }
        file.trailing = format!("{}{}", leading, &text[pos.min(n)..]);
        file
    }

    pub fn serialize(&self) -> String {
        let mut out = String::new();
        for e in &self.entries {
            out.push_str(&e.leading);
            out.push('{');
            out.push_str(&e.id.to_string());
            out.push_str("}{");
            out.push_str(&e.audio);
            out.push_str("}{");
            out.push_str(&e.text);
            out.push('}');
        }
        out.push_str(&self.trailing);
        if self.crlf {
            out = out.replace('\n', "\r\n");
        }
        out
    }

    /// Index of the last entry with this id (the game keeps the last duplicate).
    pub fn index_by_id(&self) -> HashMap<i32, usize> {
        let mut map = HashMap::new();
        for (i, e) in self.entries.iter().enumerate() {
            map.insert(e.id, i);
        }
        map
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_records_and_keeps_the_trivia() {
        let src = "# comment\r\n{100}{}{Hello}\r\n\r\n{101}{audio01}{Line one\r\nline two}\r\n";
        let f = MsgFile::parse(src);
        assert!(f.warnings.is_empty());
        assert!(f.crlf);
        assert_eq!(f.entries.len(), 2);
        assert_eq!(f.entries[0].id, 100);
        assert_eq!(f.entries[0].text, "Hello");
        assert_eq!(f.entries[0].leading, "# comment\n");
        assert_eq!(f.entries[1].audio, "audio01");
        assert_eq!(f.entries[1].text, "Line one\nline two");
        assert_eq!(f.serialize(), src);
    }

    #[test]
    fn duplicates_keep_the_last() {
        let f = MsgFile::parse("{1}{}{a}\n{1}{}{b}\n");
        assert!(!f.crlf);
        assert_eq!(f.entries[f.index_by_id()[&1]].text, "b");
    }

    #[test]
    fn broken_records_stop_the_parse() {
        let f = MsgFile::parse("{1}{}{a}\n{2}{}{never closed\n");
        assert_eq!(f.entries.len(), 1);
        assert_eq!(f.warnings, vec!["Malformed record near line 2".to_string()]);
        // a two-field record swallows the next one's id as its text, exactly like the game's reader
        let g = MsgFile::parse("{1}{}{a}\n{2}{b}\n{3}{}{c}\n");
        assert_eq!(g.entries[1].text, "3");
    }

    #[test]
    fn invalid_ids_are_skipped_with_a_warning() {
        let f = MsgFile::parse("{abc}{}{x}\n{4}{}{ok}\n");
        assert_eq!(f.entries.len(), 1);
        assert_eq!(f.entries[0].id, 4);
        assert_eq!(f.warnings, vec!["Invalid number 'abc' near line 1".to_string()]);
        assert_eq!(f.serialize(), "{abc}{}{x}\n{4}{}{ok}\n");
    }

    #[test]
    fn bom_and_non_ascii() {
        let f = MsgFile::parse("\u{FEFF}{1}{}{Déjà « vu »}");
        assert_eq!(f.entries[0].text, "Déjà « vu »");
    }
}
