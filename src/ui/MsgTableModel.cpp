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

// ----------------------------------------------------------------------------- undo / redo

bool MsgTableModel::sameRow(const TranslationRow &a, const TranslationRow &b)
{
    return a.id == b.id && a.target == b.target && a.review == b.review && a.confirmed == b.confirmed
        && a.comment == b.comment && a.validated == b.validated && a.previous == b.previous;
}

void MsgTableModel::beginOperation(const QString &label)
{
    if (m_opDepth++ == 0) {
        m_opLabel = label;
        m_opSnapshot = m_doc.rows;     // implicitly shared strings: cheap
    }
}

void MsgTableModel::endOperation()
{
    if (m_opDepth == 0 || --m_opDepth > 0)
        return;
    Step step;
    step.label = m_opLabel;
    if (m_opSnapshot.size() != m_doc.rows.size()) {
        step.full = true;
        step.beforeRows = m_opSnapshot;
        step.afterRows = m_doc.rows;
    } else {
        for (int i = 0; i < m_doc.rows.size(); ++i)
            if (!sameRow(m_opSnapshot.at(i), m_doc.rows.at(i))) {
                step.index.push_back(i);
                step.before.push_back(m_opSnapshot.at(i));
                step.after.push_back(m_doc.rows.at(i));
            }
        if (step.index.isEmpty()) {
            m_opSnapshot.clear();
            return;
        }
    }
    m_opSnapshot.clear();
    m_undo.push_back(step);
    while (m_undo.size() > 30)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

void MsgTableModel::apply(const Step &step, bool undo)
{
    if (step.full) {
        beginResetModel();
        m_doc.rows = undo ? step.beforeRows : step.afterRows;
        m_issues.fill(0, m_doc.rows.size());
        for (int i = 0; i < m_doc.rows.size(); ++i)
            updateIssues(i);
        endResetModel();
    } else {
        const QVector<TranslationRow> &rows = undo ? step.before : step.after;
        for (const TranslationRow &saved : rows) {
            const int row = rowForId(saved.id);
            if (row < 0)
                continue;
            TranslationRow &r = m_doc.rows[row];
            r.target = saved.target;
            r.review = saved.review;
            r.confirmed = saved.confirmed;
            r.comment = saved.comment;
            r.validated = saved.validated;
            r.previous = saved.previous;
            updateIssues(row);
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        }
    }
    m_dirty = true;
    emit changed();
}

bool MsgTableModel::undo()
{
    if (m_undo.isEmpty())
        return false;
    const Step step = m_undo.takeLast();
    apply(step, true);
    m_redo.push_back(step);
    emit historyChanged();
    return true;
}

bool MsgTableModel::redo()
{
    if (m_redo.isEmpty())
        return false;
    const Step step = m_redo.takeLast();
    apply(step, false);
    m_undo.push_back(step);
    emit historyChanged();
    return true;
}

void MsgTableModel::setComment(int row, const QString &comment)
{
    if (m_doc.rows.at(row).comment == comment)
        return;
    m_doc.rows[row].comment = comment;
    touchRow(row);
}

void MsgTableModel::setValidated(int row, bool validated)
{
    TranslationRow &r = m_doc.rows[row];
    const quint32 value = validated && !r.target.isEmpty() ? Project::textHash(r.target) : 0;
    if (r.validated == value)
        return;
    r.validated = value;
    touchRow(row);
}

int MsgTableModel::rowForId(int id) const
{
    for (int i = 0; i < m_doc.rows.size(); ++i)
        if (m_doc.rows.at(i).id == id)
            return i;
    return -1;
}

int MsgTableModel::addRow(int id)
{
    const int existing = rowForId(id);
    if (existing >= 0)
        return existing;
    int position = int(m_doc.rows.size());
    for (int i = 0; i < m_doc.rows.size(); ++i)
        if (m_doc.rows.at(i).id > id) {
            position = i;
            break;
        }
    TranslationRow row;
    row.id = id;
    row.inSource = false;
    row.leading = QStringLiteral("\n");
    beginInsertRows(QModelIndex(), position, position);
    m_doc.rows.insert(position, row);
    m_issues.insert(position, 0);
    endInsertRows();
    m_dirty = true;
    emit changed();
    return position;
}

void MsgTableModel::removeOrphan(int row)
{
    if (row < 0 || row >= m_doc.rows.size() || m_doc.rows.at(row).inSource)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_doc.rows.remove(row);
    m_issues.remove(row);
    endRemoveRows();
    m_dirty = true;
    emit changed();
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
        case ColNote: {
            QString note;
            if (r.validated)
                note += r.changedSinceValidation() ? QStringLiteral("≠") : QStringLiteral("✓");
            if (!r.comment.isEmpty())
                note += QStringLiteral("✎");
            return note;
        }
        }
        break;
    case Qt::ToolTipRole:
        switch (index.column()) {
        case ColNote: {
            QStringList lines;
            if (r.validated)
                lines << (r.changedSinceValidation() ? tr("Changed since it was validated") : tr("Validated"));
            if (!r.comment.isEmpty())
                lines << tr("Comment: %1").arg(r.comment);
            if (!r.previous.isEmpty())
                lines << tr("Previous translation: %1").arg(r.previous);
            return lines.join(QLatin1Char('\n'));
        }
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
        if (index.column() == ColStatus || index.column() == ColIssues || index.column() == ColNote)
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
    case ColNote: return QStringLiteral("✎");
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
    } else if (m_status == Validated) {
        if (!model->row(sourceRow).validated || model->row(sourceRow).changedSinceValidation())
            return false;
    } else if (m_status == NotValidated) {
        if (model->row(sourceRow).validated && !model->row(sourceRow).changedSinceValidation())
            return false;
    } else if (m_status == ChangedSinceValidation) {
        if (!model->row(sourceRow).changedSinceValidation())
            return false;
    } else if (m_status == WithComment) {
        if (model->row(sourceRow).comment.isEmpty())
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
