#pragma once

#include "CustomGlossary.h"
#include "Glossary.h"
#include "MsgFile.h"
#include "MsgRepair.h"
#include "Statistics.h"
#include "VirtualFileSystem.h"

#include <QHash>
#include <QMutex>
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
    // Optional: a .dat archive the translation is delivered in. `outputDir` is then a working folder of the
    // application and the archive is rebuilt from it after each save (see OutputArchive).
    QString outputArchive;
    QString outputArchiveFormat = QStringLiteral("dat2");   // "dat2" (Fallout 2, zlib) or "dat1" (Fallout 1)
    bool archiveWithSource = true;                          // also store the original texts, as reference files
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
    QString comment;          // translator's note on this line
    quint32 validated = 0;    // hash of the translation when it was last validated (0 = never)
    QString previous;         // translation before the last import / change of the source (for comparison)

    bool changedSinceValidation() const;
};

struct TranslationDoc {
    QString relPath;          // below text/<lang>/, e.g. "dialog/abel.msg"
    QVector<TranslationRow> rows;
    QString trailing;
    bool crlf = true;
    bool targetFileExists = false;
    QStringList warnings;           // malformed records of the source and of the translation
    QStringList targetWarnings;     // the part of `warnings` that comes from the translation file
};

enum class FileKind { Game, Dialog, Other };

// A segment marked "to review".
struct TodoItem {
    QString relPath;
    int id = 0;
    QString source;
    QString target;
};

// A text written in the code of a script (see ScriptStrings.h) and its translation.
struct ScriptTranslation {
    QString script;           // "abel" (scripts/abel.int)
    quint32 operand = 0;
    QString source;           // the original text, to recognise the string when the script changes
    QString target;
};

// A line to put in "to review" with a comment.
struct ReviewMark {
    QString relPath;
    int id = 0;
    QString comment;
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

    // Raw bytes of a message file of the source language (for the reference copies stored in an output archive).
    QByteArray readSourceText(const QString &rel, QString *error = nullptr) const;

    // Fixes the malformed records of the translation file of `rel` (see MsgRepair) and writes the corrected file
    // in the output folder (a loose file already there is kept as <name>.msg.bak). Nothing is written when the
    // file needs no repair. The returned result lists the fixes.
    MsgRepair::Result repairTargetFile(const QString &rel, QString *error = nullptr);

    TranslationDoc loadDocument(const QString &rel, QString *error = nullptr) const;
    // Returns false on error. `unencodable` receives characters the target encoding cannot hold.
    // `persistState` = false lets a bulk operation write the flags file once at the end (call saveState()).
    bool saveDocument(const TranslationDoc &doc, QString *error = nullptr, QString *unencodable = nullptr,
                      bool persistState = true);

    static SegmentStatus statusOf(const TranslationRow &row);
    // Stable 32-bit hash of a text (not randomised between runs, unlike qHash).
    static quint32 textHash(const QString &text);

    // --- glossary (built from the /game files)
    // The glossary is built on a worker thread (buildGlossary is thread safe) and installed on the owner's thread.
    const Glossary &glossary() const { return *m_glossary; }
    // Snapshot for a worker thread: stays valid even if the glossary is replaced meanwhile.
    std::shared_ptr<const Glossary> glossarySnapshot() const { return m_glossary; }
    Glossary buildGlossary(const std::atomic_bool *cancel = nullptr) const;
    void setGlossary(Glossary glossary) { m_glossary = std::make_shared<const Glossary>(std::move(glossary)); }

    // --- texts written in the scripts (saved in the state file)
    QVector<ScriptTranslation> scriptTranslations() const;
    void setScriptTranslations(const QVector<ScriptTranslation> &translations);   // also writes the state file
    // The compiled scripts of the sources, and the untouched original of one (even after a translated copy was
    // written in the output folder).
    QStringList scriptNames() const;
    QByteArray readScriptOriginal(const QString &name, QString *error = nullptr) const;
    QString pristineScriptsDir() const;              // empty without state file
    void keepPristineScript(const QString &name, const QByteArray &original);
    void registerScriptFile(const QString &name, const QString &absolutePath);

    // --- terms entered by hand (saved in the state file, added to the glossary by buildGlossary)
    QVector<CustomTerm> customTerms() const;
    void setCustomTerms(const QVector<CustomTerm> &terms);   // also writes the state file

    // --- statistics
    using ProgressFn = std::function<void(int done, int total, const FileStats &stats)>;
    QVector<FileStats> computeStatistics(const ProgressFn &progress = {}, const std::atomic_bool *cancel = nullptr) const;
    static FileStats statisticsOf(const TranslationDoc &doc);

    // Segments marked "to review" in the saved state, skipping the files in `skip` (e.g. those open in an editor).
    QVector<TodoItem> reviewItems(const QSet<QString> &skip = {}) const;

    // Identity of what a file's analysis depends on (source and translation files, languages, encodings, review
    // and confirmation marks): equal signatures mean the same results. Used by the caches.
    QString contentSignature(const QString &rel) const;

    // --- cache of the statistics of untouched files (so reopening a big project is quick)
    void setStatsCacheFile(const QString &file) { m_statsCacheFile = file; }
    int statsCacheHits() const { return m_cacheHits; }

    // Marks lines "to review" in the saved state and adds the comment (never twice), without rewriting any .msg
    // file. Returns the number of lines whose state changed. Files open in an editor must be refreshed by the caller.
    int markForReview(const QVector<ReviewMark> &marks);

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
        QHash<int, QString> comments;
        QHash<int, QString> previous;
        QHash<int, quint32> validated;
        bool isEmpty() const
        {
            return review.isEmpty() && confirmed.isEmpty() && comments.isEmpty() && previous.isEmpty() && validated.isEmpty();
        }
    };

    ProjectConfig m_config;
    VirtualFileSystem m_vfs;
    std::shared_ptr<const Glossary> m_glossary = std::make_shared<const Glossary>();
    QHash<QString, FileFlags> m_flags;
    QVector<CustomTerm> m_customTerms;
    QVector<ScriptTranslation> m_scriptTranslations;
    QString m_stateFile;
    // Readers: loadDocument/messageFiles/statistics (any thread). Writer: saveDocument/reload (owner thread).
    mutable QReadWriteLock m_lock;

    QString statsSignature(const QString &rel) const;   // caller holds the read lock
    void loadStatsCache() const;
    void saveStatsCache() const;
    QString m_statsCacheFile;
    mutable QMutex m_cacheMutex;
    mutable QHash<QString, QPair<QString, FileStats>> m_statsCache;
    mutable bool m_cacheLoaded = false;
    mutable bool m_cacheDirty = false;
    mutable int m_cacheHits = 0;
};
