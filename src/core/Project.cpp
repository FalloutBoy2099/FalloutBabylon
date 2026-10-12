#include "Project.h"

#include "TextCodec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QReadLocker>
#include <QSaveFile>
#include <QWriteLocker>
#include <algorithm>

namespace {

// Message files of /game, in the order in which translating them is most useful for the glossary.
const QStringList kGameOrder = {
    QStringLiteral("pro_crit"), QStringLiteral("pro_item"), QStringLiteral("pro_scen"), QStringLiteral("pro_wall"),
    QStringLiteral("pro_misc"), QStringLiteral("skill"),    QStringLiteral("trait"),    QStringLiteral("perk"),
    QStringLiteral("stat"),     QStringLiteral("worldmap"), QStringLiteral("map"),      QStringLiteral("quests"),
};

QString stemOf(const QString &rel)
{
    QString name = rel.mid(rel.lastIndexOf(QLatin1Char('/')) + 1);
    if (name.endsWith(QLatin1String(".msg")))
        name.chop(4);
    return name.toLower();
}

QString categoryFor(const QString &stem)
{
    if (stem == QLatin1String("pro_crit")) return QStringLiteral("Creature");
    if (stem == QLatin1String("pro_item")) return QStringLiteral("Item");
    if (stem == QLatin1String("pro_scen")) return QStringLiteral("Scenery");
    if (stem == QLatin1String("pro_wall")) return QStringLiteral("Wall");
    if (stem == QLatin1String("pro_misc")) return QStringLiteral("Misc");
    if (stem == QLatin1String("skill")) return QStringLiteral("Skill");
    if (stem == QLatin1String("trait")) return QStringLiteral("Trait");
    if (stem == QLatin1String("perk")) return QStringLiteral("Perk");
    if (stem == QLatin1String("stat")) return QStringLiteral("Stat");
    if (stem == QLatin1String("worldmap") || stem == QLatin1String("map")) return QStringLiteral("Location");
    if (stem == QLatin1String("quests")) return QStringLiteral("Quest");
    return stem;
}

bool looksLikeName(const QString &text)
{
    const QString t = text.trimmed();
    if (t.size() < 3 || t.size() > 40 || t.contains(QLatin1Char('\n')))
        return false;
    if (!t.at(0).isUpper())
        return false;
    if (Statistics::countWords(t) > 5)
        return false;
    const QChar last = t.back();
    if (last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?') || last == QLatin1Char(':'))
        return false;
    for (QChar c : t)
        if (c.isDigit() || c == QLatin1Char('%') || c == QLatin1Char('@') || c == QLatin1Char('#'))
            return false;
    return true;
}

// Is this record the *name* of something (as opposed to a description or a sentence)?
bool isNameRecord(const QString &stem, int id, const QString &text)
{
    if (!Statistics::isTranslatable(text))
        return false;
    if (stem.startsWith(QLatin1String("pro_"))) {
        // Fallout prototypes: id = 100 * n for the name, +1 for the description.
        if (id < 100 || id % 100 != 0 || text.trimmed().size() > 60 || text.contains(QLatin1Char('\n')))
            return false;
        // Internal placeholders ("test (don't use)", "spare", "encinodude") are not worth a glossary entry.
        const QString t = text.trimmed();
        if (t.contains(QLatin1String("don't use"), Qt::CaseInsensitive) || t.contains(QLatin1String("unused"), Qt::CaseInsensitive))
            return false;
        const QChar first = t.at(0);
        return first.isUpper() || first.isDigit() || first == QLatin1Char('.') || first == QLatin1Char('"');
    }
    if (stem == QLatin1String("skill") || stem == QLatin1String("trait"))
        return id >= 100 && id < 200 && looksLikeName(text);
    if (stem == QLatin1String("perk") || stem == QLatin1String("stat"))
        return id < 1000 && looksLikeName(text);
    if (stem == QLatin1String("worldmap") || stem == QLatin1String("map") || stem == QLatin1String("quests"))
        return looksLikeName(text);
    return false;
}

QString prefixFor(const QString &language)
{
    return QStringLiteral("text/") + language.toLower() + QLatin1Char('/');
}

}

Project::Project(const ProjectConfig &config)
    : m_config(config)
{
}

void Project::reload(QStringList *errors)
{
    m_config.sourceLanguage = m_config.sourceLanguage.toLower();
    m_config.targetLanguage = m_config.targetLanguage.toLower();
    QStringList sources = m_config.sources;
    if (!m_config.outputDir.isEmpty()) {
        const QString out = QDir::cleanPath(m_config.outputDir);
        bool known = false;
        for (const QString &s : std::as_const(sources))
            known = known || QDir::cleanPath(s) == out;
        if (!known && QFileInfo(out).isDir())
            sources << out;
    }
    QWriteLocker locker(&m_lock);
    m_vfs.setSources(sources, errors);
    loadStateLocked();
}

FileKind Project::kindOf(const QString &rel)
{
    if (rel.startsWith(QLatin1String("game/")))
        return FileKind::Game;
    if (rel.startsWith(QLatin1String("dialog/")))
        return FileKind::Dialog;
    return FileKind::Other;
}

int Project::gameFilePriority(const QString &rel)
{
    const int i = kGameOrder.indexOf(stemOf(rel));
    return i < 0 ? 1000 : i;
}

QStringList Project::messageFiles() const
{
    QReadLocker locker(&m_lock);
    QSet<QString> set;
    for (const QString &lang : {m_config.sourceLanguage, m_config.targetLanguage}) {
        const QString prefix = prefixFor(lang);
        for (const QString &path : m_vfs.list(prefix, QStringLiteral(".msg")))
            set.insert(path.mid(prefix.size()));
    }
    QStringList files = set.values();
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        const int ka = int(kindOf(a)), kb = int(kindOf(b));
        if (ka != kb)
            return ka < kb;
        if (ka == int(FileKind::Game)) {
            const int pa = gameFilePriority(a), pb = gameFilePriority(b);
            if (pa != pb)
                return pa < pb;
        }
        return a < b;
    });
    return files;
}

