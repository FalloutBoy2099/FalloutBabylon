//! The glossary panel: terms of the game files plus the project's own terms.

use super::{table_model, table_row};
use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::{MainWindow, TermDialog, TypeOption};
use babylon_core::custom_glossary::{self, CustomTerm};
use babylon_core::glossary::GlossaryTerm;
use babylon_core::i18n::{tr, trf};
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};

#[derive(Default)]
pub(crate) struct GlossaryView {
    /// Indices in `Glossary::terms()` in display order.
    order: Vec<usize>,
    sort_column: usize,
    /// Types (categories) that are switched off in the filter.
    types_off: std::collections::HashSet<String>,
    /// Types of the glossary, in the order of the filter list.
    types: Vec<String>,
    descending: bool,
    dialog: Option<TermDialog>,
}

fn target_text(t: &GlossaryTerm) -> String {
    if t.targets.is_empty() {
        String::new()
    } else {
        format!("{}{}", t.targets.join(" / "), if t.conflict() { "  ⚠" } else { "" })
    }
}

fn origin_text(t: &GlossaryTerm) -> String {
    let first = t.origins.first().cloned().unwrap_or_default();
    if t.origins.len() > 1 {
        format!("{first} +{}", t.origins.len() - 1)
    } else {
        first
    }
}

fn column_text(t: &GlossaryTerm, column: usize) -> String {
    match column {
        0 => t.source.to_lowercase(),
        1 => t.targets.join(" / ").to_lowercase(),
        2 => t.category.to_lowercase(),
        _ => t.origins.first().cloned().unwrap_or_default().to_lowercase(),
    }
}

pub(super) fn localize(host: &Host) {
    let data = host.glossary();
    data.set_menu_actions(ModelRc::new(VecModel::from(
        [
            "Edit / override this term...",
            "Keep this term as is (do not translate)",
            "Delete this term",
            "Insert the translation",
            "Copy the term",
            "Copy the translation",
        ]
        .iter()
        .map(|s| SharedString::from(tr(s)))
        .collect::<Vec<_>>(),
    )));
}

pub(super) fn wire(host: &Host) {
    let data = host.glossary();
    data.on_filter_changed(|| {
        with_app(|app, w| app.refresh_glossary_panel(w));
    });
    data.on_type_toggled(|i| {
        with_app(|app, w| {
            if let Some(t) = app.glossary_view.types.get(i as usize).cloned() {
                if !app.glossary_view.types_off.remove(&t) {
                    app.glossary_view.types_off.insert(t);
                }
                app.refresh_glossary_panel(w);
            }
        });
    });
    data.on_types_all(|all| {
        with_app(|app, w| {
            app.glossary_view.types_off = if all { Default::default() } else { app.glossary_view.types.iter().cloned().collect() };
            app.refresh_glossary_panel(w);
        });
    });
    data.on_header_clicked(|column| {
        with_app(|app, w| {
            let c = column as usize;
            if app.glossary_view.sort_column == c {
                app.glossary_view.descending = !app.glossary_view.descending;
            } else {
                app.glossary_view.sort_column = c;
                app.glossary_view.descending = false;
            }
            app.refresh_glossary_panel(w);
        });
    });
    data.on_activated(|i| {
        with_app(|app, w| app.glossary_insert(w, i as usize));
    });
    data.on_add(|| {
        with_app(|app, w| app.glossary_edit_dialog(w, None));
    });
    data.on_edit(|| {
        with_app(|app, w| {
            let current = app.host_of(super::GLOSSARY, w).glossary().get_current();
            app.glossary_edit_selected(w, current);
        });
    });
    data.on_remove(|| {
        with_app(|app, w| {
            let current = app.host_of(super::GLOSSARY, w).glossary().get_current();
            app.glossary_delete(w, current);
        });
    });
    data.on_import_terms(|| {
        with_app(|app, w| app.glossary_import(w));
    });
    data.on_export_terms(|| {
        with_app(|app, w| app.glossary_export(w));
    });
    data.on_menu_activated(|row, action| {
        with_app(|app, w| match action {
            0 => app.glossary_edit_selected(w, row),
            1 => app.glossary_keep(w, row),
            2 => app.glossary_delete(w, row),
            3 => app.glossary_insert(w, row as usize),
            4 => app.glossary_copy(row, false),
            5 => app.glossary_copy(row, true),
            _ => {}
        });
    });
}

impl App {
    fn glossary_term_at(&self, row: i32) -> Option<GlossaryTerm> {
        let si = self.active_session()?;
        let index = *self.glossary_view.order.get(usize::try_from(row).ok()?)?;
        self.sessions[si].project.glossary().terms().get(index).cloned()
    }

