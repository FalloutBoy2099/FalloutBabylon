#include "ProjectSearch.h"

namespace ProjectSearch {

QRegularExpression Options::expression() const
{
    if (text.isEmpty())
        return {};
    QString pattern = regex ? text : QRegularExpression::escape(text);
    if (wholeWord)
        pattern = QStringLiteral("(?<![\\p{L}\\p{N}_])(?:%1)(?![\\p{L}\\p{N}_])").arg(pattern);
    QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive)
        opts |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, opts);
}

int replaceIn(QString &text, const Options &options, const QString &replacement)
{
    const QRegularExpression re = options.expression();
    if (!re.isValid())
        return 0;
    int count = 0;
    QString result;
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result += QStringView(text).mid(last, m.capturedStart() - last);
        if (options.regex) {
            // \1..\9 insert a captured group; a plain search never interprets the replacement
            for (qsizetype i = 0; i < replacement.size(); ++i) {
                const QChar ch = replacement.at(i);
                if (ch == QLatin1Char('\\') && i + 1 < replacement.size()) {
                    const QChar next = replacement.at(i + 1);
                    if (next.isDigit()) {
                        result += m.captured(next.digitValue());
                        ++i;
                        continue;
                    }
                    if (next == QLatin1Char('\\')) {
                        result += QLatin1Char('\\');
                        ++i;
                        continue;
                    }
                }
                result += ch;
            }
        } else {
            result += replacement;
        }
        last = m.capturedEnd();
        ++count;
    }
    if (!count)
        return 0;
    result += QStringView(text).mid(last);
    text = result;
    return count;
}

QVector<Hit> search(const Project &project, const Options &options, const QString &replacement, const ProgressFn &progress,
                    const std::atomic_bool *cancel, int maxHits, bool *truncated)
{
    QVector<Hit> hits;
    if (truncated)
        *truncated = false;
    const QRegularExpression re = options.expression();
    if (!re.isValid())
        return hits;

    const QStringList files = project.messageFiles();
    for (int i = 0; i < files.size(); ++i) {
        if (cancel && cancel->load())
            break;
        if (progress && i % 20 == 0)
            progress(i, int(files.size()));
        const TranslationDoc doc = project.loadDocument(files.at(i));
        for (const TranslationRow &row : doc.rows) {
            for (int pass = 0; pass < 2; ++pass) {
                const bool target = pass == 1;
                if (target ? !options.inTarget : !options.inSource)
                    continue;
                const QString &text = target ? row.target : row.source;
                if (text.isEmpty() || !re.match(text).hasMatch())
                    continue;
                Hit hit;
                hit.relPath = doc.relPath;
                hit.id = row.id;
                hit.inTarget = target;
                hit.text = text;
                if (!replacement.isNull() && target) {
                    hit.after = text;
                    hit.occurrences = replaceIn(hit.after, options, replacement);
                } else {
                    auto it = re.globalMatch(text);
                    while (it.hasNext()) {
                        it.next();
                        ++hit.occurrences;
                    }
                }
                hits.push_back(hit);
                if (hits.size() >= maxHits) {
                    if (truncated)
                        *truncated = true;
                    return hits;
                }
            }
        }
    }
    if (progress)
        progress(int(files.size()), int(files.size()));
    return hits;
}

int replaceInDocument(TranslationDoc &doc, const Options &options, const QString &replacement, const QSet<int> &ids)
{
    int total = 0;
    for (TranslationRow &row : doc.rows) {
        if (!ids.isEmpty() && !ids.contains(row.id))
            continue;
        QString text = row.target;
        const int n = replaceIn(text, options, replacement);
        if (n > 0 && text != row.target) {
            row.target = text;
            row.confirmed = !text.isEmpty() && text == row.source;
            total += n;
        }
    }
    return total;
}

}
