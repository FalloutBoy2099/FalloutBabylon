#pragma once

#include "Project.h"

#include <atomic>

// Lines noticed in the game ("this text is wrong / cut / mistranslated"), written by the player or by a script
// into a plain text file, one line per remark:
//
//     dialog/abel.msg 102 the name is not the one of the quest
//     abel.msg#102
//     text/french/game/pro_item.msg:200 too long for the inventory
//     Vous n'avez pas assez de capsules      <- a text seen on screen, found in the project
//
// Empty lines and lines starting with '#' or ';' are ignored. Every line found is marked "to review" with the
// remark (and "Game report") as a comment.
namespace GameReport {

struct Remark {
    QString relPath;       // empty for a text to look for
    int id = 0;
    QString text;          // the text to look for (when relPath is empty)
    QString note;
    QString raw;           // the line of the report
};

struct Resolved {
    QString relPath;
    int id = 0;
    QString comment;
    QString raw;
};

struct Outcome {
    QVector<Resolved> found;
    QStringList notFound;      // lines of the report that matched nothing
    QStringList ambiguous;     // texts that match too many lines to choose
};

QVector<Remark> parse(const QString &report);

// Finds the lines of the project the remarks designate. A text is looked for in the translations, then in the
// sources; a text found in more than `maxMatches` lines is reported as ambiguous.
Outcome resolve(const Project &project, const QVector<Remark> &remarks, int maxMatches = 5,
                const std::atomic_bool *cancel = nullptr);

// The comment put on a marked line.
QString commentFor(const QString &note);

}
