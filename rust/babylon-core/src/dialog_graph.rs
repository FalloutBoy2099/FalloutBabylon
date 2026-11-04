//! What a decompiled Fallout script (SSL, as produced by int2ssl) says about its dialogue:
//! for every procedure the NPC lines, the player options and the procedures it calls, in source order.

use regex::Regex;
use std::collections::{HashMap, HashSet};
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventKind {
    Reply,
    Message,
    Option,
    Call,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DialogEvent {
    pub kind: EventKind,
    /// Line in the dialogue .msg file (-1 when it is not a literal number).
    pub msg_id: i32,
    /// Procedure called / chosen by an option.
    pub target: String,
    /// IQ condition of an option (giq_option: <0 low IQ only, >0 minimum IQ).
    pub iq: i32,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct DialogProc {
    pub name: String,
    pub events: Vec<DialogEvent>,
}

impl DialogProc {
    pub fn has_dialogue(&self) -> bool {
        self.events.iter().any(|e| e.kind != EventKind::Call)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeType {
    Npc,
    Message,
    Player,
    Call,
    Reference,
}

/// A node of the dialogue tree shown to the translator.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DialogNode {
    pub kind: NodeType,
    pub msg_id: i32,
    /// Call/Reference: the procedure; others: the procedure the line belongs to.
    pub proc: String,
    pub iq: i32,
    pub children: Vec<DialogNode>,
}

impl DialogNode {
    fn new(kind: NodeType) -> Self {
        DialogNode { kind, msg_id: -1, proc: String::new(), iq: 0, children: Vec::new() }
    }
}

/// Removes comments and string literals (their content could contain keywords or braces).
fn strip_noise(ssl: &str) -> String {
    let c: Vec<char> = ssl.chars().collect();
    let n = c.len();
    let mut out = String::with_capacity(ssl.len());
    let mut i = 0;
    while i < n {
        if c[i] == '/' && i + 1 < n && c[i + 1] == '/' {
            while i < n && c[i] != '\n' {
                i += 1;
            }
        } else if c[i] == '/' && i + 1 < n && c[i + 1] == '*' {
            i += 2;
            while i + 1 < n && !(c[i] == '*' && c[i + 1] == '/') {
                i += 1;
            }
            i += 2;
        } else if c[i] == '"' {
            out.push_str("\"\"");
            i += 1;
            while i < n && c[i] != '"' {
                if c[i] == '\\' {
                    i += 1;
                }
                i += 1;
            }
            i += 1;
        } else {
            out.push(c[i]);
            i += 1;
        }
    }
    out
}

/// Text between the parenthesis opened at `open` and its match; also returns the index after ')'.
fn balanced(s: &str, open: usize) -> (&str, usize) {
    let mut depth = 0;
    for (i, ch) in s[open..].char_indices() {
        match ch {
            '(' => depth += 1,
            ')' => {
                depth -= 1;
                if depth == 0 {
                    let close = open + i;
                    return (&s[open + 1..close], close + 1);
                }
            }
            _ => {}
        }
    }
    (&s[open + 1..], s.len())
}

fn split_args(args: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut depth = 0;
    let mut start = 0;
    for (i, ch) in args.char_indices() {
        match ch {
            '(' => depth += 1,
            ')' => depth -= 1,
            ',' if depth == 0 => {
                out.push(args[start..i].trim().to_string());
                start = i + 1;
            }
            _ => {}
        }
    }
    out.push(args[start..].trim().to_string());
    out
}

fn literal(arg: &str) -> i32 {
    arg.trim().parse::<i32>().unwrap_or(-1)
}

fn is_identifier(s: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"^[A-Za-z_]\w*$").unwrap()).is_match(s)
}

#[derive(Default)]
pub struct DialogGraph {
    procs: HashMap<String, DialogProc>,
    order: Vec<String>,
}

impl DialogGraph {
    pub fn parse_ssl(raw: &str) -> DialogGraph {
        static PROC_START: OnceLock<Regex> = OnceLock::new();
        static BLOCK: OnceLock<Regex> = OnceLock::new();
        static EVENT: OnceLock<Regex> = OnceLock::new();
        static NAME: OnceLock<Regex> = OnceLock::new();
        let proc_start = PROC_START.get_or_init(|| Regex::new(r"\bprocedure\s+(\w+)\s*(?:\([^)]*\))?\s*begin\b").unwrap());
        let block = BLOCK.get_or_init(|| Regex::new(r"\b(begin|end)\b").unwrap());
        let event = EVENT.get_or_init(|| Regex::new(r"\b(gsay_reply|gsay_message|giq_option|gsay_option|call)\b").unwrap());
        let name_re = NAME.get_or_init(|| Regex::new(r"^\s*([A-Za-z_]\w*)").unwrap());

        let mut graph = DialogGraph::default();
        let ssl = strip_noise(raw);

        let mut pos = 0;
        while let Some(start) = proc_start.captures_at(&ssl, pos) {
            let whole = start.get(0).unwrap();

            // Find the matching `end` of this procedure.
            let mut depth = 1;
            let mut body_end = ssl.len();
            let mut scan = whole.end();
            while depth > 0 {
                let Some(m) = block.captures_at(&ssl, scan) else { break };
                let word = m.get(1).unwrap();
                depth += if word.as_str() == "begin" { 1 } else { -1 };
                scan = m.get(0).unwrap().end();
                if depth == 0 {
                    body_end = m.get(0).unwrap().start();
                }
            }
            let body = &ssl[whole.end()..body_end];

            let mut proc = DialogProc { name: start[1].to_string(), events: Vec::new() };
            let mut p = 0;
            while let Some(m) = event.captures_at(body, p) {
                let kw = m[1].to_string();
                p = m.get(0).unwrap().end();
                let ev;
                if kw == "call" {
                    let Some(nm) = name_re.captures(&body[p..]) else { continue };
                    ev = DialogEvent { kind: EventKind::Call, msg_id: -1, target: nm[1].to_string(), iq: 0 };
                    p += nm.get(0).unwrap().end();
                } else {
                    let mut open = p;
                    while open < body.len() && body[open..].chars().next().is_some_and(|c| c.is_whitespace()) {
                        open += body[open..].chars().next().unwrap().len_utf8();
                    }
                    if open >= body.len() || !body[open..].starts_with('(') {
                        continue;
                    }
                    let (inner, end) = balanced(body, open);
                    let a = split_args(inner);
                    p = end;
                    let target = |s: &str| if is_identifier(s) { s.to_string() } else { String::new() };
                    ev = match kw.as_str() {
                        "gsay_reply" if a.len() >= 2 => {
                            DialogEvent { kind: EventKind::Reply, msg_id: literal(&a[1]), target: String::new(), iq: 0 }
                        }
                        "gsay_message" if a.len() >= 2 => {
                            DialogEvent { kind: EventKind::Message, msg_id: literal(&a[1]), target: String::new(), iq: 0 }
                        }
                        "giq_option" if a.len() >= 4 => DialogEvent {
                            kind: EventKind::Option,
                            iq: literal(&a[0]),
                            msg_id: literal(&a[2]),
                            target: target(&a[3]),
                        },
                        "gsay_option" if a.len() >= 3 => DialogEvent {
                            kind: EventKind::Option,
                            iq: 0,
                            msg_id: literal(&a[1]),
                            target: target(&a[2]),
                        },
                        _ => continue,
                    };
                }
                proc.events.push(ev);
            }
            if !graph.procs.contains_key(&proc.name) {
                graph.order.push(proc.name.clone());
            }
            graph.procs.insert(proc.name.clone(), proc);
            pos = body_end;
        }
        graph
    }

    pub fn is_empty(&self) -> bool {
        self.order.is_empty()
    }

    pub fn has_dialogue(&self) -> bool {
        self.procs.values().any(|p| p.has_dialogue())
    }

    pub fn proc(&self, name: &str) -> Option<&DialogProc> {
        self.procs.get(name)
    }

    /// Procedures that start a conversation: talk_p_proc, plus unreferenced procedures holding dialogue.
    pub fn roots(&self) -> Vec<String> {
        let referenced: HashSet<&str> = self
            .procs
            .values()
            .flat_map(|p| p.events.iter())
            .filter(|e| !e.target.is_empty())
            .map(|e| e.target.as_str())
            .collect();
        let mut roots = Vec::new();
        if self.procs.contains_key("talk_p_proc") {
            roots.push("talk_p_proc".to_string());
        }
        for name in &self.order {
            if name != "talk_p_proc" && !referenced.contains(name.as_str()) && self.procs[name].has_dialogue() {
                roots.push(name.clone());
            }
        }
        roots
    }

    fn expand(&self, name: &str, out: &mut Vec<DialogNode>, expanded: &mut HashSet<String>) {
        let Some(p) = self.procs.get(name) else { return };
        expanded.insert(name.to_string());

        // The target of an option or a call: expanded the first time, a reference afterwards.
        let attach_target = |node: &mut DialogNode, target: &str, expanded: &mut HashSet<String>| {
            if target.is_empty() || !self.procs.contains_key(target) {
                return;
            }
            if expanded.contains(target) {
                let mut r = DialogNode::new(NodeType::Reference);
                r.proc = target.to_string();
                node.children.push(r);
            } else {
                self.expand(target, &mut node.children, expanded);
            }
        };

        let mut current: Option<usize> = None; // the NPC line the following options belong to
        for e in &p.events {
            let mut node = DialogNode::new(NodeType::Npc);
            node.proc = name.to_string();
            node.msg_id = e.msg_id;
            match e.kind {
                EventKind::Reply => {
                    node.kind = NodeType::Npc;
                    out.push(node);
                    current = Some(out.len() - 1);
                }
                EventKind::Message => {
                    node.kind = NodeType::Message;
                    out.push(node);
                    current = None;
                }
                EventKind::Option => {
                    node.kind = NodeType::Player;
                    node.iq = e.iq;
                    attach_target(&mut node, &e.target, expanded);
                    match current {
                        Some(i) => out[i].children.push(node),
                        None => out.push(node),
                    }
                }
                EventKind::Call => {
                    if !self.procs.contains_key(&e.target) {
                        continue;
                    }
                    node.kind = NodeType::Call;
                    node.proc = e.target.clone();
                    node.msg_id = -1;
                    let target = if e.target == name { "" } else { e.target.as_str() };
                    attach_target(&mut node, target, expanded);
                    // A call whose procedure was already shown is a plain reference.
                    if node.children.len() == 1 && node.children[0].kind == NodeType::Reference {
                        node.kind = NodeType::Reference;
                        node.children.clear();
                    }
                    out.push(node);
                    current = None;
                }
            }
        }
    }

    /// The conversation as a tree. A procedure reached again is shown as a Reference node.
    pub fn build_tree(&self) -> Vec<DialogNode> {
        let mut tree = Vec::new();
        let mut expanded: HashSet<String> = HashSet::new();
        for root in self.roots() {
            if expanded.contains(&root) {
                continue;
            }
            let mut node = DialogNode::new(NodeType::Call);
            node.proc = root.clone();
            self.expand(&root, &mut node.children, &mut expanded);
            tree.push(node);
        }
        tree
    }

    /// Every message id used by a Reply/Message/Option, in order of first appearance.
    pub fn used_message_ids(&self) -> Vec<i32> {
        let mut ids = Vec::new();
        let mut seen = HashSet::new();
        for name in &self.order {
            for e in &self.procs[name].events {
                if e.kind != EventKind::Call && e.msg_id >= 0 && seen.insert(e.msg_id) {
                    ids.push(e.msg_id);
                }
            }
        }
        ids
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dialog_graph_from_ssl() {
        let ssl = "procedure start;\n\
procedure talk_p_proc;\n\
procedure Node00;\n procedure Node01;\n procedure Node02;\n procedure Combat;\n\
// a comment with procedure Fake begin\n\
procedure talk_p_proc\nbegin\n\
   start_gdialog(359, self_obj, 4, -1, -1);\n\
   gsay_start;\n\
   if (local_var(4) == 1) then begin\n      call Combat();\n   end\n\
   else begin\n      call Node00();\n   end\n\
   gsay_end;\n\
end\n\
procedure Node00\nbegin\n\
   gsay_reply(359, 102);\n\
   giq_option(-3, 359, 103, Node01, 0);\n\
   giq_option(4, 359, 104, Node02, 0);\n\
   giq_option(5, 359, 105, Combat, 0);\n\
end\n\
procedure Node01\nbegin\n\
   gsay_reply(359, 106 + 0);\n\
   gsay_reply(359, 107);\n\
   giq_option(-3, 359, 108, Node02, 0);\n\
end\n\
procedure Node02\nbegin\n\
   gsay_message(359, 110, 0);\n\
   call Combat();\n\
end\n\
procedure Combat\nbegin\n\
   field_change := \"begin end\";\n\
   call Combat();\n\
end\n";
        let g = DialogGraph::parse_ssl(ssl);
        assert!(g.has_dialogue());
        assert_eq!(g.roots(), vec!["talk_p_proc"]);
        let node00 = g.proc("Node00").unwrap();
        assert_eq!(node00.events.len(), 4);
        assert_eq!(node00.events[1].iq, -3);
        assert_eq!(node00.events[1].target, "Node01");
        assert_eq!(g.proc("Node01").unwrap().events[0].msg_id, -1); // not a literal: kept but unnumbered
        assert_eq!(g.proc("Combat").unwrap().events.len(), 1); // strings do not break procedure boundaries
        assert!(g.proc("Fake").is_none());

        let tree = g.build_tree();
        assert_eq!(tree.len(), 1);
        let root = &tree[0];
        assert_eq!(root.proc, "talk_p_proc");
        // talk_p_proc -> call Combat, call Node00
        assert_eq!(root.children.len(), 2);
        assert_eq!(root.children[0].proc, "Combat");
        let node00 = &root.children[1]; // Call Node00
        assert_eq!(node00.kind, NodeType::Call);
        assert_eq!(node00.children.len(), 1); // its NPC reply (102)
        let reply = &node00.children[0];
        assert_eq!(reply.kind, NodeType::Npc);
        assert_eq!(reply.msg_id, 102);
        assert_eq!(reply.children.len(), 3); // 3 player options
        assert_eq!(reply.children[0].kind, NodeType::Player);
        assert_eq!(reply.children[0].msg_id, 103);
        // option 103 -> Node01 (expanded here), whose option 108 -> Node02
        assert!(!reply.children[0].children.is_empty());
        // option 104 -> Node02, already expanded under 108: a reference
        assert_eq!(reply.children[1].children[0].kind, NodeType::Reference);
        // option 105 -> Combat, already shown first: a reference
        assert_eq!(reply.children[2].children[0].kind, NodeType::Reference);
        assert_eq!(g.used_message_ids(), vec![102, 103, 104, 105, 107, 108, 110]);
    }
}
