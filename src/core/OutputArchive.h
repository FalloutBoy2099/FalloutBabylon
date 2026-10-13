#pragma once

#include "DatArchive.h"
#include "Project.h"

// A project can deliver its translation as a .dat archive instead of a folder of loose files. The saved files stay
// in a working folder (ProjectConfig::outputDir, kept by the application) and the archive is rebuilt from it.
// The archive holds only text/<target language>/..., plus (option, on by default) the original texts of the same
// files under text/<source language>/... so that the reference files are known.
namespace OutputArchive {

inline bool enabled(const ProjectConfig &config) { return !config.outputArchive.trimmed().isEmpty(); }

DatArchive::Format formatOf(const ProjectConfig &config);
QString formatId(DatArchive::Format format);                    // "dat1" / "dat2"

// Format of the first .dat file among `sources` (Fallout 2 when there is none, or only folders).
DatArchive::Format detectFormat(const QStringList &sources);

// An archive already exists but the working folder does not hold its files yet (new computer, switch to an
// archive output): copies the text/<target language>/ files of the archive into the working folder. Existing
// files are never overwritten. Returns the number of files copied.
int seedWorkingFolder(const ProjectConfig &config, QString *error = nullptr);

struct Result {
    bool ok = false;
    int translated = 0;      // files text/<target>/...
    int originals = 0;       // files text/<source>/...
    QString error;
};

// Rewrites the archive from the working folder (atomically; the previous archive stays if anything fails).
// Safe to call from a worker thread.
Result build(const Project &project);

// Why the archive cannot be written there ("" when it can): it must not be one of the sources.
QString problemWith(const ProjectConfig &config);

}
