#include "VirtualFileSystem.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace {

const QString kTextPrefix = QStringLiteral("text/");

// Indexed paths: everything under text/ (messages) and the compiled scripts under scripts/ (dialogue trees).
bool isIndexed(const QString &normalizedPath)
{
    // Interface fonts (font1.aaf...) are read to measure text for the Pip-Boy.
    static const QRegularExpression font(QStringLiteral("^font\\d+\\.aaf$"));
    return normalizedPath.startsWith(kTextPrefix) || font.match(normalizedPath).hasMatch()
        || (normalizedPath.startsWith(QLatin1String("scripts/")) && normalizedPath.endsWith(QLatin1String(".int")));
}

bool isDatFile(const QString &path)
{
    return path.endsWith(QLatin1String(".dat"), Qt::CaseInsensitive);
}

// Fallout installs ship master.dat then critter.dat, then patches in name order.
int datRank(const QString &fileName)
{
    const QString n = fileName.toLower();
    if (n.startsWith(QLatin1String("master")))
        return 0;
    if (n.startsWith(QLatin1String("critter")))
        return 1;
    if (n.startsWith(QLatin1String("f2_res")) || n.startsWith(QLatin1String("f1_res")))
        return 2;
    return 3;
}

}

namespace {

// Name of the sub-directory matching `name` whatever its case (empty if none).
QString childDir(const QDir &dir, const QString &name)
{
    for (const QString &n : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (n.compare(name, Qt::CaseInsensitive) == 0)
            return n;
    return {};
}

}

QString VirtualFileSystem::resolveFolderRoot(const QString &folder)
{
    const QDir dir(folder);
    if (!childDir(dir, QStringLiteral("text")).isEmpty())
        return dir.absolutePath();
    const QString data = childDir(dir, QStringLiteral("data"));
    if (!data.isEmpty() && !childDir(QDir(dir.filePath(data)), QStringLiteral("text")).isEmpty())
        return QDir(dir.filePath(data)).absolutePath();
    return dir.absolutePath();
}

QStringList VirtualFileSystem::discoverSources(const QString &folder)
{
    QStringList result;
    const QDir dir(folder);
    // Archives: master.dat / critter.dat / patches as files, or extracted into a folder of the same name.
    QFileInfoList dats = dir.entryInfoList({QStringLiteral("*.dat"), QStringLiteral("*.DAT")},
                                           QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    std::stable_sort(dats.begin(), dats.end(), [](const QFileInfo &a, const QFileInfo &b) {
        const int ra = datRank(a.fileName()), rb = datRank(b.fileName());
        return ra != rb ? ra < rb : a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
    });
    for (const QFileInfo &fi : std::as_const(dats))
        if (!result.contains(fi.absoluteFilePath()))
            result << fi.absoluteFilePath();
    const QString data = childDir(dir, QStringLiteral("data"));
    if (!data.isEmpty())
        result << dir.filePath(data);

    // sfall mod loader: mods/mods_order.txt lists the mods from the lowest to the highest priority;
    // mods that are not listed come after.
    const QString modsName = childDir(dir, QStringLiteral("mods"));
    if (!modsName.isEmpty()) {
        const QDir mods(dir.filePath(modsName));
        QStringList ordered;
        QFile order(mods.filePath(QStringLiteral("mods_order.txt")));
        if (order.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!order.atEnd()) {
                const QString line = QString::fromLocal8Bit(order.readLine()).trimmed();
                if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')) && !line.startsWith(QLatin1Char(';')))
                    ordered << line;
            }
        }
        auto isMod = [&](const QString &name) {
            const QFileInfo fi(mods.filePath(name));
            return fi.exists() && (fi.isDir() || isDatFile(name));
        };
        for (const QString &name : std::as_const(ordered))
            if (isMod(name) && !result.contains(mods.filePath(name)))
                result << mods.filePath(name);
        for (const QString &name : mods.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (isMod(name) && !result.contains(mods.filePath(name)))
                result << mods.filePath(name);
    }
    if (result.isEmpty())
        result << dir.absolutePath();
    return result;
}

void VirtualFileSystem::setSources(const QStringList &sources, QStringList *errors)
{
    m_layers.clear();
    m_files.clear();

    for (const QString &source : sources) {
        const int layerIndex = int(m_layers.size());
        Layer layer;
        layer.source = source;
        const QFileInfo info(source);

        if (info.isFile() && isDatFile(source)) {
            QString error;
            std::unique_ptr<DatArchive> archive = DatArchive::open(source, &error);
            if (!archive) {
                if (errors)
                    errors->append(QStringLiteral("%1: %2").arg(source, error));
                continue;
            }
            layer.dat = std::move(archive);
            layer.stamp = QStringLiteral("%1|%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
            const auto &entries = layer.dat->entries();
            for (int i = 0; i < entries.size(); ++i) {
                if (!isIndexed(entries.at(i).path))
                    continue;
                File f;
                f.path = entries.at(i).path;
                f.layer = layerIndex;
                f.datIndex = i;
                m_files.insert(f.path, f);
            }
        } else if (info.isDir()) {
            const QString root = resolveFolderRoot(source);
            const QDir rootDir(root);
            for (const QString &name : rootDir.entryList(QDir::Files)) {
                File f;
                f.path = DatArchive::normalizePath(name);
                if (!isIndexed(f.path))
                    continue;
                f.layer = layerIndex;
                f.absolutePath = rootDir.filePath(name);
                m_files.insert(f.path, f);
            }
            for (const char *sub : {"text", "scripts"}) {
                const QString name = childDir(rootDir, QString::fromLatin1(sub));
                if (name.isEmpty())
                    continue;
                QDirIterator it(rootDir.filePath(name), QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString abs = it.next();
                    File f;
                    f.path = DatArchive::normalizePath(rootDir.relativeFilePath(abs));
                    if (!isIndexed(f.path))
                        continue;
                    f.layer = layerIndex;
                    f.absolutePath = abs;
                    m_files.insert(f.path, f);
                }
            }
        } else if (errors) {
            errors->append(QStringLiteral("%1: not found").arg(source));
            continue;
        }
        m_layers.push_back(std::move(layer));
    }
}

const VirtualFileSystem::File *VirtualFileSystem::find(const QString &path) const
{
    const auto it = m_files.constFind(DatArchive::normalizePath(path));
    return it == m_files.constEnd() ? nullptr : &it.value();
}

QByteArray VirtualFileSystem::read(const QString &path, QString *error) const
{
    const File *f = find(path);
    if (!f) {
        if (error)
            *error = QStringLiteral("File not found: %1").arg(path);
        return {};
    }
    if (f->datIndex >= 0 && f->layer >= 0 && f->layer < int(m_layers.size()))
        return m_layers[f->layer].dat->read(f->datIndex, error);

    QFile file(f->absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    return file.readAll();
}

QStringList VirtualFileSystem::list(const QString &prefix, const QString &suffix) const
{
    QStringList out;
    for (auto it = m_files.lowerBound(prefix); it != m_files.cend() && it.key().startsWith(prefix); ++it)
        if (suffix.isEmpty() || it.key().endsWith(suffix))
            out << it.key();
    return out;
}

QStringList VirtualFileSystem::languages() const
{
    QStringList langs;
    for (auto it = m_files.lowerBound(kTextPrefix); it != m_files.cend() && it.key().startsWith(kTextPrefix); ++it) {
        const int slash = it.key().indexOf(QLatin1Char('/'), kTextPrefix.size());
        if (slash < 0)
            continue;
        const QString lang = it.key().mid(kTextPrefix.size(), slash - kTextPrefix.size());
        if (!langs.contains(lang))
            langs << lang;
    }
    return langs;
}

void VirtualFileSystem::addLooseFile(const QString &path, const QString &absolutePath)
{
    File f;
    f.path = DatArchive::normalizePath(path);
    f.layer = int(m_layers.size()); // above every scanned source
    f.absolutePath = absolutePath;
    m_files.insert(f.path, f);
}

QString VirtualFileSystem::signature(const QString &path) const
{
    const File *f = find(path);
    if (!f)
        return {};
    if (f->datIndex >= 0 && f->layer >= 0 && f->layer < int(m_layers.size())) {
        const Layer &layer = m_layers[f->layer];
        const DatEntry &e = layer.dat->entries().at(f->datIndex);
        return QStringLiteral("d|%1|%2|%3|%4|%5").arg(layer.source, layer.stamp).arg(e.offset).arg(e.packedSize).arg(e.unpackedSize);
    }
    const QFileInfo info(f->absolutePath);
    return QStringLiteral("f|%1|%2|%3").arg(f->absolutePath).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
}

QString VirtualFileSystem::describe(const QString &path) const
{
    const File *f = find(path);
    if (!f)
        return {};
    if (f->absolutePath.isEmpty() && f->layer >= 0 && f->layer < int(m_layers.size()))
        return QFileInfo(m_layers[f->layer].source).fileName();
    return f->absolutePath;
}