    pub fn refresh_glossary_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::GLOSSARY, window);
        let data = host.glossary();
        let Some(si) = self.active_session() else {
            data.set_rows(table_model(Vec::new()));
            data.set_summary(tr("No project open.").into());
            data.set_editable(false);
            self.glossary_view.order.clear();
            return;
        };
        let session = &self.sessions[si];
        if session.busy && session.glossary_terms == 0 {
            data.set_summary(tr("Building the glossary...").into());
        }
        let glossary = session.project.glossary();
        let terms = glossary.terms();
        let filter = data.get_filter().to_lowercase();
        // the types that exist, for the multiple choice
        let mut types: Vec<String> = terms.iter().map(|t| t.category.clone()).collect::<std::collections::BTreeSet<_>>().into_iter().collect();
        types.retain(|t| !t.is_empty());
        let off = &self.glossary_view.types_off;
        let shown_types = types.iter().filter(|t| !off.contains(*t)).count();
        data.set_types(slint::ModelRc::new(slint::VecModel::from(
            types.iter().map(|t| TypeOption { name: t.as_str().into(), checked: !off.contains(t) }).collect::<Vec<_>>(),
        )));
        data.set_types_label(
            if shown_types == types.len() { tr("All types") } else { trf("Types (%1/%2)", &[&shown_types.to_string(), &types.len().to_string()]) }.into(),
        );
        self.glossary_view.types = types;
        let off = self.glossary_view.types_off.clone();
        let mut order: Vec<usize> = (0..terms.len())
            .filter(|&i| {
                if off.contains(&terms[i].category) {
                    return false;
                }
                if filter.is_empty() {
                    return true;
                }
                let t = &terms[i];
                t.source.to_lowercase().contains(&filter)
                    || t.targets.iter().any(|x| x.to_lowercase().contains(&filter))
                    || t.category.to_lowercase().contains(&filter)
                    || t.origins.iter().any(|x| x.to_lowercase().contains(&filter))
            })
            .collect();
        let (column, descending) = (self.glossary_view.sort_column, self.glossary_view.descending);
        order.sort_by(|&a, &b| {
            let ord = column_text(&terms[a], column).cmp(&column_text(&terms[b], column));
            if descending {
                ord.reverse()
            } else {
                ord
            }
        });
        let rows = order
            .iter()
            .map(|&i| {
                let t = &terms[i];
                let mut row = table_row(vec![t.source.clone(), target_text(t), t.category.clone(), origin_text(t)]);
                row.bold = t.custom;
                row.muted = t.targets.is_empty();
                row.warn = t.conflict();
                row
            })
            .collect();
        data.set_rows(table_model(rows));
        data.set_current(-1);
        data.set_sort_column(column as i32);
        data.set_sort_ascending(!descending);
        data.set_editable(true);
        let conflicts = terms.iter().filter(|t| t.conflict()).count();
        let untranslated = terms.iter().filter(|t| t.targets.is_empty()).count();
        data.set_summary(
            if terms.is_empty() {
                tr("No term yet. The glossary is built from the /game message files.")
            } else {
                trf(
                    "%1 terms · %2 without translation · %3 inconsistent",
                    &[&terms.len().to_string(), &untranslated.to_string(), &conflicts.to_string()],
                )
            }
            .into(),
        );
        self.glossary_view.order = order;
    }

    fn glossary_insert(&mut self, window: &MainWindow, row: usize) {
        if let Some(term) = self.glossary_term_at(row as i32) {
            if let Some(first) = term.targets.first() {
                self.insert_in_target(window, first);
            }
        }
    }

    fn glossary_copy(&self, row: i32, translation: bool) {
        let Some(term) = self.glossary_term_at(row) else { return };
        let text = if translation { term.targets.first().cloned().unwrap_or_default() } else { term.source };
        if let Ok(mut clipboard) = arboard::Clipboard::new() {
            let _ = clipboard.set_text(text);
        }
    }

    fn custom_terms_of_active(&self) -> Option<(usize, Vec<CustomTerm>)> {
        let si = self.active_session()?;
        Some((si, self.sessions[si].project.custom_terms()))
    }

    /// Stores the project's own terms and rebuilds the glossary with them.
    fn glossary_commit(&mut self, window: &MainWindow, si: usize, terms: Vec<CustomTerm>) {
        let id = self.sessions[si].id.clone();
        self.sessions[si].project.set_custom_terms(terms);
        self.sessions[si].project.save_state();
        self.start_analysis(window, &id, true);
    }

    fn glossary_edit_selected(&mut self, window: &MainWindow, row: i32) {
        let Some(term) = self.glossary_term_at(row) else {
            self.set_status(window, tr("Select a term first."));
            return;
        };
        // a term of the game files becomes a term of the project that overrides it
        let initial = CustomTerm {
            source: term.source.clone(),
            target: term.targets.first().cloned().unwrap_or_default(),
            keep: term.keep,
        };
        self.glossary_edit_dialog(window, Some((initial, !term.custom)));
    }

    /// `initial`: the term to edit and whether it is new for the project.
    fn glossary_edit_dialog(&mut self, window: &MainWindow, initial: Option<(CustomTerm, bool)>) {
        if let Some(d) = &self.glossary_view.dialog {
            if d.window().is_visible() {
                self.present(d);
                return;
            }
        }
        let dialog = match TermDialog::new() {
            Ok(d) => d,
            Err(_) => return,
        };
        let (term, is_new) = initial.unwrap_or_else(|| (CustomTerm::default(), true));
        dialog.set_new_term(is_new);
        dialog.set_term(term.source.as_str().into());
        dialog.set_translation(if term.keep { String::new() } else { term.target.clone() }.into());
        dialog.set_keep(term.keep);
        let weak = dialog.as_weak();
        let main = window.as_weak();
        dialog.on_confirmed(move || {
            let (Some(d), Some(_)) = (weak.upgrade(), main.upgrade()) else { return };
            let source = d.get_term().split_whitespace().collect::<Vec<_>>().join(" ");
            let keep = d.get_keep();
            let target = if keep { source.clone() } else { d.get_translation().split_whitespace().collect::<Vec<_>>().join(" ") };
            if source.is_empty() || target.is_empty() {
                d.set_error(tr("Both the term and its translation are required.").into());
                return;
            }
            let _ = d.hide();
            with_app(|app, w| {
                if let Some((si, existing)) = app.custom_terms_of_active() {
                    let merged = custom_glossary::merge(&existing, &[CustomTerm { source, target, keep }]);
                    app.glossary_commit(w, si, merged);
                }
                app.glossary_view.dialog = None;
            });
        });
        let weak = dialog.as_weak();
        dialog.on_dismissed(move || {
            if let Some(d) = weak.upgrade() {
                let _ = d.hide();
            }
            with_app(|app, _| app.glossary_view.dialog = None);
        });
        self.present(&dialog);
        self.glossary_view.dialog = Some(dialog);
    }

    fn glossary_keep(&mut self, window: &MainWindow, row: i32) {
        let Some(term) = self.glossary_term_at(row) else { return };
        if let Some((si, existing)) = self.custom_terms_of_active() {
            let merged = custom_glossary::merge(&existing, &[CustomTerm { source: term.source.clone(), target: term.source, keep: true }]);
            self.glossary_commit(window, si, merged);
        }
    }

    fn glossary_delete(&mut self, window: &MainWindow, row: i32) {
        let Some(term) = self.glossary_term_at(row) else { return };
        if !term.custom {
            self.set_status(window, tr("Only the terms you added (in bold) can be deleted."));
            return;
        }
        if let Some((si, mut terms)) = self.custom_terms_of_active() {
            let key = custom_glossary::key_of(&term.source);
            terms.retain(|t| custom_glossary::key_of(&t.source) != key);
            self.glossary_commit(window, si, terms);
        }
    }

    fn glossary_import(&mut self, window: &MainWindow) {
        let Some((si, existing)) = self.custom_terms_of_active() else { return };
        let Some(path) = self.picker(window)
            .set_title(tr("Import glossary terms"))
            .add_filter(tr("Glossaries"), &["csv", "tsv", "txt"])
            .pick_file()
        else {
            return;
        };
        let Ok(bytes) = std::fs::read(&path) else {
            self.set_status(window, trf("Cannot read %1.", &[&path.to_string_lossy()]));
            return;
        };
        let text = match String::from_utf8(bytes.clone()) {
            Ok(t) => t,
            Err(_) => babylon_core::text_codec::decode(&bytes, "cp1252"),
        };
        let (terms, skipped) = custom_glossary::from_csv(&text);
        if terms.is_empty() {
            self.set_status(window, tr("No term found in this file."));
            return;
        }
        let count = terms.len();
        let merged = custom_glossary::merge(&existing, &terms);
        self.glossary_commit(window, si, merged);
        self.set_status(window, trf("%1 term(s) imported, %2 line(s) ignored.", &[&count.to_string(), &skipped.to_string()]));
    }

    fn glossary_export(&mut self, window: &MainWindow) {
        let Some((_, terms)) = self.custom_terms_of_active() else { return };
        if terms.is_empty() {
            self.set_status(window, tr("There is no term of your own to export yet."));
            return;
        }
        let Some(path) = self.picker(window)
            .set_title(tr("Export glossary terms"))
            .set_file_name("glossary.csv")
            .add_filter(tr("CSV files"), &["csv"])
            .save_file()
        else {
            return;
        };
        let mut bytes = vec![0xEF, 0xBB, 0xBF];
        bytes.extend_from_slice(custom_glossary::to_csv(&terms).as_bytes());
        match std::fs::write(&path, bytes) {
            Ok(()) => self.set_status(window, trf("%1 term(s) exported.", &[&terms.len().to_string()])),
            Err(_) => self.set_status(window, trf("Cannot write %1.", &[&path.to_string_lossy()])),
        }
    }
}
