#pragma once

#include <QString>
#include <QVector>

// Progress numbers of one message file (or the sum of several).
struct FileStats {
    QString relPath;
    QString displayName;       // character / file name ("Abel" for dialog/abel.msg)
    int kind = 0;              // FileKind
    int lines = 0;
    int translated = 0;
    int review = 0;
    int untranslated = 0;
    qint64 words = 0;          // words of the source text
    qint64 wordsRemaining = 0;
    qint64 chars = 0;
    qint64 charsRemaining = 0;

    // Share of lines that have a translation (records marked "to review" count: they are translated).
    double percent() const { return lines ? 100.0 * (lines - untranslated) / lines : 100.0; }
    bool isUntouched() const { return lines > 0 && translated == 0 && review == 0; }
    bool isComplete() const { return untranslated == 0 && review == 0; }
    bool isPartial() const { return !isComplete() && !isUntouched(); }

    void add(const FileStats &o)
    {
        lines += o.lines;
        translated += o.translated;
        review += o.review;
        untranslated += o.untranslated;
        words += o.words;
        wordsRemaining += o.wordsRemaining;
        chars += o.chars;
        charsRemaining += o.charsRemaining;
    }
};

namespace Statistics {

// Number of words (CJK: number of characters).
int countWords(const QString &text);
// True when the text contains something worth translating (at least one letter).
bool isTranslatable(const QString &text);
// "dialog/abel.msg" -> "Abel", "game/pro_item.msg" -> "pro_item"
QString displayNameFor(const QString &relPath);

}
