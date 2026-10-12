#pragma once

#include "Stemmer.h"

#include <QHash>
#include <QStringList>
#include <QVector>

// Finds known terms (single or multi word) in a text, tolerating plural forms, possessives,
// case differences and, for long words, a single typo.
class TermMatcher
{
public:
    struct Match {
        int start = 0;        // offset in the text (UTF-16 units)
        int length = 0;
        int termId = -1;
        bool fuzzy = false;   // matched through a one-character difference
        bool caseMatch = true; // first letter capitalisation equals the term's one
    };

    explicit TermMatcher(const QString &language = QStringLiteral("english")) : m_language(language) {}

    void add(const QString &term, int termId);
    void build();
    QVector<Match> find(const QString &text) const;

private:
    struct Compiled {
        QVector<QString> tokens;
        int termId = -1;
        bool upperFirst = false;
    };
    static bool tokenEquals(const QString &a, const QString &b, bool *fuzzy);
    QVector<QString> deletions(const QString &word) const;

    QString m_language;
    QVector<Compiled> m_terms;
    QHash<QString, QVector<int>> m_byFirst;         // first token -> compiled term indices, longest first
    QHash<QString, QStringList> m_fuzzyKeys;        // deletion variant -> first tokens
    bool m_built = false;
};
