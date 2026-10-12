#include "ProjectTree.h"

#include "Theme.h"
#include "core/Project.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

enum Column { ColName, ColProgress, ColLines, ColWords, ColCount };

class ProgressDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        // background / selection
        const QString text = opt.text;
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

        const QModelIndex name = index.siblingAtColumn(0);   // the numbers are stored on the first column only
        if (!name.data(ProjectTree::KnownRole).toBool()) {
            painter->setPen(opt.palette.color(QPalette::Disabled, QPalette::Text));
            painter->drawText(opt.rect, Qt::AlignCenter, QStringLiteral("…"));
            return;
        }
        const int lines = name.data(ProjectTree::LinesRole).toInt();
        const int untranslated = name.data(ProjectTree::UntranslatedRole).toInt();
        const int review = name.data(ProjectTree::ReviewRole).toInt();
        const double done = lines ? double(lines - untranslated) / lines : 1.0;
        const double toReview = lines ? double(review) / lines : 0.0;

        const QRect bar = opt.rect.adjusted(4, 3, -4, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QColor track = opt.palette.color(QPalette::Mid);
        track.setAlpha(70);
        painter->setPen(Qt::NoPen);
        painter->setBrush(track);
        painter->drawRoundedRect(bar, 3, 3);

        painter->setClipRect(QRect(bar.left(), bar.top(), int(bar.width() * done + 0.5), bar.height()));
        painter->setBrush(Theme::isDark() ? QColor(0x3d, 0x9a, 0x55) : QColor(0x4c, 0xaf, 0x62));
        painter->drawRoundedRect(bar, 3, 3);
        if (toReview > 0) {
            // the share "to review" is part of the translated bar, drawn in amber at its end
            const int w2 = qMax(2, int(bar.width() * toReview + 0.5));
            const int x2 = qMax(bar.left(), int(bar.left() + bar.width() * done + 0.5) - w2);
            painter->setClipRect(QRect(x2, bar.top(), w2, bar.height()));
            painter->setBrush(QColor(0xe0, 0xa0, 0x20));
            painter->drawRoundedRect(bar, 3, 3);
        }
        painter->setClipping(false);
        painter->setPen(opt.palette.color(QPalette::Text));
        painter->drawText(bar, Qt::AlignCenter, QStringLiteral("%1%").arg(int(done * 100.0 + (done < 1.0 ? 0.0 : 0.5))));
        painter->restore();
        Q_UNUSED(text)
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), option.fontMetrics.height() + 6));
        return s;
    }
};

}

// ----------------------------------------------------------------------------- filter proxy

TreeFilterProxy::TreeFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(true);
    // Statistics arrive by the thousand: re-filtering on every change would be quadratic. The owner calls
    // invalidateFilter() once per batch when a filter is active.
    setDynamicSortFilter(false);
}

bool TreeFilterProxy::filterActive() const
{
    return m_filter != ProjectTree::ShowAll || !m_text.isEmpty();
}

void TreeFilterProxy::setFilter(int filter, const QString &text)
{
    m_filter = filter;
    m_text = text.trimmed();
    invalidateFilter();
}

bool TreeFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &parent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, parent);
    if (idx.data(ProjectTree::TypeRole).toInt() != ProjectTree::FileNode)
        return m_filter == ProjectTree::ShowAll && m_text.isEmpty(); // shown through their children otherwise

    if (!m_text.isEmpty()
        && !idx.data(Qt::DisplayRole).toString().contains(m_text, Qt::CaseInsensitive)
        && !idx.data(ProjectTree::PathRole).toString().contains(m_text, Qt::CaseInsensitive))
        return false;
    if (m_filter == ProjectTree::ShowAll)
        return true;
    if (!idx.data(ProjectTree::KnownRole).toBool())
        return false;
    const int lines = idx.data(ProjectTree::LinesRole).toInt();
    const int untranslated = idx.data(ProjectTree::UntranslatedRole).toInt();
    const int review = idx.data(ProjectTree::ReviewRole).toInt();
    switch (m_filter) {
    case ProjectTree::ShowUntouched: return lines > 0 && untranslated == lines;
    case ProjectTree::ShowPartial: return untranslated > 0 && untranslated < lines;
    case ProjectTree::ShowComplete: return untranslated == 0;
    case ProjectTree::ShowReview: return review > 0;
    }
    return true;
}

// ----------------------------------------------------------------------------- tree widget

