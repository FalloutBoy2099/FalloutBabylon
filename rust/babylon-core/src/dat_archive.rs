//! Read-only access to a Fallout 1 (big endian, LZSS) or Fallout 2 (little endian, zlib) DAT archive.
//!
//! Immutable once opened; `read`/`read_raw` may be called from several threads (the archive file stays open and
//! reads are serialised: reopening a 300 MB archive for every entry is slow, antivirus software scans each open).

use crate::error::{Error, Result};
use crate::lzss;
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::sync::Mutex;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    Fallout1,
    Fallout2,
}

#[derive(Debug, Clone)]
pub struct DatEntry {
    /// Normalised: lower case, '/' separators.
    pub path: String,
    /// As stored in the archive ('\' separators, original case).
    pub raw_path: String,
    pub compressed: bool,
    pub unpacked_size: u32,
    pub packed_size: u32,
    pub offset: u32,
}

pub struct DatArchive {
    format: Format,
    file_path: PathBuf,
    file: Mutex<File>,
    entries: Vec<DatEntry>,
    index: HashMap<String, usize>,
    data_limit: u64,
}

/// Lower case, '/' separators, no leading separator.
pub fn normalize_path(path: &str) -> String {
    let replaced = path.replace('\\', "/");
    replaced.trim_start_matches('/').to_lowercase()
}

fn latin1(bytes: &[u8]) -> String {
    bytes.iter().map(|&b| b as char).collect()
}

struct Cursor<'a> {
    file: &'a mut File,
}

impl Cursor<'_> {
    fn read_exact(&mut self, n: usize) -> Option<Vec<u8>> {
        let mut buf = vec![0u8; n];
        self.file.read_exact(&mut buf).ok()?;
        Some(buf)
    }
    fn be32(&mut self) -> Option<u32> {
        let b = self.read_exact(4)?;
        Some(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
    }
    fn name(&mut self) -> Option<String> {
        let len = self.read_exact(1)?[0] as usize;
        Some(latin1(&self.read_exact(len)?))
    }
    fn seek(&mut self, pos: u64) {
        let _ = self.file.seek(SeekFrom::Start(pos));
    }
}

impl DatArchive {
    pub fn open(path: impl AsRef<Path>) -> Result<DatArchive> {
        let path = path.as_ref();
        let mut file = File::open(path).map_err(|e| Error::new(e.to_string()))?;
        let length = file.metadata()?.len();
        if length < 16 {
            return Err(Error::new("The file is too small to be a DAT archive."));
        }

        // Fallout 2: the last 8 bytes are [tree size][archive size], and the archive size is the file size.
        let mut footer = [0u8; 8];
        file.seek(SeekFrom::Start(length - 8))?;
        let footer_ok = file.read_exact(&mut footer).is_ok();
        let mut archive = DatArchive {
            format: Format::Fallout2,
            file_path: path.to_path_buf(),
            file: Mutex::new(File::open(path)?),
            entries: Vec::new(),
            index: HashMap::new(),
            data_limit: 0,
        };
        if footer_ok && u32::from_le_bytes([footer[4], footer[5], footer[6], footer[7]]) as u64 == length {
            let tree_size = u32::from_le_bytes([footer[0], footer[1], footer[2], footer[3]]) as u64;
            archive.read_fallout2(&mut file, tree_size, length)?;
            return Ok(archive);
        }

        // Fallout 1: big endian header starting with the directory count.
        archive.format = Format::Fallout1;
        archive.read_fallout1(&mut file)?;
        Ok(archive)
    }

    fn add(&mut self, raw_path: String, compressed: bool, unpacked: u32, packed: u32, offset: u32) {
        let entry = DatEntry {
            path: normalize_path(&raw_path),
            raw_path,
            compressed,
            unpacked_size: unpacked,
            packed_size: packed,
            offset,
        };
        self.index.insert(entry.path.clone(), self.entries.len());
        self.entries.push(entry);
    }

    fn read_fallout2(&mut self, file: &mut File, tree_size: u64, archive_end: u64) -> Result<()> {
        if tree_size < 4 || tree_size + 8 > archive_end {
            return Err(Error::new("The directory tree of the DAT file is invalid."));
        }
        self.data_limit = archive_end - tree_size - 8;
        file.seek(SeekFrom::Start(self.data_limit))?;
        let mut tree = vec![0u8; tree_size as usize];
        file.read_exact(&mut tree).map_err(|_| Error::new("The DAT file is truncated."))?;
        let le32 = |p: usize| u32::from_le_bytes([tree[p], tree[p + 1], tree[p + 2], tree[p + 3]]);
        let file_count = le32(0) as i32;
        let mut pos = 4usize;
        let mut count = 0i64;
        let tree_len = tree_size as usize;
        for _ in 0..file_count.max(0) {
            if pos + 4 > tree_len {
                break;
            }
            let name_length = le32(pos) as i32;
            if name_length <= 0 || pos + 4 + name_length as usize + 13 > tree_len {
                break;
            }
            pos += 4;
            let name = latin1(&tree[pos..pos + name_length as usize]);
            pos += name_length as usize;
            let mut compressed = tree[pos] == 1;
            pos += 1;
            let unpacked = le32(pos);
            let packed = le32(pos + 4);
            let offset = le32(pos + 8);
            pos += 12;
            if !compressed && unpacked != packed {
                compressed = true; // some tools forget the flag
            }
            self.add(name, compressed, unpacked, packed, offset);
            count += 1;
        }
        if count != file_count as i64 {
            return Err(Error::new("The directory tree of the DAT file is damaged."));
        }
        Ok(())
    }

