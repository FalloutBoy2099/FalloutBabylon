//! What is remembered between two runs: the window (size, position), the layout (panel, widths) and the files
//! that were open, with the line that was selected.

use super::App;
use crate::MainWindow;
use babylon_core::app_settings::OpenFile;
use slint::{ComponentHandle, LogicalSize, PhysicalPosition, PhysicalSize};

impl App {
    /// Applies the saved size, position and layout to the window (at start).
    pub fn restore_layout(&mut self, window: &MainWindow) {
        let s = &self.settings;
        if s.window_width > 200 && s.window_height > 200 {
            window.window().set_size(LogicalSize::new(s.window_width as f32, s.window_height as f32));
        }
        if let (Some(x), Some(y)) = (s.window_x, s.window_y) {
            // a position from another screen layout could be out of reach
            if (-50..6000).contains(&x) && (-50..4000).contains(&y) {
                window.window().set_position(PhysicalPosition::new(x, y));
            }
        }
        window.set_panel_index(s.panel_index.clamp(0, 8));
        self.panel_mask = if s.dock_open { s.panel_mask } else { 0 };
        self.panel_bottom_mask = s.panel_bottom_mask;
        self.panel_float_mask = s.panel_float_mask;
        window.set_bottom_index(s.bottom_index.clamp(0, 8));
        if s.bottom_height > 0 {
            window.set_bottom_height(s.bottom_height as f32);
        }
        if s.dock_width > 0 {
            window.set_dock_width(s.dock_width as f32);
        }
        if s.tree_width > 0 {
            window.set_tree_width(s.tree_width as f32);
        }
        if s.editor_height > 0 {
            window.set_editor_height(s.editor_height as f32);
        }
    }

    /// Notes the state of the window and of the tabs in the settings (saved by the caller).
    pub fn remember_layout(&mut self) {
        if let Some(w) = self.weak.upgrade() {
            let size = w.window().size().to_logical(w.window().scale_factor());
            // a minimised window reports a tiny size: keep what was saved
            if size.width > 200.0 && size.height > 200.0 {
                self.settings.window_width = size.width as i32;
                self.settings.window_height = size.height as i32;
                let p = w.window().position();
                if p.x > -10000 && p.y > -10000 {
                    self.settings.window_x = Some(p.x);
                    self.settings.window_y = Some(p.y);
                }
            }
            self.settings.panel_index = w.get_panel_index();
            self.settings.dock_open = self.panel_mask != 0;
            self.settings.panel_mask = self.panel_mask;
            self.settings.panel_bottom_mask = self.panel_bottom_mask;
            self.settings.panel_float_mask = self.panel_float_mask;
            self.settings.bottom_index = w.get_bottom_index();
            self.settings.bottom_height = w.get_bottom_height() as i32;
            self.settings.dock_width = w.get_dock_width() as i32;
            self.settings.tree_width = w.get_tree_width() as i32;
            self.settings.editor_height = w.get_editor_height() as i32;
        }
        self.settings.open_files = self
            .editors
            .iter()
            .map(|e| OpenFile {
                project: e.session.clone(),
                rel_path: e.rel_path().to_string(),
                row_id: e.current.map_or(-1, |r| e.model.row(r).id),
            })
            .collect();
        self.settings.current_file = self.current.map_or(-1, |c| c as i32);
    }

    /// Opens again the files of the last run that belong to this project, then brings the right tab to the front.
    pub fn restore_files(&mut self, window: &MainWindow, session_id: &str) {
        if self.pending_open.iter().all(|f| f.project != session_id) {
            return;
        }
        let (mine, rest): (Vec<OpenFile>, Vec<OpenFile>) = std::mem::take(&mut self.pending_open).into_iter().partition(|f| f.project == session_id);
        self.pending_open = rest;
        let front = usize::try_from(self.settings.current_file)
            .ok()
            .and_then(|i| self.settings.open_files.get(i))
            .cloned();
        for f in &mine {
            self.open_file(window, session_id, &f.rel_path);
            if f.row_id >= 0 {
                self.select_id(window, f.row_id);
            }
        }
        // the tab that was in front (when it is one of these)
        if let Some(front) = front.filter(|f| f.project == session_id) {
            if let Some(i) = self.editors.iter().position(|e| e.session == front.project && e.rel_path() == front.rel_path) {
                self.select_tab(window, i);
            }
        }
    }
}

/// Brings a window that is larger than the screen, or outside it, back inside (the saved size may come from
/// another screen layout). Called once the window is on screen, when its real scale is known.
pub fn fit_on_screen(window: &MainWindow) {
    #[cfg(windows)]
    {
        use windows_sys::Win32::UI::WindowsAndMessaging::{
            GetSystemMetrics, SM_CXVIRTUALSCREEN, SM_CYVIRTUALSCREEN, SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN,
        };
        let (vx, vy, vw, vh) = unsafe {
            (
                GetSystemMetrics(SM_XVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_CYVIRTUALSCREEN),
            )
        };
        if vw <= 0 || vh <= 0 || window.window().is_maximized() || window.window().is_fullscreen() {
            return;
        }
        let size = window.window().size();
        let pos = window.window().position();
        let (w, h) = (size.width.min(vw as u32), size.height.min(vh as u32));
        if (w, h) != (size.width, size.height) {
            window.window().set_size(PhysicalSize::new(w, h));
        }
        let x = pos.x.clamp(vx, (vx + vw - w as i32).max(vx));
        let y = pos.y.clamp(vy, (vy + vh - h as i32).max(vy));
        if (x, y) != (pos.x, pos.y) {
            window.window().set_position(PhysicalPosition::new(x, y));
        }
    }
    #[cfg(not(windows))]
    let _ = window;
}
