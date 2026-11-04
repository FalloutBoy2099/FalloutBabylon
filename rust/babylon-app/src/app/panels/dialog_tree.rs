//! The dialogue tree of the open /dialog file, from its decompiled script.

use crate::app::{with_app, App};
use crate::app::host::Host;
use crate::{DialogData, DialogRow, MainWindow};
use babylon_core::dialog_graph::{DialogGraph, DialogNode, NodeType};
use babylon_core::i18n::{tr, trf};
use babylon_core::int2ssl;
use babylon_core::project::{FileKind, Project, SegmentStatus};
use slint::{ComponentHandle, ModelRc, VecModel};
use std::collections::{HashMap, HashSet};
use std::rc::Rc;

struct FlatNode {
    kind: NodeType,
    msg_id: i32,
    proc: String,
    iq: i32,
    depth: i32,
}

const NODE_W: f32 = 230.0;
const NODE_H: f32 = 62.0;
const CALL_H: f32 = 28.0;
const GAP_X: f32 = 52.0;
const GAP_Y: f32 = 14.0;
/// Between two levels when the tree grows downwards.
const GAP_LEVEL: f32 = 40.0;
const PAD: f32 = 18.0;

/// Where the nodes of the tree are drawn (a node, then its answers to the right or below) and the links between them.
#[derive(Default)]
struct GraphLayout {
    /// x, y, width, height of every node.
    rects: Vec<(f32, f32, f32, f32)>,
    width: f32,
    height: f32,
    /// SVG path data.
    edges: String,
}

fn layout(flat: &[FlatNode], vertical: bool) -> GraphLayout {
    let n = flat.len();
    let mut children: Vec<Vec<usize>> = vec![Vec::new(); n];
    let mut roots = Vec::new();
    let mut stack: Vec<usize> = Vec::new();
    for i in 0..n {
        while stack.last().is_some_and(|&t| flat[t].depth >= flat[i].depth) {
            stack.pop();
        }
        match stack.last() {
            Some(&p) => children[p].push(i),
            None => roots.push(i),
        }
        stack.push(i);
    }
    // a leaf takes the next slot across the tree; a node is centred on its children
    let (extent, step) = if vertical { (NODE_W, NODE_W + GAP_Y) } else { (NODE_H, NODE_H + GAP_Y) };
    let mut centre = vec![0f32; n];
    let mut next_row = 0f32;
    fn place(i: usize, children: &[Vec<usize>], centre: &mut [f32], next_row: &mut f32, extent: f32, step: f32) {
        if children[i].is_empty() {
            centre[i] = *next_row * step + extent / 2.0;
            *next_row += 1.0;
            return;
        }
        for &c in &children[i] {
            place(c, children, centre, next_row, extent, step);
        }
        let (first, last) = (children[i][0], children[i][children[i].len() - 1]);
        centre[i] = (centre[first] + centre[last]) / 2.0;
    }
    for &r in &roots {
        place(r, &children, &mut centre, &mut next_row, extent, step);
        next_row += 0.5; // a little air between two dialogues of the script
    }
    let mut out = GraphLayout::default();
    let mut max_depth = 0;
    for (i, f) in flat.iter().enumerate() {
        let h = if matches!(f.kind, NodeType::Call | NodeType::Reference) { CALL_H } else { NODE_H };
        if vertical {
            out.rects.push((PAD + centre[i] - NODE_W / 2.0, PAD + f.depth as f32 * (NODE_H + GAP_LEVEL), NODE_W, h));
        } else {
            out.rects.push((PAD + f.depth as f32 * (NODE_W + GAP_X), PAD + centre[i] - h / 2.0, NODE_W, h));
        }
        max_depth = max_depth.max(f.depth);
    }
    for (p, kids) in children.iter().enumerate() {
        let (px, py, pw, ph) = out.rects[p];
        for &c in kids {
            let (cx, cy, _, ch) = out.rects[c];
            if vertical {
                let (x1, y1, x2, y2) = (px + pw / 2.0, py + ph, cx + NODE_W / 2.0, cy);
                let mid = (y1 + y2) / 2.0;
                out.edges.push_str(&format!("M {x1:.1} {y1:.1} C {x1:.1} {mid:.1} {x2:.1} {mid:.1} {x2:.1} {y2:.1} "));
            } else {
                let (x1, y1, x2, y2) = (px + pw, py + ph / 2.0, cx, cy + ch / 2.0);
                let mid = (x1 + x2) / 2.0;
                out.edges.push_str(&format!("M {x1:.1} {y1:.1} C {mid:.1} {y1:.1} {mid:.1} {y2:.1} {x2:.1} {y2:.1} "));
            }
        }
    }
    if vertical {
        out.width = PAD * 2.0 + next_row * step;
        out.height = PAD * 2.0 + (max_depth as f32 + 1.0) * NODE_H + max_depth as f32 * GAP_LEVEL;
    } else {
        out.width = PAD * 2.0 + (max_depth as f32 + 1.0) * NODE_W + max_depth as f32 * GAP_X;
        out.height = PAD * 2.0 + next_row * step;
    }
    out
}

