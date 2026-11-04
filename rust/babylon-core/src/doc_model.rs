//! The records of one message file as they are being edited: marks, glossary alerts, and a file-level
//! undo / redo of coarse operations (mass translation, replace, review marks...). Typing is not recorded here:
//! the text editors have their own undo for that.
//!
//! Everything changed between `begin_operation` and `end_operation` is one undoable step.

use crate::glossary::Glossary;
use crate::i18n::tr;
use crate::project::{Project, ReviewMark, SegmentStatus, TranslationDoc, TranslationRow};
use crate::project_search::{self, Options as SearchOptions};
use crate::statistics::FileStats;
use std::collections::HashSet;
use std::sync::Arc;

const MAX_STEPS: usize = 30;

#[derive(Clone)]
struct Step {
    label: String,
    /// The number of rows changed: whole tables are stored.
    full: bool,
    before_rows: Vec<TranslationRow>,
    after_rows: Vec<TranslationRow>,
    /// Otherwise: the rows that changed.
    before: Vec<TranslationRow>,
    after: Vec<TranslationRow>,
}

/// Which rows an undo / redo changed.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Changed {
    /// The table was replaced: everything must be refreshed.
    pub reset: bool,
    pub rows: Vec<usize>,
}

pub struct DocModel {
    doc: TranslationDoc,
    glossary: Arc<Glossary>,
    issues: Vec<u32>,
    dirty: bool,
    undo: Vec<Step>,
    redo: Vec<Step>,
    op_depth: usize,
    op_label: String,
    op_snapshot: Vec<TranslationRow>,
}

fn same_row(a: &TranslationRow, b: &TranslationRow) -> bool {
    a.id == b.id
        && a.target == b.target
        && a.review == b.review
        && a.confirmed == b.confirmed
        && a.comment == b.comment
        && a.validated == b.validated
        && a.previous == b.previous
}

impl DocModel {
    pub fn new(doc: TranslationDoc, glossary: Arc<Glossary>) -> DocModel {
        let mut m = DocModel {
            issues: vec![0; doc.rows.len()],
            doc,
            glossary,
            dirty: false,
            undo: Vec::new(),
            redo: Vec::new(),
            op_depth: 0,
            op_label: String::new(),
            op_snapshot: Vec::new(),
        };
        m.refresh_all_issues();
        m
    }

    pub fn doc(&self) -> &TranslationDoc {
        &self.doc
    }

    pub fn rel_path(&self) -> &str {
        &self.doc.rel_path
    }

    pub fn len(&self) -> usize {
        self.doc.rows.len()
    }

    pub fn is_empty(&self) -> bool {
        self.doc.rows.is_empty()
    }

    pub fn row(&self, index: usize) -> &TranslationRow {
        &self.doc.rows[index]
    }

    pub fn rows(&self) -> &[TranslationRow] {
        &self.doc.rows
    }

    pub fn is_dirty(&self) -> bool {
        self.dirty
    }

    pub fn mark_saved(&mut self) {
        self.dirty = false;
    }

    pub fn status(&self, row: usize) -> SegmentStatus {
        Project::status_of(&self.doc.rows[row])
    }

    pub fn issue_count(&self, row: usize) -> u32 {
        self.issues.get(row).copied().unwrap_or(0)
    }

    pub fn issue_messages(&self, row: usize) -> Vec<String> {
        let r = &self.doc.rows[row];
        if !r.in_source {
            return Vec::new();
        }
        self.glossary.check_consistency(&r.source, &r.target).into_iter().map(|i| i.message).collect()
    }

    pub fn stats(&self) -> FileStats {
        Project::statistics_of(&self.doc)
    }

    /// Index of the record with this id.
    pub fn row_for_id(&self, id: i32) -> Option<usize> {
        self.doc.rows.iter().position(|r| r.id == id)
    }

    fn update_issues(&mut self, row: usize) {
        let r = &self.doc.rows[row];
        self.issues[row] = if r.in_source { self.glossary.check_consistency(&r.source, &r.target).len() as u32 } else { 0 };
    }

    fn refresh_all_issues(&mut self) {
        for i in 0..self.doc.rows.len() {
            self.update_issues(i);
        }
    }

    /// The glossary changed: recompute the alerts of every row.
    pub fn refresh_issues(&mut self, glossary: Arc<Glossary>) {
        self.glossary = glossary;
        self.issues = vec![0; self.doc.rows.len()];
        self.refresh_all_issues();
    }

    fn touch(&mut self, row: usize) {
        self.dirty = true;
        self.update_issues(row);
    }

    pub fn set_target(&mut self, row: usize, text: &str) {
        let r = &mut self.doc.rows[row];
        if r.target == text {
            return;
        }
        r.target = text.to_string();
        // Typing exactly the source text means "this stays as is" (names, interjections...).
        r.confirmed = !text.is_empty() && text == r.source;
        self.touch(row);
    }

