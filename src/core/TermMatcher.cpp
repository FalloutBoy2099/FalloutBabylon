#include "TermMatcher.h"

#include <QSet>
#include <algorithm>

namespace {

constexpr int kMinFuzzyLength = 6;

const QSet<QString> &stopWords()
{
    static const QSet<QString> words = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("you"), QStringLiteral("for"), QStringLiteral("yes"),
        QStringLiteral("not"), QStringLiteral("are"), QStringLiteral("his"), QStringLiteral("her"), QStringLiteral("with"),
        QStringLiteral("that"), QStringLiteral("this"), QStringLiteral("from"), QStringLiteral("have"), QStringLiteral("will"),
        QStringLiteral("what"), QStringLiteral("who"), QStringLiteral("how"), QStringLiteral("why"), QStringLiteral("but"),
        QStringLiteral("all"), QStringLiteral("any"), QStringLiteral("can"), QStringLiteral("out"), QStringLiteral("one"),
    };
    return words;
}

bool gapIsSeparator(const QString &text, int from, int to)
{
    for (int i = from; i < to; ++i) {
        const QChar c = text.at(i);
        if (!(c.isSpace() || c == QLatin1Char('-') || c == QChar(0x2013) || c == QChar(0x2014)))
            return false;
    }
    return true;
}

}

void TermMatcher::add(const QString &term, int termId)
{
    const QVector<Stemmer::Token> tokens = Stemmer::tokenize(term, m_language);
    if (tokens.isEmpty())
        return;
    if (tokens.size() == 1) {
        const QString &t = tokens.first().normalized;
        if (t.size() < 3 || stopWords().contains(t))
            return;
    }
    Compiled c;
    c.termId = termId;
    c.upperFirst = term.at(tokens.first().start).isUpper();
    for (const auto &t : tokens)
        c.tokens.push_back(t.normalized);
    m_terms.push_back(c);
    m_built = false;
}

QVector<QString> TermMatcher::deletions(const QString &word) const
{
    QVector<QString> out;
    out.reserve(word.size());
    for (int i = 0; i < word.size(); ++i)
        out.push_back(word.left(i) + word.mid(i + 1));
    return out;
}

void TermMatcher::build()
{
    m_byFirst.clear();
    m_fuzzyKeys.clear();
    for (int i = 0; i < m_terms.size(); ++i)
        m_byFirst[m_terms.at(i).tokens.first()].push_back(i);

    for (auto it = m_byFirst.begin(); it != m_byFirst.end(); ++it) {
        std::stable_sort(it.value().begin(), it.value().end(), [&](int a, int b) {
            return m_terms.at(a).tokens.size() > m_terms.at(b).tokens.size();
        });
        const QString &key = it.key();
        if (key.size() >= kMinFuzzyLength) {
            m_fuzzyKeys[key].push_back(key);
            for (const QString &d : deletions(key))
                m_fuzzyKeys[d].push_back(key);
        }
    }
    m_built = true;
}

bool TermMatcher::tokenEquals(const QString &a, const QString &b, bool *fuzzy)
{
    if (a == b)
        return true;
    if (a.size() >= kMinFuzzyLength && b.size() >= kMinFuzzyLength && Stemmer::withinOneEdit(a, b)) {
        if (fuzzy)
            *fuzzy = true;
        return true;
    }
    return false;
}

QVector<TermMatcher::Match> TermMatcher::find(const QString &text) const
{
    QVector<Match> matches;
    if (!m_built || m_terms.isEmpty())
        return matches;

    const QVector<Stemmer::Token> tokens = Stemmer::tokenize(text, m_language);

    auto tryTerm = [&](int tokenIndex, const Compiled &term, Match &match) {
        const int count = term.tokens.size();
        if (tokenIndex + count > tokens.size())
            return false;
        bool fuzzy = false;
        for (int k = 0; k < count; ++k) {
            const auto &tok = tokens.at(tokenIndex + k);
            if (!tokenEquals(tok.normalized, term.tokens.at(k), &fuzzy))
                return false;
            if (k > 0) {
                const auto &prev = tokens.at(tokenIndex + k - 1);
                if (!gapIsSeparator(text, prev.start + prev.length, tok.start))
                    return false;
            }
        }
        const auto &first = tokens.at(tokenIndex);
        const auto &last = tokens.at(tokenIndex + count - 1);
        match.start = first.start;
        match.length = last.start + last.length - first.start;
        match.termId = term.termId;
        match.fuzzy = fuzzy;
        match.caseMatch = text.at(first.start).isUpper() == term.upperFirst;
        return true;
    };

    for (int i = 0; i < tokens.size();) {
        const QString &norm = tokens.at(i).normalized;
        Match match;
        int consumed = 0;

        // Exact first-token lookup (longest term first), then fuzzy lookup.
        auto exact = m_byFirst.constFind(norm);
        if (exact != m_byFirst.constEnd()) {
            for (int idx : exact.value())
                if (tryTerm(i, m_terms.at(idx), match)) {
                    consumed = m_terms.at(idx).tokens.size();
                    break;
                }
        }
        if (!consumed && norm.size() >= kMinFuzzyLength) {
            QStringList keys;
            const auto direct = m_fuzzyKeys.constFind(norm);
            if (direct != m_fuzzyKeys.constEnd())
                keys += direct.value();
            for (const QString &d : deletions(norm)) {
                const auto it = m_fuzzyKeys.constFind(d);
                if (it != m_fuzzyKeys.constEnd())
                    keys += it.value();
            }
            keys.removeDuplicates();
            for (const QString &key : std::as_const(keys)) {
                if (key == norm || !Stemmer::withinOneEdit(key, norm))
                    continue;
                for (int idx : m_byFirst.value(key))
                    if (tryTerm(i, m_terms.at(idx), match)) {
                        match.fuzzy = true;
                        consumed = m_terms.at(idx).tokens.size();
                        break;
                    }
                if (consumed)
                    break;
            }
        }
        if (consumed) {
            matches.push_back(match);
            i += consumed;
        } else {
            ++i;
        }
    }
    return matches;
}
