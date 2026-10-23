#include "Glossary.h"

#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <algorithm>

void Glossary::clear()
{
    m_raw.clear();
    m_terms.clear();
    m_signature.clear();
    m_sourceMatcher.reset();
    m_targetMatcher.reset();
}

void Glossary::add(const QString &source, const QString &target, const QString &category, const QString &origin)
{
    const QString s = source.simplified();
    if (s.isEmpty())
        return;
    m_raw.push_back({s, target.simplified(), category, origin, false, false});
}

void Glossary::addCustom(const QString &source, const QString &target, bool keep, const QString &category)
{
    const QString s = source.simplified();
    if (s.isEmpty())
        return;
    m_raw.push_back({s, keep ? s : target.simplified(), category, QStringLiteral("(manual)"), true, keep});
}

void Glossary::finalize(const QString &sourceLanguage, const QString &targetLanguage)
{
    m_terms.clear();

    struct Group {
        GlossaryTerm term;
        QHash<QString, int> counts;       // lower-cased target -> occurrences
        QHash<QString, QString> spelling; // lower-cased target -> first spelling seen
        QHash<QString, int> customCounts; // same, for the terms entered by hand
    };
    QVector<Group> groups;
    QHash<QString, int> byKey;

    for (const Raw &r : std::as_const(m_raw)) {
        QStringList key;
        for (const auto &t : Stemmer::tokenize(r.source, sourceLanguage))
            key << t.normalized;
        if (key.isEmpty())
            continue;
        const QString k = key.join(QLatin1Char(' '));
        int idx = byKey.value(k, -1);
        if (idx < 0) {
            idx = groups.size();
            byKey.insert(k, idx);
            groups.push_back({});
            groups.back().term.source = r.source;
            groups.back().term.category = r.category;
        }
        Group &g = groups[idx];
        g.term.origins << r.origin;
        if (r.custom) {
            if (!g.term.custom) {
                g.term.custom = true;
                g.term.source = r.source;
                g.term.category = r.category;
            }
            g.term.keep = r.keep;
        }
        if (!r.target.isEmpty()) {
            const QString lower = r.target.toLower();
            (r.custom ? g.customCounts : g.counts)[lower] += 1;
            if (!g.spelling.contains(lower) || r.custom)
                g.spelling.insert(lower, r.target);
        }
    }

    for (Group &g : groups) {
        // What the user typed is the reference: the translations of the game files are ignored for that term.
        if (g.term.custom)
            g.counts = g.customCounts;
        QList<QString> keys = g.counts.keys();
        std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b) {
            const int ca = g.counts.value(a), cb = g.counts.value(b);
            return ca != cb ? ca > cb : a < b;
        });
        for (const QString &k : std::as_const(keys))
            g.term.targets << g.spelling.value(k);
        m_terms.push_back(g.term);
    }
    std::sort(m_terms.begin(), m_terms.end(), [](const GlossaryTerm &a, const GlossaryTerm &b) {
        return a.source.compare(b.source, Qt::CaseInsensitive) < 0;
    });

    {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        hash.addData(sourceLanguage.toUtf8());
        hash.addData(targetLanguage.toUtf8());
        for (const GlossaryTerm &t : std::as_const(m_terms)) {
            hash.addData(t.source.toUtf8());
            hash.addData(t.targets.join(QLatin1Char('\t')).toUtf8());
            hash.addData(t.custom ? "c" : "g", 1);
        }
        m_signature = QString::fromLatin1(hash.result().toHex());
    }

    m_sourceMatcher = std::make_unique<TermMatcher>(sourceLanguage);
    m_targetMatcher = std::make_unique<TermMatcher>(targetLanguage);
    for (int i = 0; i < m_terms.size(); ++i) {
        m_sourceMatcher->add(m_terms.at(i).source, i);
        for (const QString &t : m_terms.at(i).targets)
            m_targetMatcher->add(t, i);
    }
    m_sourceMatcher->build();
    m_targetMatcher->build();
    m_raw.clear();
    m_raw.squeeze();
}

int Glossary::conflictCount() const
{
    int n = 0;
    for (const GlossaryTerm &t : m_terms)
        n += t.conflict() ? 1 : 0;
    return n;
}

QVector<Glossary::Hit> Glossary::findInSource(const QString &text) const
{
    QVector<Hit> hits;
    if (!m_sourceMatcher)
        return hits;
    for (const auto &m : m_sourceMatcher->find(text))
        hits.push_back({m, &m_terms.at(m.termId)});
    return hits;
}

QVector<Glossary::Hit> Glossary::findInTarget(const QString &text) const
{
    QVector<Hit> hits;
    if (!m_targetMatcher)
        return hits;
    for (const auto &m : m_targetMatcher->find(text))
        hits.push_back({m, &m_terms.at(m.termId)});
    return hits;
}

QVector<Glossary::Issue> Glossary::checkConsistency(const QString &source, const QString &target) const
{
    QVector<Issue> issues;
    if (target.trimmed().isEmpty() || !m_sourceMatcher)
        return issues;

    QSet<const GlossaryTerm *> presentInTarget;
    for (const Hit &h : findInTarget(target))
        presentInTarget.insert(h.term);

    QSet<const GlossaryTerm *> reported;
    for (const Hit &h : findInSource(source)) {
        // Weak matches (a common word written with a different capitalisation) are not worth an alert.
        if (!h.match.caseMatch || h.term->targets.isEmpty())
            continue;
        if (presentInTarget.contains(h.term) || reported.contains(h.term))
            continue;
        reported.insert(h.term);
        Issue issue;
        issue.term = h.term;
        issue.sourceMatch = h.match;
        issue.message = QStringLiteral("%1 → %2").arg(h.term->source, h.term->targets.join(QStringLiteral(" / ")));
        issues.push_back(issue);
    }
    return issues;
}
