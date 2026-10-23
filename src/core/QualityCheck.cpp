#include "QualityCheck.h"

#include "Placeholders.h"
#include "TextCodec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace QualityCheck {

namespace {

bool isChecked(const TranslationRow &row)
{
    // Lines still to translate are reported by the statistics, not here.
    return row.inSource && !row.target.isEmpty() && Project::statusOf(row) != SegmentStatus::Untranslated;
}

QString shorten(QString s, int n = 40)
{
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return s.size() > n ? s.left(n) + QStringLiteral("…") : s;
}

QStringList sortedTokens(const QString &text)
{
    QStringList tokens = Placeholders::protect(text).tokens;
    tokens.sort();
    return tokens;
}

void add(QVector<Issue> &out, const TranslationDoc &doc, const TranslationRow &row, Kind kind, const QString &message)
{
    Issue issue;
    issue.relPath = doc.relPath;
    issue.id = row.id;
    issue.kind = kind;
    issue.message = message;
    issue.source = row.source;
    issue.target = row.target;
    out.push_back(issue);
}

}

QString kindName(Kind kind)
{
    switch (kind) {
    case Tokens: return QCoreApplication::translate("QualityCheck", "Tokens");
    case Spacing: return QCoreApplication::translate("QualityCheck", "Spacing");
    case Encoding: return QCoreApplication::translate("QualityCheck", "Encoding");
    case Length: return QCoreApplication::translate("QualityCheck", "Length");
    case Duplicates: return QCoreApplication::translate("QualityCheck", "Inconsistent duplicate");
    case Glossary: return QCoreApplication::translate("QualityCheck", "Glossary");
    default: return QString();
    }
}

QVector<Issue> checkDocument(const TranslationDoc &doc, const ProjectConfig &config, const ::Glossary *glossary,
                             const Options &options)
{
    QVector<Issue> out;
    for (const TranslationRow &row : doc.rows) {
        if (!isChecked(row))
            continue;
        const QString &src = row.source;
        const QString &tgt = row.target;

        if (options.kinds & Tokens) {
            const QStringList a = sortedTokens(src), b = sortedTokens(tgt);
            if (a != b) {
                QStringList missing = a, extra = b;
                for (const QString &t : b)
                    missing.removeOne(t);
                for (const QString &t : a)
                    extra.removeOne(t);
                QStringList parts;
                if (!missing.isEmpty())
                    parts << QCoreApplication::translate("QualityCheck", "missing: %1").arg(missing.join(QStringLiteral(" ")));
                if (!extra.isEmpty())
                    parts << QCoreApplication::translate("QualityCheck", "unexpected: %1").arg(extra.join(QStringLiteral(" ")));
                add(out, doc, row, Tokens, parts.join(QStringLiteral("; ")));
            }
        }

        if (options.kinds & Spacing) {
            QStringList problems;
            if (tgt.trimmed().isEmpty()) {
                problems << QCoreApplication::translate("QualityCheck", "the translation only contains spaces");
            } else {
                if (src.trimmed() == src && tgt.trimmed() != tgt)
                    problems << QCoreApplication::translate("QualityCheck", "spaces at the start or the end");
                else if (src.trimmed() != src && tgt.trimmed() == tgt)
                    problems << QCoreApplication::translate("QualityCheck", "the source has spaces at the start or the end");
                if (tgt.contains(QStringLiteral("  ")) && !src.contains(QStringLiteral("  ")))
                    problems << QCoreApplication::translate("QualityCheck", "doubled spaces");
            }
            if (!problems.isEmpty())
                add(out, doc, row, Spacing, problems.join(QStringLiteral("; ")));
        }

        if (options.kinds & Encoding) {
            QString bad;
            TextCodec::encode(tgt, config.targetEncoding, &bad);
            if (!bad.isEmpty())
                add(out, doc, row, Encoding,
                    QCoreApplication::translate("QualityCheck", "cannot be written as %1: %2").arg(config.targetEncoding, bad));
        }

        if (options.kinds & Length) {
            if (options.maxChars > 0 && tgt.size() > options.maxChars)
                add(out, doc, row, Length,
                    QCoreApplication::translate("QualityCheck", "%1 characters (limit %2)").arg(tgt.size()).arg(options.maxChars));
            else if (doc.relPath.startsWith(QLatin1String("game/")) && src.size() <= 24 && src.size() > 0
                     && tgt.size() > qMax(1.6 * src.size(), src.size() + 8.0))
                add(out, doc, row, Length,
                    QCoreApplication::translate("QualityCheck", "short label of the interface: %1 characters for %2 in the source")
                        .arg(tgt.size()).arg(src.size()));
            else if (tgt.size() > options.minLengthChars && src.size() > 0 && tgt.size() > options.maxRatio * src.size())
                add(out, doc, row, Length,
                    QCoreApplication::translate("QualityCheck", "%1 characters for %2 in the source").arg(tgt.size()).arg(src.size()));
        }

        if ((options.kinds & Glossary) && glossary) {
            for (const auto &issue : glossary->checkConsistency(src, tgt))
                add(out, doc, row, Glossary, issue.message);
        }
    }
    return out;
}

