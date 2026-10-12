#pragma once

#include "core/Project.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <memory>

// Table of the records of one message file: id, source, target, status, glossary alerts.
class MsgTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ColId, ColSource, ColTarget, ColStatus, ColIssues, ColumnCount };
    enum Role { StatusRole = Qt::UserRole + 1, IssueRole };

    MsgTableModel(std::shared_ptr<Project> project, TranslationDoc doc, QObject *parent = nullptr);

    const TranslationDoc &doc() const { return m_doc; }
    const TranslationRow &row(int index) const { return m_doc.rows.at(index); }
    bool isDirty() const { return m_dirty; }
    void markSaved();

    SegmentStatus status(int row) const { return Project::statusOf(m_doc.rows.at(row)); }
    int issueCount(int row) const { return m_issues.value(row); }
    QStringList issueMessages(int row) const;

    void setTarget(int row, const QString &text);
    void setReview(int row, bool review);
    void confirm(int row);   // accept the current target (even if identical to the source)

    // Glossary changed: recompute the alerts of every row.
    void refreshIssues();
    FileStats stats() const { return Project::statisticsOf(m_doc); }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void changed();

private:
    void updateIssues(int row);
    void touchRow(int row);

    std::shared_ptr<Project> m_project;
    TranslationDoc m_doc;
    QVector<int> m_issues;
    bool m_dirty = false;
};

class MsgFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum StatusFilter { AllRows = -1, Untranslated = 0, Translated = 1, ToReview = 2, WithIssues = 100 };

    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setStatusFilter(int filter);
    void setSearchText(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_status = AllRows;
    QString m_search;
};
