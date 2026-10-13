#pragma once

#include "core/ProjectSearch.h"

#include <QWidget>
#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Search / replace over all the message files of the active project.
class SearchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SearchPanel(QWidget *parent = nullptr);
    ~SearchPanel() override;

    void setProject(const std::shared_ptr<Project> &project);
    void focusQuery();

    // For the tests
    void searchNow();
    int resultCount() const;
    QVector<ProjectSearch::Hit> hits() const { return m_hits; }
    ProjectSearch::Options options() const;
    QString replacement() const;

signals:
    void openRequested(const QString &relPath, int id);
    // Replace in the translation of these hits (target hits only).
    void replaceRequested(const ProjectSearch::Options &options, const QString &replacement,
                          const QVector<ProjectSearch::Hit> &hits);

private:
    void showResults(int generation, const QVector<ProjectSearch::Hit> &hits, bool truncated);
    QVector<ProjectSearch::Hit> checkedHits() const;

    std::shared_ptr<Project> m_project;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QComboBox *m_scope = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_case = nullptr;
    QCheckBox *m_word = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_replaceChecked = nullptr;
    QPushButton *m_replaceAll = nullptr;
    QLabel *m_status = nullptr;
    QTableWidget *m_table = nullptr;
    QVector<ProjectSearch::Hit> m_hits;
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    int m_generation = 0;
};
