#pragma once

#include "core/Project.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

// Create or edit a translation project: sources (game folders / DAT archives), languages, encodings, output folder.
class ProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectDialog(const ProjectConfig &config, bool isNew, QWidget *parent = nullptr);

    ProjectConfig config() const;

    // Adds a game folder: detects the DAT archives and data folder it contains.
    void addInstallation(const QString &folder);

private:
    void addSource(const QString &path);
    void detectLanguages();
    void updateTargetEncoding();
    void updateOutputDefault();
    void accept() override;

    ProjectConfig m_config;
    bool m_isNew;
    bool m_outputEdited = false;
    QLineEdit *m_name = nullptr;
    QListWidget *m_sources = nullptr;
    QLineEdit *m_output = nullptr;
    QComboBox *m_sourceLanguage = nullptr;
    QComboBox *m_targetLanguage = nullptr;
    QComboBox *m_sourceEncoding = nullptr;
    QComboBox *m_targetEncoding = nullptr;
    QLabel *m_hint = nullptr;
};
