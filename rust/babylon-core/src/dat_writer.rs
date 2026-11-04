//! Writes Fallout 1 (files stored uncompressed) or Fallout 2 (zlib) DAT archives.

use crate::dat_archive::{DatArchive, Format};
use crate::error::{Error, Result};
use std::collections::BTreeMap;
use std::io::Write;
use std::path::Path;

/// One file to put in a new archive: either in-memory data, or an entry copied from an existing archive
/// (copied as-is, without recompression, when both archives use the same format).
pub struct DatWriteItem<'a> {
    /// '\' or '/' separated, case kept.
    pub path: String,
    pub data: Vec<u8>,
    pub source: Option<(&'a DatArchive, usize)>,
}

impl<'a> DatWriteItem<'a> {
    pub fn from_data(path: impl Into<String>, data: Vec<u8>) -> Self {
        DatWriteItem { path: path.into(), data, source: None }
    }
}

struct Prepared {
    bytes: Vec<u8>,
    unpacked: u32,
    packed: u32,
    compressed: bool,
}

fn prepare(item: &DatWriteItem, format: Format) -> Result<Prepared> {
    if let Some((src, index)) = item.source {
        let entry = &src.entries()[index];
        if src.format() == format {
            return Ok(Prepared {
                bytes: src.read_raw(index)?,
                unpacked: entry.unpacked_size,
                packed: entry.packed_size,
                compressed: entry.compressed,
            });
        }
        let data = src.read(index)?;
        return prepare(&DatWriteItem { path: item.path.clone(), data, source: None }, format);
    }

    let unpacked = item.data.len() as u32;
    if format == Format::Fallout2 && !item.data.is_empty() {
        let mut encoder = flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::best());
        encoder.write_all(&item.data)?;
        let z = encoder.finish()?;
        if (z.len() as u32) < unpacked {
            let packed = z.len() as u32;
            return Ok(Prepared { bytes: z, unpacked, packed, compressed: true });
        }
    }
    Ok(Prepared {
        bytes: item.data.clone(),
        unpacked,
        packed: if format == Format::Fallout1 { 0 } else { unpacked },
        compressed: false,
    })
}

fn upper_ascii(s: &str) -> String {
    s.chars().map(|c| if c.is_ascii_lowercase() { c.to_ascii_uppercase() } else { c }).collect()
}

fn latin1_bytes(s: &str) -> Vec<u8> {
    s.chars().map(|c| if (c as u32) < 256 { c as u32 as u8 } else { b'?' }).collect()
}

/// Writes the archive to a temporary file and moves it over `out_path` on success.
pub fn write(out_path: impl AsRef<Path>, format: Format, items: &[DatWriteItem]) -> Result<()> {
    let out_path = out_path.as_ref();
    let dir = out_path.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or_else(|| Path::new("."));
    std::fs::create_dir_all(dir)?;
    let mut tmp = tempfile::Builder::new()
        .prefix(".babylon-dat-")
        .tempfile_in(dir)
        .map_err(|e| Error::new(e.to_string()))?;
    let mut pos: u64 = 0;

    if format == Format::Fallout2 {
        let mut tree: Vec<u8> = Vec::new();
        tree.extend_from_slice(&(items.len() as u32).to_le_bytes());
        for item in items {
            let p = prepare(item, format)?;
            if pos > 0xFFFF_FFFF {
                return Err(Error::new("The archive would exceed 4 GB."));
            }
            tmp.write_all(&p.bytes)?;
            let offset = pos;
            pos += p.bytes.len() as u64;
            let name = latin1_bytes(&item.path.replace('/', "\\"));
            tree.extend_from_slice(&(name.len() as u32).to_le_bytes());
            tree.extend_from_slice(&name);
            tree.push(if p.compressed { 1 } else { 0 });
            tree.extend_from_slice(&p.unpacked.to_le_bytes());
            tree.extend_from_slice(&p.packed.to_le_bytes());
            tree.extend_from_slice(&(offset as u32).to_le_bytes());
        }
        let total = pos + tree.len() as u64 + 8;
        if total > 0xFFFF_FFFF {
            return Err(Error::new("The archive would exceed 4 GB."));
        }
        tmp.write_all(&tree)?;
        tmp.write_all(&(tree.len() as u32).to_le_bytes())?;
        tmp.write_all(&(total as u32).to_le_bytes())?;
        return persist(tmp, out_path);
    }

    // Fallout 1: [header][directory names][directory contents][data]; the index goes last because it holds offsets.
    struct Placed<'a> {
        item: &'a DatWriteItem<'a>,
        name: String,
        prepared: Option<Prepared>,
        offset: u32,
    }
    let mut directories: BTreeMap<String, Vec<Placed>> = BTreeMap::new();
    for item in items {
        let raw = item.path.replace('/', "\\");
        let slash = raw.rfind('\\');
        let mut dir = upper_ascii(slash.map(|s| &raw[..s]).unwrap_or(""));
        if dir.is_empty() {
            dir = ".".to_string();
        }
        let name = upper_ascii(&raw[slash.map(|s| s + 1).unwrap_or(0)..]);
        if name.chars().count() > 255 || dir.chars().count() > 255 {
            return Err(Error::new(format!("Name too long for a Fallout 1 archive: {raw}")));
        }
        directories.entry(dir).or_default().push(Placed { item, name, prepared: None, offset: 0 });
    }

    let mut index_size: u64 = 16;
    for (dir, files) in &directories {
        index_size += 1 + dir.chars().count() as u64 + 16;
        for p in files {
            index_size += 1 + p.name.chars().count() as u64 + 16;
        }
    }
    tmp.write_all(&vec![0u8; index_size as usize])?;
    pos = index_size;

    for files in directories.values_mut() {
        for placed in files.iter_mut() {
            let mut prepared = prepare(placed.item, format)?;
            // Fallout 1 compression is not produced: raw data, no packed size.
            if !prepared.compressed {
                prepared.packed = 0;
            }
            if pos > 0xFFFF_FFFF {
                return Err(Error::new("The archive would exceed 4 GB."));
            }
            placed.offset = pos as u32;
            tmp.write_all(&prepared.bytes)?;
            pos += prepared.bytes.len() as u64;
            placed.prepared = Some(prepared);
        }
    }

    let mut index: Vec<u8> = Vec::new();
    let be = |v: u32| v.to_be_bytes();
    index.extend_from_slice(&be(directories.len() as u32));
    index.extend_from_slice(&be(directories.len() as u32));
    index.extend_from_slice(&be(0));
    index.extend_from_slice(&be(0));
    for dir in directories.keys() {
        index.push(dir.chars().count() as u8);
        index.extend_from_slice(&latin1_bytes(dir));
    }
    for files in directories.values() {
        index.extend_from_slice(&be(files.len() as u32));
        index.extend_from_slice(&be(files.len() as u32));
        index.extend_from_slice(&be(16));
        index.extend_from_slice(&be(0));
        for p in files {
            let prepared = p.prepared.as_ref().expect("prepared");
            index.push(p.name.chars().count() as u8);
            index.extend_from_slice(&latin1_bytes(&p.name));
            index.extend_from_slice(&be(if prepared.compressed { 0x40 } else { 0x20 }));
            index.extend_from_slice(&be(p.offset));
            index.extend_from_slice(&be(prepared.unpacked));
            index.extend_from_slice(&be(prepared.packed));
        }
    }
    if index.len() as u64 != index_size {
        return Err(Error::new("Internal error while building the index."));
    }
    use std::io::{Seek, SeekFrom};
    tmp.seek(SeekFrom::Start(0))?;
    tmp.write_all(&index)?;
    persist(tmp, out_path)
}

