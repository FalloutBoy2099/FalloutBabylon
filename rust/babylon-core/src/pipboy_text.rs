//! Holodisk texts of game/pipboy.msg.
//!
//! The engine shows one message entry per line of the Pip-Boy screen (no automatic wrapping): a holodisk body is a
//! run of consecutive ids ending with an entry whose text is exactly "**END-DISK**"; "**END-PAR**" entries leave a
//! blank line. The screen is 374 pixels wide and drawn with font1.aaf, so the translator's free text has to be cut
//! into lines no wider than that. These functions do the conversion in both directions.

use crate::text_codec;
use std::collections::BTreeMap;

/// Width of the Pip-Boy text area (PIPBOY_WINDOW_CONTENT_VIEW_WIDTH).
pub const DEFAULT_WIDTH_PX: i32 = 374;
/// Used when the font cannot be read: characters per line.
pub const DEFAULT_FALLBACK_CHARS: i32 = 53;
/// The engine wraps a quest description (preceded by "N. ") at this width.
pub const QUEST_WIDTH_PX: i32 = 350;
/// The engine turns the page after 35 entries.
pub const ENTRIES_PER_PAGE: usize = 35;

pub const END_DISK: &str = "**END-DISK**";
pub const END_PAR: &str = "**END-PAR**";

fn is_single_byte(encoding: &str) -> bool {
    matches!(encoding, "cp1252" | "cp1250" | "cp1251")
}

fn is_blank(s: &str) -> bool {
    s.trim().is_empty()
}

fn first_word(line: &str) -> &str {
    let t = line.trim();
    match t.find(' ') {
        Some(i) => &t[..i],
        None => t,
    }
}

/// Glyph widths of a Fallout interface font (.aaf).
#[derive(Clone)]
pub struct Font {
    valid: bool,
    letter_spacing: i32,
    word_spacing: i32,
    glyph_width: [i32; 256],
}

impl Font {
    pub fn invalid() -> Font {
        Font { valid: false, letter_spacing: 0, word_spacing: 0, glyph_width: [0; 256] }
    }

    /// "AAFF", then big endian: max height, letter spacing, word spacing, line spacing, 256 x (width, height, offset).
    pub fn from_aaf(data: &[u8]) -> Font {
        let mut font = Font::invalid();
        if data.len() < 2060 || &data[..4] != b"AAFF" {
            return font;
        }
        let be16 = |p: usize| i16::from_be_bytes([data[p], data[p + 1]]) as i32;
        font.letter_spacing = be16(6);
        font.word_spacing = be16(8);
        for i in 0..256 {
            font.glyph_width[i] = be16(12 + i * 8);
        }
        font.valid = true;
        font
    }

    pub fn is_valid(&self) -> bool {
        self.valid
    }

    /// Width in pixels of text already encoded in the font's character set.
    pub fn width(&self, encoded: &[u8]) -> i32 {
        encoded
            .iter()
            .map(|&ch| (if ch == b' ' { self.word_spacing } else { self.glyph_width[ch as usize] }) + self.letter_spacing)
            .sum()
    }
}

/// How text is measured: with the real font when the encoding is single byte, otherwise by counting characters.
#[derive(Clone)]
pub struct Measure<'a> {
    pub font: Option<&'a Font>,
    /// Codec id of the target files (`text_codec`).
    pub encoding: String,
    pub max_width: i32,
    pub fallback_chars: i32,
}

impl<'a> Measure<'a> {
    pub fn new(font: Option<&'a Font>, encoding: &str) -> Self {
        Measure { font, encoding: encoding.to_string(), max_width: DEFAULT_WIDTH_PX, fallback_chars: DEFAULT_FALLBACK_CHARS }
    }

    /// True when the real font is used.
    pub fn exact(&self) -> bool {
        self.font.is_some_and(|f| f.is_valid()) && is_single_byte(&self.encoding)
    }

    pub fn width(&self, text: &str) -> i32 {
        if self.exact() {
            return self.font.unwrap().width(&text_codec::encode(text, &self.encoding, None));
        }
        // Without the font: the configured number of characters fills the line.
        (text.encode_utf16().count() as i64 * self.max_width as i64 / self.fallback_chars.max(1) as i64) as i32
    }
}

/// A holodisk body: consecutive ids from `first_id` to the END-DISK entry (included).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Block {
    pub first_id: i32,
    /// The END-DISK entry.
    pub last_id: i32,
}

impl Block {
    pub fn entry_count(&self) -> i32 {
        self.last_id - self.first_id + 1
    }
    pub fn contains(&self, id: i32) -> bool {
        id >= self.first_id && id <= self.last_id
    }
}

