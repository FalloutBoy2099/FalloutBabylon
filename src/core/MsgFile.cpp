#include "MsgFile.h"

#include <QStringList>

MsgFile MsgFile::parse(const QString &raw)
{
    MsgFile file;

    const int crlfCount = raw.count(QStringLiteral("\r\n"));
    const int lfCount = raw.count(QLatin1Char('\n'));
    file.crlf = lfCount == 0 ? true : crlfCount * 2 >= lfCount;

    QString text = raw;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    const int n = text.size();
    int pos = 0;
    QString leading;

    auto lineOf = [&](int p) { return text.left(p).count(QLatin1Char('\n')) + 1; };

    // Reads "{...}" starting at the first '{' at or after `from`; the content ends at the next '}'.
    auto readField = [&](int from, int &next, QString &content, bool strict) {
        int open = from;
        if (strict) {
            while (open < n && text.at(open).isSpace())
                ++open;
            if (open >= n || text.at(open) != QLatin1Char('{'))
                return false;
        } else {
            open = text.indexOf(QLatin1Char('{'), from);
            if (open < 0)
                return false;
        }
        const int close = text.indexOf(QLatin1Char('}'), open + 1);
        if (close < 0)
            return false;
        content = text.mid(open + 1, close - open - 1);
        next = close + 1;
        return true;
    };

    while (pos < n) {
        const int start = text.indexOf(QLatin1Char('{'), pos);
        if (start < 0)
            break;
        QString idField, audio, body;
        int next = 0, afterId = 0, afterAudio = 0;
        if (!readField(start, afterId, idField, false) || !readField(afterId, afterAudio, audio, true)
            || !readField(afterAudio, next, body, true)) {
            file.warnings << QStringLiteral("Malformed record near line %1").arg(lineOf(start));
            break; // the game stops at the first broken record as well
        }
        bool ok = false;
        const int id = idField.trimmed().toInt(&ok);
        if (!ok) {
            file.warnings << QStringLiteral("Invalid number '%1' near line %2").arg(idField, QString::number(lineOf(start)));
            leading += text.mid(pos, next - pos);
            pos = next;
            continue;
        }
        MsgEntry entry;
        entry.id = id;
        entry.audio = audio;
        entry.text = body;
        entry.leading = leading + text.mid(pos, start - pos);
        leading.clear();
        file.entries.push_back(entry);
        pos = next;
    }
    file.trailing = leading + text.mid(pos);
    return file;
}

QString MsgFile::serialize() const
{
    QString out;
    for (const MsgEntry &e : entries) {
        out += e.leading;
        out += QLatin1Char('{') + QString::number(e.id) + QStringLiteral("}{") + e.audio + QStringLiteral("}{")
            + e.text + QLatin1Char('}');
    }
    out += trailing;
    if (crlf)
        out.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    return out;
}

QHash<int, int> MsgFile::indexById() const
{
    QHash<int, int> map;
    for (int i = 0; i < entries.size(); ++i)
        map.insert(entries.at(i).id, i);
    return map;
}
