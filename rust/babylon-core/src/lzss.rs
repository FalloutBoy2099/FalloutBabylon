//! LZSS decompression of Fallout 1 DAT archives (blocks with a 4096 byte dictionary reset per block).

use crate::error::{Error, Result};

const DICTIONARY: usize = 4096;
const MIN_MATCH: usize = 3;
const MAX_MATCH: usize = 18;

pub fn decompress(source: &[u8], unpacked_size: usize) -> Result<Vec<u8>> {
    // A match of 18 bytes costs 2 bytes plus a flag bit: data cannot expand more than ~8.5 times.
    if unpacked_size as u64 > source.len() as u64 * 10 + 64 {
        return Err(Error::new("The announced size of the file is impossible."));
    }
    let mut out = vec![0u8; unpacked_size];
    let mut dict = [0x20u8; DICTIONARY];
    let src_size = source.len();
    let mut out_pos = 0usize;
    let mut pos = 0usize;

    while pos + 2 <= src_size && out_pos < unpacked_size {
        let block_size = i16::from_be_bytes([source[pos], source[pos + 1]]) as i32;
        pos += 2;
        if block_size == 0 {
            break;
        }
        if block_size < 0 {
            let want = (-block_size) as usize;
            let length = want.min(src_size - pos).min(unpacked_size - out_pos);
            out[out_pos..out_pos + length].copy_from_slice(&source[pos..pos + length]);
            out_pos += length;
            pos += want;
            continue;
        }

        let block_end = (pos + block_size as usize).min(src_size);
        dict.fill(0x20);
        let mut dict_pos = DICTIONARY - MAX_MATCH;

        while pos < block_end && out_pos < unpacked_size {
            let mut flags = source[pos] as u32;
            pos += 1;
            let mut bit = 0;
            while bit < 8 && pos < block_end && out_pos < unpacked_size {
                if flags & 1 != 0 {
                    let value = source[pos];
                    pos += 1;
                    out[out_pos] = value;
                    out_pos += 1;
                    dict[dict_pos % DICTIONARY] = value;
                    dict_pos += 1;
                } else {
                    if pos + 1 >= block_end {
                        pos = block_end;
                        break;
                    }
                    let low = source[pos] as usize;
                    let high = source[pos + 1] as usize;
                    pos += 2;
                    let offset = low | ((high & 0xF0) << 4);
                    let length = (high & 0x0F) + MIN_MATCH;
                    let mut i = 0;
                    while i < length && out_pos < unpacked_size {
                        let value = dict[(offset + i) % DICTIONARY];
                        out[out_pos] = value;
                        out_pos += 1;
                        dict[dict_pos % DICTIONARY] = value;
                        dict_pos += 1;
                        i += 1;
                    }
                }
                bit += 1;
                flags >>= 1;
            }
        }
        pos = block_end;
    }

    if out_pos != unpacked_size {
        return Err(Error::new("The compressed data of the file is truncated."));
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stored_block_is_copied() {
        // a negative block size means "that many raw bytes follow"
        let mut data = (-5i16).to_be_bytes().to_vec();
        data.extend_from_slice(b"hello");
        assert_eq!(decompress(&data, 5).unwrap(), b"hello");
    }

    #[test]
    fn literals_and_matches() {
        // flags 0b0000_0111: three literals 'a' 'b' 'c' (flag bit set), then a match (flag bit clear)
        let mut block = vec![0b0000_0111u8, b'a', b'b', b'c'];
        // the dictionary position starts at 4096-18 = 4078 where the three literals were stored
        let offset = 4078usize;
        block.push((offset & 0xFF) as u8);
        block.push(((offset >> 4) & 0xF0) as u8); // length = 0 + 3
        let mut data = (block.len() as i16).to_be_bytes().to_vec();
        data.extend_from_slice(&block);
        assert_eq!(decompress(&data, 6).unwrap(), b"abcabc");
    }

    #[test]
    fn impossible_size_is_refused() {
        assert!(decompress(&[0, 0], 1_000_000).is_err());
    }
}
