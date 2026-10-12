#include "DialogGraph.h"

#include <QRegularExpression>
#include <QSet>

namespace {

// Removes comments and string literals (their content could contain keywords or braces).
QString stripNoise(const QString &ssl)
{
    QString out;
    out.reserve(ssl.size());
    const int n = ssl.size();
    int i = 0;
    while (i < n) {
        const QChar c = ssl.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && ssl.at(i + 1) == QLatin1Char('/')) {
            while (i < n && ssl.at(i) != QLatin1Char('\n'))
                ++i;
        } else if (c == QLatin1Char('/') && i + 1 < n && ssl.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(ssl.at(i) == QLatin1Char('*') && ssl.at(i + 1) == QLatin1Char('/')))
                ++i;
            i += 2;
        } else if (c == QLatin1Char('"')) {
            out += QStringLiteral("\"\"");
            ++i;
            while (i < n && ssl.at(i) != QLatin1Char('"')) {
                if (ssl.at(i) == QLatin1Char('\\'))
                    ++i;
                ++i;
            }
            ++i;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// Text between the parenthesis opened at `open` and its match; `end` receives the index after ')'.
QString balanced(const QString &s, int open, int &end)
{
    int depth = 0;
    for (int i = open; i < s.size(); ++i) {
        if (s.at(i) == QLatin1Char('('))
            ++depth;
        else if (s.at(i) == QLatin1Char(')') && --depth == 0) {
            end = i + 1;
            return s.mid(open + 1, i - open - 1);
        }
    }
    end = s.size();
    return s.mid(open + 1);
}

QStringList splitArgs(const QString &args)
{
    QStringList out;
    int depth = 0, start = 0;
    for (int i = 0; i < args.size(); ++i) {
        const QChar c = args.at(i);
        if (c == QLatin1Char('('))
            ++depth;
        else if (c == QLatin1Char(')'))
            --depth;
        else if (c == QLatin1Char(',') && depth == 0) {
            out << args.mid(start, i - start).trimmed();
            start = i + 1;
        }
    }
    out << args.mid(start).trimmed();
    return out;
}

int literal(const QString &arg)
{
    bool ok = false;
    const int v = arg.trimmed().toInt(&ok);
    return ok ? v : -1;
}

bool isIdentifier(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_]\\w*$"));
    return re.match(s).hasMatch();
}

}

DialogGraph DialogGraph::parseSsl(const QString &raw)
{
    DialogGraph graph;
    const QString ssl = stripNoise(raw);

    static const QRegularExpression procStart(QStringLiteral("\\bprocedure\\s+(\\w+)\\s*(?:\\([^)]*\\))?\\s*begin\\b"));
    static const QRegularExpression block(QStringLiteral("\\b(begin|end)\\b"));
    static const QRegularExpression event(QStringLiteral("\\b(gsay_reply|gsay_message|giq_option|gsay_option|call)\\b"));

    int pos = 0;
    for (;;) {
        const QRegularExpressionMatch start = procStart.match(ssl, pos);
        if (!start.hasMatch())
            break;

        // Find the matching `end` of this procedure.
        int depth = 1;
        int bodyEnd = ssl.size();
        int scan = start.capturedEnd();
        while (depth > 0) {
            const QRegularExpressionMatch m = block.match(ssl, scan);
            if (!m.hasMatch())
                break;
            depth += m.captured(1) == QLatin1String("begin") ? 1 : -1;
            scan = m.capturedEnd();
            if (depth == 0)
                bodyEnd = m.capturedStart();
        }
        const QString body = ssl.mid(start.capturedEnd(), bodyEnd - start.capturedEnd());

        DialogProc proc;
        proc.name = start.captured(1);
        int p = 0;
        for (;;) {
            const QRegularExpressionMatch m = event.match(body, p);
            if (!m.hasMatch())
                break;
            const QString kw = m.captured(1);
            p = m.capturedEnd();
            DialogEvent ev;
            if (kw == QLatin1String("call")) {
                static const QRegularExpression name(QStringLiteral("\\G\\s*([A-Za-z_]\\w*)"));
                const QRegularExpressionMatch nm = name.match(body, p);
                if (!nm.hasMatch())
                    continue;
                ev.kind = DialogEvent::Call;
                ev.target = nm.captured(1);
                p = nm.capturedEnd();
            } else {
                int open = p;
                while (open < body.size() && body.at(open).isSpace())
                    ++open;
                if (open >= body.size() || body.at(open) != QLatin1Char('('))
                    continue;
                int end = open;
                const QStringList a = splitArgs(balanced(body, open, end));
                p = end;
                if (kw == QLatin1String("gsay_reply") && a.size() >= 2) {
                    ev.kind = DialogEvent::Reply;
                    ev.msgId = literal(a.at(1));
                } else if (kw == QLatin1String("gsay_message") && a.size() >= 2) {
                    ev.kind = DialogEvent::Message;
                    ev.msgId = literal(a.at(1));
                } else if (kw == QLatin1String("giq_option") && a.size() >= 4) {
                    ev.kind = DialogEvent::Option;
                    ev.iq = literal(a.at(0));
                    ev.msgId = literal(a.at(2));
                    ev.target = isIdentifier(a.at(3)) ? a.at(3) : QString();
                } else if (kw == QLatin1String("gsay_option") && a.size() >= 3) {
                    ev.kind = DialogEvent::Option;
                    ev.msgId = literal(a.at(1));
                    ev.target = isIdentifier(a.at(2)) ? a.at(2) : QString();
                } else {
                    continue;
                }
            }
            proc.events.push_back(ev);
        }
        if (!graph.m_procs.contains(proc.name))
            graph.m_order << proc.name;
        graph.m_procs.insert(proc.name, proc);
        pos = bodyEnd;
    }
    return graph;
}

