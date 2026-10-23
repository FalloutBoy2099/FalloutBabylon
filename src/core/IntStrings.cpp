#include "IntStrings.h"

#include <QCoreApplication>
#include <QtEndian>

namespace IntStrings {

namespace {

constexpr int kHeaderSize = 42;

quint32 u32(const QByteArray &d, qint64 pos)
{
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(d.constData()) + pos);
}

quint16 u16(const QByteArray &d, qint64 pos)
{
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(d.constData()) + pos);
}

void put32(QByteArray &d, qint64 pos, quint32 v)
{
    qToBigEndian<quint32>(v, reinterpret_cast<uchar *>(d.data()) + pos);
}

void put16(QByteArray &d, qint64 pos, quint16 v)
{
    qToBigEndian<quint16>(v, reinterpret_cast<uchar *>(d.data()) + pos);
}

constexpr quint16 kPushString = 0x9001, kPushFloat = 0xA001, kPushInt = 0xC001;
constexpr quint16 kFetchExternal = 0x8014, kStoreExternal = 0x8015, kExportVar = 0x8016;

}

Table parse(const QByteArray &s)
{
    Table t;
    const qint64 n = s.size();
    auto fail = [&](const QString &why) {
        t.error = why;
        return t;
    };
    if (n < kHeaderSize + 8)
        return fail(QStringLiteral("too short"));

    qint64 pos = kHeaderSize;
    const quint32 procedures = u32(s, pos);
    if (qint64(procedures) * 24 + pos + 4 > n)
        return fail(QStringLiteral("procedure table out of range"));
    t.bodyPos = n;
    for (quint32 i = 0; i < procedures; ++i) {           // procedure table: 24 bytes per procedure, body offset in the 5th field
        const quint32 body = u32(s, pos + 4 + qint64(i) * 24 + 16);
        if (body)                                        // 0: a procedure declared but not implemented here
            t.bodyPos = qMin<qint64>(t.bodyPos, body);
    }
    pos += 4 + qint64(procedures) * 24;
    if (pos + 4 > n)
        return fail(QStringLiteral("procedure table out of range"));
    const quint32 identifiers = u32(s, pos);
    pos += 4 + identifiers;
    if (pos + 4 > n || u32(s, pos) != 0xFFFFFFFFu)
        return fail(QStringLiteral("identifier table not terminated"));
    pos += 4;

    t.tablePos = pos;
    t.tableSize = u32(s, pos);
    if (t.tableSize == 0xFFFFFFFFu) {                    // no static string at all
        t.valid = true;
        t.tableSize = 0;
        t.codePos = pos + 4;
        t.tablePos = -1;
    } else {
        qint64 cur = pos + 4;
        const qint64 end = cur + t.tableSize;
        if (end + 4 > n || u32(s, end) != 0xFFFFFFFFu)
            return fail(QStringLiteral("string table not terminated"));
        while (cur < end) {
            if (cur + 2 > end)
                return fail(QStringLiteral("string entry out of range"));
            const int len = u16(s, cur);
            if (len < 2 || (len & 1) || cur + 2 + len > end)
                return fail(QStringLiteral("invalid string length"));
            Entry e;
            e.entryPos = int(cur);
            e.allocated = len;
            e.operand = quint32(cur + 2 - pos);          // counted from the size field
            QByteArray chars = s.mid(cur + 2, len);
            const int nul = chars.indexOf('\0');
            e.text = nul < 0 ? chars : chars.left(nul);
            t.entries.push_back(e);
            cur += 2 + len;
        }
        t.codePos = end + 4;
        t.valid = true;
    }

    // Walk the code: every opcode is 2 bytes, the three pushes carry a 4-byte operand.
    qint64 cur = t.codePos;
    bool clean = true;
    bool previousIsBodyString = false;       // the previous instruction was a string push of a procedure body
    while (cur < n) {
        if (cur + 2 > n) {
            clean = false;
            break;
        }
        const qint64 opPos = cur;
        const quint16 op = u16(s, cur);
        cur += 2;
        if (op == kPushString || op == kPushFloat || op == kPushInt) {
            if (cur + 4 > n) {
                clean = false;
                break;
            }
            previousIsBodyString = false;
            if (op == kPushString) {
                if (opPos < t.bodyPos) {
                    t.startupOperands.insert(u32(s, cur));
                } else {
                    t.pushes.push_back({cur, u32(s, cur)});
                    previousIsBodyString = true;
                }
            }
            cur += 4;
        } else {
            // The name of an external variable is pushed like a string but designates the identifier table.
            if (previousIsBodyString && (op == kFetchExternal || op == kStoreExternal || op == kExportVar))
                t.pushes.removeLast();
            previousIsBodyString = false;
        }
    }
    for (const auto &p : std::as_const(t.pushes))
        t.usedOperands.insert(p.second);
    t.codeDecoded = clean;
    return t;
}

