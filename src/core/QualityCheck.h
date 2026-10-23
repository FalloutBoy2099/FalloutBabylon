#pragma once

#include "Project.h"

#include <QHash>
#include <atomic>
#include <functional>

// Quality report over every translated line of a project.
namespace QualityCheck {

enum Kind {
    Tokens = 1,        // @Name@, %s, %d, {field} lost or invented by the translation
    Spacing = 2,       // leading / trailing / doubled spaces, text made of spaces only
    Encoding = 4,      // characters the target encoding cannot hold (they would become '?')
    Length = 8,        // translation much longer than the source
    Duplicates = 16,   // same source text translated differently elsewhere in the project
    Glossary = 32,     // glossary term not respected
    AllKinds = Tokens | Spacing | Encoding | Length | Duplicates | Glossary
};

struct Options {
    int kinds = AllKinds;
    double maxRatio = 2.5;         // Length: target longer than ratio x source (and over minLengthChars)
    int minLengthChars = 40;
    int maxChars = 0;              // Length: absolute limit of the translation (0 = none)
    int minDuplicateChars = 10;    // Duplicates: ignore shorter sources ("Yes", "No"...)
};

struct Issue {
    QString relPath;
    int id = 0;
    Kind kind = Tokens;
    QString message;
    QString source;
    QString target;
};

using ProgressFn = std::function<void(int done, int total)>;

// Results kept from one run to the next, per file: a file whose content, marks, glossary and checks did not
// change is not read again. Persisted as JSON next to the project.
class Cache
{
public:
    struct DupRecord {
        int id = 0;
        quint64 source = 0;      // hash of the simplified source text
        quint64 target = 0;      // hash of the simplified translation
    };
    struct Entry {
        QString signature;
        QVector<Issue> issues;       // every check but the duplicates
        QVector<DupRecord> dups;
    };

    // Entries are only valid for the same `key` (checks, limits, glossary...); another key starts empty.
    void open(const QString &path, const QString &key);
    void save();
    const Entry *find(const QString &rel, const QString &signature) const;
    void set(const QString &rel, const Entry &entry);
    void keepOnly(const QStringList &files);     // forgets files that no longer exist
    int size() const { return int(m_files.size()); }

private:
    QString m_path, m_key;
    QHash<QString, Entry> m_files;
    bool m_dirty = false;
};

// Checks of one file (without the project-wide duplicate check).
QVector<Issue> checkDocument(const TranslationDoc &doc, const ProjectConfig &config, const ::Glossary *glossary,
                             const Options &options);

// The whole project. `glossary` may be null (the glossary check is then skipped).
// With a `cache` (opened by the caller with cacheKey()), unchanged files are not read again; `cacheHits` receives
// the number of files answered from it.
QVector<Issue> run(const Project &project, const ::Glossary *glossary, const Options &options,
                   const ProgressFn &progress = {}, const std::atomic_bool *cancel = nullptr, Cache *cache = nullptr,
                   int *cacheHits = nullptr);

// What the cached results of a run depend on besides the files themselves.
QString cacheKey(const Project &project, const ::Glossary *glossary, const Options &options);

QString kindName(Kind kind);

}
