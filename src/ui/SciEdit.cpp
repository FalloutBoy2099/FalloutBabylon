#include "SciEdit.h"

#include "Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFont>
#include <QMenu>

SciEdit::SciEdit(bool readOnly, QWidget *parent)
    : ScintillaEditBase(parent)
    , m_readOnly(readOnly)
{
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_SETWRAPMODE, SC_WRAP_WORD);
    send(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_NONE);
    send(SCI_SETMARGINLEFT, 0, 6);
    send(SCI_SETMARGINRIGHT, 0, 6);
    send(SCI_SETMARGINWIDTHN, 0, 0);
    send(SCI_SETMARGINWIDTHN, 1, 0);
    send(SCI_SETMARGINWIDTHN, 2, 0);
    send(SCI_USEPOPUP, SC_POPUP_NEVER);
    send(SCI_SETMOUSEDWELLTIME, 350);
    send(SCI_SETSCROLLWIDTHTRACKING, 1);
    send(SCI_SETCARETLINEVISIBLE, 0);
    send(SCI_SETCARETSTYLE, readOnly ? CARETSTYLE_INVISIBLE : CARETSTYLE_LINE);
    send(SCI_SETEXTRAASCENT, 1);
    send(SCI_SETEXTRADESCENT, 2);
    send(SCI_SETUNDOCOLLECTION, 1);

    send(SCI_INDICSETSTYLE, TermIndicator, INDIC_COMPOSITIONTHICK);
    send(SCI_INDICSETSTYLE, FuzzyTermIndicator, INDIC_DOTS);
    send(SCI_INDICSETSTYLE, IssueIndicator, INDIC_SQUIGGLE);
    send(SCI_INDICSETSTYLE, SpellingIndicator, INDIC_SQUIGGLE);
    send(SCI_INDICSETSTYLE, GrammarIndicator, INDIC_SQUIGGLE);
    for (int i = 0; i <= GrammarIndicator; ++i) {
        send(SCI_INDICSETUNDER, i, 0);
        send(SCI_INDICSETALPHA, i, 255);
        send(SCI_INDICSETOUTLINEALPHA, i, 255);
    }

    applyTheme();
    if (readOnly)
        send(SCI_SETREADONLY, 1);

    connect(this, &ScintillaEditBase::notifyChange, this, [this] {
        if (!m_silent)
            emit userEdited();
    });
    connect(this, &ScintillaEditBase::dwellStart, this, [this](int x, int y) {
        const int charIndex = charIndexAt(QPoint(x, y));
        if (charIndex >= 0)
            emit hoverAt(charIndex, viewport()->mapToGlobal(QPoint(x, y)));
    });
    connect(this, &ScintillaEditBase::dwellEnd, this, [this](int, int) { emit hoverEnded(); });
}

void SciEdit::applyFont()
{
    QFont base = QApplication::font();
    send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(base.family().toUtf8().constData()));
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, qMax(10, base.pointSize() + 2));
}

void SciEdit::applyTheme()
{
    const Theme::Colors &c = Theme::colors();
    applyFont();
    send(SCI_STYLESETBACK, STYLE_DEFAULT, Theme::toScintilla(c.editorBack));
    send(SCI_STYLESETFORE, STYLE_DEFAULT, Theme::toScintilla(c.editorFore));
    send(SCI_STYLECLEARALL);
    send(SCI_SETSELBACK, 1, Theme::toScintilla(c.editorSelection));
    send(SCI_SETSELFORE, 0, 0);
    send(SCI_SETCARETFORE, Theme::toScintilla(c.editorFore));
    send(SCI_SETCARETLINEBACK, Theme::toScintilla(c.caretLine));

    send(SCI_INDICSETFORE, TermIndicator, Theme::toScintilla(c.term));
    send(SCI_INDICSETFORE, FuzzyTermIndicator, Theme::toScintilla(c.fuzzyTerm));
    send(SCI_INDICSETFORE, IssueIndicator, Theme::toScintilla(c.issue));
    send(SCI_INDICSETFORE, SpellingIndicator, Theme::toScintilla(c.spelling));
    send(SCI_INDICSETFORE, GrammarIndicator, Theme::toScintilla(c.grammar));
}

int SciEdit::byteOffset(int charIndex) const
{
    return m_text.left(charIndex).toUtf8().size();
}