namespace {

quint64 hash64(const QString &text)
{
    quint64 h = 14695981039346656037ull;               // FNV-1a over the UTF-16 units
    for (QChar c : text)
        h = (h ^ c.unicode()) * 1099511628211ull;
    return h ? h : 1ull;
}

QString hex(quint64 v)
{
    return QString::number(v, 16);
}

struct Where {
    QString relPath;
    int id = 0;
};

QJsonArray issueToJson(const Issue &i)
{
    return QJsonArray{i.id, int(i.kind), i.message, i.source, i.target};
}

}

QString cacheKey(const Project &project, const ::Glossary *glossary, const Options &o)
{
    const ProjectConfig &c = project.config();
    return QStringLiteral("v1|%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(o.kinds & ~Duplicates)
        .arg(o.maxRatio)
        .arg(o.minLengthChars)
        .arg(o.maxChars)
        .arg(o.minDuplicateChars)
        .arg(glossary ? glossary->signature() : QString())
        .arg(c.targetEncoding, c.sourceLanguage, c.targetLanguage);
}

// ----------------------------------------------------------------------------- cache

void Cache::open(const QString &path, const QString &key)
{
    m_path = path;
    m_key = key;
    m_files.clear();
    m_dirty = false;
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("key")).toString() != key)
        return;
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.signature = o.value(QStringLiteral("sig")).toString();
        const QString rel = it.key();
        for (const QJsonValue &v : o.value(QStringLiteral("issues")).toArray()) {
            const QJsonArray a = v.toArray();
            if (a.size() < 5)
                continue;
            Issue i;
            i.relPath = rel;
            i.id = a.at(0).toInt();
            i.kind = Kind(a.at(1).toInt());
            i.message = a.at(2).toString();
            i.source = a.at(3).toString();
            i.target = a.at(4).toString();
            e.issues.push_back(i);
        }
        for (const QJsonValue &v : o.value(QStringLiteral("dups")).toArray()) {
            const QJsonArray a = v.toArray();
            if (a.size() < 3)
                continue;
            e.dups.push_back({a.at(0).toInt(), a.at(1).toString().toULongLong(nullptr, 16),
                              a.at(2).toString().toULongLong(nullptr, 16)});
        }
        m_files.insert(rel, e);
    }
}

void Cache::save()
{
    if (m_path.isEmpty() || !m_dirty)
        return;
    QJsonObject files;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        QJsonArray issues, dups;
        for (const Issue &i : it->issues)
            issues.append(issueToJson(i));
        for (const DupRecord &d : it->dups)
            dups.append(QJsonArray{d.id, hex(d.source), hex(d.target)});
        files.insert(it.key(), QJsonObject{{QStringLiteral("sig"), it->signature},
                                           {QStringLiteral("issues"), issues},
                                           {QStringLiteral("dups"), dups}});
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("key"), m_key}, {QStringLiteral("files"), files}}).toJson(QJsonDocument::Compact));
        if (f.commit())
            m_dirty = false;
    }
}

const Cache::Entry *Cache::find(const QString &rel, const QString &signature) const
{
    const auto it = m_files.constFind(rel);
    return it != m_files.constEnd() && it->signature == signature ? &it.value() : nullptr;
}

void Cache::set(const QString &rel, const Entry &entry)
{
    m_files.insert(rel, entry);
    m_dirty = true;
}

void Cache::keepOnly(const QStringList &files)
{
    const QSet<QString> keep(files.begin(), files.end());
    for (auto it = m_files.begin(); it != m_files.end();) {
        if (keep.contains(it.key())) {
            ++it;
        } else {
            it = m_files.erase(it);
            m_dirty = true;
        }
    }
}

// ----------------------------------------------------------------------------- the run

