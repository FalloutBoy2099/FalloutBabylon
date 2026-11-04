//! Texts written in the code of the scripts (see `int_strings`): finding them in a project and writing the
//! translated scripts into the output folder (scripts/<name>.int, which overrides the original).

use crate::error::Result;
use crate::fsutil;
use crate::int_strings;
use crate::paths;
use crate::project::{Project, ScriptTranslation};
use crate::text_codec;
use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};

pub type Translation = ScriptTranslation;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Text {
    /// "abel" (scripts/abel.int)
    pub script: String,
    pub operand: u32,
    /// The original text, as read in the source encoding.
    pub source: String,
    /// Longest translation (in bytes of the target encoding) that fits in place.
    pub capacity: usize,
}

/// The texts shown to the player found in every script of the sources (untouched originals).
pub fn scan(project: &Project, mut progress: Option<&mut dyn FnMut(usize, usize)>, cancel: Option<&AtomicBool>) -> Vec<Text> {
    let mut texts = Vec::new();
    let names = project.script_names();
    for (i, name) in names.iter().enumerate() {
        if cancel.is_some_and(|c| c.load(Ordering::Relaxed)) {
            break;
        }
        if i % 25 == 0 {
            if let Some(p) = progress.as_deref_mut() {
                p(i, names.len());
            }
        }
        let Ok(script) = project.read_script_original(name) else { continue };
        if script.is_empty() {
            continue;
        }
        let table = int_strings::parse(&script);
        if !table.valid {
            continue;
        }
        for e in &table.entries {
            if !table.used_operands.contains(&e.operand) || !int_strings::looks_translatable(&e.text) {
                continue;
            }
            texts.push(Text {
                script: name.clone(),
                operand: e.operand,
                source: text_codec::decode(&e.text, &project.config().source_encoding),
                capacity: e.capacity(),
            });
        }
    }
    if let Some(p) = progress.as_deref_mut() {
        p(names.len(), names.len());
    }
    texts
}

#[derive(Debug, Clone, Default)]
pub struct ApplyResult {
    /// Scripts written.
    pub scripts: usize,
    pub in_place: usize,
    pub enlarged: usize,
    pub problems: Vec<String>,
}

