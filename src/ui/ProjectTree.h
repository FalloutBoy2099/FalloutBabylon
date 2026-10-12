#pragma once

#include "core/Statistics.h"

#include <QHash>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class TreeFilterProxy;

// Project view: every open project with its message files in the recommended order
// (/game first, then /dialog), a progress bar per file and filters on the progress.
class ProjectTree : public QWidget
{
    Q_OBJECT

public:
    enum Role {
        TypeRole = Qt::UserRole + 1, ProjectRole, PathRole,
        LinesRole, UntranslatedRole, ReviewRole, WordsRole, WordsLeftRole, KnownRole,
    };
    enum NodeType { ProjectNode = 1, GroupNode, FileNode };
    enum Filter { ShowAll, ShowUntouched, ShowPartial, ShowComplete, ShowReview };

    explicit ProjectTree(QWidget *parent = nullptr);

    void addProject(const QString &id, const QString &name, const QStringList &files);
    void removeProject(const QString &id);
    void renameProject(const QString &id, const QString &name);
    void setStats(const QString &id, const QVector<FileStats> &stats);
    void updateStats(const QString &id, const FileStats &stats);
    void setFileDirty(const QString &id, const QString &relPath, bool dirty);
    bool hasProject(const QString &id) const { return m_projects.contains(id); }
    void expandProject(const QString &id);

signals:
    void fileActivated(const QString &projectId, const QString &relPath);
    void refreshRequested(const QString &projectId);
    void settingsRequested(const QString &projectId);
    void closeRequested(const QString &projectId);
    void exportRequested(const QString &projectId);
    void openFolderRequested(const QString &projectId);
    void rebuildGlossaryRequested(const QString &projectId);

protected:
    void changeEvent(QEvent *event) override;

private:
    QStandardItem *findFile(const QString &id, const QString &relPath) const;
    void applyStats(QStandardItem *fileItem, const FileStats &s);
    void recomputeAggregates(QStandardItem *project);
    void aggregate(QStandardItem *node);
    void fillRow(QStandardItem *nameItem);
    void contextMenu(const QPoint &pos);

    QStandardItemModel *m_model = nullptr;
    TreeFilterProxy *m_proxy = nullptr;
    QTreeView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_filter = nullptr;
    QHash<QString, QStandardItem *> m_projects;
};

class TreeFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TreeFilterProxy(QObject *parent = nullptr);
    void setFilter(int filter, const QString &text);
    bool filterActive() const;
    void refilter() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_filter = 0;
    QString m_text;
};
