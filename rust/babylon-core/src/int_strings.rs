//! Texts written directly in the code of compiled scripts (`display_msg("...")`, `float_msg(...)`,
//! "You see Sammy."), which do not come from a .msg file. They sit in the static string table of the .int file:
//!
//! ```text
//! [42 bytes of start-up code][procedures][identifiers, ends with 0xFFFFFFFF]
//! [size][entries: u16 length (even, NUL padded), bytes...][0xFFFFFFFF][code]
//! ```
//!
//! The code pushes a string with the opcode 0x9001 and a 4-byte operand: the offset of the characters counted from
//! the size field. A translation that fits the room of the original string is written in place; a longer one
//! can be appended at the end of the file and the pushes redirected to it (optional: the game then reads a
//! string outside of its table, which the engines do not forbid but which is not guaranteed).

use crate::i18n::{tr, trf};
use std::collections::{HashMap, HashSet};

const HEADER_SIZE: usize = 42;
const PUSH_STRING: u16 = 0x9001;
const PUSH_FLOAT: u16 = 0xA001;
const PUSH_INT: u16 = 0xC001;
const FETCH_EXTERNAL: u16 = 0x8014;
const STORE_EXTERNAL: u16 = 0x8015;
const EXPORT_VAR: u16 = 0x8016;

fn u32_at(d: &[u8], pos: usize) -> u32 {
    u32::from_be_bytes([d[pos], d[pos + 1], d[pos + 2], d[pos + 3]])
}

fn u16_at(d: &[u8], pos: usize) -> u16 {
    u16::from_be_bytes([d[pos], d[pos + 1]])
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    /// What the code pushes to designate this string.
    pub operand: u32,
    /// Position of the length field in the file.
    pub entry_pos: usize,
    /// Bytes reserved for the characters (including the NUL padding).
    pub allocated: usize,
    /// The characters, without the NUL padding.
    pub text: Vec<u8>,
}

impl Entry {
    /// Longest text that fits in place.
    pub fn capacity(&self) -> usize {
        self.allocated - 1
    }
}

#[derive(Debug, Clone, Default)]
pub struct Table {
    pub valid: bool,
    pub error: String,
    /// Position of the size field (`None` when the script has no static string).
    pub table_pos: Option<usize>,
    pub table_size: u32,
    /// First byte after the table terminator.
    pub code_pos: usize,
    pub entries: Vec<Entry>,
    /// Start of the procedure bodies (what lies between `code_pos` and it is start-up code).
    pub body_pos: usize,
    /// Operands of the 0x9001 pushes found in the procedure bodies (names excluded).
    pub used_operands: HashSet<u32>,
    /// Position of the operand and its value, for those pushes.
    pub pushes: Vec<(usize, u32)>,
    /// ... and in the start-up code, where they may also designate identifiers.
    pub startup_operands: HashSet<u32>,
    /// The code could be walked up to the end of the file.
    pub code_decoded: bool,
}

pub fn parse(s: &[u8]) -> Table {
    let mut t = Table::default();
    let n = s.len();
    let fail = |mut t: Table, why: &str| {
        t.error = why.to_string();
        t
    };
    if n < HEADER_SIZE + 8 {
        return fail(t, "too short");
    }

    let mut pos = HEADER_SIZE;
    let procedures = u32_at(s, pos) as usize;
    if procedures * 24 + pos + 4 > n {
        return fail(t, "procedure table out of range");
    }
    t.body_pos = n;
    for i in 0..procedures {
        // procedure table: 24 bytes per procedure, body offset in the 5th field
        let body = u32_at(s, pos + 4 + i * 24 + 16) as usize;
        if body != 0 {
            // 0: a procedure declared but not implemented here
            t.body_pos = t.body_pos.min(body);
        }
    }
    pos += 4 + procedures * 24;
    if pos + 4 > n {
        return fail(t, "procedure table out of range");
    }
    let identifiers = u32_at(s, pos) as usize;
    pos += 4 + identifiers;
    if pos + 4 > n || u32_at(s, pos) != 0xFFFF_FFFF {
        return fail(t, "identifier table not terminated");
    }
    pos += 4;

    t.table_size = u32_at(s, pos);
    if t.table_size == 0xFFFF_FFFF {
        // no static string at all
        t.valid = true;
        t.table_size = 0;
        t.code_pos = pos + 4;
        t.table_pos = None;
    } else {
        t.table_pos = Some(pos);
        let mut cur = pos + 4;
        let end = cur + t.table_size as usize;
        if end + 4 > n || u32_at(s, end) != 0xFFFF_FFFF {
            return fail(t, "string table not terminated");
        }
        while cur < end {
            if cur + 2 > end {
                return fail(t, "string entry out of range");
            }
            let len = u16_at(s, cur) as usize;
            if len < 2 || len & 1 == 1 || cur + 2 + len > end {
                return fail(t, "invalid string length");
            }
            let chars = &s[cur + 2..cur + 2 + len];
            let nul = chars.iter().position(|&b| b == 0).unwrap_or(chars.len());
            t.entries.push(Entry {
                entry_pos: cur,
                allocated: len,
                operand: (cur + 2 - pos) as u32, // counted from the size field
                text: chars[..nul].to_vec(),
            });
            cur += 2 + len;
        }
        t.code_pos = end + 4;
        t.valid = true;
    }

    // Walk the code: every opcode is 2 bytes, the three pushes carry a 4-byte operand.
    let mut cur = t.code_pos;
    let mut clean = true;
    let mut previous_is_body_string = false; // the previous instruction was a string push of a procedure body
    while cur < n {
        if cur + 2 > n {
            clean = false;
            break;
        }
        let op_pos = cur;
        let op = u16_at(s, cur);
        cur += 2;
        if op == PUSH_STRING || op == PUSH_FLOAT || op == PUSH_INT {
            if cur + 4 > n {
                clean = false;
                break;
            }
            previous_is_body_string = false;
            if op == PUSH_STRING {
                if op_pos < t.body_pos {
                    t.startup_operands.insert(u32_at(s, cur));
                } else {
                    t.pushes.push((cur, u32_at(s, cur)));
                    previous_is_body_string = true;
                }
            }
            cur += 4;
        } else {
            // The name of an external variable is pushed like a string but designates the identifier table.
            if previous_is_body_string && matches!(op, FETCH_EXTERNAL | STORE_EXTERNAL | EXPORT_VAR) {
                t.pushes.pop();
            }
            previous_is_body_string = false;
        }
    }
    t.used_operands = t.pushes.iter().map(|p| p.1).collect();
    t.code_decoded = clean;
    t
}

