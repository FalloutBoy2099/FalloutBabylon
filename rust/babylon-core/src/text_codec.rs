//! Legacy encodings used by Fallout localisations, converted to/from Unicode.
//!
//! Single byte code pages are table driven (identical on every platform); double byte ones (Big5, GBK,
//! Shift-JIS, EUC-KR) go through `encoding_rs`.

use std::collections::HashMap;
use std::sync::OnceLock;

/// Upper half (0x80..0xFF) of the supported single byte code pages. 0 = same code point as the byte.
const CP1252: [u16; 128] = [
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
];

const CP1250: [u16; 128] = [
    0x20AC, 0, 0x201A, 0, 0x201E, 0x2026, 0x2020, 0x2021, 0, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
];

const CP1251: [u16; 128] = [
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
];

struct SingleByte {
    to_unicode: [u16; 256],
    from_unicode: HashMap<u16, u8>,
}

impl SingleByte {
    fn new(upper: &[u16; 128]) -> Self {
        let mut to_unicode = [0u16; 256];
        for (i, slot) in to_unicode.iter_mut().enumerate().take(128) {
            *slot = i as u16;
        }
        for i in 0..128 {
            // 0xA0..0xFF of Windows-1252 is Latin-1 (table entries left at 0 map to themselves)
            to_unicode[128 + i] = if upper[i] != 0 { upper[i] } else { (128 + i) as u16 };
        }
        let from_unicode = to_unicode.iter().enumerate().map(|(i, &u)| (u, i as u8)).collect();
        SingleByte { to_unicode, from_unicode }
    }
}

fn single_byte(id: &str) -> Option<&'static SingleByte> {
    static TABLES: OnceLock<[SingleByte; 3]> = OnceLock::new();
    let tables = TABLES.get_or_init(|| [SingleByte::new(&CP1252), SingleByte::new(&CP1250), SingleByte::new(&CP1251)]);
    match id {
        "cp1252" => Some(&tables[0]),
        "cp1250" => Some(&tables[1]),
        "cp1251" => Some(&tables[2]),
        _ => None,
    }
}

struct MultiByte {
    id: &'static str,
    label: &'static str,
    encoding: &'static encoding_rs::Encoding,
}

fn multi_byte_table() -> [MultiByte; 4] {
    [
        MultiByte { id: "big5", label: "Big5 (Traditional Chinese)", encoding: encoding_rs::BIG5 },
        MultiByte { id: "gbk", label: "GBK (Simplified Chinese)", encoding: encoding_rs::GBK },
        MultiByte { id: "shift_jis", label: "Shift-JIS (Japanese)", encoding: encoding_rs::SHIFT_JIS },
        MultiByte { id: "euc-kr", label: "EUC-KR / CP949 (Korean)", encoding: encoding_rs::EUC_KR },
    ]
}

fn multi_byte(id: &str) -> Option<&'static encoding_rs::Encoding> {
    multi_byte_table().iter().find(|m| m.id == id).map(|m| m.encoding)
}

/// An encoding offered to the user.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Info {
    /// Stable identifier stored in project files.
    pub id: String,
    /// Shown to the user.
    pub label: String,
}

pub fn available() -> Vec<Info> {
    let mut list = vec![
        Info { id: "cp1252".into(), label: "CP1252 (Western European)".into() },
        Info { id: "cp1250".into(), label: "CP1250 (Central European)".into() },
        Info { id: "cp1251".into(), label: "CP1251 (Cyrillic)".into() },
    ];
    for m in multi_byte_table() {
        list.push(Info { id: m.id.into(), label: m.label.into() });
    }
    list.push(Info { id: "utf-8".into(), label: "UTF-8".into() });
    list
}

pub fn is_known(id: &str) -> bool {
    available().iter().any(|i| i.id == id)
}

/// Sensible default for a game language directory name ("french" -> "cp1252", "russian" -> "cp1251"...).
pub fn default_for(language: &str) -> &'static str {
    match language.to_lowercase().as_str() {
        "russian" | "ukrainian" | "bulgarian" => "cp1251",
        "polish" | "czech" | "hungarian" | "slovak" | "romanian" => "cp1250",
        "chinese" | "tchinese" | "cht" | "traditional_chinese" => "big5",
        "schinese" | "chs" | "simplified_chinese" => "gbk",
        "japanese" => "shift_jis",
        "korean" => "euc-kr",
        _ => "cp1252",
    }
}

