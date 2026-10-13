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
    enum Column { ColId, ColSource, ColTarget, ColStatus, ColIssues, ColNote, ColumnCount };
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
    void setComment(int row, const QString &comment);
    // "Validated": the reviewer approved this exact text; editing it afterwards shows "changed since validation".
    void setValidated(int row, bool validated);
    int rowForId(int id) const;     // index of the record with this id, -1 if none
    // Adds a record that exists only in the translation (e.g. extra Pip-Boy lines); returns its row.
    int addRow(int id);
    void removeOrphan(int row);   // removes a record added with addRow() (ignored for records of the source)

    // --- file-level undo / redo of coarse operations (mass translation, replace, review marks...).
    // Typing is not recorded here: the text editors have their own undo for that.
    // Everything changed between beginOperation() and endOperation() is one undoable step.
    void beginOperation(const QString &label);
    void endOperation();
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoLabel() const { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    QString redoLabel() const { return m_redo.isEmpty() ? QString() : m_redo.last().label; }
    bool undo();
    bool redo();

    class Operation
    {
    public:
        Operation(MsgTableModel *model, const QString &label) : m_model(model) { m_model->beginOperation(label); }
        ~Operation() { m_model->endOperation(); }
        Operation(const Operation &) = delete;
        Operation &operator=(const Operation &) = delete;

    private:
        MsgTableModel *m_model;
    };

    // Glossary changed: recompute the alerts of every row.
    void refreshIssues();
    FileStats stats() const { return Project::statisticsOf(m_doc); }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void changed();
    void historyChanged();

private:
    struct Step {
        QString label;
        bool full = false;                    // the number of rows changed: whole tables are stored
        QVector<TranslationRow> beforeRows, afterRows;
        QVector<int> index;                   // otherwise: the rows that changed
        QVector<TranslationRow> before, after;
    };
    static bool sameRow(const TranslationRow &a, const TranslationRow &b);
    void apply(const Step &step, bool undo);

    void updateIssues(int row);
    void touchRow(int row);

    QList<Step> m_undo, m_redo;
    int m_opDepth = 0;
    QString m_opLabel;
    QVector<TranslationRow> m_opSnapshot;

    std::shared_ptr<Project> m_project;
    TranslationDoc m_doc;
    QVector<int> m_issues;
    bool m_dirty = false;
};

class MsgFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum StatusFilter {
        AllRows = -1, Untranslated = 0, Translated = 1, ToReview = 2, WithIssues = 100,
        Validated = 200, NotValidated = 201, ChangedSinceValidation = 202, WithComment = 203
    };

    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setStatusFilter(int filter);
    void setSearchText(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_status = AllRows;
    QString m_search;
};
