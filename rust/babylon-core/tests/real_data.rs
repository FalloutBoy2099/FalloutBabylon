//! Tests on real Fallout data, enabled by environment variables (the installs are only read):
//!
//! * `BABYLON_SAMPLE_DAT`      a master.dat (Fallout 1 or 2): entries are read (`BABYLON_SAMPLE_DAT_LIMIT` of them)
//! * `BABYLON_SAMPLE_FOLDER`   a folder holding text/<language>/**.msg: byte for byte round trip
//! * `BABYLON_SAMPLE_INSTALL`  a Fallout 2 install: project, glossary, statistics, Pip-Boy
//! * `BABYLON_TM_NEW` / `BABYLON_TM_OLD`  two installs: import of the old translation into the new one
//! * `BABYLON_F1_SCRIPTS`      a folder of Fallout 1 .int scripts: script strings and decompilation
//!
//! Run with `cargo test --release -p babylon-core --test real_data -- --nocapture`.

use babylon_core::dat_archive::{DatArchive, Format};
use babylon_core::dialog_graph::DialogGraph;
use babylon_core::int2ssl;
use babylon_core::int_strings;
use babylon_core::msg_file::MsgFile;
use babylon_core::pipboy_text::{self, Font, Measure};
use babylon_core::project::{FileKind, Project, ProjectConfig};
use babylon_core::quality_check;
use babylon_core::statistics::FileStats;
use babylon_core::text_codec;
use babylon_core::translation_index::TranslationIndex;
use babylon_core::translation_memory;
use babylon_core::vfs::VirtualFileSystem;
use std::collections::{BTreeMap, HashMap};
use std::time::Instant;

fn env(name: &str) -> Option<String> {
    std::env::var(name).ok().filter(|v| !v.is_empty())
}

macro_rules! require {
    ($name:expr) => {
        match env($name) {
            Some(v) => v,
            None => {
                eprintln!("skipped: {} not set", $name);
                return;
            }
        }
    };
}

fn config_for(install: &str, id: &str, out: &std::path::Path, target: &str) -> ProjectConfig {
    ProjectConfig {
        id: id.into(),
        name: id.into(),
        sources: VirtualFileSystem::discover_sources(install),
        output_dir: out.to_string_lossy().replace('\\', "/"),
        source_language: "english".into(),
        target_language: target.into(),
        target_encoding: text_codec::default_for(target).into(),
        ..Default::default()
    }
}

#[test]
fn real_dat() {
    let path = require!("BABYLON_SAMPLE_DAT");
    let archive = DatArchive::open(&path).unwrap();
    assert!(!archive.entries().is_empty());
    let compressed = archive.entries().iter().filter(|e| e.compressed).count();
    eprintln!(
        "DAT format {} entries {} compressed {}",
        if archive.format() == Format::Fallout1 { "Fallout 1" } else { "Fallout 2" },
        archive.entries().len(),
        compressed
    );
    let limit = env("BABYLON_SAMPLE_DAT_LIMIT").and_then(|v| v.parse().ok()).filter(|&n| n > 0).unwrap_or(200usize);
    for i in 0..archive.entries().len().min(limit) {
        let data = archive.read(i).unwrap_or_else(|e| panic!("{}: {e}", archive.entries()[i].raw_path));
        assert_eq!(data.len(), archive.entries()[i].unpacked_size as usize, "{}", archive.entries()[i].raw_path);
    }
}

#[test]
fn real_msg_round_trip() {
    let folder = require!("BABYLON_SAMPLE_FOLDER");
    let mut vfs = VirtualFileSystem::new();
    vfs.set_sources(&[folder], None);
    let files = vfs.list("text/", ".msg");
    assert!(!files.is_empty());
    let mut checked = 0;
    for path in &files {
        let bytes = vfs.read(path).unwrap();
        let lang = path.split('/').nth(1).unwrap_or("english");
        let codec = text_codec::default_for(lang);
        let parsed = MsgFile::parse(&text_codec::decode(&bytes, codec));
        assert!(parsed.warnings.is_empty(), "{path}: {:?}", parsed.warnings);
        let crlf = bytes.windows(2).filter(|w| w == b"\r\n").count();
        let lf = bytes.iter().filter(|&&b| b == b'\n').count();
        let mixed = crlf != 0 && crlf != lf;
        if !mixed && !bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
            assert!(text_codec::encode(&parsed.serialize(), codec, None) == bytes, "{path}");
        }
        checked += 1;
    }
    eprintln!("round-tripped {checked} message files");
}