    pub fn set_review(&mut self, row: usize, review: bool) {
        if self.doc.rows[row].review == review {
            return;
        }
        self.doc.rows[row].review = review;
        self.touch(row);
    }

    /// Accept the current target (even if identical to the source).
    pub fn confirm(&mut self, row: usize) {
        let r = &mut self.doc.rows[row];
        if r.target.is_empty() {
            r.target = r.source.clone();
        }
        r.confirmed = true;
        self.touch(row);
    }

    pub fn set_comment(&mut self, row: usize, comment: &str) {
        if self.doc.rows[row].comment == comment {
            return;
        }
        self.doc.rows[row].comment = comment.to_string();
        self.touch(row);
    }

    /// "Validated": the reviewer approved this exact text; editing it afterwards shows "changed since validation".
    pub fn set_validated(&mut self, row: usize, validated: bool) {
        let r = &mut self.doc.rows[row];
        let value = if validated && !r.target.is_empty() { Project::text_hash(&r.target) } else { 0 };
        if r.validated == value {
            return;
        }
        r.validated = value;
        self.touch(row);
    }

    /// Adds a record that exists only in the translation (e.g. extra Pip-Boy lines); returns its row.
    pub fn add_row(&mut self, id: i32) -> usize {
        if let Some(existing) = self.row_for_id(id) {
            return existing;
        }
        let position = self.doc.rows.iter().position(|r| r.id > id).unwrap_or(self.doc.rows.len());
        let row = TranslationRow { id, in_source: false, leading: "\n".into(), ..Default::default() };
        self.doc.rows.insert(position, row);
        self.issues.insert(position, 0);
        self.dirty = true;
        position
    }

    /// Removes a record added with `add_row` (ignored for records of the source).
    pub fn remove_orphan(&mut self, row: usize) {
        if row >= self.doc.rows.len() || self.doc.rows[row].in_source {
            return;
        }
        self.doc.rows.remove(row);
        self.issues.remove(row);
        self.dirty = true;
    }

    // ------------------------------------------------------------------------- undo / redo

    pub fn begin_operation(&mut self, label: &str) {
        self.op_depth += 1;
        if self.op_depth == 1 {
            self.op_label = label.to_string();
            self.op_snapshot = self.doc.rows.clone();
        }
    }

    pub fn end_operation(&mut self) {
        if self.op_depth == 0 {
            return;
        }
        self.op_depth -= 1;
        if self.op_depth > 0 {
            return;
        }
        let snapshot = std::mem::take(&mut self.op_snapshot);
        let mut step = Step {
            label: std::mem::take(&mut self.op_label),
            full: false,
            before_rows: Vec::new(),
            after_rows: Vec::new(),
            before: Vec::new(),
            after: Vec::new(),
        };
        if snapshot.len() != self.doc.rows.len() {
            step.full = true;
            step.before_rows = snapshot;
            step.after_rows = self.doc.rows.clone();
        } else {
            for (i, (b, a)) in snapshot.iter().zip(self.doc.rows.iter()).enumerate() {
                let _ = i;
                if !same_row(b, a) {
                    step.before.push(b.clone());
                    step.after.push(a.clone());
                }
            }
            if step.before.is_empty() {
                return;
            }
        }
        self.undo.push(step);
        if self.undo.len() > MAX_STEPS {
            self.undo.remove(0);
        }
        self.redo.clear();
    }

    /// Runs `f` as one undoable step.
    pub fn operation<R>(&mut self, label: &str, f: impl FnOnce(&mut DocModel) -> R) -> R {
        self.begin_operation(label);
        let r = f(self);
        self.end_operation();
        r
    }

    pub fn can_undo(&self) -> bool {
        !self.undo.is_empty()
    }

    pub fn can_redo(&self) -> bool {
        !self.redo.is_empty()
    }

    pub fn undo_label(&self) -> String {
        self.undo.last().map(|s| s.label.clone()).unwrap_or_default()
    }

    pub fn redo_label(&self) -> String {
        self.redo.last().map(|s| s.label.clone()).unwrap_or_default()
    }

    fn apply(&mut self, step: &Step, undo: bool) -> Changed {
        let mut changed = Changed::default();
        if step.full {
            self.doc.rows = if undo { step.before_rows.clone() } else { step.after_rows.clone() };
            self.issues = vec![0; self.doc.rows.len()];
            self.refresh_all_issues();
            changed.reset = true;
        } else {
            let rows = if undo { &step.before } else { &step.after };
            for saved in rows {
                let Some(row) = self.row_for_id(saved.id) else { continue };
                let r = &mut self.doc.rows[row];
                r.target = saved.target.clone();
                r.review = saved.review;
                r.confirmed = saved.confirmed;
                r.comment = saved.comment.clone();
                r.validated = saved.validated;
                r.previous = saved.previous.clone();
                self.update_issues(row);
                changed.rows.push(row);
            }
        }
        self.dirty = true;
        changed
    }