#[derive(Default)]
pub(crate) struct DialogView {
    layout: GraphLayout,
    model: Rc<VecModel<DialogRow>>,
    /// Node the graph was last scrolled to.
    focused: i32,
    focus_serial: i32,
    graphs: HashMap<String, Result<DialogGraph, String>>,
    pending: HashSet<String>,
    key: String,
    flat: Vec<FlatNode>,
    by_id: HashMap<i32, usize>,
    procs: HashMap<String, usize>,
    used_ids: HashSet<i32>,
}

fn flatten(nodes: &[DialogNode], depth: i32, out: &mut Vec<FlatNode>) {
    for n in nodes {
        out.push(FlatNode { kind: n.kind, msg_id: n.msg_id, proc: n.proc.clone(), iq: n.iq, depth });
        flatten(&n.children, depth + 1, out);
    }
}

pub(super) fn wire(host: &Host) {
    host.dialog().on_clicked(|i| {
        with_app(|app, w| app.dialog_clicked(w, i as usize));
    });
    host.dialog().on_layout_changed(|| {
        with_app(|app, w| {
            app.settings.dialog_vertical = app.host_of(super::DIALOG, w).dialog().get_vertical();
            app.dialog.layout = layout(&app.dialog.flat, app.settings.dialog_vertical);
            app.dialog.focused = -1;
            app.refresh_dialog_panel(w);
        });
    });
    host.dialog().on_canvas_clicked(|x, y| {
        with_app(|app, w| {
            let hit = app.dialog.layout.rects.iter().position(|&(rx, ry, rw, rh)| x >= rx && x <= rx + rw && y >= ry && y <= ry + rh);
            if let Some(i) = hit {
                app.dialog_clicked(w, i);
            }
        });
    });
}

impl App {
    fn dialog_clicked(&mut self, window: &MainWindow, index: usize) {
        let Some(node) = self.dialog.flat.get(index) else { return };
        if node.kind == NodeType::Reference {
            if let Some(&target) = self.dialog.procs.get(&node.proc) {
                self.host_of(super::DIALOG, window).dialog().set_current(target as i32);
            }
            return;
        }
        let id = node.msg_id;
        if id >= 0 {
            self.host_of(super::DIALOG, window).dialog().set_current(index as i32);
            self.select_id(window, id);
        }
    }

    pub fn refresh_dialog_panel(&mut self, window: &MainWindow) {
        let host = self.host_of(super::DIALOG, window);
        let data = host.dialog();
        let clear = |data: &DialogData<'_>, message: String| {
            data.set_rows(ModelRc::new(VecModel::<DialogRow>::default()));
            data.set_edges("".into());
            data.set_canvas_w(0.0);
            data.set_canvas_h(0.0);
            data.set_status(message.into());
        };
        let Some(ed) = self.current_editor() else {
            self.dialog.key.clear();
            clear(&data, tr("Open a dialogue file to see its tree."));
            return;
        };
        let rel = ed.rel_path().to_string();
        if Project::kind_of(&rel) != FileKind::Dialog {
            self.dialog.key.clear();
            clear(&data, tr("The dialogue tree is available for files of the /dialog folder."));
            return;
        }
        let Some(si) = self.session_index(&ed.session) else { return };
        let key = format!("{}|{rel}", ed.session);

        if !self.dialog.graphs.contains_key(&key) {
            if self.dialog.pending.insert(key.clone()) {
                let project = self.sessions[si].project.clone();
                let weak = window.as_weak();
                let (k, r) = (key.clone(), rel.clone());
                std::thread::spawn(move || {
                    let result = load_graph(&project, &r);
                    let _ = weak.upgrade_in_event_loop(move |_| {
                        with_app(|app, w| {
                            app.dialog.pending.remove(&k);
                            app.dialog.graphs.insert(k, result);
                            app.refresh_dialog_panel(w);
                        });
                    });
                });
            }
            clear(&data, tr("Reading the script..."));
            return;
        }
        let graph = match &self.dialog.graphs[&key] {
            Ok(g) => g,
            Err(message) => {
                let message = message.clone();
                self.dialog.key.clear();
                clear(&data, message);
                return;
            }
        };
        if !graph.has_dialogue() {
            self.dialog.key.clear();
            clear(&data, tr("This script contains no dialogue structure."));
            return;
        }
        if self.dialog.key != key {
            let mut flat = Vec::new();
            flatten(&graph.build_tree(), 0, &mut flat);
            self.dialog.by_id.clear();
            self.dialog.procs.clear();
            for (i, n) in flat.iter().enumerate() {
                if n.msg_id >= 0 {
                    self.dialog.by_id.entry(n.msg_id).or_insert(i);
                }
                if n.kind == NodeType::Call {
                    self.dialog.procs.entry(n.proc.clone()).or_insert(i);
                }
            }
            self.dialog.used_ids = graph.used_message_ids().into_iter().collect();
            self.dialog.layout = layout(&flat, self.settings.dialog_vertical);
            self.dialog.focused = -1;
            self.dialog.flat = flat;
            self.dialog.key = key;
        }

