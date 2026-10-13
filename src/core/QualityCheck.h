#pragma once

#include "Project.h"

#include <atomic>
#include <functional>

// Quality report over every translated line of a project.
namespace QualityCheck {

enum Kind {
    Tokens = 1,        // @Name@, %s, %d, {field} lost or invented by the translation
    Spacing = 2,       // leading / trailing / doubled spaces, text made of spaces only
    Encoding = 4,      // characters the target encoding cannot hold (they would become '?')
    Length = 8,        // translation much longer than the source
    Duplicates = 16,   // same source text translated differently elsewhere in the project
    Glossary = 32,     // glossary term not respected
    AllKinds = Tokens | Spacing | Encoding | Length | Duplicates | Glossary
};

struct Options {
    int kinds = AllKinds;
    double maxRatio = 2.5;         // Length: target longer than ratio x source (and over minLengthChars)
    int minLengthChars = 40;
    int maxChars = 0;              // Length: absolute limit of the translation (0 = none)
    int minDuplicateChars = 10;    // Duplicates: ignore shorter sources ("Yes", "No"...)
};

struct Issue {
    QString relPath;
    int id = 0;
    Kind kind = Tokens;
    QString message;
    QString source;
    QString target;
};

using ProgressFn = std::function<void(int done, int total)>;

// Checks of one file (without the project-wide duplicate check).
QVector<Issue> checkDocument(const TranslationDoc &doc, const ProjectConfig &config, const ::Glossary *glossary,
                             const Options &options);

// The whole project. `glossary` may be null (the glossary check is then skipped).
QVector<Issue> run(const Project &project, const ::Glossary *glossary, const Options &options,
                   const ProgressFn &progress = {}, const std::atomic_bool *cancel = nullptr);

QString kindName(Kind kind);

}
