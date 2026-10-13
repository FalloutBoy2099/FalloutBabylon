#include "Placeholders.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace Placeholders {

namespace {

QString escapeXml(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return out;
}

QString unescapeXml(const QString &s)
{
    QString out = s;
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return out;
}

}

namespace {

struct Span {
    int start = 0, end = 0;
    QString replacement;
};

bool atSentenceStart(const QString &text, int start)
{
    int i = start - 1;
    while (i >= 0 && text.at(i).isSpace() && text.at(i) != QLatin1Char('\n'))
        --i;
    if (i < 0)
        return true;
    const QChar c = text.at(i);
    return c == QLatin1Char('\n') || c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')
        || c == QLatin1Char(':') || c == QLatin1Char('"') || c == QLatin1Char('(') || c == QLatin1Char('\'')
        || c == QLatin1Char('-') || c == QChar(0x2026);
}

QVector<Span> glossarySpans(const QString &text, const Glossary &glossary)
{
    QVector<Span> spans;
    for (const Glossary::Hit &h : glossary.findInSource(text)) {
        const GlossaryTerm &term = *h.term;
        if (h.match.fuzzy || !h.match.caseMatch || term.targets.size() != 1 || term.targets.first().isEmpty())
            continue;
        const QString found = text.mid(h.match.start, h.match.length);
        if (found.compare(term.source, Qt::CaseInsensitive) != 0)
            continue;      // plural / possessive forms are left to the service
        const bool multiWord = term.source.contains(QLatin1Char(' ')) || term.source.contains(QLatin1Char('-'));
        const bool capitalised = term.source.at(0).isUpper();
        if (!term.custom && !multiWord && !(capitalised && !atSentenceStart(text, h.match.start)))
            continue;      // a common word: only a safe match is worth forcing
        spans.push_back({h.match.start, h.match.start + h.match.length, term.targets.first()});
    }
    return spans;
}

}

Protected protect(const QString &text, const Glossary *glossary)
{
    // @Name@ style script variables, printf conversions, and {braced} fields.
    static const QRegularExpression token(QStringLiteral("@[^@\\s][^@\\n]{0,40}@|%[-+0# ]?\\d*[sdiuxXc]|\\{[^{}\\n]{1,40}\\}"));
    QVector<Span> spans;
    auto it = token.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        spans.push_back({int(m.capturedStart()), int(m.capturedEnd()), m.captured()});
    }
    if (glossary && !glossary->isEmpty()) {
        QVector<Span> terms = glossarySpans(text, *glossary);
        std::stable_sort(terms.begin(), terms.end(), [](const Span &a, const Span &b) {
            return (a.end - a.start) > (b.end - b.start);      // longest first
        });
        for (const Span &t : std::as_const(terms)) {
            bool free = true;
            for (const Span &s : std::as_const(spans))
                free = free && (t.end <= s.start || t.start >= s.end);
            if (free)
                spans.push_back(t);
        }
        std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    }

    Protected p;
    int last = 0;
    for (const Span &s : std::as_const(spans)) {
        p.xml += escapeXml(text.mid(last, s.start - last));
        p.xml += QStringLiteral("<x>%1</x>").arg(p.tokens.size());
        p.tokens << s.replacement;
        last = s.end;
    }
    p.xml += escapeXml(text.mid(last));
    return p;
}

Protected protect(const QString &text)
{
    return protect(text, nullptr);
}

bool restore(const QString &xml, const QStringList &tokens, QString *result)
{
    static const QRegularExpression marker(QStringLiteral("<x>\\s*(\\d+)\\s*</x>"));
    QString out;
    QSet<int> seen;
    bool ok = true;
    int last = 0;
    auto it = marker.globalMatch(xml);
    while (it.hasNext()) {
        const auto m = it.next();
        out += unescapeXml(xml.mid(last, m.capturedStart() - last));
        const int n = m.captured(1).toInt();
        if (n >= 0 && n < tokens.size()) {
            out += tokens.at(n);
            seen.insert(n);
        } else {
            ok = false;
        }
        last = m.capturedEnd();
    }
    out += unescapeXml(xml.mid(last));
    if (seen.size() != tokens.size())
        ok = false;
    // Any other tag left by the service is not part of the text.
    static const QRegularExpression stray(QStringLiteral("</?x\\s*/?>"));
    if (stray.match(out).hasMatch()) {
        out.remove(stray);
        ok = false;
    }
    if (result)
        *result = out;
    return ok;
}

}
