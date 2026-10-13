#pragma once

#include "Project.h"

#include <QRegularExpression>
#include <atomic>
#include <functional>

// Search and replace over every message file of a project.
namespace ProjectSearch {

struct Options {
    QString text;
    bool inSource = false;           // look in the source texts
    bool inTarget = true;            // look in the translations
    bool regex = false;              // `text` is a regular expression (replacement may use \1, \2...)
    bool caseSensitive = false;
    bool wholeWord = false;

    QRegularExpression expression() const;   // invalid when the pattern is empty or malformed
};

struct Hit {
    QString relPath;
    int id = 0;
    bool inTarget = true;            // which text matched
    QString text;                    // the text that matched
    QString after;                   // the text after replacement (when a replacement was given)
    int occurrences = 0;
};

using ProgressFn = std::function<void(int done, int total)>;

// All matching records (at most `maxHits`; `truncated` tells when the limit was reached).
QVector<Hit> search(const Project &project, const Options &options, const QString &replacement = {},
                    const ProgressFn &progress = {}, const std::atomic_bool *cancel = nullptr, int maxHits = 5000,
                    bool *truncated = nullptr);

// Replacement inside one text; returns the number of replacements made.
int replaceIn(QString &text, const Options &options, const QString &replacement);

// Applies the replacement to the translation of the rows of `doc` listed in `ids` (all rows when empty).
// Returns the number of replacements.
int replaceInDocument(TranslationDoc &doc, const Options &options, const QString &replacement, const QSet<int> &ids = {});

}