bool looksTranslatable(const QByteArray &text)
{
    if (text.size() < 6)
        return false;
    int letters = 0, spaces = 0, other = 0;
    for (uchar c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0)
            ++letters;
        else if (c == ' ')
            ++spaces;
        else if (c < 0x20 || c == '/' || c == '\\' || c == '_' || c == '=' || c == '{' || c == '}' || c == '@')
            ++other;
    }
    if (spaces == 0 || letters < 4 || other > 0)
        return false;
    if (letters * 10 < text.size() * 6)          // mostly letters
        return false;
    return true;
}

PatchResult patch(const QByteArray &script, const QHash<quint32, QByteArray> &texts, bool allowEnlarge)
{
    PatchResult result;
    result.data = script;
    const Table table = parse(script);
    if (!table.valid) {
        result.problems << QCoreApplication::translate("IntStrings", "not a readable script (%1)").arg(table.error);
        return result;
    }

    QByteArray data = script;
    QHash<quint32, quint32> redirect;          // old operand -> new operand (strings moved to the end)
    for (const Entry &e : table.entries) {
        const auto it = texts.constFind(e.operand);
        if (it == texts.constEnd() || it.value() == e.text)
            continue;
        const QByteArray &text = it.value();
        if (text.contains('\0')) {
            result.problems << QCoreApplication::translate("IntStrings", "a text contains a NUL character");
            continue;
        }
        if (text.size() <= e.capacity()) {
            QByteArray chars = text;
            chars.append(QByteArray(e.allocated - text.size(), '\0'));
            data.replace(e.entryPos + 2, e.allocated, chars);
            ++result.inPlace;
            continue;
        }
        if (!allowEnlarge) {
            result.problems << QCoreApplication::translate("IntStrings", "\"%1\": %2 characters for %3 available")
                                   .arg(QString::fromLatin1(e.text.left(30)))
                                   .arg(text.size()).arg(e.capacity());
            continue;
        }
        if (table.startupOperands.contains(e.operand)) {
            result.problems << QCoreApplication::translate("IntStrings", "\"%1\": used by the start-up code of the script, the text cannot be lengthened")
                                   .arg(QString::fromLatin1(e.text.left(30)));
            continue;
        }
        if (!table.codeDecoded) {
            result.problems << QCoreApplication::translate("IntStrings", "\"%1\": the code cannot be read safely, the text cannot be lengthened")
                                   .arg(QString::fromLatin1(e.text.left(30)));
            continue;
        }
        // a new entry at the end of the file; its length is even, with at least one NUL
        const int allocated = (text.size() + 2) & ~1;
        const qint64 entryPos = data.size();
        QByteArray entry(2, '\0');
        entry.append(text);
        entry.append(QByteArray(allocated - text.size(), '\0'));
        put16(entry, 0, quint16(allocated));
        data.append(entry);
        const quint32 operand = quint32(entryPos + 2 - table.tablePos);
        redirect.insert(e.operand, operand);
        ++result.enlarged;
    }

    if (!redirect.isEmpty()) {
        // the pushes of the moved strings now designate the new place
        for (const auto &push : std::as_const(table.pushes)) {
            const auto it = redirect.constFind(push.second);
            if (it != redirect.constEnd())
                put32(data, push.first, it.value());
        }
    }
    result.data = data;
    return result;
}

}
