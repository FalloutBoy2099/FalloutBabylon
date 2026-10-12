#pragma once

#include "core/Statistics.h"

#include <QHash>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QTableWidget;

// Progress numbers: whole project by category, current file, and the files with the most work left.
class StatsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatsPanel(QWidget *parent = nullptr);

    void setProject(const QString &name, const QVector<FileStats> &stats);
    void updateFile(const FileStats &stats);
    void setCurrentFile(const QString &relPath);
    void clear();

signals:
    void fileActivated(const QString &relPath);

private:
    void refresh();      // coalesced: several changes in a row cause one update
    void refreshNow();
    static QString summary(const FileStats &s);

    QString m_name;
    QHash<QString, FileStats> m_stats;
    QString m_current;
    bool m_known = false;
    QTimer m_refreshTimer;
    QLabel *m_title = nullptr;
    QTableWidget *m_categories = nullptr;
    QLabel *m_currentLabel = nullptr;
    QCheckBox *m_onlyIncomplete = nullptr;
    QTableWidget *m_files = nullptr;
};
