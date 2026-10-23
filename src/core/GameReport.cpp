#include "GameReport.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace GameReport {

QString commentFor(const QString &note)
{
    const QString prefix = QCoreApplication::translate("GameReport", "Game report");
    return note.trimmed().isEmpty() ? prefix : prefix + QStringLiteral(": ") + note.trimmed();
}

QVector<Remark> parse(const QString &report)
{
    // [text/<language>/]<path>.msg, then a separator (space, #, :, ',' or ';'), the id, then a free note
    static const QRegularExpression located(
        QStringLiteral("^(?:text[/\\\\][^/\\\\]+[/\\\\])?([\\w .\\\\/-]+?\\.msg)\\s*[#:,;\\s]\\s*(\\d+)\\b\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);

    QVector<Remark> remarks;
    QString text = report;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        Remark r;
        r.raw = line;
        const QRegularExpressionMatch m = located.match(line);
        if (m.hasMatch()) {
            r.relPath = m.captured(1).replace(QLatin1Char('\\'), QLatin1Char('/')).trimmed().toLower();
            r.id = m.captured(2).toInt();
            r.note = m.captured(3).trimmed();
        } else {
            r.text = line;
        }
        remarks.push_back(r);
    }
    return remarks;
}

Outcome resolve(const Project &project, const QVector<Remark> &remarks, int maxMatches, const std::atomic_bool *cancel)
{
    Outcome out;
    const QStringList files = project.messageFiles();

    // path of a remark: complete ("dialog/abel.msg"), or only a name ("abel.msg")
    auto fileFor = [&](const QString &path) -> QString {
        for (const QString &f : files)
            if (f.compare(path, Qt::CaseInsensitive) == 0)
                return f;
        const QString name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
        QString found;
        for (const QString &f : files)
            if (f.mid(f.lastIndexOf(QLatin1Char('/')) + 1).compare(name, Qt::CaseInsensitive) == 0) {
                if (!found.isEmpty())
                    return {};          // the name is ambiguous
                found = f;
            }
        return found;
    };

    struct Wanted {
        int remark;
        QString key;                    // simplified, lower case
    };
    QVector<Wanted> wanted;
    QHash<int, QVector<Resolved>> inTranslation, inSource;     // matches of the texts, by remark index

    for (int i = 0; i < remarks.size(); ++i) {
        const Remark &r = remarks.at(i);
        if (!r.relPath.isEmpty()) {
            const QString file = fileFor(r.relPath);
            bool exists = false;
            if (!file.isEmpty()) {
                const TranslationDoc doc = project.loadDocument(file);
                for (const TranslationRow &row : doc.rows)
                    exists = exists || row.id == r.id;
                if (exists)
                    out.found.push_back({file, r.id, commentFor(r.note), r.raw});
            }
            if (!exists)
                out.notFound << r.raw;
        } else {
            wanted.push_back({i, r.text.simplified().toLower()});
        }
    }

    if (!wanted.isEmpty()) {
        for (const QString &file : files) {
            if (cancel && cancel->load())
                break;
            const TranslationDoc doc = project.loadDocument(file);
            for (const TranslationRow &row : doc.rows) {
                const QString target = row.target.simplified().toLower();
                const QString source = row.source.simplified().toLower();
                for (const Wanted &w : std::as_const(wanted)) {
                    if (w.key.isEmpty())
                        continue;
                    const bool hitTarget = !target.isEmpty() && target.contains(w.key);
                    const bool hitSource = !hitTarget && source.contains(w.key);
                    if (!hitTarget && !hitSource)
                        continue;
                    const Resolved found{file, row.id, commentFor(remarks.at(w.remark).note), remarks.at(w.remark).raw};
                    (hitTarget ? inTranslation : inSource)[w.remark].push_back(found);
                }
            }
        }
        for (const Wanted &w : std::as_const(wanted)) {
            // what is seen on screen is the translation; the source is only a fallback
            const QVector<Resolved> matches = inTranslation.contains(w.remark) ? inTranslation.value(w.remark)
                                                                                : inSource.value(w.remark);
            if (matches.isEmpty()) {
                out.notFound << remarks.at(w.remark).raw;
            } else if (matches.size() > maxMatches) {
                out.ambiguous << remarks.at(w.remark).raw;
            } else {
                out.found += matches;
            }
        }
    }
    return out;
}

}