ProjectTree::ProjectTree(QWidget *parent)
    : QWidget(parent)
{
    m_model = new QStandardItemModel(this);
    m_model->setColumnCount(ColCount);
    m_proxy = new TreeFilterProxy(this);
    m_proxy->setSourceModel(m_model);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Filter files..."));
    m_search->setClearButtonEnabled(true);
    m_filter = new QComboBox;
    m_filter->addItem(tr("All files"), ShowAll);
    m_filter->addItem(tr("Not translated"), ShowUntouched);
    m_filter->addItem(tr("Partially translated"), ShowPartial);
    m_filter->addItem(tr("Complete"), ShowComplete);
    m_filter->addItem(tr("To review"), ShowReview);

    m_view = new QTreeView;
    m_view->setModel(m_proxy);
    m_view->setItemDelegateForColumn(ColProgress, new ProgressDelegate(m_view));
    m_view->setUniformRowHeights(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(ColProgress, QHeaderView::Interactive);
    m_view->header()->setSectionResizeMode(ColLines, QHeaderView::ResizeToContents);
    m_view->header()->setSectionResizeMode(ColWords, QHeaderView::ResizeToContents);
    m_view->setColumnWidth(ColProgress, 110);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_search);
    layout->addWidget(m_filter);
    layout->addWidget(m_view, 1);

    m_model->setHorizontalHeaderLabels({tr("File"), tr("Progress"), tr("Lines"), tr("Words left")});

    auto refilter = [this] { m_proxy->setFilter(m_filter->currentData().toInt(), m_search->text()); };
    connect(m_search, &QLineEdit::textChanged, this, refilter);
    connect(m_filter, &QComboBox::currentIndexChanged, this, refilter);

    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex &proxyIndex) {
        const QModelIndex idx = m_proxy->mapToSource(proxyIndex).siblingAtColumn(0);
        if (idx.data(TypeRole).toInt() == FileNode)
            emit fileActivated(idx.data(ProjectRole).toString(), idx.data(PathRole).toString());
    });
    connect(m_view, &QTreeView::customContextMenuRequested, this, &ProjectTree::contextMenu);
}

void ProjectTree::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
        m_view->viewport()->update();
    QWidget::changeEvent(event);
}

void ProjectTree::addProject(const QString &id, const QString &name, const QStringList &files)
{
    removeProject(id);

    auto makeNode = [&](const QString &text, NodeType type, const QString &path = {}) {
        auto *item = new QStandardItem(text);
        item->setData(type, TypeRole);
        item->setData(id, ProjectRole);
        item->setData(path, PathRole);
        item->setData(false, KnownRole);
        item->setEditable(false);
        return item;
    };
    auto rowFor = [&](QStandardItem *first) {
        QList<QStandardItem *> row{first};
        for (int c = 1; c < ColCount; ++c) {
            auto *cell = new QStandardItem;
            cell->setEditable(false);
            cell->setData(first->data(TypeRole), TypeRole);
            cell->setData(false, KnownRole);
            row << cell;
        }
        return row;
    };

    auto *project = makeNode(name, ProjectNode);
    QFont bold = project->font();
    bold.setBold(true);
    project->setFont(bold);
    QStandardItem *groups[3] = {
        makeNode(tr("1 · Game files — do first"), GroupNode),
        makeNode(tr("2 · Dialogues"), GroupNode),
        makeNode(tr("3 · Other files"), GroupNode),
    };
    groups[0]->setToolTip(tr("Recommended: translate the /game files before the /dialog files. They hold the names of "
                             "items, creatures, skills and places, which feed the glossary."));
    QList<QStandardItem *> projectRow = rowFor(project);
    m_model->appendRow(projectRow);
    for (QStandardItem *g : groups)
        project->appendRow(rowFor(g));

    for (const QString &rel : files) {
        const FileKind kind = Project::kindOf(rel);
        QStandardItem *group = groups[int(kind)];
        auto *item = makeNode(Statistics::displayNameFor(rel), FileNode, rel);
        item->setToolTip(rel);
        group->appendRow(rowFor(item));
    }
    for (int g = 0; g < 3; ++g)
        if (groups[g]->rowCount() == 0)
            project->removeRow(groups[g]->row());

    m_projects.insert(id, project);
    expandProject(id);
}

void ProjectTree::expandProject(const QString &id)
{
    QStandardItem *p = m_projects.value(id);
    if (!p)
        return;
    m_view->expand(m_proxy->mapFromSource(p->index()));
}

void ProjectTree::removeProject(const QString &id)
{
    if (QStandardItem *p = m_projects.take(id))
        m_model->removeRow(p->row());
}

void ProjectTree::renameProject(const QString &id, const QString &name)
{
    if (QStandardItem *p = m_projects.value(id))
        p->setText(name);
}

QStandardItem *ProjectTree::findFile(const QString &id, const QString &relPath) const
{
    QStandardItem *project = m_projects.value(id);
    if (!project)
        return nullptr;
    for (int g = 0; g < project->rowCount(); ++g) {
        QStandardItem *group = project->child(g);
        for (int f = 0; f < group->rowCount(); ++f)
            if (group->child(f)->data(PathRole).toString() == relPath)
                return group->child(f);
    }
    return nullptr;
}

