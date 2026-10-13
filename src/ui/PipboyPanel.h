#pragma once

#include "core/PipboyText.h"
#include "core/Project.h"

#include <QPointer>
#include <QTimer>
#include <QWidget>

class MsgTableModel;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class SciEdit;
class TranslationEditor;

// Pip-Boy holodisk editor for game/pipboy.msg: the translator writes the body as free paragraphs, the panel cuts
// it into entries that fit the Pip-Boy screen (see core/PipboyText.h) and writes them into the file's records.
class PipboyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PipboyPanel(QWidget *parent = nullptr);

    // The editor whose file is shown (nullptr or another file than game/pipboy.msg: the panel is idle).
    void setEditor(TranslationEditor *editor);
    // The line selected in the editor.
    void showId(int id);

    // For the tests
    SciEdit *targetEdit() const { return m_target; }
    QStringList entries() const { return m_entries; }
    void applyNow();
    bool hasBlock() const { return m_hasBlock; }
    bool questMode() const { return m_questMode; }
    QString previewText() const;

private:
    void loadBlock(const PipboyText::Block &block);
    void showQuest(int id);       // quests.msg: preview of the wrapping done by the engine
    void ensureFont();
    void updatePreview();
    void scheduleApply();
    PipboyText::Measure measure() const;
    void reloadFromLines();
    void setIdle(const QString &message);

    QPointer<TranslationEditor> m_editor;
    MsgTableModel *m_model = nullptr;
    std::shared_ptr<Project> m_project;
    PipboyText::Font m_font;
    QString m_fontProject;

    PipboyText::Block m_block;
    bool m_hasBlock = false;
    bool m_questMode = false;
    bool m_loading = false;
    QStringList m_entries;           // what the target text turns into

    QLabel *m_info = nullptr;
    QLabel *m_mode = nullptr;
    QLabel *m_status = nullptr;
    SciEdit *m_source = nullptr;
    SciEdit *m_target = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_chars = nullptr;
    QPushButton *m_reload = nullptr;
    QTimer m_applyTimer;
};
