#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// Texts written directly in the code of compiled scripts (display_msg("..."), float_msg(...), "You see Sammy."),
// which do not come from a .msg file. They sit in the static string table of the .int file:
//
//     [42 bytes of start-up code][procedures][identifiers, ends with 0xFFFFFFFF]
//     [size][entries: u16 length (even, NUL padded), bytes...][0xFFFFFFFF][code]
//
// The code pushes a string with the opcode 0x9001 and a 4-byte operand: the offset of the characters counted from
// the size field. A translation that fits the room of the original string is written in place; a longer one
// can be appended at the end of the file and the pushes redirected to it (optional: the game then reads a
// string outside of its table, which the engines do not forbid but which is not guaranteed).
namespace IntStrings {

struct Entry {
    quint32 operand = 0;       // what the code pushes to designate this string
    int entryPos = 0;          // position of the length field in the file
    int allocated = 0;         // bytes reserved for the characters (including the NUL padding)
    QByteArray text;           // the characters, without the NUL padding
    int capacity() const { return allocated - 1; }   // longest text that fits in place
};

struct Table {
    bool valid = false;
    QString error;
    qint64 tablePos = 0;       // position of the size field
    quint32 tableSize = 0;
    qint64 codePos = 0;        // first byte after the table terminator
    QVector<Entry> entries;
    qint64 bodyPos = 0;             // start of the procedure bodies (what lies between codePos and it is start-up code)
    QSet<quint32> usedOperands;     // operands of the 0x9001 pushes found in the procedure bodies (names excluded)
    QVector<QPair<qint64, quint32>> pushes;   // position of the operand and its value, for those pushes
    QSet<quint32> startupOperands;  // ... and in the start-up code, where they may also designate identifiers
    bool codeDecoded = false;       // the code could be walked up to the end of the file
};

Table parse(const QByteArray &script);

// Does this look like text shown to the player (and not an internal name or a debug label)?
bool looksTranslatable(const QByteArray &text);

struct PatchResult {
    QByteArray data;           // the patched script (the input when nothing could be done)
    int inPlace = 0;
    int enlarged = 0;
    QStringList problems;      // strings left untouched, with the reason
};

// Replaces strings, by operand. `texts` holds the final bytes (already in the encoding of the game).
PatchResult patch(const QByteArray &script, const QHash<quint32, QByteArray> &texts, bool allowEnlarge);

}