        // texts and markers follow the lines of the file as they are being edited
        let ed = self.current_editor().expect("checked above");
        let lines: HashMap<i32, (&str, SegmentStatus)> =
            ed.model.rows().iter().enumerate().map(|(i, r)| (r.id, (r.source.as_str(), ed.model.status(i)))).collect();
        let rows: Vec<DialogRow> = self
            .dialog
            .flat
            .iter()
            .enumerate()
            .map(|(i, n)| {
                let (x, y, w, h) = self.dialog.layout.rects.get(i).copied().unwrap_or_default();
                let (text, header, body, kind, status) = match n.kind {
                    NodeType::Call => (format!("\u{21aa} {}", n.proc), format!("\u{21aa} {}", n.proc), String::new(), 2, 3),
                    NodeType::Reference => {
                        let label = trf("\u{2934} %1 (see above)", &[&n.proc]);
                        (label.clone(), label, String::new(), 3, 3)
                    }
                    _ => {
                        let (marker, text, status) = match lines.get(&n.msg_id) {
                            Some((source, status)) => (
                                match status {
                                    SegmentStatus::Translated => "\u{2714} ",
                                    SegmentStatus::ToReview => "\u{2691} ",
                                    SegmentStatus::Untranslated => "\u{2022} ",
                                },
                                source.replace('\n', " \u{23ce} "),
                                match status {
                                    SegmentStatus::Translated => 1,
                                    SegmentStatus::ToReview => 2,
                                    SegmentStatus::Untranslated => 0,
                                },
                            ),
                            None => ("? ", trf("(line %1 not found in this file)", &[&n.msg_id.to_string()]), 3),
                        };
                        let prefix = match n.kind {
                            NodeType::Npc => tr("NPC"),
                            NodeType::Message => tr("NPC (no answer)"),
                            _ => {
                                let mut p = "\u{25b6}".to_string();
                                if n.iq < 0 {
                                    p.push_str(&tr(" (low IQ)"));
                                } else if n.iq > 4 {
                                    p.push_str(&trf(" (IQ %1+)", &[&n.iq.to_string()]));
                                }
                                p
                            }
                        };
                        (
                            format!("{marker}{prefix} [{}] {text}", n.msg_id),
                            format!("{prefix} [{}]", n.msg_id),
                            text,
                            if n.kind == NodeType::Player { 1 } else { 0 },
                            status,
                        )
                    }
                };
                DialogRow { text: text.into(), indent: n.depth, kind, header: header.into(), body: body.into(), status, x, y, w, h }
            })
            .collect();
        let unused = lines.keys().filter(|id| !self.dialog.used_ids.contains(id)).count();
        let current_id = ed.current.map(|r| ed.model.row(r).id);
        let status = trf("%1 nodes · %2 line(s) of the file are not part of the tree", &[&self.dialog.flat.len().to_string(), &unused.to_string()]);
        let shown = data.get_current();
        let still_on_it = usize::try_from(shown).ok().and_then(|i| self.dialog.flat.get(i)).is_some_and(|n| Some(n.msg_id) == current_id);
        let target = if still_on_it { shown } else { current_id.and_then(|id| self.dialog.by_id.get(&id)).map_or(-1, |&i| i as i32) };
        // the model is kept and only the nodes that changed are touched (the graph keeps its scroll position)
        crate::app::editor::sync_model(&self.dialog.model, rows);
        data.set_rows(ModelRc::from(self.dialog.model.clone()));
        data.set_vertical(self.settings.dialog_vertical);
        data.set_canvas_w(self.dialog.layout.width);
        data.set_canvas_h(self.dialog.layout.height);
        data.set_edges(self.dialog.layout.edges.as_str().into());
        data.set_status(status.into());
        data.set_current(target);
        // the graph scrolls to the node of the current line, or to the start of the script when the line is not in the tree
        let wanted = if target >= 0 { target } else if self.dialog.focused < 0 && !self.dialog.layout.rects.is_empty() { 0 } else { -1 };
        if wanted >= 0 && wanted != self.dialog.focused {
            if let Some(&(x, y, w, h)) = self.dialog.layout.rects.get(wanted as usize) {
                data.set_focus_x(x + w / 2.0);
                data.set_focus_y(y + h / 2.0);
                self.dialog.focus_serial += 1;
                data.set_focus_serial(self.dialog.focus_serial);
            }
            self.dialog.focused = wanted;
        }
    }
}

fn load_graph(project: &Project, rel: &str) -> Result<DialogGraph, String> {
    let script = project.read_dialog_script(rel).map_err(|e| e.to_string())?;
    if script.is_empty() {
        return Err(tr("No script found for this dialogue."));
    }
    if int2ssl::find_executable().is_none() {
        return Err(tr("int2ssl was not found next to the application."));
    }
    match int2ssl::decompile(&script) {
        Ok((ssl, _)) if !ssl.is_empty() => Ok(DialogGraph::parse_ssl(&ssl)),
        Ok(_) => Err(trf("Decompilation failed: %1", &[""])),
        Err(e) => Err(trf("Decompilation failed: %1", &[&e])),
    }
}