    pub fn undo(&mut self) -> Option<Changed> {
        let step = self.undo.pop()?;
        let changed = self.apply(&step, true);
        self.redo.push(step);
        Some(changed)
    }

    pub fn redo(&mut self) -> Option<Changed> {
        let step = self.redo.pop()?;
        let changed = self.apply(&step, false);
        self.undo.push(step);
        Some(changed)
    }

    // ------------------------------------------------------------------------- operations

    /// Search and replace in the translation of the listed records (all when `ids` is empty).
    /// Returns the number of replacements.
    pub fn replace_in_target(&mut self, options: &SearchOptions, replacement: &str, ids: &HashSet<i32>) -> usize {
        self.operation(&tr("Replace"), |m| {
            let mut total = 0;
            for row in 0..m.len() {
                if !ids.is_empty() && !ids.contains(&m.doc.rows[row].id) {
                    continue;
                }
                let mut text = m.doc.rows[row].target.clone();
                let n = project_search::replace_in(&mut text, options, replacement);
                if n > 0 && text != m.doc.rows[row].target {
                    m.set_target(row, &text);
                    total += n;
                }
            }
            total
        })
    }

    /// Marks lines of this file "to review" with a comment (marks of other files are ignored).
    /// Returns how many changed.
    pub fn mark_for_review(&mut self, marks: &[ReviewMark]) -> usize {
        self.operation(&tr("Review mark"), |m| {
            let mut changed = 0;
            for mark in marks {
                if mark.rel_path != m.doc.rel_path {
                    continue;
                }
                let Some(row) = m.row_for_id(mark.id) else { continue };
                let mut touched = !m.doc.rows[row].review;
                m.set_review(row, true);
                let comment = m.doc.rows[row].comment.clone();
                if !mark.comment.is_empty() && !comment.contains(&mark.comment) {
                    let new = if comment.is_empty() { mark.comment.clone() } else { format!("{comment} | {}", mark.comment) };
                    m.set_comment(row, &new);
                    touched = true;
                }
                if touched {
                    changed += 1;
                }
            }
            changed
        })
    }

    /// Puts a translation suggested by the translation memory on a line; below a perfect match the line is
    /// marked "to review".
    pub fn apply_suggestion(&mut self, row: usize, translation: &str, score: f64) {
        self.operation(&tr("Suggestion used"), |m| {
            m.set_target(row, translation);
            if score < 1.0 {
                m.set_review(row, true);
            }
        });
    }

    /// Toggles the review mark of the rows: if every one is already flagged the flag is cleared, otherwise all are flagged.
    pub fn toggle_review(&mut self, rows: &[usize]) {
        if rows.is_empty() {
            return;
        }
        self.operation(&tr("Review mark"), |m| {
            let all_flagged = rows.iter().all(|&r| m.doc.rows[r].review);
            for &r in rows {
                m.set_review(r, !all_flagged);
            }
        });
    }

