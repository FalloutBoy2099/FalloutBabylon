#pragma once

#include <QString>
#include <QVector>

// Repairs the usual damage of hand-edited .msg files, which the game answers by silently dropping every record
// after the first broken one: a record with two fields, a missing closing or opening brace, a record that
// swallowed the next one, a record whose id is not a number (put in a comment so the file stays readable).
namespace MsgRepair {

struct Fix {
    int line = 0;          // 1-based line of the record in the original text
    QString kind;          // "two fields", "missing }", "missing {", "invalid id", "unrepaired"
    QString before;
    QString after;
};

struct Result {
    QString text;          // the repaired text (same line ending style as the input)
    QVector<Fix> fixes;
    int unrepaired = 0;    // records that could not be fixed automatically
    bool changed() const { return text != original; }
    QString original;
};

Result repair(const QString &decoded);

}
