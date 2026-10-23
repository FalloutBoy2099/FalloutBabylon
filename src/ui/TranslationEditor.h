#pragma once

#include "DialogTreePanel.h"
#include "core/GrammarClient.h"
#include "core/Project.h"
#include "core/ProjectSearch.h"

#include <QTimer>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QModelIndex;
class QTableView;
class MsgFilterProxy;
class MsgTableModel;
class SciEdit;
class TranslationService;

// Linear view of one message file: the list of records, the read-only source and the editable translation.
class TranslationEditor : public QWidget
{
    Q_OBJECT

public:
    TranslationEditor(std::shared_ptr<Project> project, TranslationDoc doc, QWidget *parent = nullptr);

    Project *project() const { return m_project.get(); }
    std::shared_ptr<Project> sharedProject() const { return m_project; }
    const QString &relPath() const { return m_doc.relPath; }
    bool isDirty() const;
    FileStats stats() const;

    // Returns false if saving failed (an error was shown).
    bool save();

    void refreshGlossary();   // the project glossary changed
    void applyTheme();
    void selectId(int id);

    // Editing commands (menu / shortcuts)
    void toggleReview();
    void toggleValidated();
    void copySourceToTarget();
    void confirmCurrent();
    void goToRow(int delta);
    void goToNextUntranslated(bool forward);
    void insertText(const QString &text);
    void focusSearch();

    void setService(TranslationService *service);
    MsgTableModel *model() const { return m_model; }
    int currentId() const;   // id of the line shown in the editors, -1 if none
    int grammarIssueCount() const { return int(m_grammarIssues.size()); }
    // Puts the model's text of the current line back into the editors (after a bulk change).
    void refreshCurrent();
    // Context sent to DeepL with a line: the dialogue name and the neighbouring source lines.
    static QString contextFor(const TranslationDoc &doc, int row);
    // File-level undo / redo of coarse operations (see MsgTableModel); typing keeps the editor's own undo.
    bool undoOperation();
    bool redoOperation();
    // Marks lines of this file "to review" with a comment (marks of other files are ignored). Returns how many changed.
    int markForReview(const QVector<ReviewMark> &marks);
    // Puts a translation suggested by the translation memory on the current line; below a perfect match the line
    // is marked "to review".
    void applySuggestion(const QString &translation, double score);
    // Search and replace in the translation of the listed records; returns the number of replacements.
    int replaceInTarget(const ProjectSearch::Options &options, const QString &replacement, const QSet<int> &ids);
    void translateCurrent();      // DeepL on the current line
    void massTranslate();         // DeepL on many lines, with preview

    QVector<TodoItem> reviewItems() const;
    QHash<int, DialogLine> dialogLines() const;
    SciEdit *sourceEditor() const { return m_source; }
    SciEdit *targetEditor() const { return m_target; }

signals:
    void dirtyChanged(bool dirty);
    void statsChanged(const FileStats &stats);
    void currentIdChanged(int id);
    void historyChanged();       // the undo / redo stacks of the file changed

private:
    int currentSourceRow() const;
    void onCurrentChanged();
    void onTargetEdited();
    void updateHighlights();
    void updateIssueLabel();
    void showTermTooltip(SciEdit *edit, bool inSource, int charIndex, const QPoint &globalPos);
    void applyFilters();
    QString termHtml(const GlossaryTerm &term) const;
    void runCheck();              // grammar / spelling check of the translation (asynchronous)
    void applyIssueSpans();
    const GrammarIssue *issueAt(int charIndex) const;
    void showMessage(const QString &html);

    std::shared_ptr<Project> m_project;
    TranslationDoc m_doc;
    MsgTableModel *m_model = nullptr;
    MsgFilterProxy *m_proxy = nullptr;
    QTableView *m_table = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_filter = nullptr;
    SciEdit *m_source = nullptr;
    SciEdit *m_target = nullptr;
    QLabel *m_issues = nullptr;
    QLineEdit *m_comment = nullptr;
    QLabel *m_previous = nullptr;
    QWidget *m_previousRow = nullptr;
    QPushButton *m_restorePrevious = nullptr;
    QLabel *m_info = nullptr;
    QTimer m_highlightTimer;
    QTimer m_checkTimer;
    TranslationService *m_service = nullptr;
    QVector<GrammarIssue> m_grammarIssues;   // for the text currently in the target editor
    int m_checkSerial = 0;
    bool m_translating = false;
    int m_currentRow = -1;      // source-model row shown in the editors
    bool m_loading = false;
    bool m_lastDirty = false;
};
