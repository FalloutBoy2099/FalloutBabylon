//! Small modal-style dialogs: a message and a yes / no question.

use super::{with_app, App};
use crate::{ConfirmDialog, MainWindow, MessageDialog, SaveChangesDialog};
use slint::ComponentHandle;
use std::cell::RefCell;
use std::rc::Rc;

type Action = Box<dyn FnOnce(&mut App, &MainWindow)>;

/// Dialogs currently on screen (kept alive here; a dialog is dropped when the next one replaces it).
#[derive(Default)]
pub(crate) struct Dialogs {
    confirm: Option<ConfirmDialog>,
    message: Option<MessageDialog>,
    save_changes: Option<SaveChangesDialog>,
}

impl App {
    /// Before closing files: when some are modified, offers to save them or to discard the changes.
    /// `then` runs once the files can be closed (immediately when none is modified).
    pub fn confirm_close(&mut self, window: &MainWindow, uids: Vec<u64>, then: impl FnOnce(&mut App, &MainWindow) + 'static) {
        let dirty: Vec<(u64, String)> =
            self.editors.iter().filter(|e| uids.contains(&e.uid) && e.model.is_dirty()).map(|e| (e.uid, e.rel_path().to_string())).collect();
        if dirty.is_empty() {
            then(self, window);
            return;
        }
        let Ok(dialog) = SaveChangesDialog::new() else { return };
        dialog.set_heading(babylon_core::i18n::tr("Unsaved changes").into());
        let names: Vec<&str> = dirty.iter().map(|(_, n)| n.as_str()).collect();
        dialog.set_message(babylon_core::i18n::trf("These files have unsaved changes:\n\n%1", &[&names.join("\n")]).into());
        let uids: Vec<u64> = dirty.iter().map(|(u, _)| *u).collect();
        let then: Rc<RefCell<Option<Action>>> = Rc::new(RefCell::new(Some(Box::new(then))));
        let weak = dialog.as_weak();
        let (save_uids, save_then) = (uids.clone(), then.clone());
        dialog.on_save(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
            let (uids, then) = (save_uids.clone(), save_then.clone());
            with_app(move |app, w| {
                for uid in &uids {
                    if let Some(i) = app.editors.iter().position(|e| e.uid == *uid) {
                        app.save_editor(i);
                    }
                }
                if let Some(action) = then.borrow_mut().take() {
                    action(app, w);
                }
            });
        });
        let weak = dialog.as_weak();
        let discard_then = then.clone();
        dialog.on_discard(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
            let (uids, then) = (uids.clone(), discard_then.clone());
            with_app(move |app, w| {
                for e in app.editors.iter_mut().filter(|e| uids.contains(&e.uid)) {
                    e.model.mark_saved();
                }
                if let Some(action) = then.borrow_mut().take() {
                    action(app, w);
                }
            });
        });
        let weak = dialog.as_weak();
        dialog.on_cancel(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
        });
        self.present(&dialog);
        self.dialogs.save_changes = Some(dialog);
    }

    /// Closes the application, after asking about the modified files.
    pub fn request_quit(&mut self, window: &MainWindow) {
        let uids: Vec<u64> = self.editors.iter().map(|e| e.uid).collect();
        self.confirm_close(window, uids, |app, _| {
            app.closing = true;
            app.flush();
            let _ = slint::quit_event_loop();
        });
    }

    pub fn show_about(&mut self) {
        let text = format!(
            "Fallout Babylon {}\n\n{}\n\n{}",
            env!("CARGO_PKG_VERSION"),
            babylon_core::i18n::tr("Translation editor for the dialogue and text files of Fallout 1 and Fallout 2 mods."),
            babylon_core::i18n::tr("Built with Rust and Slint.")
        );
        self.notify(&babylon_core::i18n::tr("About Fallout Babylon"), &text);
    }

    /// Shows a message with an OK button.
    pub fn notify(&mut self, heading: &str, message: &str) {
        let Ok(dialog) = MessageDialog::new() else { return };
        dialog.set_heading(heading.into());
        dialog.set_message(message.into());
        let weak = dialog.as_weak();
        dialog.on_confirmed(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
        });
        self.present(&dialog);
        self.dialogs.message = Some(dialog);
    }

    /// Asks a yes / no question: `yes` or `no` runs depending on the answer.
    pub fn ask_choice(
        &mut self,
        heading: &str,
        message: &str,
        yes: impl FnOnce(&mut App, &MainWindow) + 'static,
        no: impl FnOnce(&mut App, &MainWindow) + 'static,
    ) {
        let Ok(dialog) = ConfirmDialog::new() else { return };
        dialog.set_heading(heading.into());
        dialog.set_message(message.into());
        let yes: Rc<RefCell<Option<Action>>> = Rc::new(RefCell::new(Some(Box::new(yes))));
        let no: Rc<RefCell<Option<Action>>> = Rc::new(RefCell::new(Some(Box::new(no))));
        fn answer(weak: &slint::Weak<ConfirmDialog>, chosen: &Rc<RefCell<Option<Action>>>, other: &Rc<RefCell<Option<Action>>>) {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
            other.borrow_mut().take();
            if let Some(action) = chosen.borrow_mut().take() {
                with_app(|app, w| action(app, w));
            }
        }
        let weak = dialog.as_weak();
        let (y, n) = (yes.clone(), no.clone());
        dialog.on_confirmed(move || answer(&weak, &y, &n));
        let weak = dialog.as_weak();
        dialog.on_dismissed(move || answer(&weak, &no, &yes));
        self.present(&dialog);
        self.dialogs.confirm = Some(dialog);
    }

    /// Asks a yes / no question; `yes` runs when it is confirmed.
    pub fn ask(&mut self, heading: &str, message: &str, yes: impl FnOnce(&mut App, &MainWindow) + 'static) {
        let Ok(dialog) = ConfirmDialog::new() else { return };
        dialog.set_heading(heading.into());
        dialog.set_message(message.into());
        let action: Rc<RefCell<Option<Action>>> = Rc::new(RefCell::new(Some(Box::new(yes))));
        let weak = dialog.as_weak();
        dialog.on_confirmed(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
            if let Some(action) = action.borrow_mut().take() {
                with_app(|app, w| action(app, w));
            }
        });
        let weak = dialog.as_weak();
        dialog.on_dismissed(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
        });
        self.present(&dialog);
        self.dialogs.confirm = Some(dialog);
    }
}