pub fn decode(bytes: &[u8], id: &str) -> String {
    if let Some(sb) = single_byte(id) {
        return bytes
            .iter()
            .map(|&b| char::from_u32(sb.to_unicode[b as usize] as u32).unwrap_or('\u{FFFD}'))
            .collect();
    }
    if id == "utf-8" {
        let b = bytes.strip_prefix(&[0xEF, 0xBB, 0xBF]).unwrap_or(bytes);
        return String::from_utf8_lossy(b).into_owned();
    }
    if let Some(enc) = multi_byte(id) {
        let (text, _) = enc.decode_without_bom_handling(bytes);
        return text.into_owned();
    }
    bytes.iter().map(|&b| b as char).collect() // Latin-1
}

/// Characters the encoding cannot represent are replaced by '?' and collected in `unencodable` (if given).
pub fn encode(text: &str, id: &str, mut unencodable: Option<&mut String>) -> Vec<u8> {
    let mut note = |c: char| {
        if let Some(u) = unencodable.as_deref_mut() {
            if !u.contains(c) {
                u.push(c);
            }
        }
    };

    if let Some(sb) = single_byte(id) {
        let mut out = Vec::with_capacity(text.len());
        for c in text.chars() {
            // The table is over UTF-16 units: characters outside the BMP never match.
            let found = if (c as u32) <= 0xFFFF { sb.from_unicode.get(&(c as u32 as u16)) } else { None };
            match found {
                Some(&b) => out.push(b),
                None => {
                    out.push(b'?');
                    note(c);
                }
            }
        }
        return out;
    }
    if id == "utf-8" {
        return text.as_bytes().to_vec();
    }
    if let Some(enc) = multi_byte(id) {
        let (bytes, _, had_errors) = enc.encode(text);
        if !had_errors {
            return bytes.into_owned();
        }
        // encoding_rs writes numeric references for what it cannot encode: do it character by character.
        let mut out = Vec::with_capacity(text.len());
        let mut buf = [0u8; 4];
        for c in text.chars() {
            let s = c.encode_utf8(&mut buf);
            let (b, _, err) = enc.encode(s);
            if err {
                out.push(b'?');
                note(c);
            } else {
                out.extend_from_slice(&b);
            }
        }
        return out;
    }
    // Latin-1
    text.chars().map(|c| if (c as u32) < 256 { c as u32 as u8 } else { b'?' }).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn single_byte_round_trip() {
        let text = "Déjà vu, œuvre « test » – 5 €";
        let bytes = encode(text, "cp1252", None);
        assert_eq!(decode(&bytes, "cp1252"), text);
        assert_eq!(encode("é", "cp1252", None), vec![0xE9]);
        assert_eq!(decode(&[0x80], "cp1252"), "€");
    }

    #[test]
    fn unencodable_characters_are_reported() {
        let mut bad = String::new();
        let bytes = encode("a≠b≠c", "cp1252", Some(&mut bad));
        assert_eq!(bytes, b"a?b?c");
        assert_eq!(bad, "≠");
    }

    #[test]
    fn cyrillic_and_central_european() {
        let bytes = encode("Привет", "cp1251", None);
        assert_eq!(decode(&bytes, "cp1251"), "Привет");
        let bytes = encode("Zażółć gęślą jaźń", "cp1250", None);
        assert_eq!(decode(&bytes, "cp1250"), "Zażółć gęślą jaźń");
    }

    #[test]
    fn multi_byte_encodings() {
        for (id, text) in [("gbk", "你好，世界"), ("big5", "你好，世界"), ("shift_jis", "こんにちは"), ("euc-kr", "안녕하세요")] {
            let bytes = encode(text, id, None);
            assert_eq!(decode(&bytes, id), text, "{id}");
        }
        let mut bad = String::new();
        let bytes = encode("a\u{1F600}b", "gbk", Some(&mut bad));
        assert!(bytes.contains(&b'?'));
        assert_eq!(bad, "\u{1F600}");
    }

    #[test]
    fn defaults_by_language() {
        assert_eq!(default_for("French"), "cp1252");
        assert_eq!(default_for("russian"), "cp1251");
        assert_eq!(default_for("czech"), "cp1250");
        assert_eq!(default_for("japanese"), "shift_jis");
        assert!(is_known("utf-8") && !is_known("klingon"));
    }
}