#[test]
fn real_install() {
    let folder = require!("BABYLON_SAMPLE_INSTALL");
    let target = env("BABYLON_SAMPLE_TARGET").unwrap_or_else(|| "french".into());
    let out = tempfile::tempdir().unwrap();
    let c = config_for(&folder, "real", out.path(), &target);

    let t = Instant::now();
    let p = Project::new(c.clone());
    let errors = p.reload();
    eprintln!("sources {:?} errors {:?} open {:?}", c.sources, errors, t.elapsed());
    assert!(errors.is_empty());
    eprintln!("languages {:?}", p.available_languages());

    let files = p.message_files();
    let game = files.iter().filter(|f| Project::kind_of(f) == FileKind::Game).count();
    let dialog = files.iter().filter(|f| Project::kind_of(f) == FileKind::Dialog).count();
    eprintln!("message files {} game {game} dialog {dialog}", files.len());
    assert!(game > 0 && dialog > 0);

    let t = Instant::now();
    let g = p.build_glossary(None);
    eprintln!("glossary terms {} conflicts {} in {:?}", g.terms().len(), g.conflict_count(), t.elapsed());
    assert!(!g.is_empty());

    let t = Instant::now();
    let mut total = FileStats::default();
    p.compute_statistics(Some(&mut |_, _, s: &FileStats| total.add(s)), None);
    eprintln!("stats lines {} untranslated {} words left {} in {:?}", total.lines, total.untranslated, total.words_remaining, t.elapsed());
    assert!(total.lines > 1000);

    // the Pip-Boy: real font1.aaf and pipboy.msg must round-trip (lines -> text -> lines)
    let Some(font_data) = p.read_interface_font(1) else { return };
    let font = Font::from_aaf(&font_data);
    assert!(font.is_valid());
    let doc = p.load_document("game/pipboy.msg").unwrap();
    let texts: BTreeMap<i32, String> = doc.rows.iter().map(|r| (r.id, r.source.clone())).collect();
    let blocks = pipboy_text::find_holodisks(&texts);
    assert!(blocks.len() > 5);
    let m = Measure::new(Some(&font), "cp1252");
    let normalize = |list: &[String]| -> Vec<String> {
        list.iter()
            .map(|e| {
                let s = e.split_whitespace().collect::<Vec<_>>().join(" ");
                if s.is_empty() {
                    pipboy_text::END_PAR.to_string()
                } else {
                    s
                }
            })
            .collect()
    };
    let (mut identical, mut over_wide) = (0, 0);
    for b in &blocks {
        let entries: Vec<String> = (b.first_id..=b.last_id).map(|id| texts.get(&id).cloned().unwrap_or_default()).collect();
        let again = pipboy_text::layout(&pipboy_text::reflow(&entries, &m), &m);
        if normalize(&again) == normalize(&entries) {
            identical += 1;
        }
        if !pipboy_text::too_wide(&again, &m).is_empty() {
            over_wide += 1;
        }
    }
    eprintln!("holodisks {} round trip identical {identical} with a too wide line after layout {over_wide}", blocks.len());
    assert_eq!(over_wide, 0); // layout never exceeds the screen
    assert!(identical * 10 >= blocks.len() * 8); // and rebuilds nearly every original body
}

