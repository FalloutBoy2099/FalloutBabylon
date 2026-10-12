#pragma once

#include "core/Project.h"

#include <QTimer>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QTableView;
class MsgFilterProxy;
class MsgTableModel;
class SciEdit;

// Linear view of one message file: the list of records, the read-only source and the editable translation.
class TranslationEditor : public QWidget
{
    Q_OBJECT

public:
    TranslationEditor(std::shared_ptr<Project> project, TranslationDoc doc, QWidget *parent = nullptr);

    Project *project() const { return m_project.get(); }
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
    void copySourceToTarget();
    void confirmCurrent();
    void goToRow(int delta);
    void goToNextUntranslated(bool forward);
    void insertText(const QString &text);
    void focusSearch();

    QVector<TodoItem> reviewItems() const;
    SciEdit *sourceEditor() const { return m_source; }
    SciEdit *targetEditor() const { return m_target; }

signals:
    void dirtyChanged(bool dirty);
    void statsChanged(const FileStats &stats);
    void currentIdChanged(int id);

private:
    int currentSourceRow() const;
    void onCurrentChanged();
    void onTargetEdited();
    void updateHighlights();
    void updateIssueLabel();
    void showTermTooltip(SciEdit *edit, bool inSource, int charIndex, const QPoint &globalPos);
    void applyFilters();
    QString termHtml(const GlossaryTerm &term) const;

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
    QLabel *m_info = nullptr;
    QTimer m_highlightTimer;
    int m_currentRow = -1;      // source-model row shown in the editors
    bool m_loading = false;
    bool m_lastDirty = false;
};
