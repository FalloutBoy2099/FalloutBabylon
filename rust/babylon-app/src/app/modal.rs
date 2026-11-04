//! Dialogs are owned by the main window (so they have no button of their own in the task bar) and modal: the
//! main window is disabled while one of them is open. Windows only; elsewhere they are plain windows.

use crate::MainWindow;
use slint::ComponentHandle;
use std::cell::RefCell;
use std::time::Duration;

struct Open {
    visible: Box<dyn Fn() -> bool>,
}

thread_local! {
    static OPEN: RefCell<Vec<Open>> = const { RefCell::new(Vec::new()) };
    static WATCH: slint::Timer = slint::Timer::default();
}

/// Shows a dialog as a modal window owned by the main window; when it is already on screen, brings it forward.
pub fn present<T: ComponentHandle + 'static>(dialog: &T, owner: &MainWindow) {
    if dialog.window().is_visible() {
        let _ = dialog.show();
        return;
    }
    let _ = dialog.show();
    #[cfg(windows)]
    {
        if let (Some(child), Some(parent)) = (hwnd(dialog.window()), hwnd(owner.window())) {
            native::own_and_disable(child, parent);
            // the window may be resized once its content is laid out: centre again
            slint::Timer::single_shot(Duration::from_millis(60), move || native::center_over(child, parent));
            let weak = dialog.as_weak();
            OPEN.with(|o| o.borrow_mut().push(Open { visible: Box::new(move || weak.upgrade().is_some_and(|d| d.window().is_visible())) }));
            start_watch(owner.as_weak());
        }
    }
}

#[cfg(windows)]
fn hwnd(window: &slint::Window) -> Option<isize> {
    use raw_window_handle::{HasWindowHandle, RawWindowHandle};
    match window.window_handle().window_handle().ok()?.as_raw() {
        RawWindowHandle::Win32(h) => Some(h.hwnd.get()),
        _ => None,
    }
}

/// Re-enables the main window when the last dialog has closed (whatever the way: button, cross, code).
#[cfg(windows)]
fn start_watch(owner: slint::Weak<MainWindow>) {
    WATCH.with(|t| {
        if t.running() {
            return;
        }
        t.start(slint::TimerMode::Repeated, Duration::from_millis(120), move || {
            let none_left = OPEN.with(|o| {
                let mut o = o.borrow_mut();
                o.retain(|d| (d.visible)());
                o.is_empty()
            });
            if none_left {
                WATCH.with(|t| t.stop());
                if let Some(w) = owner.upgrade() {
                    if let Some(h) = hwnd(w.window()) {
                        native::enable_and_focus(h);
                    }
                }
            }
        });
    });
}

#[cfg(windows)]
mod native {
    use windows_sys::Win32::Foundation::{HWND, RECT};
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::{EnableWindow, SetFocus};
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        GetWindowRect, SetForegroundWindow, SetWindowLongPtrW, SetWindowPos, ShowWindow, GWLP_HWNDPARENT, SWP_NOACTIVATE,
        SWP_NOSIZE, SWP_NOZORDER, SW_HIDE, SW_SHOW,
    };

    /// `child` becomes a window owned by `parent` (no task bar button) and `parent` is disabled.
    pub fn own_and_disable(child: isize, parent: isize) {
        unsafe {
            // an owner set on a window that is already shown only takes effect after a hide / show
            ShowWindow(child as HWND, SW_HIDE);
            SetWindowLongPtrW(child as HWND, GWLP_HWNDPARENT, parent);
            // centred while hidden: it never shows up in the wrong place
            center_over(child, parent);
            ShowWindow(child as HWND, SW_SHOW);
            EnableWindow(parent as HWND, 0);
            SetForegroundWindow(child as HWND);
        }
    }

    /// `child` becomes a window owned by `parent` (no task bar button of its own).
    pub fn own(child: isize, parent: isize) {
        unsafe {
            ShowWindow(child as HWND, SW_HIDE);
            SetWindowLongPtrW(child as HWND, GWLP_HWNDPARENT, parent);
            ShowWindow(child as HWND, SW_SHOW);
        }
    }

    /// Puts `child` in the middle of `parent` (both in physical pixels: right at any DPI scaling).
    pub fn center_over(child: isize, parent: isize) {
        unsafe {
            let (mut c, mut p) = (std::mem::zeroed::<RECT>(), std::mem::zeroed::<RECT>());
            if GetWindowRect(child as HWND, &mut c) == 0 || GetWindowRect(parent as HWND, &mut p) == 0 {
                return;
            }
            let (cw, ch) = (c.right - c.left, c.bottom - c.top);
            let x = p.left + ((p.right - p.left) - cw) / 2;
            let y = p.top + ((p.bottom - p.top) - ch) / 2;
            SetWindowPos(child as HWND, std::ptr::null_mut(), x.max(0), y.max(0), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    pub fn enable_and_focus(window: isize) {
        unsafe {
            EnableWindow(window as HWND, 1);
            SetForegroundWindow(window as HWND);
            SetFocus(window as HWND);
        }
    }
}

/// Shows a window owned by the main window: no button of its own in the task bar, not modal.
pub fn show_owned<T: ComponentHandle + 'static>(window: &T, owner: &MainWindow) {
    let _ = window.show();
    #[cfg(windows)]
    {
        if let (Some(child), Some(parent)) = (hwnd(window.window()), hwnd(owner.window())) {
            native::own(child, parent);
        }
    }
    #[cfg(not(windows))]
    let _ = owner;
}

/// A native file picker owned by `owner`: it opens in the middle of it (and the owner is blocked meanwhile).
pub fn picker(owner: &slint::Window) -> rfd::FileDialog {
    rfd::FileDialog::new().set_parent(&owner.window_handle())
}

impl super::App {
    /// A file picker over the dialog that is open (project, import), else over the main window.
    pub fn picker(&self, main: &MainWindow) -> rfd::FileDialog {
        if let Some(s) = &self.project_dialog {
            if s.dialog.window().is_visible() {
                return picker(s.dialog.window());
            }
        }
        if let Some(d) = &self.import.dialog {
            if d.window().is_visible() {
                return picker(d.window());
            }
        }
        picker(main.window())
    }

    pub fn show_owned<T: ComponentHandle + 'static>(&self, window: &T, owner: &MainWindow) {
        show_owned(window, owner);
    }

    /// Shows a dialog as a modal window of the main window.
    pub fn present<T: ComponentHandle + 'static>(&self, dialog: &T) {
        match self.weak.upgrade() {
            Some(owner) => present(dialog, &owner),
            None => {
                let _ = dialog.show();
            }
        }
    }
}
