#include "ProjectStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace ProjectStore {

namespace {
constexpr int kMaxRecent = 12;

QString projectsDir()
{
    return QDir(baseDirectory()).filePath(QStringLiteral("projects"));
}
}

QString baseDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

QString stateFile(const QString &id)
{
    return QDir(projectsDir()).filePath(id + QStringLiteral(".state.json"));
}

QString stagingDir(const QString &id)
{
    return QDir(projectsDir()).filePath(id + QStringLiteral(".output"));
}

QString statsCacheFile(const QString &id)
{
    return QDir(projectsDir()).filePath(id + QStringLiteral(".stats.json"));
}

QVector<ProjectConfig> loadAll()
{
    QVector<ProjectConfig> list;
    const QDir dir(projectsDir());
    for (const QString &name : dir.entryList({QStringLiteral("*.fbproj")}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        ProjectConfig c;
        c.id = o.value(QStringLiteral("id")).toString();
        if (c.id.isEmpty())
            continue;
        c.name = o.value(QStringLiteral("name")).toString();
        for (const QJsonValue &v : o.value(QStringLiteral("sources")).toArray())
            c.sources << v.toString();
        c.outputDir = o.value(QStringLiteral("outputDir")).toString();
        c.outputArchive = o.value(QStringLiteral("outputArchive")).toString();
        c.outputArchiveFormat = o.value(QStringLiteral("outputArchiveFormat")).toString(c.outputArchiveFormat);
        c.archiveWithSource = o.value(QStringLiteral("archiveWithSource")).toBool(true);
        c.sourceLanguage = o.value(QStringLiteral("sourceLanguage")).toString(c.sourceLanguage);
        c.targetLanguage = o.value(QStringLiteral("targetLanguage")).toString(c.targetLanguage);
        c.sourceEncoding = o.value(QStringLiteral("sourceEncoding")).toString(c.sourceEncoding);
        c.targetEncoding = o.value(QStringLiteral("targetEncoding")).toString(c.targetEncoding);
        list.push_back(c);
    }
    return list;
}

bool save(const ProjectConfig &c)
{
    QDir().mkpath(projectsDir());
    QJsonObject o;
    o.insert(QStringLiteral("id"), c.id);
    o.insert(QStringLiteral("name"), c.name);
    o.insert(QStringLiteral("sources"), QJsonArray::fromStringList(c.sources));
    o.insert(QStringLiteral("outputDir"), c.outputDir);
    o.insert(QStringLiteral("outputArchive"), c.outputArchive);
    o.insert(QStringLiteral("outputArchiveFormat"), c.outputArchiveFormat);
    o.insert(QStringLiteral("archiveWithSource"), c.archiveWithSource);
    o.insert(QStringLiteral("sourceLanguage"), c.sourceLanguage);
    o.insert(QStringLiteral("targetLanguage"), c.targetLanguage);
    o.insert(QStringLiteral("sourceEncoding"), c.sourceEncoding);
    o.insert(QStringLiteral("targetEncoding"), c.targetEncoding);
    QSaveFile f(QDir(projectsDir()).filePath(c.id + QStringLiteral(".fbproj")));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

void remove(const QString &id)
{
    QFile::remove(QDir(projectsDir()).filePath(id + QStringLiteral(".fbproj")));
    QFile::remove(stateFile(id));
    QFile::remove(statsCacheFile(id));
}

QStringList openProjectIds()
{
    return QSettings().value(QStringLiteral("projects/open")).toStringList();
}

void setOpenProjectIds(const QStringList &ids)
{
    QSettings().setValue(QStringLiteral("projects/open"), ids);
}

QStringList recentFolders()
{
    return QSettings().value(QStringLiteral("projects/recentFolders")).toStringList();
}

void addRecentFolder(const QString &path)
{
    QStringList list = recentFolders();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecent)
        list.removeLast();
    QSettings().setValue(QStringLiteral("projects/recentFolders"), list);
}

}
