// no console window for the release build on Windows
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use babylon_app::{app, shot, MainWindow};
use slint::ComponentHandle;

fn main() {
    let window = MainWindow::new().expect("cannot create the main window");
    app::install(&window);
    shot::schedule(&window);
    window.run().expect("event loop failed");
    app::shutdown();
}