/// Does this look like text shown to the player (and not an internal name or a debug label)?
pub fn looks_translatable(text: &[u8]) -> bool {
    if text.len() < 6 {
        return false;
    }
    let (mut letters, mut spaces, mut other) = (0, 0, 0);
    for &c in text {
        if c.is_ascii_alphabetic() || c >= 0xC0 {
            letters += 1;
        } else if c == b' ' {
            spaces += 1;
        } else if c < 0x20 || matches!(c, b'/' | b'\\' | b'_' | b'=' | b'{' | b'}' | b'@') {
            other += 1;
        }
    }
    if spaces == 0 || letters < 4 || other > 0 {
        return false;
    }
    letters * 10 >= text.len() * 6 // mostly letters
}

#[derive(Debug, Clone, Default)]
pub struct PatchResult {
    /// The patched script (the input when nothing could be done).
    pub data: Vec<u8>,
    pub in_place: usize,
    pub enlarged: usize,
    /// Strings left untouched, with the reason.
    pub problems: Vec<String>,
}

/// Replaces strings, by operand. `texts` holds the final bytes (already in the encoding of the game).
pub fn patch(script: &[u8], texts: &HashMap<u32, Vec<u8>>, allow_enlarge: bool) -> PatchResult {
    let mut result = PatchResult { data: script.to_vec(), ..Default::default() };
    let table = parse(script);
    if !table.valid {
        result.problems.push(trf("not a readable script (%1)", &[&table.error]));
        return result;
    }

    let mut data = script.to_vec();
    let mut redirect: HashMap<u32, u32> = HashMap::new(); // old operand -> new operand (strings moved to the end)
    let label = |e: &Entry| e.text.iter().take(30).map(|&b| b as char).collect::<String>();
    for e in &table.entries {
        let Some(text) = texts.get(&e.operand) else { continue };
        if *text == e.text {
            continue;
        }
        if text.contains(&0) {
            result.problems.push(tr("a text contains a NUL character"));
            continue;
        }
        if text.len() <= e.capacity() {
            let mut chars = text.clone();
            chars.resize(e.allocated, 0);
            data[e.entry_pos + 2..e.entry_pos + 2 + e.allocated].copy_from_slice(&chars);
            result.in_place += 1;
            continue;
        }
        if !allow_enlarge {
            result.problems.push(trf("\"%1\": %2 characters for %3 available", &[&label(e), &text.len().to_string(), &e.capacity().to_string()]));
            continue;
        }
        if table.startup_operands.contains(&e.operand) {
            result.problems.push(trf("\"%1\": used by the start-up code of the script, the text cannot be lengthened", &[&label(e)]));
            continue;
        }
        if !table.code_decoded {
            result.problems.push(trf("\"%1\": the code cannot be read safely, the text cannot be lengthened", &[&label(e)]));
            continue;
        }
        // a new entry at the end of the file; its length is even, with at least one NUL
        let allocated = (text.len() + 2) & !1;
        let entry_pos = data.len();
        data.extend_from_slice(&(allocated as u16).to_be_bytes());
        data.extend_from_slice(text);
        data.resize(entry_pos + 2 + allocated, 0);
        let table_pos = table.table_pos.unwrap_or(0);
        redirect.insert(e.operand, (entry_pos + 2 - table_pos) as u32);
        result.enlarged += 1;
    }

    // the pushes of the moved strings now designate the new place
    for &(pos, operand) in &table.pushes {
        if let Some(&new) = redirect.get(&operand) {
            data[pos..pos + 4].copy_from_slice(&new.to_be_bytes());
        }
    }
    result.data = data;
    result
}

