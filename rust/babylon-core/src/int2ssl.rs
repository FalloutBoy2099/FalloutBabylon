//! Decompilation of compiled scripts with the external `int2ssl` tool (shipped next to the application).

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

const EXE: &str = if cfg!(windows) { "int2ssl.exe" } else { "int2ssl" };

/// Where int2ssl is: `BABYLON_INT2SSL`, next to the application, or in a `tools` folder around it.
pub fn find_executable() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("BABYLON_INT2SSL") {
        if !p.is_empty() && Path::new(&p).exists() {
            return Some(PathBuf::from(p));
        }
    }
    let exe = std::env::current_exe().ok()?;
    let dir = exe.parent()?;
    let mut candidates = vec![dir.join(EXE), dir.join("tools").join(EXE)];
    // development layouts: rust/target/{debug,release}/ and the tools folder of the repository
    let mut up = dir.to_path_buf();
    for _ in 0..4 {
        up = match up.parent() {
            Some(p) => p.to_path_buf(),
            None => break,
        };
        candidates.push(up.join("tools").join(EXE));
    }
    candidates.into_iter().find(|c| c.exists())
}

fn run(exe: &Path, dir: &Path, options: &[&str]) -> Result<String, String> {
    let _ = std::fs::remove_file(dir.join("script.ssl"));
    let mut child = Command::new(exe)
        .current_dir(dir)
        .args(options)
        .args(["script.int", "script.ssl"])
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| e.to_string())?;
    let started = Instant::now();
    let status = loop {
        match child.try_wait() {
            Ok(Some(s)) => break s,
            Ok(None) if started.elapsed() > Duration::from_secs(30) => {
                let _ = child.kill();
                return Err("int2ssl timed out".into());
            }
            Ok(None) => std::thread::sleep(Duration::from_millis(5)),
            Err(e) => return Err(e.to_string()),
        }
    };
    let output = child.wait_with_output().map_err(|e| e.to_string())?;
    let produced = std::fs::read(dir.join("script.ssl"));
    match produced {
        Ok(bytes) if status.success() => Ok(bytes.iter().map(|&b| b as char).collect()),
        _ => {
            // int2ssl prints a banner and progress lines: the useful part is its "Error: ..." line.
            let text: String = output.stdout.iter().chain(output.stderr.iter()).map(|&b| b as char).collect();
            let lines: Vec<&str> = text.split(['\r', '\n']).filter(|l| !l.is_empty()).collect();
            let message = lines
                .iter()
                .rev()
                .find(|l| l.trim().to_lowercase().starts_with("error"))
                .map(|l| l.trim().to_string())
                .or_else(|| lines.last().map(|l| l.trim().to_string()))
                .unwrap_or_default();
            Err(message)
        }
    }
}

/// The SSL source of a compiled script, and whether the Fallout 1 mode of int2ssl was needed.
pub fn decompile(int_data: &[u8]) -> Result<(String, bool), String> {
    let exe = find_executable().ok_or_else(|| "int2ssl not found".to_string())?;
    let dir = tempfile::tempdir().map_err(|_| "cannot create a temporary folder".to_string())?;
    std::fs::write(dir.path().join("script.int"), int_data).map_err(|e| e.to_string())?;

    match run(&exe, dir.path(), &["-a", "-b"]) {
        Ok(ssl) if !ssl.is_empty() => Ok((ssl, false)),
        first => {
            let error = first.err().unwrap_or_default();
            match run(&exe, dir.path(), &["-1", "-a", "-b"]) {
                Ok(ssl) if !ssl.is_empty() => Ok((ssl, true)),
                _ => Err(error),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_tool_is_reported() {
        // only meaningful when the tool is not around; otherwise decompiling garbage must fail cleanly
        let result = decompile(b"not a script");
        assert!(result.is_err());
    }
}
