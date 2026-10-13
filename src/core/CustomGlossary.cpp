#include "CustomGlossary.h"

#include <QHash>
#include <QStringList>

namespace CustomGlossary {

namespace {

QString quote(const QString &s)
{
    if (!s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char(';')) && !s.contains(QLatin1Char('\n')))
        return s;
    QString t = s;
    t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

// Splits CSV text into rows of cells (quotes may hold separators and line breaks).
QVector<QStringList> parse(const QString &text, QChar sep)
{
    QVector<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false, any = false;
    auto endCell = [&] {
        row << cell.trimmed();
        cell.clear();
    };
    auto endRow = [&] {
        endCell();
        if (any || row.size() > 1 || !row.first().isEmpty())
            rows.push_back(row);
        row.clear();
        any = false;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += ch;
            }
        } else if (ch == QLatin1Char('"') && cell.trimmed().isEmpty()) {
            quoted = true;
            any = true;
        } else if (ch == sep) {
            endCell();
            any = true;
        } else if (ch == QLatin1Char('\n')) {
            endRow();
        } else if (ch != QLatin1Char('\r')) {
            cell += ch;
        }
    }
    if (!cell.isEmpty() || !row.isEmpty())
        endRow();
    return rows;
}

bool isTrue(const QString &s)
{
    const QString v = s.trimmed().toLower();
    return v == QLatin1String("1") || v == QLatin1String("x") || v == QLatin1String("yes") || v == QLatin1String("oui")
        || v == QLatin1String("true") || v == QLatin1String("keep") || v == QLatin1String("si") || v == QLatin1String("ja");
}

}

QString keyOf(const QString &source)
{
    return source.simplified().toLower();
}

QString toCsv(const QVector<CustomTerm> &terms)
{
    QString out = QStringLiteral("term;translation;keep\r\n");
    for (const CustomTerm &t : terms)
        out += quote(t.source) + QLatin1Char(';') + quote(t.keep ? QString() : t.target) + QLatin1Char(';')
            + (t.keep ? QStringLiteral("1") : QStringLiteral("0")) + QStringLiteral("\r\n");
    return out;
}

QVector<CustomTerm> fromCsv(const QString &text, int *skipped)
{
    if (skipped)
        *skipped = 0;
    // The separator is the most frequent of tab, semicolon and comma on the first lines.
    int tabs = 0, semis = 0, commas = 0;
    const QStringList head = text.left(4000).split(QLatin1Char('\n')).mid(0, 5);
    for (const QString &l : head) {
        tabs += l.count(QLatin1Char('\t'));
        semis += l.count(QLatin1Char(';'));
        commas += l.count(QLatin1Char(','));
    }
    QChar sep = QLatin1Char(';');
    if (tabs >= semis && tabs >= commas && tabs > 0)
        sep = QLatin1Char('\t');
    else if (commas > semis)
        sep = QLatin1Char(',');

    QVector<CustomTerm> terms;
    const QVector<QStringList> rows = parse(text, sep);
    for (int i = 0; i < rows.size(); ++i) {
        const QStringList &r = rows.at(i);
        const QString source = r.value(0).simplified();
        if (i == 0) {
            const QString h = source.toLower();
            if (h == QLatin1String("term") || h == QLatin1String("source") || h == QLatin1String("terme")
                || h == QLatin1String("original"))
                continue;
        }
        if (source.isEmpty()) {
            if (skipped)
                ++*skipped;
            continue;
        }
        CustomTerm t;
        t.source = source;
        const QString target = r.value(1).simplified();
        t.keep = target.isEmpty() || isTrue(r.value(2)) || target == source;
        t.target = t.keep ? source : target;
        terms.push_back(t);
    }
    return terms;
}

QVector<CustomTerm> merge(const QVector<CustomTerm> &existing, const QVector<CustomTerm> &imported)
{
    QVector<CustomTerm> out = existing;
    QHash<QString, int> index;
    for (int i = 0; i < out.size(); ++i)
        index.insert(keyOf(out.at(i).source), i);
    for (const CustomTerm &t : imported) {
        const QString key = keyOf(t.source);
        const int at = index.value(key, -1);
        if (at >= 0) {
            out[at] = t;
        } else {
            index.insert(key, int(out.size()));
            out.push_back(t);
        }
    }
    return out;
}

}
