#include "GlossaryPanel.h"

#include "Theme.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

class GlossaryTableModel : public QAbstractTableModel
{
public:

    enum Column { ColSource, ColTarget, ColCategory, ColOrigin, ColumnCount };

    void setTerms(const QVector<GlossaryTerm> &terms)
    {
        beginResetModel();
        m_terms = terms;
        endResetModel();
    }
    const GlossaryTerm &term(int row) const { return m_terms.at(row); }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_terms.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return {};
        const GlossaryTerm &t = m_terms.at(index.row());
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColSource: return t.source;
            case ColTarget:
                return t.targets.isEmpty() ? QString() : t.targets.join(QStringLiteral(" / "))
                        + (t.conflict() ? QStringLiteral("  ⚠") : QString());
            case ColCategory: return t.category;
            case ColOrigin: return t.origins.value(0) + (t.origins.size() > 1 ? QStringLiteral(" +%1").arg(t.origins.size() - 1) : QString());
            }
        } else if (role == Qt::ForegroundRole) {
            if (t.conflict() && index.column() == ColTarget)
                return Theme::colors().issue;
            if (t.targets.isEmpty())
                return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
        } else if (role == Qt::ToolTipRole) {
            QStringList lines{t.source + QStringLiteral(" → ") + t.targets.join(QStringLiteral(" / "))};
            lines << t.origins.mid(0, 8);
            if (t.conflict())
                lines << QCoreApplication::translate("GlossaryPanel", "⚠ Several different translations exist in the game files.");
            if (t.targets.isEmpty())
                lines << QCoreApplication::translate("GlossaryPanel", "Not translated yet: translate the matching /game file first.");
            return lines.join(QLatin1Char('\n'));
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation o, int role) const override
    {
        if (o != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case ColSource: return QCoreApplication::translate("GlossaryPanel", "Term");
        case ColTarget: return QCoreApplication::translate("GlossaryPanel", "Translation");
        case ColCategory: return QCoreApplication::translate("GlossaryPanel", "Type");
        case ColOrigin: return QCoreApplication::translate("GlossaryPanel", "Origin");
        }
        return {};
    }

private:
    QVector<GlossaryTerm> m_terms;
};

GlossaryPanel::GlossaryPanel(QWidget *parent)
    : QWidget(parent)
{
    m_model = new GlossaryTableModel;
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_model->setParent(m_proxy);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search the glossary..."));
    m_search->setClearButtonEnabled(true);
    m_summary = new QLabel;

    m_view = new QTableView;
    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setColumnWidth(GlossaryTableModel::ColSource, 150);
    m_view->setColumnWidth(GlossaryTableModel::ColTarget, 150);
    m_view->setColumnWidth(GlossaryTableModel::ColCategory, 70);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_search);
    layout->addWidget(m_summary);
    layout->addWidget(m_view, 1);

    connect(m_search, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex &idx) {
        const GlossaryTerm &t = m_model->term(m_proxy->mapToSource(idx).row());
        if (!t.targets.isEmpty())
            emit insertRequested(t.targets.first());
    });
    connect(m_view, &QTableView::customContextMenuRequested, this, &GlossaryPanel::contextMenu);
    setGlossary({});
}

void GlossaryPanel::showBuilding()
{
    m_summary->setText(tr("Building the glossary..."));
}

void GlossaryPanel::setGlossary(const QVector<GlossaryTerm> &terms)
{
    m_model->setTerms(terms);
    int conflicts = 0, untranslated = 0;
    for (const GlossaryTerm &t : terms) {
        conflicts += t.conflict() ? 1 : 0;
        untranslated += t.targets.isEmpty() ? 1 : 0;
    }
    if (terms.isEmpty())
        m_summary->setText(tr("No term yet. The glossary is built from the /game message files."));
    else
        m_summary->setText(tr("%1 terms · %2 without translation · %3 inconsistent")
                               .arg(terms.size()).arg(untranslated).arg(conflicts));
}

void GlossaryPanel::contextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    if (!idx.isValid())
        return;
    const GlossaryTerm &t = m_model->term(m_proxy->mapToSource(idx).row());
    QMenu menu(this);
    for (const QString &target : t.targets)
        menu.addAction(tr("Insert \"%1\"").arg(target), [this, target] { emit insertRequested(target); });
    menu.addAction(tr("Copy the term"), [t] { QApplication::clipboard()->setText(t.source); });
    if (!t.targets.isEmpty())
        menu.addAction(tr("Copy the translation"), [t] { QApplication::clipboard()->setText(t.targets.first()); });
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}