/// Writes scripts/<name>.int in the output folder for every script that has translations (and removes the
/// ones that have none any more). The originals are kept next to the project state.
pub fn apply(project: &Project, translations: &[Translation], allow_enlarge: bool) -> Result<ApplyResult> {
    let mut result = ApplyResult::default();
    let config = project.config();
    let scripts_dir = paths::join(&config.output_dir, "scripts");

    let mut by_script: HashMap<&str, Vec<&Translation>> = HashMap::new();
    for t in translations {
        if !t.target.is_empty() && t.target != t.source {
            by_script.entry(&t.script).or_default().push(t);
        }
    }

    // scripts that were written earlier and have nothing to say any more: back to the original
    if let Some(pristine) = project.pristine_scripts_dir() {
        if let Ok(rd) = std::fs::read_dir(&pristine) {
            for e in rd.flatten() {
                let file = e.file_name().to_string_lossy().into_owned();
                let Some(name) = file.strip_suffix(".int") else { continue };
                if !by_script.contains_key(name) {
                    let _ = std::fs::remove_file(paths::join(&scripts_dir, &file));
                }
            }
        }
    }

    let mut names: Vec<&&str> = by_script.keys().collect();
    names.sort();
    for name in names {
        let list = &by_script[*name];
        let Ok(original) = project.read_script_original(name) else {
            result.problems.push(format!("{name}: script not found"));
            continue;
        };
        let table = int_strings::parse(&original);
        let mut texts: HashMap<u32, Vec<u8>> = HashMap::new();
        for t in list {
            // the string must still be what was translated (the sources may have changed)
            let found = table
                .entries
                .iter()
                .any(|e| e.operand == t.operand && text_codec::decode(&e.text, &config.source_encoding) == t.source);
            if !found {
                let short: String = t.source.chars().take(30).collect();
                result.problems.push(format!("{name}: \"{short}\" is not in the script any more"));
                continue;
            }
            let mut unencodable = String::new();
            texts.insert(t.operand, text_codec::encode(&t.target, &config.target_encoding, Some(&mut unencodable)));
            if !unencodable.is_empty() {
                result.problems.push(format!("{name}: characters not available in {}: {unencodable}", config.target_encoding));
            }
        }
        let patched = int_strings::patch(&original, &texts, allow_enlarge);
        for p in &patched.problems {
            result.problems.push(format!("{name}: {p}"));
        }
        let path = paths::join(&scripts_dir, &format!("{name}.int"));
        if patched.in_place + patched.enlarged == 0 {
            let _ = std::fs::remove_file(&path); // nothing could be written: keep the original
            continue;
        }
        project.keep_pristine_script(name, &original);
        if let Err(e) = fsutil::write_atomic(&path, &patched.data) {
            result.problems.push(format!("{name}: {e}"));
            continue;
        }
        project.register_script_file(name, &path);
        result.scripts += 1;
        result.in_place += patched.in_place;
        result.enlarged += patched.enlarged;
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::int_strings::testing::tiny_script;
    use crate::output_archive;
    use crate::project::ProjectConfig;
    use std::path::Path;

    fn write(path: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, bytes).unwrap();
    }

    #[test]
    fn script_strings_project() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        write(&root.join("m/text/english/dialog/a.msg"), b"{1}{}{Hello}\r\n");
        write(&root.join("m/scripts/sammy.int"), &tiny_script(b"You see Sammy.", b"Hurt dude"));
        let fwd = |p: std::path::PathBuf| p.to_string_lossy().replace('\\', "/");
        let config = ProjectConfig {
            id: "ss".into(),
            name: "ss".into(),
            sources: vec![fwd(root.join("m"))],
            output_dir: fwd(root.join("out")),
            ..Default::default()
        };
        let make = |c: &ProjectConfig| {
            let mut p = Project::new(c.clone());
            p.set_state_file(root.join("state/ss.state.json"));
            p.reload();
            p
        };
        let p = make(&config);

        assert_eq!(p.script_names(), vec!["sammy".to_string()]);
        let texts = scan(&p, None, None);
        assert_eq!(texts.len(), 2);
        assert_eq!(texts[0].source, "You see Sammy.");
        assert_eq!(texts[0].capacity, 15);

        // one text fits, one does not
        let translations = vec![
            Translation { script: "sammy".into(), operand: texts[0].operand, source: texts[0].source.clone(), target: "C'est Sammy.".into() },
            Translation {
                script: "sammy".into(),
                operand: texts[1].operand,
                source: texts[1].source.clone(),
                target: "Il s'est fait mal, le pauvre.".into(),
            },
        ];
        p.set_script_translations(translations.clone());
        let r = apply(&p, &p.script_translations(), false).unwrap();
        assert_eq!(r.scripts, 1);
        assert_eq!(r.in_place, 1);
        assert_eq!(r.problems.len(), 1);
        assert!(root.join("out/scripts/sammy.int").exists());
        assert!(root.join("state/ss.scripts/sammy.int").exists()); // the original is kept
        let t = int_strings::parse(&std::fs::read(root.join("out/scripts/sammy.int")).unwrap());
        assert_eq!(t.entries[0].text, b"C'est Sammy.");
        assert_eq!(t.entries[1].text, b"Hurt dude");

        // the translations survive a reload of the project, and the original is still what is scanned
        let q = make(&config);
        assert_eq!(q.script_translations().len(), 2);
        assert_eq!(q.script_translations()[0].target, "C'est Sammy.");
        assert_eq!(scan(&q, None, None)[0].source, "You see Sammy.");

        // the output archive carries the translated script
        let with_archive = ProjectConfig { output_archive: fwd(root.join("delivery/french.dat")), ..config.clone() };
        let d = make(&with_archive);
        let built = output_archive::build(&d).unwrap();
        assert_eq!(built.scripts, 1);
        let dat = crate::dat_archive::DatArchive::open(&with_archive.output_archive).unwrap();
        assert!(dat.index_of("scripts/sammy.int").is_some());

        // nothing left to say: the translated script goes away
        let r = apply(&p, &[], false).unwrap();
        assert_eq!(r.scripts, 0);
        assert!(!root.join("out/scripts/sammy.int").exists());
    }
}