QString Project::sourcePath(const QString &rel) const
{
    return prefixFor(m_config.sourceLanguage) + rel;
}

QString Project::targetPath(const QString &rel) const
{
    return prefixFor(m_config.targetLanguage) + rel;
}

QString Project::targetDiskPath(const QString &rel) const
{
    return QDir(m_config.outputDir).filePath(targetPath(rel));
}

SegmentStatus Project::statusOf(const TranslationRow &row)
{
    if (row.review)
        return SegmentStatus::ToReview;
    if (!row.inSource || !Statistics::isTranslatable(row.source))
        return SegmentStatus::Translated;
    if (row.target.trimmed().isEmpty())
        return SegmentStatus::Untranslated;
    if (row.target == row.source && !row.confirmed)
        return SegmentStatus::Untranslated;
    return SegmentStatus::Translated;
}

QByteArray Project::readDialogScript(const QString &relPath, QString *error) const
{
    QReadLocker locker(&m_lock);
    const QString path = QStringLiteral("scripts/%1.int").arg(stemOf(relPath));
    if (!m_vfs.exists(path)) {
        if (error)
            *error = QStringLiteral("scripts/%1.int not found in the sources").arg(stemOf(relPath));
        return {};
    }
    return m_vfs.read(path, error);
}

QByteArray Project::readInterfaceFont(int index) const
{
    QReadLocker locker(&m_lock);
    const QString name = QStringLiteral("font%1.aaf").arg(index);
    for (const QString &path : {prefixFor(m_config.targetLanguage) + name, prefixFor(m_config.sourceLanguage) + name, name})
        if (m_vfs.exists(path))
            return m_vfs.read(path);
    return {};
}

