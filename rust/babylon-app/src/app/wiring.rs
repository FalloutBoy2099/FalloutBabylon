//! Connects the callbacks of the window to the application state.

use super::editor::filter_names;
use super::{with_app, App, APP};
use crate::{MainWindow, ThemeMode};
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

/// `wire!(window.on_xxx(|app, w, args...| expr))`: the callback runs `expr` on the application state.
macro_rules! wire {
    ($window:expr, $setter:ident, |$app:ident, $w:ident $(, $arg:ident)*| $body:expr) => {
        $window.$setter(move |$($arg),*| {
            with_app(|$app, $w| {
                $body;
            });
        })
    };
}

pub fn install(window: &MainWindow) {
    let mut app = App::new(window);
    super::language::select_language(&app.settings.ui_language);
    app.sync_menu_choices(window);
    window.set_filter_names(ModelRc::new(VecModel::from(filter_names())));
    window.set_tree_filter_names(ModelRc::new(VecModel::from(super::editor::tree_filter_names())));
    if app.settings.window_width > 200 && app.settings.window_height > 200 {
        window.window().set_size(slint::LogicalSize::new(app.settings.window_width as f32, app.settings.window_height as f32));
    }
    App::wire_panels(window);
    window.set_saved_projects(ModelRc::new(VecModel::<SharedString>::default()));

    wire!(window, on_tree_activated, |app, w, i| app.tree_activated(w, i as usize));
    wire!(window, on_tab_selected, |app, w, i| app.select_tab(w, i as usize));
    wire!(window, on_tab_menu, |app, w, i, action| app.tab_menu(w, i as usize, action));
    wire!(window, on_tab_closed, |app, w, i| app.close_tab(w, i as usize));
    wire!(window, on_row_clicked, |app, w, i, ctrl, shift| app.row_clicked(w, i as usize, ctrl, shift));
    wire!(window, on_row_step, |app, w, d| app.step(w, d));
    wire!(window, on_row_page, |app, w, d| app.page(w, d));
    wire!(window, on_sort_requested, |app, w, c| app.sort_by(w, c as usize));
    wire!(window, on_filter_changed, |app, w| app.filter_changed(w));
    wire!(window, on_target_edited, |app, w, text| app.target_edited(w, &text));
    wire!(window, on_target_caret, |app, w, offset| app.target_caret(w, offset.max(0) as usize));
    wire!(window, on_source_caret, |app, w, offset| app.source_caret(w, offset.max(0) as usize));
    wire!(window, on_comment_edited, |app, w, text| app.comment_edited(w, &text));
    wire!(window, on_restore_previous, |app, w| app.restore_previous(w));
    wire!(window, on_info_action, |app, w, i| app.info_action(w, i as usize));

    wire!(window, on_show_panel, |app, w, i| app.show_panel(w, i));
    wire!(window, on_toggle_panel, |app, w, i| app.toggle_panel(w, i));
    wire!(window, on_move_panel, |app, w, i| app.move_panel(w, i));
    wire!(window, on_detach_panel, |app, w, i| app.detach_panel(w, i));
    wire!(window, on_panel_changed, |app, w, _i| app.refresh_panel(w));
    wire!(window, on_new_project, |app, w| app.new_project(w));
    wire!(window, on_open_folder, |app, w| app.open_game_folder(w));
    wire!(window, on_open_recent_folder, |app, w, i| {
        let folder = app.settings.recent_folders.get(i as usize).cloned();
        if let Some(folder) = folder {
            app.open_project_dialog(w, None, Some(folder));
        }
    });
    wire!(window, on_project_settings, |app, w| {
        if let Some(si) = app.active_session() {
            let id = app.sessions[si].id.clone();
            app.edit_project_settings(w, &id);
        }
    });
    wire!(window, on_close_project, |app, w| {
        if let Some(si) = app.active_session() {
            let id = app.sessions[si].id.clone();
            app.close_project(w, &id);
        }
    });
    wire!(window, on_export_dat, |app, w| {
        if let Some(si) = app.active_session() {
            let id = app.sessions[si].id.clone();
            app.export_to_dat(w, &id);
        }
    });
    wire!(window, on_build_archive, |app, w| {
        if let Some(si) = app.active_session() {
            let id = app.sessions[si].id.clone();
            app.build_archive_now(w, &id);
        }
    });
    wire!(window, on_project_menu, |app, w, row, action| app.project_menu(w, row as usize, action));
    wire!(window, on_import_translation, |app, w| app.import_translation(w));
    wire!(window, on_import_report, |app, w| app.import_game_report(w));
    wire!(window, on_follow_report, |app, w| app.toggle_report_watch(w));
    wire!(window, on_language_chosen, |app, w, i| app.set_ui_language(w, i as usize));
    wire!(window, on_tree_filter_changed, |app, w| app.rebuild_tree(w));
    wire!(window, on_about, |app, _w| app.show_about());
    wire!(window, on_translate_line, |app, w| app.translate_current(w));
    wire!(window, on_mass_translate, |app, w| app.open_mass_dialog(w));
    wire!(window, on_open_settings, |app, w| app.open_settings_dialog(w));
    wire!(window, on_save, |app, w| app.save(w));
    wire!(window, on_save_all, |app, w| app.save_all(w));
    wire!(window, on_close_tab, |app, w| app.close_current_tab(w));
    wire!(window, on_refresh, |app, w| {
        let ids: Vec<String> = app.sessions.iter().map(|s| s.id.clone()).collect();
        for id in ids {
            app.start_analysis(w, &id, false);
        }
    });
    wire!(window, on_accept_as_is, |app, w| app.accept_as_is(w));
    wire!(window, on_toggle_review, |app, w| app.toggle_review(w));
    wire!(window, on_toggle_validated, |app, w| app.toggle_validated(w));
    wire!(window, on_copy_source, |app, w| app.copy_source(w));
    wire!(window, on_go_next, |app, w| app.step(w, 1));
    wire!(window, on_go_previous, |app, w| app.step(w, -1));
    wire!(window, on_go_next_untranslated, |app, w| app.go_untranslated(w, true));
    wire!(window, on_go_previous_untranslated, |app, w| app.go_untranslated(w, false));
    wire!(window, on_undo_operation, |app, w| app.undo_operation(w));
    wire!(window, on_redo_operation, |app, w| app.redo_operation(w));
    wire!(window, on_open_saved, |app, w, i| {
        let id = app.saved_ids.get(i as usize).cloned();
        if let Some(id) = id {
            app.open_saved_project(w, &id);
        }
    });
    wire!(window, on_theme_chosen, |app, w, mode| {
        app.settings.theme = match mode {
            1 => "light",
            2 => "dark",
            _ => "system",
        }
        .into();
        app.sync_menu_choices(w);
        // the editors' underline colours depend on the scheme
        app.refresh_overlays(w);
    });
    window.on_quit(|| {
        with_app(|app, w| app.request_quit(w));
    });
    window.window().on_close_requested(|| {
        let close = with_app(|app, w| {
            if app.closing || !app.editors.iter().any(|e| e.model.is_dirty()) {
                app.flush();
                true
            } else {
                app.request_quit(w);
                false
            }
        })
        .unwrap_or(true);
        if close {
            slint::CloseRequestResponse::HideWindow
        } else {
            slint::CloseRequestResponse::KeepWindowShown
        }
    });

    app.restore_layout(window);
    window.global::<ThemeMode>().set_mode(match app.settings.theme.as_str() {
        "light" => 1,
        "dark" => 2,
        _ => 0,
    });
    APP.with(|cell| *cell.borrow_mut() = Some(app));
    {
        let weak = window.as_weak();
        slint::Timer::single_shot(std::time::Duration::from_millis(500), move || {
            if let Some(w) = weak.upgrade() {
                super::layout::fit_on_screen(&w);
            }
        });
    }

    // Projects that were open at the last run, or the one asked for by the environment (tests).
    with_app(|app, w| {
        app.start_report_polling();
        app.refresh_saved_menu(w);
        let mut wanted: Vec<String> = Vec::new();
        if let Ok(name) = std::env::var("BABYLON_OPEN_PROJECT") {
            let configs = app.store.load_all();
            let found = name
                .parse::<usize>()
                .ok()
                .and_then(|i| configs.get(i))
                .or_else(|| configs.iter().find(|c| c.name.eq_ignore_ascii_case(&name) || c.id == name));
            if let Some(c) = found {
                wanted.push(c.id.clone());
            }
        } else {
            wanted = app.settings.open_projects.clone();
        }
        if let Some(panel) = std::env::var("BABYLON_PANEL").ok().and_then(|p| p.parse::<i32>().ok()) {
            app.panel_mask |= 1 << panel.clamp(0, 8);
            w.set_panel_index(panel);
        }
        app.apply_panel_state(w);
        app.settings.open_projects.clear();
        // the files that were open come back once their project is read
        app.pending_open = if std::env::var("BABYLON_OPEN_PROJECT").is_ok() { Vec::new() } else { app.settings.open_files.clone() };
        for id in wanted {
            app.open_saved_project(w, &id);
        }
    });
}

/// Flushes what can be flushed when the application closes.
pub fn shutdown() {
    with_app(|app, _| app.flush());
}

/// Opens a dialog (`project`, `settings`, `mass`) for visual checks.
pub fn open_dialog(kind: &str) {
    with_app(|app, w| match kind {
        "project" => app.new_project(w),
        "settings" => app.open_settings_dialog(w),
        "mass" => app.open_mass_dialog(w),
        "float" => app.detach_panel(w, 1),
        _ => {}
    });
}

/// Screenshot of the dialog opened by `open_dialog`.
pub fn dialog_snapshot(kind: &str) -> Option<slint::SharedPixelBuffer<slint::Rgba8Pixel>> {
    with_app(|app, _| match kind {
        "project" => app.project_dialog.as_ref().and_then(|s| s.dialog.window().take_snapshot().ok()),
        "settings" => app.deepl.settings_snapshot(),
        "mass" => app.mass.snapshot(),
        "float" => app.floats.get(&1).and_then(|f| f.window().take_snapshot().ok()),
        _ => None,
    })
    .flatten()
}
