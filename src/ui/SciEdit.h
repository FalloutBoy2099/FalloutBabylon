#pragma once

#include "ScintillaEditBase.h"

#include <QPair>
#include <QString>
#include <QVector>

class QMenu;

// Thin Qt-friendly layer over the Scintilla editing component: QString in/out (Scintilla works in UTF-8),
// decoration of character ranges with indicators (underlines), hover and context menu hooks.
class SciEdit : public ScintillaEditBase
{
    Q_OBJECT

public:
    enum Indicator { TermIndicator = 0, FuzzyTermIndicator = 1, IssueIndicator = 2, SpellingIndicator = 3 };
    using Span = QPair<int, int>;   // start, length in QString (UTF-16) units

    explicit SciEdit(bool readOnly, QWidget *parent = nullptr);

    // Replaces the content without emitting userEdited() and resets the undo history.
    void setPlainText(const QString &text);
    QString plainText() const;

    void setSpans(Indicator indicator, const QVector<Span> &spans);
    void clearSpans(Indicator indicator);
    void clearAllSpans();

    int charIndexAt(const QPoint &viewportPoint) const;  // -1 when not over text
    int caretCharIndex() const;
    void insertAtCaret(const QString &text);
    void applyTheme();

signals:
    void userEdited();
    void hoverAt(int charIndex, const QPoint &globalPoint);
    void hoverEnded();
    void contextMenuAboutToShow(QMenu *menu, int charIndex);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    int byteOffset(int charIndex) const;
    int charIndexFromByte(int byte) const;
    void applyFont();

    bool m_readOnly;
    bool m_silent = false;
    QString m_text;
};
