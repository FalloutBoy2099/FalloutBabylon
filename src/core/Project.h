#pragma once

#include "Glossary.h"
#include "MsgFile.h"
#include "Statistics.h"
#include "VirtualFileSystem.h"

#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

enum class SegmentStatus { Untranslated, Translated, ToReview };

struct ProjectConfig {
    QString id;
    QString name;
    QStringList sources;          // .dat files and/or folders, lowest priority first
    QString outputDir;            // where translated files are written (loose files)
    QString sourceLanguage = QStringLiteral("english");
    QString targetLanguage = QStringLiteral("french");
    QString sourceEncoding = QStringLiteral("cp1252");
    QString targetEncoding = QStringLiteral("cp1252");
};

// One line of a translation: the source record and the translator's text.
struct TranslationRow {
    int id = 0;
    QString audio;
    QString source;
    QString target;
    QString leading;          // file trivia kept for the round trip
    bool inSource = true;     // false: record only present in the target file
    bool confirmed = false;   // translation accepted even though it equals the source text
    bool review = false;      // marked "to review"
};

struct TranslationDoc {
    QString relPath;          // below text/<lang>/, e.g. "dialog/abel.msg"
    QVector<TranslationRow> rows;
    QString trailing;
    bool crlf = true;
    bool targetFileExists = false;
    QStringList warnings;
};

enum class FileKind { Game, Dialog, Other };

// A segment marked "to review".
struct TodoItem {
    QString relPath;
    int id = 0;
    QString source;
    QString target;
};

// A translation project: sources, languages, glossary and the per-segment state.
class Project
{
public:
    explicit Project(const ProjectConfig &config);

    const ProjectConfig &config() const { return m_config; }
    ProjectConfig &config() { return m_config; }
    // Re-reads the sources (call after the configuration changed).
    void reload(QStringList *errors = nullptr);

    const VirtualFileSystem &vfs() const { return m_vfs; }
    QStringList availableLanguages() const { return m_vfs.languages(); }

    // Message files of the project (source + target), relative to text/<lang>/, in the recommended
    // translation order: /game first, then /dialog, then the rest.
    QStringList messageFiles() const;
    static FileKind kindOf(const QString &relPath);
    static int gameFilePriority(const QString &relPath);

    QString sourcePath(const QString &rel) const;
    QString targetPath(const QString &rel) const;   // normalised path inside text/
    QString targetDiskPath(const QString &rel) const;

    // Compiled script (scripts/<stem>.int) that drives a dialogue file; empty if there is none.
    // dialog/abel.msg -> scripts/abel.int
    QByteArray readDialogScript(const QString &relPath, QString *error = nullptr) const;

    // Interface font (font1.aaf is the Pip-Boy's): text/<target language>/, text/<source language>/, then the root.
    QByteArray readInterfaceFont(int index) const;

    TranslationDoc loadDocument(const QString &rel, QString *error = nullptr) const;
    // Returns false on error. `unencodable` receives characters the target encoding cannot hold.
    bool saveDocument(const TranslationDoc &doc, QString *error = nullptr, QString *unencodable = nullptr);

    static SegmentStatus statusOf(const TranslationRow &row);

    // --- glossary (built from the /game files)
    // The glossary is built on a worker thread (buildGlossary is thread safe) and installed on the owner's thread.
    const Glossary &glossary() const { return m_glossary; }
    Glossary buildGlossary(const std::atomic_bool *cancel = nullptr) const;
    void setGlossary(Glossary glossary) { m_glossary = std::move(glossary); }

    // --- statistics
    using ProgressFn = std::function<void(int done, int total, const FileStats &stats)>;
    QVector<FileStats> computeStatistics(const ProgressFn &progress = {}, const std::atomic_bool *cancel = nullptr) const;
    static FileStats statisticsOf(const TranslationDoc &doc);

    // Segments marked "to review" in the saved state, skipping the files in `skip` (e.g. those open in an editor).
    QVector<TodoItem> reviewItems(const QSet<QString> &skip = {}) const;

    // --- persisted per-segment flags
    void setStateFile(const QString &file) { m_stateFile = file; }
    void loadState();
    void saveState() const;

private:
    void loadStateLocked();
    void saveStateLocked() const;

    struct FileFlags {
        QSet<int> review;
        QSet<int> confirmed;
    };

    ProjectConfig m_config;
    VirtualFileSystem m_vfs;
    Glossary m_glossary;
    QHash<QString, FileFlags> m_flags;
    QString m_stateFile;
    // Readers: loadDocument/messageFiles/statistics (any thread). Writer: saveDocument/reload (owner thread).
    mutable QReadWriteLock m_lock;
};
