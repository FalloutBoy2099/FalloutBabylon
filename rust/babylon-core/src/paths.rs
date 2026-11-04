//! Small path helpers: the core keeps paths as '/' separated strings (what Qt's `QDir` did).

use std::path::Path;
use std::time::UNIX_EPOCH;

/// Collapses `.`, `..` and repeated separators, '/' separators, no trailing slash (except for a root).
pub fn clean_path(path: &str) -> String {
    let replaced = path.replace('\\', "/");
    let mut prefix = String::new();
    let mut rest: &str = &replaced;
    // "C:" drive prefix or "//server" share
    let bytes = replaced.as_bytes();
    if bytes.len() >= 2 && bytes[1] == b':' && bytes[0].is_ascii_alphabetic() {
        prefix.push_str(&replaced[..2]);
        rest = &replaced[2..];
    }
    let absolute = rest.starts_with('/');
    let mut parts: Vec<&str> = Vec::new();
    for part in rest.split('/') {
        match part {
            "" | "." => {}
            ".." => {
                if matches!(parts.last(), Some(p) if *p != "..") {
                    parts.pop();
                } else if !absolute {
                    parts.push("..");
                }
            }
            p => parts.push(p),
        }
    }
    let mut out = prefix;
    if absolute {
        out.push('/');
    }
    out.push_str(&parts.join("/"));
    if out.is_empty() {
        out.push('.');
    }
    out
}

/// Absolute, cleaned version of a path (symbolic links are not resolved).
pub fn absolute_file_path(path: &str) -> String {
    let p = Path::new(path);
    if p.is_absolute() || path.replace('\\', "/").starts_with('/') {
        return clean_path(path);
    }
    match std::env::current_dir() {
        Ok(cwd) => clean_path(&format!("{}/{}", cwd.to_string_lossy(), path)),
        Err(_) => clean_path(path),
    }
}

/// Last component of a path.
pub fn file_name(path: &str) -> String {
    let p = path.replace('\\', "/");
    p.trim_end_matches('/').rsplit('/').next().unwrap_or("").to_string()
}

/// Folder containing the path ("." when there is none).
pub fn parent_dir(path: &str) -> String {
    let p = clean_path(path);
    match p.rfind('/') {
        Some(0) => "/".to_string(),
        Some(i) => p[..i].to_string(),
        None => ".".to_string(),
    }
}

/// "folder/name" with a single separator.
pub fn join(folder: &str, name: &str) -> String {
    if folder.is_empty() {
        return name.to_string();
    }
    format!("{}/{}", folder.trim_end_matches(['/', '\\']), name.trim_start_matches(['/', '\\']))
}

/// Paths as the user sees them: backslashes on Windows.
pub fn to_native(path: &str) -> String {
    if cfg!(windows) {
        path.replace('/', "\\")
    } else {
        path.to_string()
    }
}

/// Size and modification time (ms since the epoch) of a file, 0 when it does not exist.
pub fn size_and_mtime(path: &str) -> (u64, i64) {
    match std::fs::metadata(path) {
        Ok(m) => {
            let ms = m
                .modified()
                .ok()
                .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
                .map(|d| d.as_millis() as i64)
                .unwrap_or(0);
            (m.len(), ms)
        }
        Err(_) => (0, 0),
    }
}

pub fn is_dir(path: &str) -> bool {
    Path::new(path).is_dir()
}

pub fn is_file(path: &str) -> bool {
    Path::new(path).is_file()
}

pub fn exists(path: &str) -> bool {
    Path::new(path).exists()
}

/// Case-insensitive comparison of two paths after cleaning.
pub fn same_path(a: &str, b: &str) -> bool {
    absolute_file_path(a).to_lowercase() == absolute_file_path(b).to_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cleaning() {
        assert_eq!(clean_path("C:\\Games\\Fallout2\\.\\data\\..\\master.dat"), "C:/Games/Fallout2/master.dat");
        assert_eq!(clean_path("a//b/"), "a/b");
        assert_eq!(clean_path("../x"), "../x");
        assert_eq!(clean_path(""), ".");
        assert_eq!(clean_path("/"), "/");
    }

    #[test]
    fn names_and_parents() {
        assert_eq!(file_name("C:\\a\\b\\french.dat"), "french.dat");
        assert_eq!(parent_dir("C:/a/b/french.dat"), "C:/a/b");
        assert_eq!(join("x/", "/y"), "x/y");
        assert!(same_path("C:\\A\\b", "c:/a/B/"));
    }
}
