#pragma once

#include "core/TranslationMemory.h"

#include <QDialog>
#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

// Reuse the translation of another version of the mod: pick its folder, choose the languages, import.
class ImportTranslationDialog : public QDialog
{
    Q_OBJECT

public:
    ImportTranslationDialog(std::shared_ptr<Project> project, QWidget *parent = nullptr);
    ~ImportTranslationDialog() override;

    bool imported() const { return m_imported; }
    TranslationMemory::Report report() const { return m_report; }

    // Same as choosing a folder and pressing "Import" (used by the tests)
    void setFolder(const QString &folder);
    void startNow() { start(); }
    bool isRunning() const { return m_running; }

private:
    void chooseFolder();
    void scan();
    void start();
    void finished(const TranslationMemory::Report &report);
    void reject() override;

    std::shared_ptr<Project> m_project;
    TranslationMemory::Memory m_memory;
    QLineEdit *m_path = nullptr;
    QLabel *m_found = nullptr;
    QComboBox *m_sourceLanguage = nullptr;
    QComboBox *m_targetLanguage = nullptr;
    QCheckBox *m_byText = nullptr;
    QCheckBox *m_changed = nullptr;
    QCheckBox *m_overwrite = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_start = nullptr;
    QPushButton *m_cancel = nullptr;
    std::atomic_bool m_stop{false};
    bool m_running = false;
    bool m_imported = false;
    TranslationMemory::Report m_report;
};