namespace {

void storeNumbers(QStandardItem *nameItem, qint64 lines, qint64 untranslated, qint64 review, qint64 words, qint64 wordsLeft,
                  bool known)
{
    nameItem->setData(int(lines), ProjectTree::LinesRole);
    nameItem->setData(int(untranslated), ProjectTree::UntranslatedRole);
    nameItem->setData(int(review), ProjectTree::ReviewRole);
    nameItem->setData(words, ProjectTree::WordsRole);
    nameItem->setData(wordsLeft, ProjectTree::WordsLeftRole);
    nameItem->setData(known, ProjectTree::KnownRole);
}

void setNumberCell(QStandardItem *cell, qint64 value)
{
    if (!cell)
        return;
    cell->setText(QString::number(value));
    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

}

void ProjectTree::applyStats(QStandardItem *item, const FileStats &s)
{
    QStandardItem *parent = item->parent();
    storeNumbers(item, s.lines, s.untranslated, s.review, s.words, s.wordsRemaining, true);
    setNumberCell(parent->child(item->row(), ColLines), s.lines);
    setNumberCell(parent->child(item->row(), ColWords), s.wordsRemaining);
    item->setToolTip(tr("%1\n%2 lines, %3 untranslated, %4 to review\n%5 words / %6 characters left")
                         .arg(s.relPath).arg(s.lines).arg(s.untranslated).arg(s.review)
                         .arg(s.wordsRemaining).arg(s.charsRemaining));
}

void ProjectTree::aggregate(QStandardItem *node)
{
    qint64 lines = 0, untranslated = 0, review = 0, words = 0, wordsLeft = 0;
    bool known = node->rowCount() > 0;
    for (int i = 0; i < node->rowCount(); ++i) {
        QStandardItem *child = node->child(i);
        if (child->data(TypeRole).toInt() != FileNode)
            aggregate(child);
        if (!child->data(KnownRole).toBool()) {
            known = false;
            continue;
        }
        lines += child->data(LinesRole).toLongLong();
        untranslated += child->data(UntranslatedRole).toLongLong();
        review += child->data(ReviewRole).toLongLong();
        words += child->data(WordsRole).toLongLong();
        wordsLeft += child->data(WordsLeftRole).toLongLong();
    }
    storeNumbers(node, lines, untranslated, review, words, wordsLeft, known || lines > 0);
    QStandardItem *parent = node->parent();
    auto cellAt = [&](int col) { return parent ? parent->child(node->row(), col) : m_model->item(node->row(), col); };
    setNumberCell(cellAt(ColLines), lines);
    setNumberCell(cellAt(ColWords), wordsLeft);
}

void ProjectTree::recomputeAggregates(QStandardItem *project)
{
    for (int g = 0; g < project->rowCount(); ++g)
        aggregate(project->child(g));
    aggregate(project);
}

void ProjectTree::setStats(const QString &id, const QVector<FileStats> &stats)
{
    for (const FileStats &s : stats)
        if (QStandardItem *item = findFile(id, s.relPath))
            applyStats(item, s);
    if (QStandardItem *p = m_projects.value(id))
        recomputeAggregates(p);
    if (m_proxy->filterActive())
        m_proxy->refilter();
}

void ProjectTree::updateStats(const QString &id, const FileStats &stats)
{
    if (QStandardItem *item = findFile(id, stats.relPath)) {
        applyStats(item, stats);
        recomputeAggregates(m_projects.value(id));
        // Re-filtering is left to the next batch / filter change: a row must not vanish while it is edited.
    }
}

void ProjectTree::setFileDirty(const QString &id, const QString &relPath, bool dirty)
{
    if (QStandardItem *item = findFile(id, relPath)) {
        QFont f = item->font();
        f.setItalic(dirty);
        item->setFont(f);
        item->setText(Statistics::displayNameFor(relPath) + (dirty ? QStringLiteral(" *") : QString()));
    }
}

void ProjectTree::contextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_proxy->mapToSource(m_view->indexAt(pos)).siblingAtColumn(0);
    if (!idx.isValid())
        return;
    const QString id = idx.data(ProjectRole).toString();
    QMenu menu(this);
    if (idx.data(TypeRole).toInt() == FileNode) {
        const QString rel = idx.data(PathRole).toString();
        menu.addAction(tr("Open"), [this, id, rel] { emit fileActivated(id, rel); });
        menu.addSeparator();
    }
    menu.addAction(tr("Refresh statistics"), [this, id] { emit refreshRequested(id); });
    menu.addAction(tr("Rebuild glossary"), [this, id] { emit rebuildGlossaryRequested(id); });
    menu.addAction(tr("Export to a DAT archive..."), [this, id] { emit exportRequested(id); });
    menu.addAction(tr("Open the output folder"), [this, id] { emit openFolderRequested(id); });
    menu.addSeparator();
    menu.addAction(tr("Project settings..."), [this, id] { emit settingsRequested(id); });
    menu.addAction(tr("Close project"), [this, id] { emit closeRequested(id); });
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}
