//! Headless tests of the window: the real Slint component with the application logic behind it, driven
//! with synthetic events on the testing backend.

use babylon_app::{app, MainWindow, QualityData, SearchData};
use babylon_core::project::ProjectConfig;
use babylon_core::project_store::ProjectStore;
use slint::platform::{Key, WindowEvent};
use slint::{ComponentHandle, Model, SharedString};
use std::cell::RefCell;
use std::path::Path;
use std::rc::Rc;
use std::time::{Duration, Instant};

fn write(path: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, bytes).unwrap();
}

/// A sequence of steps run from the event loop: a step returns true when it is done, false to be called
/// again (it is waiting for a background job).
struct Script {
    steps: RefCell<Vec<Box<dyn FnMut(&MainWindow) -> bool>>>,
    started: Instant,
}

fn run(window: &MainWindow, steps: Vec<Box<dyn FnMut(&MainWindow) -> bool>>) {
    let script = Rc::new(Script { steps: RefCell::new(steps), started: Instant::now() });
    let weak = window.as_weak();
    let timer = Rc::new(slint::Timer::default());
    let t = timer.clone();
    timer.start(slint::TimerMode::Repeated, Duration::from_millis(15), move || {
        let _keep = &t;
        let w = weak.upgrade().unwrap();
        assert!(script.started.elapsed() < Duration::from_secs(60), "the script timed out");
        loop {
            let mut steps = script.steps.borrow_mut();
            if steps.is_empty() {
                let _ = slint::quit_event_loop();
                return;
            }
            if steps[0](&w) {
                drop(steps.remove(0));
            } else {
                return;
            }
        }
    });
    slint::run_event_loop().unwrap();
}

fn type_text(window: &MainWindow, text: &str) {
    for c in text.chars() {
        let s: SharedString = c.to_string().into();
        window.window().dispatch_event(WindowEvent::KeyPressed { text: s.clone() });
        window.window().dispatch_event(WindowEvent::KeyReleased { text: s });
    }
}

fn press(window: &MainWindow, key: Key) {
    let s: SharedString = key.into();
    window.window().dispatch_event(WindowEvent::KeyPressed { text: s.clone() });
    window.window().dispatch_event(WindowEvent::KeyReleased { text: s });
}

