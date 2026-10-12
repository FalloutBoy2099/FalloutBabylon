#include "Statistics.h"

namespace Statistics {

namespace {
bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3040 && u <= 0x30FF) || (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF)
        || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xAC00 && u <= 0xD7AF);
}
}

int countWords(const QString &text)
{
    int words = 0;
    bool inWord = false;
    for (QChar c : text) {
        if (isCjk(c)) {
            ++words;
            inWord = false;
        } else if (c.isLetterOrNumber()) {
            if (!inWord)
                ++words;
            inWord = true;
        } else if (!(c == QLatin1Char('\'') || c == QChar(0x2019) || c == QLatin1Char('-'))) {
            inWord = false;
        }
    }
    return words;
}

bool isTranslatable(const QString &text)
{
    for (QChar c : text)
        if (c.isLetter())
            return true;
    return false;
}

QString displayNameFor(const QString &relPath)
{
    QString name = relPath.mid(relPath.lastIndexOf(QLatin1Char('/')) + 1);
    if (name.endsWith(QLatin1String(".msg"), Qt::CaseInsensitive))
        name.chop(4);
    if (relPath.startsWith(QLatin1String("dialog/")) && !name.isEmpty()) {
        name.replace(QLatin1Char('_'), QLatin1Char(' '));
        name[0] = name.at(0).toUpper();
    }
    return name;
}

}