TranslationDoc Project::loadDocument(const QString &rel, QString *error) const
{
    QReadLocker locker(&m_lock);
    TranslationDoc doc;
    doc.relPath = rel;

    MsgFile source, target;
    bool haveSource = false;
    if (m_vfs.exists(sourcePath(rel))) {
        const QByteArray bytes = m_vfs.read(sourcePath(rel), error);
        source = MsgFile::parse(TextCodec::decode(bytes, m_config.sourceEncoding));
        haveSource = true;
    }
    if (m_vfs.exists(targetPath(rel))) {
        const QByteArray bytes = m_vfs.read(targetPath(rel), error);
        target = MsgFile::parse(TextCodec::decode(bytes, m_config.targetEncoding));
        doc.targetFileExists = true;
    }
    doc.warnings = source.warnings + target.warnings;
    doc.crlf = haveSource ? source.crlf : target.crlf;
    doc.trailing = haveSource ? source.trailing : target.trailing;

    const QHash<int, int> targetIndex = target.indexById();
    const auto flagsIt = m_flags.constFind(rel);

    QSet<int> seen;
    for (const MsgEntry &s : std::as_const(source.entries)) {
        TranslationRow row;
        row.id = s.id;
        row.audio = s.audio;
        row.source = s.text;
        row.leading = s.leading;
        const int ti = targetIndex.value(s.id, -1);
        if (ti >= 0)
            row.target = target.entries.at(ti).text;
        if (flagsIt != m_flags.constEnd()) {
            row.review = flagsIt->review.contains(s.id);
            row.confirmed = flagsIt->confirmed.contains(s.id);
        }
        seen.insert(s.id);
        doc.rows.push_back(row);
    }
    for (const MsgEntry &t : std::as_const(target.entries)) {
        if (seen.contains(t.id) || targetIndex.value(t.id) != int(&t - target.entries.constData()))
            continue;
        TranslationRow row;
        row.id = t.id;
        row.audio = t.audio;
        row.target = t.text;
        row.leading = t.leading;
        row.inSource = false;
        if (flagsIt != m_flags.constEnd())
            row.review = flagsIt->review.contains(t.id);
        doc.rows.push_back(row);
    }
    return doc;
}

bool Project::saveDocument(const TranslationDoc &doc, QString *error, QString *unencodable)
{
    MsgFile file;
    file.crlf = doc.crlf;
    file.trailing = doc.trailing;
    for (const TranslationRow &row : doc.rows) {
        MsgEntry e;
        e.id = row.id;
        e.audio = row.audio;
        // An untranslated record keeps the source text so the game never shows an empty line.
        e.text = row.target.isEmpty() && row.inSource ? row.source : row.target;
        e.leading = row.leading;
        file.entries.push_back(e);
    }

    const QByteArray bytes = TextCodec::encode(file.serialize(), m_config.targetEncoding, unencodable);
    const QString path = targetDiskPath(doc.relPath);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error)
            *error = out.errorString();
        return false;
    }
    QWriteLocker locker(&m_lock);
    m_vfs.addLooseFile(targetPath(doc.relPath), path);

    FileFlags flags;
    for (const TranslationRow &row : doc.rows) {
        if (row.review)
            flags.review.insert(row.id);
        if (row.confirmed)
            flags.confirmed.insert(row.id);
    }
    if (flags.review.isEmpty() && flags.confirmed.isEmpty())
        m_flags.remove(doc.relPath);
    else
        m_flags.insert(doc.relPath, flags);
    saveStateLocked();
    return true;
}

FileStats Project::statisticsOf(const TranslationDoc &doc)
{
    FileStats s;
    s.relPath = doc.relPath;
    s.displayName = Statistics::displayNameFor(doc.relPath);
    s.kind = int(kindOf(doc.relPath));
    for (const TranslationRow &row : doc.rows) {
        if (!row.inSource)
            continue;
        ++s.lines;
        const int words = Statistics::countWords(row.source);
        s.words += words;
        s.chars += row.source.size();
        switch (statusOf(row)) {
        case SegmentStatus::Translated: ++s.translated; break;
        case SegmentStatus::ToReview: ++s.review; break;
        case SegmentStatus::Untranslated:
            ++s.untranslated;
            s.wordsRemaining += words;
            s.charsRemaining += row.source.size();
            break;
        }
    }
    return s;
}