    fn read_fallout1(&mut self, file: &mut File) -> Result<()> {
        self.data_limit = file.metadata()?.len();
        file.seek(SeekFrom::Start(0))?;
        let mut cur = Cursor { file };
        let directory_count = cur.be32().unwrap_or(0);
        if directory_count == 0 || directory_count > 0xFFFFF {
            return Err(Error::new("The file is not a valid DAT archive."));
        }
        cur.seek(16);

        let mut directories: Vec<String> = Vec::new();
        for _ in 0..directory_count {
            let name = cur.name().ok_or_else(|| Error::new("The DAT file is truncated."))?;
            let name = name.trim_start_matches('\\').trim_end_matches('\\').to_string();
            directories.push(if name == "." || name.is_empty() { String::new() } else { format!("{name}\\") });
        }

        for directory in &directories {
            let corrupted = || Error::new("The directory contents of the DAT file are corrupted.");
            let file_count = cur.be32().ok_or_else(corrupted)?;
            cur.be32().ok_or_else(corrupted)?;
            cur.be32().ok_or_else(corrupted)?;
            cur.be32().ok_or_else(corrupted)?;
            if file_count > 0x7FFF_FFFF {
                return Err(corrupted());
            }
            for _ in 0..file_count {
                let truncated = || Error::new("The DAT file is truncated.");
                let name = cur.name().ok_or_else(truncated)?;
                let attributes = cur.be32().ok_or_else(truncated)?;
                let offset = cur.be32().ok_or_else(truncated)?;
                let unpacked = cur.be32().ok_or_else(truncated)?;
                let packed = cur.be32().ok_or_else(truncated)?;
                self.add(format!("{directory}{name}"), attributes == 0x40, unpacked, packed, offset);
            }
        }
        Ok(())
    }

    pub fn format(&self) -> Format {
        self.format
    }

    pub fn file_path(&self) -> &Path {
        &self.file_path
    }

    pub fn entries(&self) -> &[DatEntry] {
        &self.entries
    }

    /// Index of the entry with this normalised path.
    pub fn index_of(&self, normalized_path: &str) -> Option<usize> {
        self.index.get(normalized_path).copied()
    }

    /// Bytes as stored in the archive (used to copy entries without recompressing them).
    pub fn read_raw(&self, index: usize) -> Result<Vec<u8>> {
        let e = self.entries.get(index).ok_or_else(|| Error::new("Invalid entry."))?;
        let size = if e.compressed { e.packed_size } else { e.unpacked_size } as u64;
        if e.offset as u64 + size > self.data_limit {
            return Err(Error::new(format!("The data of '{}' is outside the archive.", e.raw_path)));
        }
        let mut data = vec![0u8; size as usize];
        {
            let mut file = self.file.lock().map_err(|_| Error::new("archive lock poisoned"))?;
            file.seek(SeekFrom::Start(e.offset as u64))?;
            file.read_exact(&mut data).map_err(|_| Error::new("The DAT file is truncated."))?;
        }
        Ok(data)
    }

    /// Decompressed content of an entry.
    pub fn read(&self, index: usize) -> Result<Vec<u8>> {
        let raw = self.read_raw(index)?;
        let e = &self.entries[index];
        if !e.compressed {
            return Ok(raw);
        }
        if raw.is_empty() && e.packed_size != 0 {
            return Ok(Vec::new());
        }
        if self.format == Format::Fallout1 {
            return lzss::decompress(&raw, e.unpacked_size as usize);
        }
        // zlib stream
        let mut out = Vec::with_capacity(e.unpacked_size as usize);
        let mut decoder = flate2::read::ZlibDecoder::new(&raw[..]);
        decoder
            .read_to_end(&mut out)
            .map_err(|_| Error::new(format!("Cannot decompress '{}'.", e.raw_path)))?;
        if out.len() != e.unpacked_size as usize {
            return Err(Error::new(format!("Cannot decompress '{}'.", e.raw_path)));
        }
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths_are_normalised() {
        assert_eq!(normalize_path("\\Text\\English\\Game\\SKILL.MSG"), "text/english/game/skill.msg");
        assert_eq!(normalize_path("/a/B"), "a/b");
    }

    #[test]
    fn small_files_are_refused() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("tiny.dat");
        std::fs::write(&path, b"not a dat").unwrap();
        assert!(DatArchive::open(&path).is_err());
    }
}