int SciEdit::charIndexFromByte(int byte) const
{
    const QString text = m_readOnly ? m_text : plainText();
    return QString::fromUtf8(text.toUtf8().left(byte)).size();
}

void SciEdit::setPlainText(const QString &text)
{
    m_silent = true;
    m_text = text;
    send(SCI_SETREADONLY, 0);
    send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.toUtf8().constData()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    if (m_readOnly)
        send(SCI_SETREADONLY, 1);
    m_silent = false;
}

QString SciEdit::plainText() const
{
    const int length = int(send(SCI_GETLENGTH));
    QByteArray buffer(length + 1, '\0');
    send(SCI_GETTEXT, length + 1, reinterpret_cast<sptr_t>(buffer.data()));
    buffer.resize(length);
    return QString::fromUtf8(buffer);
}

void SciEdit::setSpans(Indicator indicator, const QVector<Span> &spans)
{
    clearSpans(indicator);
    // The caller computed the spans on the text it set; read it back if the user has typed since.
    const QString current = m_readOnly ? m_text : plainText();
    if (current != m_text)
        m_text = current;
    send(SCI_SETINDICATORCURRENT, indicator);
    for (const Span &s : spans) {
        if (s.first < 0 || s.second <= 0 || s.first + s.second > m_text.size())
            continue;
        const int start = byteOffset(s.first);
        const int end = byteOffset(s.first + s.second);
        send(SCI_INDICATORFILLRANGE, start, end - start);
    }
}

void SciEdit::clearSpans(Indicator indicator)
{
    send(SCI_SETINDICATORCURRENT, indicator);
    send(SCI_INDICATORCLEARRANGE, 0, send(SCI_GETLENGTH));
}

void SciEdit::clearAllSpans()
{
    for (int i = 0; i <= GrammarIndicator; ++i)
        clearSpans(Indicator(i));
}

int SciEdit::charIndexAt(const QPoint &p) const
{
    const sptr_t byte = send(SCI_POSITIONFROMPOINTCLOSE, p.x(), p.y());
    if (byte < 0)
        return -1;
    return charIndexFromByte(int(byte));
}

int SciEdit::caretCharIndex() const
{
    const int byte = int(send(SCI_GETCURRENTPOS));
    const QString text = m_readOnly ? m_text : plainText();
    return QString::fromUtf8(text.toUtf8().left(byte)).size();
}

void SciEdit::insertAtCaret(const QString &text)
{
    if (m_readOnly)
        return;
    send(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(text.toUtf8().constData()));
    setFocus();
}

void SciEdit::replaceRange(int start, int length, const QString &text)
{
    if (m_readOnly)
        return;
    m_text = plainText();
    if (start < 0 || length < 0 || start + length > m_text.size())
        return;
    const int from = byteOffset(start);
    const int to = byteOffset(start + length);
    send(SCI_SETTARGETRANGE, from, to);
    send(SCI_REPLACETARGET, text.toUtf8().size(), reinterpret_cast<sptr_t>(text.toUtf8().constData()));
}

void SciEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const QPoint inView = viewport()->mapFromGlobal(event->globalPos());
    const int charIndex = charIndexAt(inView);

    if (!m_readOnly) {
        QAction *undo = menu.addAction(tr("Undo"), [this] { send(SCI_UNDO); });
        undo->setEnabled(send(SCI_CANUNDO));
        QAction *redo = menu.addAction(tr("Redo"), [this] { send(SCI_REDO); });
        redo->setEnabled(send(SCI_CANREDO));
        menu.addSeparator();
        QAction *cut = menu.addAction(tr("Cut"), [this] { send(SCI_CUT); });
        cut->setEnabled(send(SCI_GETSELECTIONEMPTY) == 0);
    }
    QAction *copy = menu.addAction(tr("Copy"), [this] { send(SCI_COPY); });
    copy->setEnabled(send(SCI_GETSELECTIONEMPTY) == 0);
    if (!m_readOnly) {
        QAction *paste = menu.addAction(tr("Paste"), [this] { send(SCI_PASTE); });
        paste->setEnabled(send(SCI_CANPASTE));
    }
    menu.addAction(tr("Select all"), [this] { send(SCI_SELECTALL); });

    emit contextMenuAboutToShow(&menu, charIndex);
    menu.exec(event->globalPos());
}
