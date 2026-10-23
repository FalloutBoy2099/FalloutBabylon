#include "MsgRepair.h"

#include <QRegularExpression>
#include <QStringList>

namespace MsgRepair {

namespace {

struct Chunk {
    QStringList lines;
    int firstLine = 0;      // 1-based
    bool record = false;    // starts with '{'
};

QString oneLine(QString s)
{
    s.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
    return s.size() > 120 ? s.left(120) + QStringLiteral("…") : s;
}

}

Result repair(const QString &decoded)
{
    Result result;
    result.original = decoded;

    const bool crlf = decoded.contains(QStringLiteral("\r\n"));
    QString text = decoded;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QString bom;
    if (text.startsWith(QChar(0xFEFF))) {
        bom = QString(QChar(0xFEFF));
        text.remove(0, 1);
    }
    const QStringList lines = text.split(QLatin1Char('\n'));

    // --- split into chunks: one per record (a line starting with '{' and what follows until the next one)
    QVector<Chunk> chunks;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const bool starts = line.trimmed().startsWith(QLatin1Char('{'));
        if (starts) {
            Chunk c;
            c.record = true;
            c.firstLine = i + 1;
            c.lines << line;
            chunks.push_back(c);
            continue;
        }
        // A line that is not a record continues an unfinished record, otherwise it is trivia.
        if (!chunks.isEmpty() && chunks.last().record) {
            const QString joined = chunks.last().lines.join(QLatin1Char('\n'));
            if (joined.count(QLatin1Char('}')) < 3) {
                chunks.last().lines << line;
                continue;
            }
        }
        if (chunks.isEmpty() || chunks.last().record) {
            Chunk c;
            c.firstLine = i + 1;
            chunks.push_back(c);
        }
        chunks.last().lines << line;
    }

    static const QRegularExpression valid(QStringLiteral("^\\s*\\{\\s*-?\\d+\\s*\\}\\s*\\{[^{}]*\\}\\s*\\{[^{}]*\\}"),
                                          QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression twoFields(QStringLiteral("^(\\s*)\\{\\s*(-?\\d+)\\s*\\}\\s*\\{([^{}]*)\\}(\\s*)$"),
                                              QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression noClose(QStringLiteral("^(\\s*)\\{\\s*(-?\\d+)\\s*\\}\\s*\\{([^{}]*)\\}\\s*\\{([^{}]*?)(\\s*)$"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression noOpen(QStringLiteral("^(\\s*)\\{\\s*(-?\\d+)\\s*\\}\\s*\\{([^{}]*)\\}\\s*([^{}\\s][^{}]*?)\\}(\\s*)$"),
                                           QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression badId(QStringLiteral("^\\s*\\{([^{}]*)\\}"));

    QStringList out;
    for (const Chunk &c : std::as_const(chunks)) {
        if (!c.record || valid.match(c.lines.join(QLatin1Char('\n'))).hasMatch()) {
            out << c.lines.join(QLatin1Char('\n'));
            continue;
        }
        // Blank lines and comments after a broken record are not part of it.
        QStringList head = c.lines, tail;
        while (head.size() > 1 && (head.last().trimmed().isEmpty() || head.last().trimmed().startsWith(QLatin1Char('#'))))
            tail.prepend(head.takeLast());
        const QString chunkText = head.join(QLatin1Char('\n'));
        const QString tailText = tail.isEmpty() ? QString() : QLatin1Char('\n') + tail.join(QLatin1Char('\n'));
        Fix fix;
        fix.line = c.firstLine;
        fix.before = oneLine(chunkText.trimmed());

        QRegularExpressionMatch m;
        if ((m = twoFields.match(chunkText)).hasMatch()) {
            fix.kind = QStringLiteral("two fields");
            fix.after = QStringLiteral("%1{%2}{}{%3}%4").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4));
        } else if ((m = noClose.match(chunkText)).hasMatch()) {
            fix.kind = QStringLiteral("missing }");
            fix.after = QStringLiteral("%1{%2}{%3}{%4}%5").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4), m.captured(5));
        } else if ((m = noOpen.match(chunkText)).hasMatch()) {
            fix.kind = QStringLiteral("missing {");
            fix.after = QStringLiteral("%1{%2}{%3}{%4}%5").arg(m.captured(1), m.captured(2), m.captured(3), m.captured(4), m.captured(5));
        } else if ((m = badId.match(chunkText)).hasMatch()) {
            // not a number: keep the text readable, but out of the game's way
            fix.kind = QStringLiteral("invalid id");
            QStringList commented;
            for (QString l : head) {
                // braces would still be read by the parser, even inside a comment
                l.replace(QLatin1Char('{'), QLatin1Char('[')).replace(QLatin1Char('}'), QLatin1Char(']'));
                commented << QStringLiteral("# ") + l;
            }
            fix.after = commented.join(QLatin1Char('\n'));
        } else {
            fix.kind = QStringLiteral("unrepaired");
            fix.after = fix.before;
            ++result.unrepaired;
            result.fixes.push_back(fix);
            out << c.lines.join(QLatin1Char('\n'));
            continue;
        }
        result.fixes.push_back(fix);
        out << fix.after + tailText;
    }

    QString repaired = bom + out.join(QLatin1Char('\n'));
    if (crlf)
        repaired.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    result.text = result.fixes.isEmpty() ? decoded : repaired;
    return result;
}

}
