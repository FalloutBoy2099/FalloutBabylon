#include "TranslationIndex.h"

#include <QReadLocker>
#include <QRegularExpression>
#include <QWriteLocker>
#include <algorithm>

namespace {

QStringList tokensOf(const QString &key)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}]{2,}"));
    QSet<QString> set;
    auto it = word.globalMatch(key);
    while (it.hasNext())
        set.insert(it.next().captured());
    return set.values();
}

}

bool TranslationIndex::usable(const TranslationRow &row)
{
    return row.inSource && !row.target.isEmpty() && Statistics::isTranslatable(row.source)
        && row.source.simplified().size() >= 3 && Project::statusOf(row) == SegmentStatus::Translated;
}

double TranslationIndex::similarity(const QString &a, const QString &b)
{
    if (a == b)
        return 1.0;
    const int la = int(a.size()), lb = int(b.size());
    const int longest = qMax(la, lb);
    if (longest == 0)
        return 1.0;
    // The ratio can never beat the length ratio: no need to compute when that is already too low.
    if (double(qMin(la, lb)) / longest < 0.3)
        return double(qMin(la, lb)) / longest * 0.5;
    QVector<int> prev(lb + 1), cur(lb + 1);
    for (int j = 0; j <= lb; ++j)
        prev[j] = j;
    for (int i = 1; i <= la; ++i) {
        cur[0] = i;
        for (int j = 1; j <= lb; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            cur[j] = qMin(qMin(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
        }
        std::swap(prev, cur);
    }
    return 1.0 - double(prev[lb]) / longest;
}

void TranslationIndex::addRows(const TranslationDoc &doc)
{
    QSet<QString> seen;                   // the same pair twice in one file is kept once, with a count
    QHash<QString, int> firstOf;
    QVector<int> added;
    for (const TranslationRow &row : doc.rows) {
        if (!usable(row))
            continue;
        const QString key = row.source.simplified().toLower();
        const QString pair = key + QChar(0x1F) + row.target.simplified();
        if (seen.contains(pair)) {
            ++m_entries[firstOf.value(pair)].count;
            continue;
        }
        seen.insert(pair);
        Entry e;
        e.relPath = doc.relPath;
        e.id = row.id;
        e.source = row.source;
        e.target = row.target;
        e.key = key;
        const QStringList tokens = tokensOf(key);
        e.tokens = int(tokens.size());
        const int index = int(m_entries.size());
        m_entries.push_back(e);
        firstOf.insert(pair, index);
        added << index;
        for (const QString &t : tokens)
            m_postings[t].push_back(index);
    }
    if (!added.isEmpty())
        m_byFile[doc.relPath] += added;
}

std::shared_ptr<TranslationIndex> TranslationIndex::build(const Project &project, const ProgressFn &progress,
                                                          const std::atomic_bool *cancel)
{
    auto index = std::make_shared<TranslationIndex>();
    const QStringList files = project.messageFiles();
    for (int i = 0; i < files.size(); ++i) {
        if (cancel && cancel->load())
            return std::make_shared<TranslationIndex>();
        index->addRows(project.loadDocument(files.at(i)));
        if (progress && i % 10 == 0)
            progress(i, int(files.size()));
    }
    if (progress)
        progress(int(files.size()), int(files.size()));
    return index;
}

void TranslationIndex::updateFile(const TranslationDoc &doc)
{
    QWriteLocker locker(&m_lock);
    for (int index : m_byFile.take(doc.relPath))
        if (m_entries[index].alive) {
            m_entries[index].alive = false;
            ++m_dead;
        }
    addRows(doc);
    if (m_dead > 1000 && m_dead * 3 > m_entries.size())
        compact();
}

void TranslationIndex::compact()
{
    const QVector<Entry> old = m_entries;
    m_entries.clear();
    m_postings.clear();
    m_byFile.clear();
    m_dead = 0;
    for (const Entry &e : old) {
        if (!e.alive)
            continue;
        const int index = int(m_entries.size());
        m_entries.push_back(e);
        m_byFile[e.relPath].push_back(index);
        for (const QString &t : tokensOf(e.key))
            m_postings[t].push_back(index);
    }
}

int TranslationIndex::size() const
{
    QReadLocker locker(&m_lock);
    return int(m_entries.size()) - m_dead;
}

QVector<TranslationIndex::Match> TranslationIndex::find(const QString &source, int max, double minScore,
                                                        const QString &excludeRel, int excludeId) const
{
    QVector<Match> result;
    const QString key = source.simplified().toLower();
    const QStringList tokens = tokensOf(key);
    if (tokens.isEmpty())
        return result;

    QReadLocker locker(&m_lock);
    // candidates: entries sharing words with the query (very common words are skipped when others exist)
    QHash<int, int> overlap;
    for (const QString &t : tokens) {
        const auto it = m_postings.constFind(t);
        if (it == m_postings.constEnd())
            continue;
        if (it->size() > 4000 && tokens.size() > 1)
            continue;
        for (int index : *it)
            ++overlap[index];
    }
    struct Candidate {
        int index;
        double dice;
    };
    QVector<Candidate> candidates;
    const double floor = qMax(0.3, minScore - 0.35);
    for (auto it = overlap.cbegin(); it != overlap.cend(); ++it) {
        const Entry &e = m_entries.at(it.key());
        if (!e.alive)
            continue;
        const double dice = 2.0 * it.value() / (tokens.size() + e.tokens);
        if (dice >= floor)
            candidates.push_back({it.key(), dice});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.dice > b.dice; });
    if (candidates.size() > 80)
        candidates.resize(80);

    QHash<QString, int> byTarget;                       // the same translation found several times: one line
    for (const Candidate &c : std::as_const(candidates)) {
        const Entry &e = m_entries.at(c.index);
        if (e.relPath == excludeRel && e.id == excludeId && e.count == 1)
            continue;
        const double score = similarity(key, e.key);
        if (score < minScore)
            continue;
        const QString targetKey = e.target.simplified();
        const auto seen = byTarget.constFind(targetKey);
        if (seen != byTarget.constEnd()) {
            Match &m = result[seen.value()];
            m.count += e.count;
            if (score > m.score) {
                m.score = score;
                m.relPath = e.relPath;
                m.id = e.id;
                m.source = e.source;
            }
            continue;
        }
        byTarget.insert(targetKey, int(result.size()));
        result.push_back({e.relPath, e.id, e.source, e.target, score, e.count});
    }
    std::stable_sort(result.begin(), result.end(), [](const Match &a, const Match &b) {
        return a.score != b.score ? a.score > b.score : a.count > b.count;
    });
    if (result.size() > max)
        result.resize(max);
    return result;
}
