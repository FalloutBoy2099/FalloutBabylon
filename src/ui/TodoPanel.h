#pragma once

#include "core/Project.h"

#include <QWidget>

class QLabel;
class QTableWidget;

// TODO list: every segment of the active project that is marked "to review".
class TodoPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TodoPanel(QWidget *parent = nullptr);

    void setItems(const QVector<TodoItem> &items);
    int itemCount() const;

signals:
    void itemActivated(const QString &relPath, int id);

private:
    QLabel *m_summary = nullptr;
    QTableWidget *m_table = nullptr;
};
