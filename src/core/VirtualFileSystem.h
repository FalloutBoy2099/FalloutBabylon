#pragma once

#include "DatArchive.h"

#include <QMap>
#include <QStringList>
#include <memory>
#include <vector>

// A merged, case-insensitive view of several game sources (DAT archives and loose folders).
// Sources are given from lowest to highest priority; a later source overrides an earlier one,
// like the game does with master.dat < critter.dat < patch000.dat < data/.
// Only the "text/" subtree and scripts/*.int are indexed: this is a translation tool.
class VirtualFileSystem
{
public:
    struct File {
        QString path;          // normalised: lower case, '/' separators, e.g. "text/english/game/skill.msg"
        int layer = -1;
        int datIndex = -1;     // entry in the archive, or -1 for a loose file
        QString absolutePath;  // for loose files
    };

    // Resolves each path (a .dat file, or a folder that contains "text/" or "data/text/").
    // Paths that cannot be read are reported in `errors`.
    void setSources(const QStringList &sources, QStringList *errors = nullptr);

    const File *find(const QString &path) const;
    bool exists(const QString &path) const { return find(path) != nullptr; }
    QByteArray read(const QString &path, QString *error = nullptr) const;

    // Files whose path starts with `prefix` and ends with `suffix` (both normalised).
    QStringList list(const QString &prefix, const QString &suffix = QString()) const;
    // Language directory names below text/.
    QStringList languages() const;

    // Registers (or replaces) a loose file written on disk after the sources were scanned.
    void addLooseFile(const QString &path, const QString &absolutePath);

    QString describe(const QString &path) const;

    // Cheap identity of a file's content source (archive + entry, or loose file size/date): equal signatures
    // mean the content did not change. Empty when the file does not exist.
    QString signature(const QString &path) const;

    // Sources recognised in an installation folder: *.dat files in a sensible priority order, then data/.
    static QStringList discoverSources(const QString &folder);
    static QString resolveFolderRoot(const QString &folder);

private:
    struct Layer {
        QString source;
        std::shared_ptr<DatArchive> dat;  // null for a folder
        QString stamp;                    // archive file size + date, computed once
    };
    std::vector<Layer> m_layers;
    QMap<QString, File> m_files;
};