fn persist(tmp: tempfile::NamedTempFile, out_path: &Path) -> Result<()> {
    tmp.persist(out_path).map(|_| ()).map_err(|e| Error::new(e.error.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_items() -> Vec<DatWriteItem<'static>> {
        vec![
            DatWriteItem::from_data("text/english/game/skill.msg", b"{100}{}{Small Guns}\r\n".repeat(50)),
            DatWriteItem::from_data("text\\french\\dialog\\abel.msg", b"{1}{}{Bonjour}".to_vec()),
            DatWriteItem::from_data("scripts/empty.int", Vec::new()),
        ]
    }

    #[test]
    fn fallout2_round_trip() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("out.dat");
        write(&path, Format::Fallout2, &sample_items()).unwrap();
        let dat = DatArchive::open(&path).unwrap();
        assert_eq!(dat.format(), Format::Fallout2);
        assert_eq!(dat.entries().len(), 3);
        let i = dat.index_of("text/english/game/skill.msg").unwrap();
        assert!(dat.entries()[i].compressed);
        assert_eq!(dat.read(i).unwrap(), b"{100}{}{Small Guns}\r\n".repeat(50));
        let j = dat.index_of("text/french/dialog/abel.msg").unwrap();
        assert_eq!(dat.read(j).unwrap(), b"{1}{}{Bonjour}");
        let k = dat.index_of("scripts/empty.int").unwrap();
        assert!(dat.read(k).unwrap().is_empty());
    }

    #[test]
    fn fallout1_round_trip() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("out1.dat");
        write(&path, Format::Fallout1, &sample_items()).unwrap();
        let dat = DatArchive::open(&path).unwrap();
        assert_eq!(dat.format(), Format::Fallout1);
        assert_eq!(dat.entries().len(), 3);
        let i = dat.index_of("text/english/game/skill.msg").unwrap();
        assert_eq!(dat.read(i).unwrap(), b"{100}{}{Small Guns}\r\n".repeat(50));
        let j = dat.index_of("text/french/dialog/abel.msg").unwrap();
        assert_eq!(dat.read(j).unwrap(), b"{1}{}{Bonjour}");
    }

    #[test]
    fn entries_are_copied_between_archives_of_the_same_format() {
        let dir = tempfile::tempdir().unwrap();
        let first = dir.path().join("a.dat");
        write(&first, Format::Fallout2, &sample_items()).unwrap();
        let src = DatArchive::open(&first).unwrap();
        let index = src.index_of("text/english/game/skill.msg").unwrap();
        let second = dir.path().join("b.dat");
        let copy = vec![DatWriteItem { path: "x/skill.msg".into(), data: Vec::new(), source: Some((&src, index)) }];
        write(&second, Format::Fallout2, &copy).unwrap();
        let dat = DatArchive::open(&second).unwrap();
        assert_eq!(dat.read(0).unwrap(), b"{100}{}{Small Guns}\r\n".repeat(50));
        // and across formats the content is converted
        let third = dir.path().join("c.dat");
        write(&third, Format::Fallout1, &copy).unwrap();
        let dat1 = DatArchive::open(&third).unwrap();
        assert_eq!(dat1.read(0).unwrap(), b"{100}{}{Small Guns}\r\n".repeat(50));
    }
}