#[cfg(test)]
pub(crate) mod testing {
    /// A tiny compiled script: one procedure, no identifier, two strings, code that pushes both.
    pub fn tiny_script(first: &[u8], second: &[u8]) -> Vec<u8> {
        let entry = |text: &[u8]| {
            let allocated = (text.len() + 2) & !1;
            let mut e = (allocated as u16).to_be_bytes().to_vec();
            e.extend_from_slice(text);
            e.resize(2 + allocated, 0);
            e
        };
        let be32 = |v: u32| v.to_be_bytes().to_vec();
        let be16 = |v: u16| v.to_be_bytes().to_vec();
        let mut table = entry(first);
        table.extend(entry(second));
        let body_offset = (42 + 4 + 24 + 4 + 4 + 4 + table.len() + 4) as u32;
        let mut out = vec![0u8; 42];
        out.extend(be32(1)); // one procedure: name, type, time, condition, body offset, arguments
        for v in [0, 0, 0, 0, body_offset, 0] {
            out.extend(be32(v));
        }
        out.extend(be32(0)); // identifiers (empty)
        out.extend(be32(0xFFFF_FFFF));
        out.extend(be32(table.len() as u32));
        out.extend(&table);
        out.extend(be32(0xFFFF_FFFF));
        let first_operand = 4 + 2;
        let second_operand = 4 + entry(first).len() as u32 + 2;
        for (op, operand) in [(0x9001, first_operand), (0x9001, second_operand), (0x9001, first_operand)] {
            out.extend(be16(op));
            out.extend(be32(operand));
        }
        out.extend(be16(0x8019)); // some operator without operand
        out
    }
}

#[cfg(test)]
mod tests {
    use super::testing::tiny_script;
    use super::*;

    #[test]
    fn int_strings_parse_and_patch() {
        let script = tiny_script(b"You see Sammy.", b"Hurt dude");
        let t = parse(&script);
        assert!(t.valid, "{}", t.error);
        assert!(t.code_decoded);
        assert_eq!(t.entries.len(), 2);
        assert_eq!(t.entries[0].text, b"You see Sammy.");
        assert_eq!(t.entries[0].capacity(), 15); // 14 characters + NUL + padding = 16 bytes
        assert_eq!(t.entries[1].text, b"Hurt dude");
        assert_eq!(t.used_operands.len(), 2);
        assert!(t.used_operands.contains(&t.entries[0].operand) && t.used_operands.contains(&t.entries[1].operand));
        assert!(looks_translatable(b"You see Sammy."));
        assert!(!looks_translatable(b"obj_name"));
        assert!(!looks_translatable(b"art/critters/hmwarr"));

        // fits in place: the file keeps its size and its layout
        let mut texts: HashMap<u32, Vec<u8>> = HashMap::new();
        texts.insert(t.entries[0].operand, b"Vous voyez Sammy.".to_vec()); // 17 characters: too long for 15
        let r = patch(&script, &texts, false);
        assert_eq!(r.data, script);
        assert_eq!(r.problems.len(), 1);
        texts.insert(t.entries[0].operand, b"C'est Sammy.".to_vec());
        let r = patch(&script, &texts, false);
        assert_eq!(r.in_place, 1);
        assert_eq!(r.data.len(), script.len());
        let t2 = parse(&r.data);
        assert!(t2.valid);
        assert_eq!(t2.entries[0].text, b"C'est Sammy.");
        assert_eq!(t2.entries[1].text, b"Hurt dude");
        assert_eq!(t2.used_operands.len(), 2);

        // longer: appended at the end, both pushes of that string redirected
        let mut texts: HashMap<u32, Vec<u8>> = HashMap::new();
        texts.insert(t.entries[0].operand, b"Vous voyez Sammy, la fille du forgeron.".to_vec());
        let r = patch(&script, &texts, true);
        assert_eq!(r.enlarged, 1);
        assert!(r.data.len() > script.len());
        let moved = parse(&r.data);
        assert!(moved.valid && moved.code_decoded);
        assert_eq!(moved.entries.len(), 2); // the table itself is unchanged
        assert_eq!(moved.used_operands.len(), 2); // both pushes of the first string moved together
        let new_operand = *moved.used_operands.iter().find(|&&op| op != moved.entries[1].operand).unwrap();
        assert!(new_operand > moved.entries[1].operand);
        // the new entry sits right at the end of the file and is read like any other
        let pos = moved.table_pos.unwrap() + new_operand as usize;
        assert_eq!(&r.data[pos..pos + 39], b"Vous voyez Sammy, la fille du forgeron.");
        assert_eq!(r.data[pos + 39], 0);
    }
}
