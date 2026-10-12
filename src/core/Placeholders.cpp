#include "Placeholders.h"

#include <QRegularExpression>
#include <QSet>

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

Protected protect(const QString &text)
{
    // @Name@ style script variables, printf conversions, and {braced} fields.
    static const QRegularExpression token(QStringLiteral("@[^@\\s][^@\\n]{0,40}@|%[-+0# ]?\\d*[sdiuxXc]|\\{[^{}\\n]{1,40}\\}"));
    Protected p;
    int last = 0;
    auto it = token.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        p.xml += escapeXml(text.mid(last, m.capturedStart() - last));
        p.xml += QStringLiteral("<x>%1</x>").arg(p.tokens.size());
        p.tokens << m.captured();
        last = m.capturedEnd();
    }
    p.xml += escapeXml(text.mid(last));
    return p;
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