QVector<FileStats> Project::computeStatistics(const ProgressFn &progress, const std::atomic_bool *cancel) const
{
    QVector<FileStats> all;
    const QStringList files = messageFiles();
    for (int i = 0; i < files.size(); ++i) {
        if (cancel && cancel->load())
            break;
        FileStats s = statisticsOf(loadDocument(files.at(i)));
        all.push_back(s);
        if (progress)
            progress(i + 1, int(files.size()), s);
    }
    return all;
}

Glossary Project::buildGlossary(const std::atomic_bool *cancel) const
{
    Glossary glossary;
    for (const QString &rel : messageFiles()) {
        if (cancel && cancel->load())
            return glossary;
        if (kindOf(rel) != FileKind::Game)
            continue;
        const QString stem = stemOf(rel);
        const TranslationDoc doc = loadDocument(rel);

        // A name kept as is (Stimpak -> Stimpak) is a real translation when the file itself is translated;
        // in a file that is still a copy of the source it only means "not done yet".
        int differing = 0, translatable = 0;
        for (const TranslationRow &row : doc.rows)
            if (row.inSource && Statistics::isTranslatable(row.source) && !row.target.isEmpty()) {
                ++translatable;
                differing += row.target != row.source ? 1 : 0;
            }
        const bool fileIsTranslated = doc.targetFileExists && translatable > 0 && differing * 10 >= translatable;

        for (const TranslationRow &row : doc.rows) {
            if (!row.inSource || !isNameRecord(stem, row.id, row.source))
                continue;
            const bool translated = statusOf(row) != SegmentStatus::Untranslated
                || (fileIsTranslated && !row.target.trimmed().isEmpty());
            glossary.add(row.source, translated ? row.target : QString(), categoryFor(stem),
                         QStringLiteral("%1#%2").arg(rel.mid(rel.lastIndexOf(QLatin1Char('/')) + 1)).arg(row.id));
        }
    }
    glossary.finalize(m_config.sourceLanguage, m_config.targetLanguage);
    return glossary;
}

QVector<TodoItem> Project::reviewItems(const QSet<QString> &skip) const
{
    QStringList files;
    {
        QReadLocker locker(&m_lock);
        for (auto it = m_flags.constBegin(); it != m_flags.constEnd(); ++it)
            if (!it->review.isEmpty() && !skip.contains(it.key()))
                files << it.key();
    }
    files.sort();

    QVector<TodoItem> items;
    for (const QString &rel : std::as_const(files)) {
        const TranslationDoc doc = loadDocument(rel);
        for (const TranslationRow &row : doc.rows)
            if (row.review)
                items.push_back({rel, row.id, row.source, row.target});
    }
    return items;
}

// ----------------------------------------------------------------------------- persisted flags

void Project::loadState()
{
    QWriteLocker locker(&m_lock);
    loadStateLocked();
}

void Project::loadStateLocked()
{
    m_flags.clear();
    if (m_stateFile.isEmpty())
        return;
    QFile f(m_stateFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject files = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        FileFlags flags;
        for (const QJsonValue &v : it.value().toObject().value(QStringLiteral("review")).toArray())
            flags.review.insert(v.toInt());
        for (const QJsonValue &v : it.value().toObject().value(QStringLiteral("confirmed")).toArray())
            flags.confirmed.insert(v.toInt());
        m_flags.insert(it.key(), flags);
    }
}

void Project::saveState() const
{
    QReadLocker locker(&m_lock);
    saveStateLocked();
}

void Project::saveStateLocked() const
{
    if (m_stateFile.isEmpty())
        return;
    QJsonObject files;
    for (auto it = m_flags.begin(); it != m_flags.end(); ++it) {
        QJsonArray review, confirmed;
        QList<int> r = it->review.values(), c = it->confirmed.values();
        std::sort(r.begin(), r.end());
        std::sort(c.begin(), c.end());
        for (int id : std::as_const(r)) review.append(id);
        for (int id : std::as_const(c)) confirmed.append(id);
        files.insert(it.key(), QJsonObject{{QStringLiteral("review"), review}, {QStringLiteral("confirmed"), confirmed}});
    }
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile f(m_stateFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("files"), files}}).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
