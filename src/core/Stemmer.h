#pragma once

#include <QString>
#include <QVector>

// Tokenisation and light normalisation (case, possessives, plural forms) used to recognise
// glossary terms in text even when they do not match exactly.
namespace Stemmer {

struct Token {
    int start = 0;       // offset in the original text (UTF-16 units)
    int length = 0;
    QString normalized;
};

// Lower-cases, drops possessive 's and strips plural endings according to the language
// (game language directory name: "english", "french"...).
QString normalize(const QString &word, const QString &language);

QVector<Token> tokenize(const QString &text, const QString &language);

// True when a and b differ by at most one insertion, deletion or substitution.
bool withinOneEdit(const QString &a, const QString &b);

}
