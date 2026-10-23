#pragma once

#include "IntStrings.h"
#include "Project.h"

#include <atomic>
#include <functional>

// Texts written in the code of the scripts (see IntStrings.h): finding them in a project and writing the
// translated scripts into the output folder (scripts/<name>.int, which overrides the original).
namespace ScriptStrings {

struct Text {
    QString script;           // "abel" (scripts/abel.int)
    quint32 operand = 0;
    QString source;           // the original text, as read in the source encoding
    int capacity = 0;         // longest translation (in bytes of the target encoding) that fits in place
};

using Translation = ScriptTranslation;

using ProgressFn = std::function<void(int done, int total)>;

// The texts shown to the player found in every script of the sources (untouched originals).
QVector<Text> scan(const Project &project, const ProgressFn &progress = {}, const std::atomic_bool *cancel = nullptr);

struct ApplyResult {
    int scripts = 0;          // scripts written
    int inPlace = 0;
    int enlarged = 0;
    QStringList problems;
};

// Writes scripts/<name>.int in the output folder for every script that has translations (and removes the
// ones that have none any more). The originals are kept next to the project state.
ApplyResult apply(Project &project, const QVector<Translation> &translations, bool allowEnlarge);

}