#[test]
fn edit_a_line_and_save() {
    i_slint_backend_testing::init_integration_test_with_system_time();
    let dir = tempfile::tempdir().unwrap();
    let root = dir.path();
    let forward = |p: std::path::PathBuf| p.to_string_lossy().replace('\\', "/");

    write(&root.join("game/text/english/game/pro_item.msg"), b"{100}{}{Leather Armor}\r\n");
    write(&root.join("game/text/french/game/pro_item.msg"), b"{100}{}{Armure en cuir}\r\n");
    write(
        &root.join("game/text/english/dialog/abel.msg"),
        b"{100}{}{He wears Leather Armor.}\r\n{101}{}{Hello there.}\r\n{102}{}{Bye.}\r\n",
    );
    write(&root.join("game/text/english/dialog/bob.msg"), b"{100}{}{Good morning.}\r\n");
    let data = root.join("data");
    let config = ProjectConfig {
        id: "t1".into(),
        name: "Test".into(),
        sources: vec![forward(root.join("game"))],
        output_dir: forward(root.join("out")),
        ..Default::default()
    };
    ProjectStore::new(&data).save(&config).unwrap();
    // the texts are checked in English whatever the language of the system
    write(&data.join("settings.json"), br#"{"uiLanguage":"en","theme":"light"}"#);
    std::env::set_var("BABYLON_DATA_DIR", &data);
    std::env::set_var("BABYLON_OPEN_PROJECT", "Test");
    std::env::set_var("BABYLON_OPEN_FILE", "dialog/abel.msg");

    let window = MainWindow::new().unwrap();
    app::install(&window);
    window.window().dispatch_event(WindowEvent::WindowActiveChanged(true));
    let out_file = root.join("out/text/french/dialog/abel.msg");
    let out_for_check = out_file.clone();

    run(
        &window,
        vec![
            // the project is analysed and the file opened
            Box::new(|w| w.get_has_editor() && !w.get_busy()),
            Box::new(|w| {
                assert_eq!(w.get_rows().row_count(), 3);
                assert_eq!(w.get_source_text(), "He wears Leather Armor.");
                assert_eq!(w.get_target_text(), "");
                assert_eq!(w.get_tabs().row_count(), 1);
                assert!(w.get_tree().row_count() >= 3, "project, group and file are listed");
                w.invoke_focus_target();
                type_text(w, "Il porte une armure");
                true
            }),
            Box::new(|w| {
                assert_eq!(w.get_target_text(), "Il porte une armure");
                let row = w.get_rows().row_data(0).unwrap();
                assert_eq!(row.target, "Il porte une armure");
                assert_eq!(row.status, 1);
                assert_eq!(row.issues, 1, "the glossary term 'Leather Armor' is expected as 'Armure en cuir'");
                assert!(w.get_tabs().row_data(0).unwrap().dirty);
                // the underlines and the alert follow after a short delay
                if w.get_issue_text().is_empty() {
                    return false;
                }
                assert!(w.get_issue_text().contains("Armure en cuir"), "{}", w.get_issue_text());
                // the arrow keys move the caret, not the line; the save writes the file
                w.invoke_save();
                true
            }),
            Box::new(move |w| {
                assert!(!w.get_tabs().row_data(0).unwrap().dirty);
                let written = std::fs::read(&out_for_check).unwrap();
                assert!(String::from_utf8_lossy(&written).contains("{100}{}{Il porte une armure}"));
                // next line, untranslated filter
                w.invoke_row_step(1);
                true
            }),
            Box::new(|w| {
                assert_eq!(w.get_current_row(), 1);
                assert_eq!(w.get_source_text(), "Hello there.");
                // search in the project (the saved file holds the translation of line 100)
                w.set_dock_open(true);
                w.invoke_show_panel(4);
                let search = w.global::<SearchData>();
                search.set_find("armure".into());
                search.invoke_search();
                true
            }),
            Box::new(|w| {
                let search = w.global::<SearchData>();
                if search.get_rows().row_count() == 0 {
                    return false;
                }
                assert_eq!(search.get_rows().row_count(), 2, "the dialogue line and the game file term");
                assert!(search.get_can_replace());
                // the quality report of the saved files: the missing glossary term is reported
                w.invoke_show_panel(5);
                w.global::<QualityData>().invoke_run();
                true
            }),
            Box::new(|w| {
                let quality = w.global::<QualityData>();
                if quality.get_rows().row_count() == 0 {
                    return false;
                }
                assert!(quality.get_status().contains("problem"), "{}", quality.get_status());
                // a second file in a second tab; the context menu of the tabs closes the others
                let tree = w.get_tree();
                let bob = (0..tree.row_count()).find(|&i| tree.row_data(i).unwrap().label == "Bob").expect("bob.msg is listed");
                w.invoke_tree_activated(bob as i32);
                true
            }),
            Box::new(|w| {
                assert_eq!(w.get_tabs().row_count(), 2);
                assert_eq!(w.get_current_tab(), 1);
                assert_eq!(w.get_source_text(), "Good morning.");
                w.invoke_tab_menu(1, 1); // close all but this one
                true
            }),
            Box::new(|w| {
                assert_eq!(w.get_tabs().row_count(), 1);
                assert_eq!(w.get_tabs().row_data(0).unwrap().title, "Bob");
                w.invoke_tab_menu(0, 4); // close all
                true
            }),
            Box::new(|w| {
                assert_eq!(w.get_tabs().row_count(), 0);
                assert!(!w.get_has_editor());
                // the file filter: untouched files only (Bob), not the partly translated one (Abel)
                w.set_tree_filter_index(1);
                w.invoke_tree_filter_changed();
                let labels: Vec<String> = (0..w.get_tree().row_count()).map(|i| w.get_tree().row_data(i).unwrap().label.to_string()).collect();
                assert!(labels.iter().any(|l| l == "Bob"), "{labels:?}");
                assert!(!labels.iter().any(|l| l == "Abel"), "{labels:?}");
                w.set_tree_filter_index(0);
                w.invoke_tree_filter_changed();
                // a panel detached into a window of its own, then docked again
                w.invoke_detach_panel(3);
                assert!(w.get_panel_visible().row_data(3).unwrap());
                assert!(!w.get_panel_docked().row_data(3).unwrap());
                // the dialogs open without a problem
                w.invoke_open_settings();
                w.invoke_mass_translate();
                w.invoke_show_panel(8);
                w.invoke_show_panel(7);
                w.invoke_show_panel(6);
                true
            }),
        ],
    );
    drop(window);
    assert!(out_file.exists());
}
