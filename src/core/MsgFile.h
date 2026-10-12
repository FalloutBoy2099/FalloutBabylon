#pragma once

#include <QHash>
#include <QString>
#include <QVector>

// A Fallout .msg file: a list of {id}{audio}{text} records. Everything outside the braces
// (blank lines, '#' comments...) is ignored by the game; it is kept as "leading" trivia so an
// unmodified file serialises back to the very same text.
struct MsgEntry {
    int id = 0;
    QString audio;
    QString text;     // line breaks normalised to '\n'
    QString leading;  // text found before this record
};

struct MsgFile {
    QVector<MsgEntry> entries;
    QString trailing;
    bool crlf = true;   // line ending used when serialising
    QStringList warnings;

    // `decoded` is the file content already converted to Unicode.
    static MsgFile parse(const QString &decoded);
    QString serialize() const;

    // Index of the last entry with this id (the game keeps the last duplicate), or -1.
    QHash<int, int> indexById() const;
};
