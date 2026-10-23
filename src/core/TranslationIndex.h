#pragma once

#include "Project.h"

#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <atomic>
#include <functional>
#include <memory>

// Translation memory of the project itself: every translated line (source -> translation), searchable by
// similarity, so the translator is offered what was already done for the same or a close text.
// find() and updateFile() may be called from different threads.
class TranslationIndex
{
public:
    struct Match {
        QString relPath;
        int id = 0;
        QString source;
        QString target;
        double score = 0;     // 1.0: same text
        int count = 1;        // how many lines carry this very pair
    };
    using ProgressFn = std::function<void(int done, int total)>;

    // Reads every message file of the project (saved state). Returns an empty index when cancelled.
    static std::shared_ptr<TranslationIndex> build(const Project &project, const ProgressFn &progress = {},
                                                   const std::atomic_bool *cancel = nullptr);

    // Replaces what the index knows of one file (after it was saved or imported).
    void updateFile(const TranslationDoc &doc);

    // The closest translated lines for `source`, best first (at most `max`, at least `minScore` similar).
    // The line (excludeRel, excludeId) itself is not offered.
    QVector<Match> find(const QString &source, int max = 6, double minScore = 0.7, const QString &excludeRel = {},
                        int excludeId = -1) const;

    int size() const;             // lines currently indexed

    // Only translated lines with real text are worth remembering.
    static bool usable(const TranslationRow &row);
    static double similarity(const QString &a, const QString &b);     // 0..1, on simplified lower-case text

private:
    struct Entry {
        QString relPath;
        int id = 0;
        QString source, target;
        QString key;                 // simplified, lower case source
        int tokens = 0;
        int count = 1;
        bool alive = true;
    };
    void addRows(const TranslationDoc &doc);
    void compact();

    mutable QReadWriteLock m_lock;
    QVector<Entry> m_entries;
    QHash<QString, QVector<int>> m_postings;            // token -> entries
    QHash<QString, QVector<int>> m_byFile;              // file -> entries
    QHash<QString, int> m_pairs;                        // "source key \x1f target" -> entry (to count repeats)
    int m_dead = 0;
};
