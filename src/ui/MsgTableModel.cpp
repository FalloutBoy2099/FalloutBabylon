#include "MsgTableModel.h"

#include "Theme.h"

#include <QCoreApplication>
#include <QFont>

namespace {

QString oneLine(const QString &text)
{
    QString s = text;
    s.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
    return s;
}

}

MsgTableModel::MsgTableModel(std::shared_ptr<Project> project, TranslationDoc doc, QObject *parent)
    : QAbstractTableModel(parent)
    , m_project(std::move(project))
    , m_doc(std::move(doc))
{
    m_issues.fill(0, m_doc.rows.size());
    refreshIssues();
}

void MsgTableModel::markSaved()
{
    m_dirty = false;
}

QStringList MsgTableModel::issueMessages(int row) const
{
    QStringList out;
    const TranslationRow &r = m_doc.rows.at(row);
    if (!r.inSource)
        return out;
    for (const auto &issue : m_project->glossary().checkConsistency(r.source, r.target))
        out << issue.message;
    return out;
}

void MsgTableModel::updateIssues(int row)
{
    const TranslationRow &r = m_doc.rows.at(row);
    m_issues[row] = r.inSource ? int(m_project->glossary().checkConsistency(r.source, r.target).size()) : 0;
}

void MsgTableModel::refreshIssues()
{
    if (m_doc.rows.isEmpty())
        return;
    for (int i = 0; i < m_doc.rows.size(); ++i)
        updateIssues(i);
    emit dataChanged(index(0, ColIssues), index(m_doc.rows.size() - 1, ColIssues));
}

void MsgTableModel::touchRow(int row)
{
    m_dirty = true;
    updateIssues(row);
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    emit changed();
}

void MsgTableModel::setTarget(int row, const QString &text)
{
    TranslationRow &r = m_doc.rows[row];
    if (r.target == text)
        return;
    r.target = text;
    // Typing exactly the source text means "this stays as is" (names, interjections...).
    r.confirmed = !text.isEmpty() && text == r.source;
    touchRow(row);
}

void MsgTableModel::setReview(int row, bool review)
{
    if (m_doc.rows.at(row).review == review)
        return;
    m_doc.rows[row].review = review;
    touchRow(row);
}

void MsgTableModel::confirm(int row)
{
    TranslationRow &r = m_doc.rows[row];
    if (r.target.isEmpty())
        r.target = r.source;
    r.confirmed = true;
    touchRow(row);
}

int MsgTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_doc.rows.size());
}

int MsgTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MsgTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_doc.rows.size())
        return {};
    const TranslationRow &r = m_doc.rows.at(index.row());
    const SegmentStatus st = Project::statusOf(r);

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColId: return r.id;
        case ColSource: return oneLine(r.source);
        case ColTarget: return oneLine(r.target);
        case ColStatus:
            return st == SegmentStatus::Translated ? QStringLiteral("✔")
                : st == SegmentStatus::ToReview ? QStringLiteral("⚑") : QStringLiteral("•");
        case ColIssues: return m_issues.value(index.row()) ? QStringLiteral("⚠") : QString();
        }
        break;
    case Qt::ToolTipRole:
        switch (index.column()) {
        case ColSource: return r.source.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        case ColTarget: return r.target.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        case ColStatus: return Theme::statusText(st);
        case ColIssues: return issueMessages(index.row()).join(QLatin1Char('\n'));
        }
        break;
    case Qt::BackgroundRole:
        if (index.column() == ColStatus || index.column() == ColId)
            return Theme::statusTint(st);
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == ColId)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (index.column() == ColStatus || index.column() == ColIssues)
            return int(Qt::AlignCenter);
        break;
    case Qt::ForegroundRole:
        if (index.column() == ColIssues)
            return Theme::colors().issue;
        if (!r.inSource)
            return QColor(Qt::gray);
        break;
    case StatusRole: return int(st);
    case IssueRole: return m_issues.value(index.row());
    }
    return {};
}

QVariant MsgTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColId: return tr("ID");
    case ColSource: return tr("Source (%1)").arg(m_project->config().sourceLanguage);
    case ColTarget: return tr("Translation (%1)").arg(m_project->config().targetLanguage);
    case ColStatus: return tr("St.");
    case ColIssues: return QStringLiteral("⚠");
    }
    return {};
}

// ----------------------------------------------------------------------------- filter

void MsgFilterProxy::setStatusFilter(int filter)
{
    m_status = filter;
    invalidateFilter();
}

void MsgFilterProxy::setSearchText(const QString &text)
{
    m_search = text.trimmed();
    invalidateFilter();
}

bool MsgFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &parent) const
{
    const auto *model = static_cast<const MsgTableModel *>(sourceModel());
    if (m_status == WithIssues) {
        if (!model->issueCount(sourceRow))
            return false;
    } else if (m_status != AllRows && int(model->status(sourceRow)) != m_status) {
        return false;
    }
    if (m_search.isEmpty())
        return true;
    const TranslationRow &r = model->row(sourceRow);
    return r.source.contains(m_search, Qt::CaseInsensitive) || r.target.contains(m_search, Qt::CaseInsensitive)
        || QString::number(r.id) == m_search;
    Q_UNUSED(parent)
}
