//! Headless screenshots for visual checks: `BABYLON_SHOT=<file.png>` writes the window after
//! `BABYLON_SHOT_DELAY` milliseconds (default 1500) and quits.

use crate::MainWindow;
use slint::ComponentHandle;
use std::time::Duration;

pub fn schedule(window: &MainWindow) {
    let Ok(path) = std::env::var("BABYLON_SHOT") else { return };
    let delay = std::env::var("BABYLON_SHOT_DELAY").ok().and_then(|v| v.parse().ok()).unwrap_or(1500);
    let weak = window.as_weak();
    if let Ok(kind) = std::env::var("BABYLON_DIALOG") {
        // the dialog is opened half way, so that it has been drawn once when it is captured
        slint::Timer::single_shot(Duration::from_millis(delay / 2), move || crate::app::open_dialog(&kind));
    }
    slint::Timer::single_shot(Duration::from_millis(delay), move || {
        if let Some(w) = weak.upgrade() {
            if let Err(e) = write_png(&w, &path) {
                eprintln!("screenshot failed: {e}");
            }
        }
        let _ = slint::quit_event_loop();
    });
}

fn write_png(window: &MainWindow, path: &str) -> Result<(), String> {
    let snap = match std::env::var("BABYLON_DIALOG") {
        Ok(kind) if !kind.is_empty() => crate::app::dialog_snapshot(&kind).ok_or_else(|| "no such dialog".to_string())?,
        _ => window.window().take_snapshot().map_err(|e| e.to_string())?,
    };
    let mut png = Vec::new();
    {
        let mut enc = png::Encoder::new(&mut png, snap.width(), snap.height());
        enc.set_color(png::ColorType::Rgba);
        enc.set_depth(png::BitDepth::Eight);
        let mut writer = enc.write_header().map_err(|e| e.to_string())?;
        writer.write_image_data(snap.as_bytes()).map_err(|e| e.to_string())?;
    }
    std::fs::write(path, png).map_err(|e| e.to_string())
}
