//! The Pip-Boy panel: holodisks of game/pipboy.msg are edited as free text that is wrapped with the width of
//! the game font and written back as records; game/quests.msg lines show how the Pip-Boy wraps them.

use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::MainWindow;
use babylon_core::i18n::{tr, trf};
use babylon_core::pipboy_text::{self, Block, Font, Measure};
use babylon_core::project::SegmentStatus;
use slint::TimerMode;
use std::collections::BTreeMap;
use std::time::Duration;

#[derive(Default)]
pub(crate) struct PipboyState {
    editor_uid: u64,
    block: Option<Block>,
    quest_mode: bool,
    loading: bool,
    entries: Vec<String>,
    font_project: String,
    font: Option<Font>,
    apply_timer: slint::Timer,
}

pub(super) fn wire(host: &Host) {
    let data = host.pipboy();
    data.on_target_edited(|text| {
        with_app(|app, w| app.pipboy_edited(w, &text));
    });
    data.on_settings_changed(|| {
        with_app(|app, w| {
            let host = app.host_of(super::PIPBOY, w);
            let d = host.pipboy();
            app.settings.pipboy_max_width = d.get_width();
            app.settings.pipboy_fallback_chars = d.get_chars();
            if app.pipboy.block.is_some() {
                app.pipboy_schedule(w);
            }
        });
    });
    data.on_reload(|| {
        with_app(|app, w| app.pipboy_reload(w));
    });
}

impl App {
    fn pipboy_idle(&mut self, window: &MainWindow, message: String) {
        let host = self.host_of(super::PIPBOY, window);
        let data = host.pipboy();
        self.pipboy.apply_timer.stop();
        self.pipboy.block = None;
        self.pipboy.entries.clear();
        self.pipboy.loading = true;
        data.set_source_text("".into());
        data.set_target_text("".into());
        self.pipboy.loading = false;
        data.set_preview("".into());
        data.set_info(message.into());
        data.set_mode("".into());
        data.set_status("".into());
        data.set_editing(false);
    }

    /// Loads the font of the project (cached): the game draws the Pip-Boy with font1.aaf.
    fn pipboy_font(&mut self, si: usize) {
        let id = self.sessions[si].id.clone();
        if self.pipboy.font_project != id {
            self.pipboy.font = self.sessions[si].project.read_interface_font(1).map(|b| Font::from_aaf(&b));
            self.pipboy.font_project = id;
        }
    }

