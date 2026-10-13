#include "QualityCheck.h"

#include "Placeholders.h"
#include "TextCodec.h"

#include <QCoreApplication>
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

QVector<Issue> run(const Project &project, const ::Glossary *glossary, const Options &options, const ProgressFn &progress,
                   const std::atomic_bool *cancel)
{
    QVector<Issue> issues;
    struct Where {
        QString relPath;
        int id;
    };
    // source -> translation -> places (only for the duplicate check)
    QHash<QString, QHash<QString, QVector<Where>>> seen;
    const bool duplicates = options.kinds & Duplicates;

    const QStringList files = project.messageFiles();
    for (int i = 0; i < files.size(); ++i) {
        if (cancel && cancel->load())
            break;
        if (progress && i % 10 == 0)
            progress(i, int(files.size()));
        const TranslationDoc doc = project.loadDocument(files.at(i));
        issues += checkDocument(doc, project.config(), glossary, options);
        if (!duplicates)
            continue;
        for (const TranslationRow &row : doc.rows) {
            if (!isChecked(row))
                continue;
            const QString key = row.source.simplified();
            if (key.size() < options.minDuplicateChars)
                continue;
            seen[key][row.target.simplified()].push_back({doc.relPath, row.id});
        }
    }

    if (duplicates && !(cancel && cancel->load())) {
        for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
            if (it->size() < 2)
                continue;
            // The most used translation is the reference; the others are reported.
            QString best;
            int bestCount = -1;
            for (auto t = it->cbegin(); t != it->cend(); ++t)
                if (t->size() > bestCount || (t->size() == bestCount && t.key() < best)) {
                    best = t.key();
                    bestCount = int(t->size());
                }
            for (auto t = it->cbegin(); t != it->cend(); ++t) {
                if (t.key() == best)
                    continue;
                for (const Where &w : *t) {
                    Issue issue;
                    issue.relPath = w.relPath;
                    issue.id = w.id;
                    issue.kind = Duplicates;
                    issue.source = it.key();
                    issue.target = t.key();
                    issue.message = QCoreApplication::translate("QualityCheck", "elsewhere (%1 times): %2")
                                        .arg(bestCount)
                                        .arg(shorten(best));
                    issues.push_back(issue);
                }
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
