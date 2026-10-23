#pragma once

#include "core/QualityCheck.h"

#include <QWidget>
#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Quality report of the active project: lost tokens, spacing, encoding, length, inconsistent duplicates, glossary.
class QualityPanel : public QWidget
{
    Q_OBJECT

public:
    explicit QualityPanel(QWidget *parent = nullptr);
    ~QualityPanel() override;

    void setProject(const std::shared_ptr<Project> &project);

    // For the tests
    void runNow();
    int resultCount() const;      // rows currently shown
    int totalCount() const { return int(m_issues.size()); }
    int cacheHits() const { return m_cacheHits; }       // files of the last run answered from the cache
    bool running() const { return m_running; }
    QVector<QualityCheck::Issue> issues() const { return m_issues; }
    void setKinds(int kinds);

signals:
    void openRequested(const QString &relPath, int id);

private:
    void showResults(int generation, const QVector<QualityCheck::Issue> &issues, int cacheHits);
    void applyFilter();
    void exportCsv();
    QualityCheck::Options options() const;

    std::shared_ptr<Project> m_project;
    QVector<QCheckBox *> m_kindBoxes;
    QVector<int> m_kindValues;
    QSpinBox *m_maxChars = nullptr;
    QPushButton *m_run = nullptr;
    QPushButton *m_export = nullptr;
    QComboBox *m_filter = nullptr;
    QLabel *m_status = nullptr;
    QTableWidget *m_table = nullptr;
    QVector<QualityCheck::Issue> m_issues;
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    int m_generation = 0;
    int m_cacheHits = 0;
    bool m_running = false;
};