#[test]
fn real_translation_memory_quality_and_suggestions() {
    let new_install = require!("BABYLON_TM_NEW");
    let old_install = require!("BABYLON_TM_OLD");
    let out = tempfile::tempdir().unwrap();
    let c = config_for(&new_install, "tmreal", out.path(), "french");
    let make = || {
        let mut p = Project::new(c.clone());
        p.set_state_file(out.path().join("state.json"));
        p.reload();
        p
    };
    let p = make();
    let mut before = FileStats::default();
    for s in p.compute_statistics(None, None) {
        before.add(&s);
    }
    let memory = translation_memory::Memory { sources: VirtualFileSystem::discover_sources(&old_install), ..Default::default() };
    let t = Instant::now();
    let report = translation_memory::import(&p, &memory, &translation_memory::Options::default(), None, None);
    eprintln!(
        "memory import {:?}: files {} examined {} exact {} changed {} byText {} kept {} saved {} errors {:?}",
        t.elapsed(),
        report.files,
        report.examined,
        report.exact,
        report.changed,
        report.by_text,
        report.kept,
        report.saved,
        report.errors.iter().take(3).collect::<Vec<_>>()
    );
    let q = make();
    let mut after = FileStats::default();
    for s in q.compute_statistics(None, None) {
        after.add(&s);
    }
    eprintln!("untranslated before {} after {} of {} to review {}", before.untranslated, after.untranslated, after.lines, after.review);
    assert!(after.untranslated < before.untranslated);
    assert!(report.exact > 1000);

    // quality report with its cache, translation memory of the project
    let t = Instant::now();
    let glossary = q.build_glossary(None);
    let options = quality_check::Options::default();
    let cache_file = out.path().join("qa.json");
    let key = quality_check::cache_key(&q, Some(&glossary), &options);
    let mut hits = 0;
    let mut first = quality_check::Cache::open(&cache_file, &key);
    let issues = quality_check::run(&q, Some(&glossary), &options, None, None, Some(&mut first), Some(&mut hits));
    eprintln!("quality first run {:?}: issues {} hits {hits}", t.elapsed(), issues.len());
    assert_eq!(hits, 0);
    let t = Instant::now();
    let mut second = quality_check::Cache::open(&cache_file, &key);
    let again = quality_check::run(&q, Some(&glossary), &options, None, None, Some(&mut second), Some(&mut hits));
    eprintln!("quality cached run {:?}: issues {} hits {hits}", t.elapsed(), again.len());
    assert_eq!(again.len(), issues.len());
    assert!(hits > 100);

    let t = Instant::now();
    let index = TranslationIndex::build(&q, None, None);
    eprintln!("translation memory {:?}: lines {}", t.elapsed(), index.len());
    assert!(index.len() > 1000);
    let t = Instant::now();
    let found = index.find("Welcome to Vault City. How may I help you today?", 5, 0.6, "", -1);
    eprintln!("query {:?}: {} matches", t.elapsed(), found.len());
}

#[test]
fn real_f1_scripts() {
    let folder = require!("BABYLON_F1_SCRIPTS");
    let mut files: Vec<_> = std::fs::read_dir(&folder)
        .unwrap()
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("int")))
        .collect();
    files.sort();
    assert!(!files.is_empty());
    let (mut with_strings, mut candidates, mut outside) = (0, 0, 0);
    for path in &files {
        let bytes = std::fs::read(path).unwrap();
        let t = int_strings::parse(&bytes);
        assert!(t.valid, "{}: {}", path.display(), t.error);
        assert!(t.code_decoded, "{}: code does not decode", path.display());
        let known: std::collections::HashSet<u32> = t.entries.iter().map(|e| e.operand).collect();
        outside += t.used_operands.iter().filter(|o| !known.contains(o)).count();
        if !t.entries.is_empty() {
            with_strings += 1;
        }
        candidates += t.entries.iter().filter(|e| int_strings::looks_translatable(&e.text)).count();
        // an unchanged text leaves the file as it was
        let same: HashMap<u32, Vec<u8>> = t.entries.iter().map(|e| (e.operand, e.text.clone())).collect();
        assert_eq!(int_strings::patch(&bytes, &same, true).data, bytes);
    }
    eprintln!("scripts {} with strings {with_strings} texts {candidates}", files.len());
    assert_eq!(outside, 0); // in the procedure bodies, every push designates a string of the table
    assert!(candidates > 20);

    // end to end with the real decompiler
    let barstow = std::path::Path::new(&folder).join("BARSTOW.INT");
    if barstow.exists() && int2ssl::find_executable().is_some() {
        let original = std::fs::read(&barstow).unwrap();
        let t = int_strings::parse(&original);
        let entry = t
            .entries
            .iter()
            .find(|e| int_strings::looks_translatable(&e.text) && t.used_operands.contains(&e.operand) && e.capacity() >= 10)
            .unwrap();
        let mut texts = HashMap::new();
        texts.insert(entry.operand, b"Bonjour !".to_vec());
        let patched = int_strings::patch(&original, &texts, false);
        assert_eq!(patched.in_place, 1);
        let (before, _) = int2ssl::decompile(&original).unwrap();
        let (after, _) = int2ssl::decompile(&patched.data).unwrap();
        assert!(after.contains("Bonjour !"));
        assert!(!before.contains("Bonjour !"));
    }

    // decompile every script and read its dialogue tree
    if int2ssl::find_executable().is_some() {
        let (mut ok, mut failed, mut with_dialogue) = (0, 0, 0);
        for path in &files {
            match int2ssl::decompile(&std::fs::read(path).unwrap()) {
                Ok((ssl, _)) => {
                    ok += 1;
                    if DialogGraph::parse_ssl(&ssl).has_dialogue() {
                        with_dialogue += 1;
                    }
                }
                Err(_) => failed += 1,
            }
        }
        eprintln!("F1 scripts decompiled {ok} failed {failed} with dialogue {with_dialogue}");
        assert!(ok > files.len() / 2);
        assert!(with_dialogue > 20);
    }
}
