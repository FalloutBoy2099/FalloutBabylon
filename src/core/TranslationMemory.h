#pragma once

#include "Project.h"

#include <atomic>
#include <functional>

// Reuse of an existing translation (for instance the French of an older version of the mod) in a project.
// The "memory" is another set of sources holding text/<language>/... files: it is only read.
namespace TranslationMemory {

struct Options {
    bool matchByText = true;         // no record with this id: same source text elsewhere in the memory
    bool changedAsReview = true;     // same id but the source text changed: take the old translation, mark it "to review"
    bool overwrite = false;          // replace translations the project already has (default: untranslated lines only)
};

struct Report {
    int files = 0;                   // files examined
    int examined = 0;                // records examined
    int exact = 0;                   // same id, same source text
    int changed = 0;                 // same id, source text changed (marked to review)
    int byText = 0;                  // same source text found under another id
    int kept = 0;                    // already translated, left alone
    int saved = 0;                   // files written
    QStringList errors;
};

struct Memory {
    QStringList sources;             // folders / .dat archives, lowest priority first
    QString sourceLanguage = QStringLiteral("english");   // language folder holding the memory's source texts
    QString targetLanguage = QStringLiteral("french");    // ... and its translations
    QString sourceEncoding = QStringLiteral("cp1252");
    QString targetEncoding = QStringLiteral("cp1252");
};

using ProgressFn = std::function<void(int done, int total)>;

// Applies the memory to every message file of the project and writes the files that changed.
// Open editors on this project must be closed first (the files are rewritten on disk).
Report import(Project &project, const Memory &memory, const Options &options, const ProgressFn &progress = {},
              const std::atomic_bool *cancel = nullptr);

}
