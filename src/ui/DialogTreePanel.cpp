#include "DialogTreePanel.h"

#include "Theme.h"
#include "core/Int2Ssl.h"

#include <QApplication>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

enum Role { TypeRole = Qt::UserRole + 1, MsgIdRole, ProcRole, IqRole };

QString oneLine(const QString &text)
{
    QString s = text;
    s.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
    return s;
}

}

DialogTreePanel::DialogTreePanel(QWidget *parent)
    : QWidget(parent)
{
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(16);
    m_tree->setTextElideMode(Qt::ElideRight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_status);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) { onCurrentChanged(item); });
    clear();
}

QString DialogTreePanel::statusText() const
{
    return m_status->text();
}

void DialogTreePanel::clear()
{
    m_key.clear();
    m_tree->clear();
    m_procItems.clear();
    m_byId.clear();
    m_all.clear();
    m_status->setText(tr("Open a dialogue file to see its tree."));
}

void DialogTreePanel::showFile(const std::shared_ptr<Project> &project, const QString &relPath,
                               const QHash<int, DialogLine> &lines)
{
    if (Project::kindOf(relPath) != FileKind::Dialog) {
        clear();
        m_status->setText(tr("The dialogue tree is available for files of the /dialog folder."));
        return;
    }
    const QString key = project->config().id + QLatin1Char('|') + relPath;
    if (key == m_key) {
        updateLines(lines);
        return;
    }

    m_key = key;
    m_lines = lines;
    m_tree->clear();
    m_procItems.clear();
    m_byId.clear();
    m_all.clear();

    if (!m_graphs.contains(key) && !m_errors.contains(key)) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString error;
        const QByteArray script = project->readDialogScript(relPath, &error);
        QString ssl;
        if (!script.isEmpty()) {
            if (Int2Ssl::findExecutable().isEmpty())
                error = tr("int2ssl was not found next to the application.");
            else {
                ssl = Int2Ssl::decompile(script, &error);
                if (ssl.isEmpty())
                    error = tr("Decompilation failed: %1").arg(error);
            }
        }
        QApplication::restoreOverrideCursor();
        if (ssl.isEmpty())
            m_errors.insert(key, error.isEmpty() ? tr("No script found for this dialogue.") : error);
        else
            m_graphs.insert(key, DialogGraph::parseSsl(ssl));
    }

    if (m_errors.contains(key)) {
        m_status->setText(m_errors.value(key));
        return;
    }
    const DialogGraph &graph = m_graphs[key];
    if (!graph.hasDialogue()) {
        m_status->setText(tr("This script contains no dialogue structure."));
        return;
    }
    populate(graph.buildTree(), nullptr);
    m_tree->expandAll();

    // Lines of the file that the script never shows (floating texts, other scripts...).
    int unused = 0;
    QSet<int> used;
    for (int id : graph.usedMessageIds())
        used.insert(id);
    for (auto it = m_lines.cbegin(); it != m_lines.cend(); ++it)
        unused += used.contains(it.key()) ? 0 : 1;
    m_status->setText(tr("%1 nodes · %2 line(s) of the file are not part of the tree").arg(m_all.size()).arg(unused));
}

void DialogTreePanel::populate(const QVector<DialogNode> &nodes, QTreeWidgetItem *parent)
{
    for (const DialogNode &n : nodes) {
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setData(0, TypeRole, n.type);
        item->setData(0, MsgIdRole, n.msgId);
        item->setData(0, ProcRole, n.proc);
        item->setData(0, IqRole, n.iq);
        m_all << item;
        if (n.msgId >= 0)
            m_byId.insert(n.msgId, item);
        if (n.type == DialogNode::Call && !m_procItems.contains(n.proc))
            m_procItems.insert(n.proc, item);
        applyText(item);
        populate(n.children, item);
    }
}

void DialogTreePanel::applyText(QTreeWidgetItem *item) const
{
    const auto type = DialogNode::Type(item->data(0, TypeRole).toInt());
    const int id = item->data(0, MsgIdRole).toInt();
    const QString proc = item->data(0, ProcRole).toString();
    const int iq = item->data(0, IqRole).toInt();
    QFont font = item->font(0);
    font.setBold(false);
    font.setItalic(false);

    if (type == DialogNode::Call || type == DialogNode::Reference) {
        const bool ref = type == DialogNode::Reference;
        item->setText(0, ref ? tr("⤴ %1 (see above)").arg(proc) : QStringLiteral("↪ %1").arg(proc));
        font.setItalic(ref);
        font.setBold(!ref);
        item->setFont(0, font);
        item->setForeground(0, QApplication::palette().color(QPalette::Disabled, QPalette::Text));
        item->setToolTip(0, ref ? tr("Click to jump to the first occurrence") : QString());
        return;
    }

    const auto it = m_lines.constFind(id);
    QString marker = QStringLiteral("? ");
    QString tip;
    if (it != m_lines.constEnd()) {
        marker = it->status == SegmentStatus::Translated ? QStringLiteral("✔ ")
            : it->status == SegmentStatus::ToReview ? QStringLiteral("⚑ ") : QStringLiteral("• ");
        tip = it->source.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        if (!it->target.isEmpty())
            tip += QStringLiteral("<hr>") + it->target.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    }
    const QString text = it == m_lines.constEnd() ? tr("(line %1 not found in this file)").arg(id) : oneLine(it->source);

    QString prefix;
    switch (type) {
    case DialogNode::Npc: prefix = tr("NPC"); break;
    case DialogNode::Message: prefix = tr("NPC (no answer)"); break;
    default:
        prefix = QStringLiteral("▶");
        if (iq < 0)
            prefix += tr(" (low IQ)");
        else if (iq > 4) // 4 is the ordinary option (NOption); only unusual IQ requirements are worth showing
            prefix += tr(" (IQ %1+)").arg(iq);
        break;
    }
    item->setText(0, QStringLiteral("%1%2 [%3] %4").arg(marker, prefix).arg(id).arg(text));
    item->setToolTip(0, tip);
    item->setFont(0, font);
    item->setForeground(0, type == DialogNode::Player ? Theme::colors().term : QApplication::palette().color(QPalette::Text));
}

void DialogTreePanel::updateLines(const QHash<int, DialogLine> &lines)
{
    m_lines = lines;
    for (QTreeWidgetItem *item : std::as_const(m_all))
        applyText(item);
}

void DialogTreePanel::selectId(int id)
{
    const auto it = m_byId.constFind(id);
    if (it == m_byId.constEnd())
        return;
    // The line is reachable from several places; stay on the current item if it already shows this id.
    QTreeWidgetItem *current = m_tree->currentItem();
    if (current && current->data(0, MsgIdRole).toInt() == id)
        return;
    m_selecting = true;
    m_tree->setCurrentItem(it.value());
    m_tree->scrollToItem(it.value());
    m_selecting = false;
}

void DialogTreePanel::onCurrentChanged(QTreeWidgetItem *item)
{
    if (!item || m_selecting)
        return;
    const auto type = DialogNode::Type(item->data(0, TypeRole).toInt());
    if (type == DialogNode::Reference) {
        QTreeWidgetItem *target = m_procItems.value(item->data(0, ProcRole).toString());
        if (target) {
            m_tree->setCurrentItem(target);
            m_tree->scrollToItem(target);
        }
        return;
    }
    const int id = item->data(0, MsgIdRole).toInt();
    if (id >= 0)
        emit idActivated(id);
}
