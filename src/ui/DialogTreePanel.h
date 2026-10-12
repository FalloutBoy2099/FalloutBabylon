#pragma once

#include "core/DialogGraph.h"
#include "core/Project.h"

#include <QHash>
#include <QWidget>
#include <memory>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

// Text of one record of the dialogue file shown in the tree.
struct DialogLine {
    QString source;
    QString target;
    SegmentStatus status = SegmentStatus::Untranslated;
};

// Dialogue tree rebuilt from the script (scripts/<name>.int decompiled with int2ssl) that drives the
// current dialogue file: NPC lines with the player options below them, following the script's procedures.
class DialogTreePanel : public QWidget
{
    Q_OBJECT

public:
    explicit DialogTreePanel(QWidget *parent = nullptr);

    // Shows the tree of `relPath` (rebuilt only when the file changes).
    void showFile(const std::shared_ptr<Project> &project, const QString &relPath, const QHash<int, DialogLine> &lines);
    void updateLines(const QHash<int, DialogLine> &lines);
    void selectId(int id);
    void clear();
    int nodeCount() const { return int(m_all.size()); }
    QString statusText() const;

signals:
    void idActivated(int id);

private:
    void populate(const QVector<DialogNode> &nodes, QTreeWidgetItem *parent);
    void applyText(QTreeWidgetItem *item) const;
    void onCurrentChanged(QTreeWidgetItem *item);

    QLabel *m_status = nullptr;
    QTreeWidget *m_tree = nullptr;
    QString m_key;
    QHash<QString, DialogGraph> m_graphs;           // decompiled once per project file
    QHash<QString, QString> m_errors;
    QHash<int, DialogLine> m_lines;
    QHash<QString, QTreeWidgetItem *> m_procItems;  // first expansion of each procedure
    QMultiHash<int, QTreeWidgetItem *> m_byId;
    QList<QTreeWidgetItem *> m_all;
    bool m_selecting = false;
};
