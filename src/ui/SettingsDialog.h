#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class TranslationService;

// Tools > Settings: DeepL account and options, correction servers.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(TranslationService *service, QWidget *parent = nullptr);

private:
    void save();
    void testDeepL();
    void testServer(bool grammalecte);

    TranslationService *m_service;
    QLineEdit *m_key = nullptr;
    QLabel *m_keyStatus = nullptr;
    QComboBox *m_formality = nullptr;
    QCheckBox *m_glossary = nullptr;
    QCheckBox *m_context = nullptr;
    QCheckBox *m_markReview = nullptr;
    QCheckBox *m_check = nullptr;
    QLineEdit *m_grammalecte = nullptr;
    QLineEdit *m_languageTool = nullptr;
    QLabel *m_grammalecteStatus = nullptr;
    QLabel *m_languageToolStatus = nullptr;
};