/// Blocks found in the texts of a file (id -> text). Runs of ids that do not end with END-DISK are labels.
pub fn find_holodisks(texts: &BTreeMap<i32, String>) -> Vec<Block> {
    let mut blocks = Vec::new();
    let mut run_start = 0;
    let mut previous = 0;
    let mut in_run = false;
    for (&id, text) in texts {
        if !in_run || id != previous + 1 {
            run_start = id;
            in_run = true;
        }
        previous = id;
        if text == END_DISK {
            blocks.push(Block { first_id: run_start, last_id: id });
            run_start = id + 1; // a following disk may start right after
        }
    }
    blocks
}

/// Entries of a body (END-PAR/END-DISK included or not) -> free text: paragraphs separated by a blank line.
/// Lines cut only because the next word did not fit are joined again; other line ends stay hard line breaks.
pub fn reflow(entries: &[String], measure: &Measure) -> String {
    // Split into paragraphs (END-PAR or blank entries), then rebuild each one.
    let mut paragraphs: Vec<Vec<String>> = vec![Vec::new()];
    for entry in entries {
        if entry == END_DISK {
            break;
        }
        if entry == END_PAR || is_blank(entry) {
            if !paragraphs.last().unwrap().is_empty() {
                paragraphs.push(Vec::new());
            }
            continue;
        }
        paragraphs.last_mut().unwrap().push(entry.trim().to_string());
    }
    if paragraphs.last().unwrap().is_empty() {
        paragraphs.pop();
    }

    let mut out: Vec<String> = Vec::new();
    for lines in &paragraphs {
        let mut text = String::new();
        for (i, line) in lines.iter().enumerate() {
            text.push_str(line);
            if i + 1 == lines.len() {
                break;
            }
            // The line was cut by the layout when the next word would not have fitted: join them again.
            let wrapped = measure.width(&format!("{} {}", line, first_word(&lines[i + 1]))) > measure.max_width;
            text.push(if wrapped { ' ' } else { '\n' });
        }
        out.push(text);
    }
    out.join("\n\n")
}

/// Greedy wrap of one hard line.
fn wrap_line(line: &str, m: &Measure) -> Vec<String> {
    let mut lines: Vec<String> = Vec::new();
    let mut current = String::new();
    for word in line.split(' ').filter(|w| !w.is_empty()) {
        let mut word: Vec<char> = word.chars().collect();
        let as_string = |w: &[char]| w.iter().collect::<String>();
        // A word wider than the screen is cut character by character.
        while m.width(&as_string(&word)) > m.max_width && word.len() > 1 {
            let mut cut = word.len() - 1;
            while cut > 1 && m.width(&as_string(&word[..cut])) > m.max_width {
                cut -= 1;
            }
            if !current.is_empty() {
                lines.push(std::mem::take(&mut current));
            }
            lines.push(as_string(&word[..cut]));
            word = word[cut..].to_vec();
        }
        let word = as_string(&word);
        if current.is_empty() {
            current = word;
        } else if m.width(&format!("{current} {word}")) <= m.max_width {
            current.push(' ');
            current.push_str(&word);
        } else {
            lines.push(std::mem::replace(&mut current, word));
        }
    }
    if !current.is_empty() {
        lines.push(current);
    }
    lines
}

/// Free text -> entries, ending with END-DISK, with END-PAR between paragraphs. Every line fits in `max_width`.
pub fn layout(raw_text: &str, measure: &Measure) -> Vec<String> {
    let text = raw_text.replace("\r\n", "\n").replace('\r', "\n");

    // Paragraphs are separated by blank lines.
    let mut paragraphs: Vec<Vec<&str>> = vec![Vec::new()];
    for line in text.split('\n') {
        if is_blank(line) {
            if !paragraphs.last().unwrap().is_empty() {
                paragraphs.push(Vec::new());
            }
        } else {
            paragraphs.last_mut().unwrap().push(line);
        }
    }
    if paragraphs.last().unwrap().is_empty() {
        paragraphs.pop();
    }

    let mut entries: Vec<String> = Vec::new();
    for (p, lines) in paragraphs.iter().enumerate() {
        if p > 0 {
            entries.push(END_PAR.to_string());
        }
        for hard_line in lines {
            entries.extend(wrap_line(hard_line, measure));
        }
    }
    entries.push(END_DISK.to_string());
    entries
}

/// Lines wider than the screen in a list of entries (indexes).
pub fn too_wide(entries: &[String], measure: &Measure) -> Vec<usize> {
    entries
        .iter()
        .enumerate()
        .filter(|(_, e)| e.as_str() != END_PAR && e.as_str() != END_DISK && measure.width(e) > measure.max_width)
        .map(|(i, _)| i)
        .collect()
}

