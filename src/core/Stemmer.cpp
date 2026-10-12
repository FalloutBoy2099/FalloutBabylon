#include "Stemmer.h"

namespace Stemmer {

namespace {

bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3040 && u <= 0x30FF) || (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF)
        || (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xAC00 && u <= 0xD7AF);
}

bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019);
}

bool endsWithAny(const QString &w, std::initializer_list<const char *> suffixes)
{
    for (const char *s : suffixes)
        if (w.endsWith(QLatin1String(s)))
            return true;
    return false;
}

}

QString normalize(const QString &word, const QString &language)
{
    QString w = word.toLower();
    // possessive: "gizmo's", "raiders'"
    if (w.endsWith(QLatin1String("'s")) || w.endsWith(QChar(0x2019) + QStringLiteral("s")))
        w.chop(2);
    else if (!w.isEmpty() && isApostrophe(w.back()))
        w.chop(1);

    const int n = w.size();
    if (n <= 3)
        return w;

    if (language == QLatin1String("english")) {
        if (w.endsWith(QLatin1String("ies")) && n > 4)
            return w.left(n - 3) + QLatin1Char('y');
        if (endsWithAny(w, {"sses", "xes", "zes", "ches", "shes"}))
            return w.left(n - 2);
        if (w.endsWith(QLatin1Char('s')) && !endsWithAny(w, {"ss", "us", "is"}))
            return w.left(n - 1);
    } else if (language == QLatin1String("french")) {
        if (w.endsWith(QLatin1String("aux")) && n > 4)
            return w.left(n - 3) + QLatin1String("al");
        if (w.endsWith(QLatin1Char('s')) || w.endsWith(QLatin1Char('x')))
            return w.left(n - 1);
    } else if (language == QLatin1String("spanish") || language == QLatin1String("portuguese")) {
        if (w.endsWith(QLatin1String("es")) && n > 4)
            return w.left(n - 2);
        if (w.endsWith(QLatin1Char('s')))
            return w.left(n - 1);
    } else if (language == QLatin1String("italian")) {
        if (w.endsWith(QLatin1Char('i')) || w.endsWith(QLatin1Char('e')))
            return w.left(n - 1);
    } else if (language == QLatin1String("german")) {
        if (n > 5 && endsWithAny(w, {"en", "er", "es"}))
            return w.left(n - 2);
        if (n > 4 && (w.endsWith(QLatin1Char('e')) || w.endsWith(QLatin1Char('n')) || w.endsWith(QLatin1Char('s'))))
            return w.left(n - 1);
    } else if (language == QLatin1String("polish") || language == QLatin1String("czech")
               || language == QLatin1String("russian")) {
        // Inflected languages: drop one trailing vowel/consonant ending so declined forms still meet.
        if (n > 5)
            return w.left(n - 1);
    } else if (w.endsWith(QLatin1Char('s')) && !w.endsWith(QLatin1String("ss"))) {
        return w.left(n - 1);
    }
    return w;
}

QVector<Token> tokenize(const QString &text, const QString &language)
{
    QVector<Token> tokens;
    const int n = text.size();
    // French and Italian elide articles with an apostrophe (l'armure, dell'arma): it separates words there.
    const bool elision = language == QLatin1String("french") || language == QLatin1String("italian");
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (isCjk(c)) {
            tokens.push_back({i, 1, QString(c)});
            ++i;
            continue;
        }
        if (!c.isLetterOrNumber()) {
            ++i;
            continue;
        }
        int j = i + 1;
        while (j < n) {
            const QChar d = text.at(j);
            if (isCjk(d))
                break;
            if (d.isLetterOrNumber()) {
                ++j;
            } else if (!elision && isApostrophe(d) && j + 1 < n && text.at(j + 1).isLetter()) {
                ++j; // inner apostrophe: gizmo's
            } else if (isApostrophe(d) && j + 1 >= n) {
                ++j;
            } else {
                break;
            }
        }
        const QString raw = text.mid(i, j - i);
        tokens.push_back({i, j - i, normalize(raw, language)});
        i = j;
    }
    return tokens;
}

bool withinOneEdit(const QString &a, const QString &b)
{
    const int la = a.size(), lb = b.size();
    if (qAbs(la - lb) > 1)
        return false;
    int i = 0;
    while (i < la && i < lb && a.at(i) == b.at(i))
        ++i;
    if (i == la || i == lb)
        return qAbs(la - lb) <= 1;
    if (la == lb)
        return a.mid(i + 1) == b.mid(i + 1);
    if (la > lb)
        return a.mid(i + 1) == b.mid(i);
    return a.mid(i) == b.mid(i + 1);
}

}