    /// Validates the rows (or removes the validation when all are validated and unchanged).
    pub fn toggle_validated(&mut self, rows: &[usize]) {
        if rows.is_empty() {
            return;
        }
        self.operation(&tr("Validation"), |m| {
            let all_valid = rows.iter().all(|&r| {
                let row = &m.doc.rows[r];
                row.validated != 0 && !row.changed_since_validation()
            });
            for &r in rows {
                m.set_validated(r, !all_valid);
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn doc() -> TranslationDoc {
        let rows = vec![
            TranslationRow { id: 100, source: "Hello Stimpak".into(), target: "Bonjour Stimpak".into(), ..Default::default() },
            TranslationRow { id: 101, source: "Wear the Leather Armor".into(), target: "Mets la cuirasse".into(), ..Default::default() },
            TranslationRow { id: 102, source: "Goodbye".into(), ..Default::default() },
        ];
        TranslationDoc { rel_path: "dialog/a.msg".into(), rows, crlf: true, ..Default::default() }
    }

    fn glossary() -> Arc<Glossary> {
        let mut g = Glossary::new();
        g.add("Leather Armor", "Armure en cuir", "Item", "x");
        g.finalize("english", "french");
        Arc::new(g)
    }

    #[test]
    fn marks_and_alerts() {
        let mut m = DocModel::new(doc(), glossary());
        assert_eq!(m.issue_count(1), 1);
        assert_eq!(m.issue_count(0), 0);
        assert_eq!(m.issue_messages(1), vec!["Leather Armor → Armure en cuir".to_string()]);
        assert!(!m.is_dirty());
        m.set_target(1, "Mets l'armure en cuir");
        assert_eq!(m.issue_count(1), 0);
        assert!(m.is_dirty());
        m.mark_saved();
        // typing the source text confirms it
        m.set_target(2, "Goodbye");
        assert!(m.row(2).confirmed);
        m.set_target(2, "Au revoir");
        assert!(!m.row(2).confirmed);
        assert_eq!(m.status(2), SegmentStatus::Translated);
        m.set_review(2, true);
        assert_eq!(m.status(2), SegmentStatus::ToReview);
        assert_eq!(m.stats().review, 1);
    }

    #[test]
    fn undo_and_redo_of_coarse_operations() {
        let mut m = DocModel::new(doc(), glossary());
        m.toggle_review(&[0, 1]);
        assert!(m.row(0).review && m.row(1).review);
        assert!(m.can_undo());
        assert!(!m.undo_label().is_empty());
        let changed = m.undo().unwrap();
        assert_eq!(changed.rows.len(), 2);
        assert!(!m.row(0).review && !m.row(1).review);
        assert!(m.can_redo());
        m.redo().unwrap();
        assert!(m.row(0).review);
        m.undo().unwrap();

        // typing after the operation is not lost by undoing it, except in the rows it changed
        m.toggle_review(&[0]);
        m.set_target(2, "Salut");
        m.undo().unwrap();
        assert!(!m.row(0).review);
        assert_eq!(m.row(2).target, "Salut");

        // an operation that changes nothing leaves no step; a new step clears the redo stack
        let before = m.undo.len();
        m.operation("noop", |_| {});
        assert_eq!(m.undo.len(), before);
        m.toggle_review(&[1]);
        assert!(!m.can_redo());

        // nested operations make a single step
        let steps = m.undo.len();
        m.operation("outer", |m| {
            m.set_target(0, "A");
            m.operation("inner", |m| m.set_target(1, "B"));
        });
        assert_eq!(m.undo.len(), steps + 1);
        m.undo().unwrap();
        assert_eq!(m.row(0).target, "Bonjour Stimpak");
        assert_eq!(m.row(1).target, "Mets la cuirasse");

        // at most 30 levels
        for i in 0..40 {
            m.operation("many", |m| m.set_target(2, &format!("x{i}")));
        }
        assert_eq!(m.undo.len(), MAX_STEPS);
    }

    #[test]
    fn added_rows_make_a_full_step() {
        let mut m = DocModel::new(doc(), glossary());
        m.operation("pipboy", |m| {
            let r = m.add_row(103);
            m.set_target(r, "extra");
        });
        assert_eq!(m.len(), 4);
        assert!(!m.row(3).in_source);
        let changed = m.undo().unwrap();
        assert!(changed.reset);
        assert_eq!(m.len(), 3);
        m.redo().unwrap();
        assert_eq!(m.len(), 4);
        let r = m.row_for_id(103).unwrap();
        m.remove_orphan(r);
        assert_eq!(m.len(), 3);
        m.remove_orphan(0); // records of the source stay
        assert_eq!(m.len(), 3);
    }

    #[test]
    fn replace_review_and_validation() {
        let mut m = DocModel::new(doc(), glossary());
        let o = SearchOptions { text: "stimpak".into(), ..Default::default() };
        assert_eq!(m.replace_in_target(&o, "Seringue", &HashSet::new()), 1);
        assert_eq!(m.row(0).target, "Bonjour Seringue");
        m.undo().unwrap();
        assert_eq!(m.row(0).target, "Bonjour Stimpak");

        m.toggle_validated(&[0]);
        assert!(m.row(0).validated != 0 && !m.row(0).changed_since_validation());
        m.set_target(0, "Salut");
        assert!(m.row(0).changed_since_validation());
        m.toggle_validated(&[0]);
        assert!(!m.row(0).changed_since_validation());
        m.toggle_validated(&[0]);
        assert_eq!(m.row(0).validated, 0);

        let marks = vec![ReviewMark { rel_path: "dialog/a.msg".into(), id: 102, comment: "Game report: x".into() }];
        assert_eq!(m.mark_for_review(&marks), 1);
        assert!(m.row(2).review && m.row(2).comment.contains("Game report"));
        assert_eq!(m.mark_for_review(&marks), 0);
        let other = vec![ReviewMark { rel_path: "dialog/b.msg".into(), id: 102, comment: String::new() }];
        assert_eq!(m.mark_for_review(&other), 0);

        m.apply_suggestion(1, "Autre", 0.8);
        assert!(m.row(1).review);
        m.apply_suggestion(0, "Exact", 1.0);
        assert!(!m.row(0).review);
    }
}