pub fn page_count(entries: usize) -> usize {
    if entries == 0 {
        0
    } else {
        (entries - 1) / ENTRIES_PER_PAGE + 1
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(list: &[&str]) -> Vec<String> {
        list.iter().map(|x| x.to_string()).collect()
    }

    /// font: lower case 6 px, upper case 7 px, others 5 px; letter spacing 1, word spacing 4 (space = 5 px)
    fn synthetic_font() -> Font {
        fn be16(out: &mut Vec<u8>, v: i32) {
            out.extend_from_slice(&(v as i16).to_be_bytes());
        }
        let mut aaf: Vec<u8> = b"AAFF".to_vec();
        be16(&mut aaf, 9);
        be16(&mut aaf, 1);
        be16(&mut aaf, 4);
        be16(&mut aaf, 1);
        for ch in 0..256u32 {
            let w = if (b'a' as u32..=b'z' as u32).contains(&ch) {
                6
            } else if (b'A' as u32..=b'Z' as u32).contains(&ch) {
                7
            } else {
                5
            };
            be16(&mut aaf, w);
            be16(&mut aaf, 8);
            aaf.extend_from_slice(&[0; 4]);
        }
        aaf.extend_from_slice(&[0; 100]);
        Font::from_aaf(&aaf)
    }

    #[test]
    fn layout_with_a_synthetic_font() {
        let font = synthetic_font();
        assert!(font.is_valid());
        assert_eq!(font.width(b"ab"), 14);
        assert_eq!(font.width(b" "), 5);
        assert!(!Font::from_aaf(b"nope").is_valid());

        let mut m = Measure::new(Some(&font), "cp1252");
        m.max_width = 70;
        assert!(m.exact());
        assert_eq!(m.width("aaaa"), 28);

        // greedy wrap: "aaaa bbbb" = 61 px fits, adding " cccc" (94) does not
        assert_eq!(layout("aaaa bbbb cccc dddd", &m), s(&["aaaa bbbb", "cccc dddd", "**END-DISK**"]));
        // blank line = paragraph (END-PAR); single line break = hard break
        assert_eq!(layout("aaaa\n\nbbbb", &m), s(&["aaaa", "**END-PAR**", "bbbb", "**END-DISK**"]));
        assert_eq!(layout("aaaa\r\nbbbb", &m), s(&["aaaa", "bbbb", "**END-DISK**"]));
        assert_eq!(layout("  \n\n", &m), s(&["**END-DISK**"]));
        // a word wider than the screen is cut
        let cut = layout("aaaaaaaaaaaaaaaaaaaa", &m); // 20 x 7 = 140 px
        assert!(cut.len() >= 3);
        assert!(too_wide(&cut, &m).is_empty());

        // reflow undoes layout
        let entries = s(&["aaaa bbbb", "cccc dddd", "**END-PAR**", "eeee", "**END-DISK**"]);
        let text = reflow(&entries, &m);
        assert_eq!(text, "aaaa bbbb cccc dddd\n\neeee");
        assert_eq!(layout(&text, &m), entries);
        // a short line that was not cut by the layout stays a separate line (poems, lists...)
        assert_eq!(reflow(&s(&["Short", "line two", "**END-DISK**"]), &m), "Short\nline two");
        // blank entries separate paragraphs like END-PAR
        assert_eq!(reflow(&s(&["one", " ", "two"]), &m), "one\n\ntwo");

        // wider than the screen
        assert_eq!(too_wide(&s(&["aaaaaaaaaaaaaaaa", "**END-PAR**", "bbb"]), &m), vec![0]);

        // 35 entries per page
        assert_eq!(page_count(35), 1);
        assert_eq!(page_count(36), 2);
        assert_eq!(page_count(0), 0);

        // fallback: characters per line
        let mut f = Measure::new(None, "cp1252");
        f.max_width = 374;
        f.fallback_chars = 53;
        assert!(!f.exact());
        let fifty3 = "a".repeat(53);
        let fifty4 = "a".repeat(54);
        assert!(f.width(&fifty3) <= 374);
        assert!(f.width(&fifty4) > 374);
        let text = format!("{} {} {}", &fifty3[..26], &fifty3[..26], &fifty3[..10]);
        assert_eq!(layout(&text, &f)[0].chars().count(), 53);
    }

    #[test]
    fn finds_holodisks() {
        let texts: BTreeMap<i32, String> = [
            (100, "Back"),
            (101, "More"),
            (300, "x"),
            (301, "y"),
            (302, "**END-DISK**"),
            (303, "z"),
            (304, "**END-DISK**"),
            (400, "label"),
            (401, "**END-PAR**"),
        ]
        .into_iter()
        .map(|(k, v)| (k, v.to_string()))
        .collect();
        let blocks = find_holodisks(&texts);
        assert_eq!(blocks.len(), 2);
        assert_eq!(blocks[0].first_id, 300);
        assert_eq!(blocks[0].last_id, 302);
        assert_eq!(blocks[0].entry_count(), 3);
        assert_eq!(blocks[1].first_id, 303); // a disk may start right after the previous one
        assert!(blocks[1].contains(304));
        assert!(!blocks[1].contains(305));
    }
}