QVector<Issue> run(const Project &project, const ::Glossary *glossary, const Options &options, const ProgressFn &progress,
                   const std::atomic_bool *cancel, Cache *cache, int *cacheHits)
{
    QVector<Issue> issues;
    if (cacheHits)
        *cacheHits = 0;
    // source hash -> translation hash -> where
    QHash<quint64, QHash<quint64, QVector<Where>>> seen;
    const bool duplicates = options.kinds & Duplicates;

    const QStringList files = project.messageFiles();
    for (int i = 0; i < files.size(); ++i) {
        if (cancel && cancel->load())
            break;
        if (progress && i % 10 == 0)
            progress(i, int(files.size()));
        const QString &rel = files.at(i);
        const QString signature = cache ? project.contentSignature(rel) : QString();
        const Cache::Entry *cached = cache ? cache->find(rel, signature) : nullptr;
        Cache::Entry fresh;
        if (cached) {
            if (cacheHits)
                ++*cacheHits;
        } else {
            const TranslationDoc doc = project.loadDocument(rel);
            fresh.signature = signature;
            fresh.issues = checkDocument(doc, project.config(), glossary, options);
            for (const TranslationRow &row : doc.rows) {
                if (!isChecked(row))
                    continue;
                const QString key = row.source.simplified();
                if (key.size() < options.minDuplicateChars)
                    continue;
                fresh.dups.push_back({row.id, hash64(key), hash64(row.target.simplified())});
            }
            if (cache)
                cache->set(rel, fresh);
            cached = &fresh;
        }
        issues += cached->issues;
        if (duplicates)
            for (const Cache::DupRecord &d : cached->dups)
                seen[d.source][d.target].push_back({rel, d.id});
    }
    if (cache && !(cancel && cancel->load())) {
        cache->keepOnly(files);
        cache->save();
    }

    if (duplicates && !(cancel && cancel->load())) {
        // Groups of the same source translated in several ways: the most used translation is the reference.
        struct Conflict {
            quint64 best = 0;
            int bestCount = 0;
        };
        QHash<quint64, Conflict> conflicts;
        QHash<QString, QVector<QPair<int, quint64>>> wanted;      // file -> (id, source hash) of the rows to report
        QHash<quint64, Where> bestWhere;
        for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
            if (it->size() < 2)
                continue;
            Conflict c;
            for (auto t = it->cbegin(); t != it->cend(); ++t)
                if (int(t->size()) > c.bestCount || (int(t->size()) == c.bestCount && t.key() < c.best)) {
                    c.best = t.key();
                    c.bestCount = int(t->size());
                }
            conflicts.insert(it.key(), c);
            bestWhere.insert(it.key(), it->value(c.best).first());
            for (auto t = it->cbegin(); t != it->cend(); ++t)
                if (t.key() != c.best)
                    for (const Where &w : *t)
                        wanted[w.relPath].push_back({w.id, it.key()});
        }
        // The texts of the reference translations: one row of each group.
        QHash<quint64, QString> bestText;
        {
            QHash<QString, QVector<QPair<int, quint64>>> need;
            for (auto it = bestWhere.cbegin(); it != bestWhere.cend(); ++it)
                need[it->relPath].push_back({it->id, it.key()});
            for (auto it = need.cbegin(); it != need.cend(); ++it) {
                if (cancel && cancel->load())
                    break;
                const TranslationDoc doc = project.loadDocument(it.key());
                for (const auto &p : it.value())
                    for (const TranslationRow &row : doc.rows)
                        if (row.id == p.first)
                            bestText.insert(p.second, row.target);
            }
        }
        for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
            if (cancel && cancel->load())
                break;
            const TranslationDoc doc = project.loadDocument(it.key());
            for (const auto &p : it.value())
                for (const TranslationRow &row : doc.rows) {
                    if (row.id != p.first)
                        continue;
                    Issue issue;
                    issue.relPath = it.key();
                    issue.id = row.id;
                    issue.kind = Duplicates;
                    issue.source = row.source;
                    issue.target = row.target;
                    issue.message = QCoreApplication::translate("QualityCheck", "elsewhere (%1 times): %2")
                                        .arg(conflicts.value(p.second).bestCount)
                                        .arg(shorten(bestText.value(p.second)));
                    issues.push_back(issue);
                }
        }
    }
    if (progress)
        progress(int(files.size()), int(files.size()));
    std::stable_sort(issues.begin(), issues.end(), [](const Issue &a, const Issue &b) {
        if (a.relPath != b.relPath)
            return Project::gameFilePriority(a.relPath) != Project::gameFilePriority(b.relPath)
                       ? Project::gameFilePriority(a.relPath) < Project::gameFilePriority(b.relPath)
                       : a.relPath < b.relPath;
        return a.id != b.id ? a.id < b.id : a.kind < b.kind;
    });
    return issues;
}

}