bool DialogGraph::hasDialogue() const
{
    for (const DialogProc &p : m_procs)
        if (p.hasDialogue())
            return true;
    return false;
}

const DialogProc *DialogGraph::proc(const QString &name) const
{
    const auto it = m_procs.constFind(name);
    return it == m_procs.constEnd() ? nullptr : &it.value();
}

QStringList DialogGraph::roots() const
{
    QSet<QString> referenced;
    for (const DialogProc &p : m_procs)
        for (const DialogEvent &e : p.events)
            if (!e.target.isEmpty())
                referenced.insert(e.target);

    QStringList roots;
    if (m_procs.contains(QStringLiteral("talk_p_proc")))
        roots << QStringLiteral("talk_p_proc");
    for (const QString &name : m_order)
        if (name != QLatin1String("talk_p_proc") && !referenced.contains(name) && m_procs.value(name).hasDialogue())
            roots << name;
    return roots;
}

void DialogGraph::expand(const QString &name, QVector<DialogNode> &out, QSet<QString> &expanded) const
{
    const DialogProc *p = proc(name);
    if (!p)
        return;
    expanded.insert(name);

    // The target of an option or a call: expanded the first time, a reference afterwards.
    auto attachTarget = [&](DialogNode &node, const QString &target) {
        if (target.isEmpty() || !m_procs.contains(target))
            return;
        if (expanded.contains(target)) {
            DialogNode ref;
            ref.type = DialogNode::Reference;
            ref.proc = target;
            node.children.push_back(ref);
        } else {
            expand(target, node.children, expanded);
        }
    };

    DialogNode *current = nullptr;   // the NPC line the following options belong to
    for (const DialogEvent &e : p->events) {
        DialogNode node;
        node.proc = name;
        node.msgId = e.msgId;
        switch (e.kind) {
        case DialogEvent::Reply:
            node.type = DialogNode::Npc;
            out.push_back(node);
            current = &out.last();
            break;
        case DialogEvent::Message:
            node.type = DialogNode::Message;
            out.push_back(node);
            current = nullptr;
            break;
        case DialogEvent::Option: {
            node.type = DialogNode::Player;
            node.iq = e.iq;
            attachTarget(node, e.target);
            QVector<DialogNode> &dest = current ? current->children : out;
            dest.push_back(node);
            break;
        }
        case DialogEvent::Call:
            if (!m_procs.contains(e.target))
                break;
            node.type = DialogNode::Call;
            node.proc = e.target;
            node.msgId = -1;
            attachTarget(node, e.target == name ? QString() : e.target);
            // A call whose procedure was already shown is a plain reference.
            if (node.children.size() == 1 && node.children.first().type == DialogNode::Reference)
                node.type = DialogNode::Reference, node.children.clear();
            out.push_back(node);
            current = nullptr;
            break;
        }
    }
}

QVector<DialogNode> DialogGraph::buildTree() const
{
    QVector<DialogNode> tree;
    QSet<QString> expanded;
    for (const QString &root : roots()) {
        if (expanded.contains(root))
            continue;
        DialogNode node;
        node.type = DialogNode::Call;
        node.proc = root;
        expand(root, node.children, expanded);
        tree.push_back(node);
    }
    return tree;
}

QVector<int> DialogGraph::usedMessageIds() const
{
    QVector<int> ids;
    QSet<int> seen;
    for (const QString &name : m_order)
        for (const DialogEvent &e : m_procs.value(name).events)
            if (e.kind != DialogEvent::Call && e.msgId >= 0 && !seen.contains(e.msgId)) {
                seen.insert(e.msgId);
                ids << e.msgId;
            }
    return ids;
}