    pub fn refresh_pipboy_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::PIPBOY, window);
        let data = host.pipboy();
        data.set_width(self.settings.pipboy_max_width);
        data.set_chars(self.settings.pipboy_fallback_chars);
        let Some(ed) = self.current_editor() else {
            self.pipboy_idle(window, tr("The holodisk editor works on game/pipboy.msg."));
            return;
        };
        let rel = ed.rel_path().to_string();
        let quests = rel == "game/quests.msg";
        if !quests && rel != "game/pipboy.msg" {
            self.pipboy.quest_mode = false;
            self.pipboy_idle(window, tr("The holodisk editor works on game/pipboy.msg."));
            return;
        }
        let uid = ed.uid;
        let Some(row) = ed.current else { return };
        let id = ed.model.row(row).id;
        let Some(si) = self.session_index(&ed.session) else { return };
        if self.pipboy.editor_uid != uid {
            if self.pipboy.block.is_some() && self.pipboy.apply_timer.running() {
                self.pipboy.apply_timer.stop();
            }
            self.pipboy.editor_uid = uid;
            self.pipboy.block = None;
        }
        self.pipboy.quest_mode = quests;
        self.pipboy_font(si);
        if quests {
            self.pipboy_show_quest(window, si, id);
            return;
        }
        if self.pipboy.block.is_some_and(|b| b.contains(id)) {
            return; // still the same holodisk
        }
        let ed = self.current_editor().expect("checked above");
        let texts: BTreeMap<i32, String> = ed.model.rows().iter().filter(|r| r.in_source).map(|r| (r.id, r.source.clone())).collect();
        match pipboy_text_blocks(&texts).into_iter().find(|b| b.contains(id)) {
            Some(block) => {
                self.pipboy.block = Some(block);
                data.set_editing(true);
                data.set_info(
                    trf(
                        "Holodisk: records %1 to %2 (%3 entries, %4 page(s) in the Pip-Boy).",
                        &[
                            &block.first_id.to_string(),
                            &block.last_id.to_string(),
                            &block.entry_count().to_string(),
                            &pipboy_text::page_count(block.entry_count() as usize).to_string(),
                        ],
                    )
                    .into(),
                );
                self.pipboy_reload(window);
            }
            None => self.pipboy_idle(window, tr("This line is not part of a holodisk body (a holodisk body ends with **END-DISK**).")),
        }
    }

    fn measure_params(&self, si: usize) -> (i32, i32, String) {
        (self.settings.pipboy_max_width, self.settings.pipboy_fallback_chars, self.sessions[si].project.config().target_encoding.clone())
    }

    fn pipboy_show_quest(&mut self, window: &MainWindow, si: usize, id: i32) {
        let host = self.host_of(super::PIPBOY, window);
        let data = host.pipboy();
        let Some(ed) = self.current_editor() else { return };
        let Some(row) = ed.model.row_for_id(id).map(|r| ed.model.row(r).clone()) else { return };
        let (_, chars, encoding) = self.measure_params(si);
        let mut measure = Measure::new(self.pipboy.font.as_ref(), &encoding);
        measure.max_width = pipboy_text::QUEST_WIDTH_PX;
        measure.fallback_chars = chars;
        // the engine writes "N. " before the description and wraps the whole at 350 px
        let wrap = |text: &str| -> Vec<String> {
            pipboy_text::layout(&format!("1. {text}"), &measure)
                .into_iter()
                .filter(|l| l != pipboy_text::END_DISK && l != pipboy_text::END_PAR)
                .collect()
        };
        let source = wrap(&row.source);
        let target = wrap(if row.target.is_empty() { &row.source } else { &row.target });
        let preview: Vec<String> = target.iter().map(|l| format!("{:>4}  {l}", measure.width(l))).collect();
        self.pipboy.block = None;
        self.pipboy.loading = true;
        data.set_source_text(source.join("\n").into());
        data.set_target_text(target.join("\n").into());
        self.pipboy.loading = false;
        data.set_editing(false);
        data.set_preview(preview.join("\n").into());
        data.set_info(trf("Quest %1: the Pip-Boy wraps the text (after the number) at %2 px.", &[&id.to_string(), &measure.max_width.to_string()]).into());
        data.set_mode(
            if measure.exact() {
                tr("Measured with the game font (font1.aaf).")
            } else {
                tr("Game font not available (or multi-byte encoding): approximate measure.")
            }
            .into(),
        );
        data.set_status(trf("%1 line(s) in the translation, %2 in the source", &[&target.len().to_string(), &source.len().to_string()]).into());
    }

    /// Rebuilds the text of the translation from the records of the file.
    fn pipboy_reload(&mut self, window: &MainWindow) {
        let host = self.host_of(super::PIPBOY, window);
        let data = host.pipboy();
        let (Some(block), Some(ed)) = (self.pipboy.block, self.current_editor()) else { return };
        let Some(si) = self.session_index(&ed.session) else { return };
        self.pipboy.apply_timer.stop();
        let (width, chars, encoding) = self.measure_params(si);
        let mut measure = Measure::new(self.pipboy.font.as_ref(), &encoding);
        measure.max_width = width;
        measure.fallback_chars = chars;

        let (mut source, mut target) = (Vec::new(), Vec::new());
        let mut any_translated = false;
        for id in block.first_id..=block.last_id {
            if let Some(row) = ed.model.row_for_id(id) {
                let r = ed.model.row(row);
                source.push(r.source.clone());
                target.push(r.target.clone());
                any_translated |= ed.model.status(row) != SegmentStatus::Untranslated;
            }
        }
        // records past the source block that an earlier edit added belong to the translation too
        let mut id = block.last_id + 1;
        while let Some(row) = ed.model.row_for_id(id) {
            if ed.model.row(row).in_source {
                break;
            }
            target.push(ed.model.row(row).target.clone());
            id += 1;
        }
        let source_text = pipboy_text::reflow(&source, &measure);
        let target_text = if any_translated { pipboy_text::reflow(&target, &measure) } else { String::new() };
        self.pipboy.entries = if any_translated { pipboy_text::layout(&target_text, &measure) } else { Vec::new() };
        self.pipboy.loading = true;
        data.set_source_text(source_text.into());
        data.set_target_text(target_text.into());
        self.pipboy.loading = false;
        data.set_mode(
            if measure.exact() {
                trf("Measured with the game font (font1.aaf): lines are cut at %1 px.", &[&width.to_string()])
            } else {
                trf("Game font not available (or multi-byte encoding): lines are cut at about %1 characters.", &[&chars.to_string()])
            }
            .into(),
        );
        self.pipboy_preview(window);
    }

    fn pipboy_preview(&mut self, window: &MainWindow) {
        let host = self.host_of(super::PIPBOY, window);
        let data = host.pipboy();
        let (Some(block), Some(ed)) = (self.pipboy.block, self.current_editor()) else { return };
        let Some(si) = self.session_index(&ed.session) else { return };
        let (width, chars, encoding) = self.measure_params(si);
        let mut measure = Measure::new(self.pipboy.font.as_ref(), &encoding);
        measure.max_width = width;
        measure.fallback_chars = chars;
        let wide = pipboy_text::too_wide(&self.pipboy.entries, &measure);
        let lines: Vec<String> = self
            .pipboy
            .entries
            .iter()
            .enumerate()
            .map(|(i, entry)| {
                let marker = entry == pipboy_text::END_PAR || entry == pipboy_text::END_DISK;
                let w = if marker {
                    "      ".to_string()
                } else {
                    format!("{:>4}{}", measure.width(entry), if wide.contains(&i) { "!" } else { " " })
                };
                format!("{:>5}  {w}  {entry}", block.first_id + i as i32)
            })
            .collect();
        data.set_preview(lines.join("\n").into());
        let needed = self.pipboy.entries.len();
        let mut status = trf("%1 entries, %2 page(s) in the Pip-Boy", &[&needed.to_string(), &pipboy_text::page_count(needed).to_string()]);
        if !wide.is_empty() {
            status.push_str("  ⚠ ");
            status.push_str(&trf("%1 line(s) wider than the screen", &[&wide.len().to_string()]));
        }
        if needed > 500 {
            status.push_str("  ⚠ ");
            status.push_str(&tr("the game only reads 500 entries per holodisk"));
        }
        data.set_status(status.into());
    }

    fn pipboy_edited(&mut self, window: &MainWindow, text: &str) {
        if self.pipboy.loading || self.pipboy.block.is_none() {
            return;
        }
        let Some(si) = self.current_editor().and_then(|e| self.session_index(&e.session)) else { return };
        let (width, chars, encoding) = self.measure_params(si);
        let mut measure = Measure::new(self.pipboy.font.as_ref(), &encoding);
        measure.max_width = width;
        measure.fallback_chars = chars;
        self.pipboy.entries = pipboy_text::layout(text, &measure);
        self.host_of(super::PIPBOY, window).pipboy().set_status(tr("Editing...").into());
        self.pipboy_preview(window);
        self.pipboy_schedule(window);
    }

    fn pipboy_schedule(&mut self, _window: &MainWindow) {
        self.pipboy.apply_timer.start(TimerMode::SingleShot, Duration::from_millis(600), || {
            with_app(|app, w| app.pipboy_apply(w));
        });
    }

    /// Writes the entries as records of the file.
    pub fn pipboy_apply(&mut self, window: &MainWindow) {
        self.pipboy.apply_timer.stop();
        let host = self.host_of(super::PIPBOY, window);
        let data = host.pipboy();
        let Some(block) = self.pipboy.block else { return };
        let text = data.get_target_text().to_string();
        if self.pipboy.loading || text.trim().is_empty() {
            return; // nothing written: the records stay as they are
        }
        let Some(si) = self.current_editor().and_then(|e| self.session_index(&e.session)) else { return };
        let (width, chars, encoding) = self.measure_params(si);
        let entries = {
            let mut measure = Measure::new(self.pipboy.font.as_ref(), &encoding);
            measure.max_width = width;
            measure.fallback_chars = chars;
            pipboy_text::layout(&text, &measure)
        };
        let needed = entries.len() as i32;
        let available = block.entry_count();
        let Some(ci) = self.current else { return };
        let keep_id = self.editors[ci].current.map(|r| self.editors[ci].model.row(r).id);
        {
            let model = &self.editors[ci].model;
            // extra entries need free ids right after the block: stop if they belong to something else
            for i in available..needed {
                if model.row_for_id(block.first_id + i).is_some_and(|r| model.row(r).in_source) {
                    data.set_status(
                        format!(
                            "⚠ {}",
                            trf(
                                "The translation needs %1 entries but only %2 ids are free before record %3. Shorten it.",
                                &[&needed.to_string(), &i.to_string(), &(block.first_id + i).to_string()],
                            )
                        )
                        .into(),
                    );
                    self.pipboy.entries = entries;
                    self.pipboy_preview(window);
                    return;
                }
            }
        }
        let model = &mut self.editors[ci].model;
        for (i, entry) in entries.iter().enumerate() {
            let id = block.first_id + i as i32;
            let row = model.row_for_id(id).unwrap_or_else(|| model.add_row(id));
            model.set_target(row, entry);
        }
        // source records the translation no longer uses (it is shorter): they keep their text, counted as done
        for i in needed..available {
            if let Some(row) = model.row_for_id(block.first_id + i) {
                let source = model.row(row).source.clone();
                model.set_target(row, &source);
                model.confirm(row);
            }
        }
        // records an earlier, longer version had added past the source block
        let mut id = block.first_id + needed.max(available);
        while let Some(row) = model.row_for_id(id) {
            if model.row(row).in_source {
                break;
            }
            model.remove_orphan(row);
            id += 1;
        }
        self.pipboy.entries = entries;
        self.refresh_editor_after_external_change(window, keep_id);
        self.pipboy_preview(window);
        let status = data.get_status();
        data.set_status(format!("{status}  ✔ {}", tr("written to the file")).into());
    }
}

fn pipboy_text_blocks(texts: &BTreeMap<i32, String>) -> Vec<Block> {
    pipboy_text::find_holodisks(texts)
}
