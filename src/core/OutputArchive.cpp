#include "OutputArchive.h"

#include "DatWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace OutputArchive {

namespace {

QString languagePrefix(const QString &language)
{
    return QStringLiteral("text/") + language.toLower() + QLatin1Char('/');
}

bool samePath(const QString &a, const QString &b)
{
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()),
                                                                   Qt::CaseInsensitive) == 0;
}

}

DatArchive::Format formatOf(const ProjectConfig &config)
{
    return config.outputArchiveFormat == QLatin1String("dat1") ? DatArchive::Format::Fallout1 : DatArchive::Format::Fallout2;
}

QString formatId(DatArchive::Format format)
{
    return format == DatArchive::Format::Fallout1 ? QStringLiteral("dat1") : QStringLiteral("dat2");
}

DatArchive::Format detectFormat(const QStringList &sources)
{
    for (const QString &s : sources) {
        const QFileInfo fi(s);
        if (!fi.isFile())
            continue;
        if (const auto dat = DatArchive::open(s))
            return dat->format();
    }
    return DatArchive::Format::Fallout2;
}

QString problemWith(const ProjectConfig &config)
{
    if (!enabled(config))
        return {};
    for (const QString &s : config.sources)
        if (samePath(s, config.outputArchive))
            return QCoreApplication::translate("OutputArchive", "The output archive cannot be one of the sources of the project.");
    if (!config.outputArchive.endsWith(QLatin1String(".dat"), Qt::CaseInsensitive))
        return QCoreApplication::translate("OutputArchive", "The output archive must be a .dat file.");
    return {};
}

int seedWorkingFolder(const ProjectConfig &config, QString *error)
{
    if (!enabled(config) || !QFileInfo(config.outputArchive).isFile())
        return 0;
    QString openError;
    const auto dat = DatArchive::open(config.outputArchive, &openError);
    if (!dat) {
        if (error)
            *error = openError;
        return 0;
    }
    const QString prefix = languagePrefix(config.targetLanguage);
    int copied = 0;
    for (int i = 0; i < dat->entries().size(); ++i) {
        const DatEntry &entry = dat->entries().at(i);
        if (!entry.path.startsWith(prefix))
            continue;
        const QString target = QDir(config.outputDir).filePath(entry.path);
        if (QFileInfo::exists(target))
            continue;
        const QByteArray data = dat->read(i);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size())
            ++copied;
    }
    return copied;
}

Result build(const Project &project)
{
    Result result;
    const ProjectConfig &config = project.config();
    result.error = problemWith(config);
    if (!result.error.isEmpty() || !enabled(config))
        return result;

    const QString targetPrefix = languagePrefix(config.targetLanguage);
    const QDir work(config.outputDir);
    const QString translatedRoot = work.filePath(targetPrefix);

    QVector<DatWriteItem> items;
    QStringList relatives;      // path below text/<target>/, for the reference copies
    if (QDir(translatedRoot).exists()) {
        QDirIterator it(translatedRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString abs = it.next();
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) {
                result.error = QCoreApplication::translate("OutputArchive", "Cannot read %1.").arg(abs);
                return result;
            }
            DatWriteItem item;
            const QString rel = QDir(translatedRoot).relativeFilePath(abs);
            item.path = targetPrefix + rel;
            item.data = f.readAll();
            items.push_back(item);
            relatives << rel;
            ++result.translated;
        }
    }

    // translated scripts (texts written in their code), if any
    const QString scriptsRoot = work.filePath(QStringLiteral("scripts"));
    if (QDir(scriptsRoot).exists()) {
        QDirIterator scripts(scriptsRoot, {QStringLiteral("*.int")}, QDir::Files);
        while (scripts.hasNext()) {
            QFile f(scripts.next());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            DatWriteItem item;
            item.path = QStringLiteral("scripts/") + QFileInfo(f.fileName()).fileName();
            item.data = f.readAll();
            items.push_back(item);
            ++result.scripts;
        }
    }

    if (config.archiveWithSource) {
        const QString sourcePrefix = languagePrefix(config.sourceLanguage);
        for (const QString &rel : std::as_const(relatives)) {
            if (!rel.endsWith(QLatin1String(".msg"), Qt::CaseInsensitive))
                continue;
            const QByteArray data = project.readSourceText(rel);
            if (data.isEmpty())
                continue;     // the file only exists in the translation
            DatWriteItem item;
            item.path = sourcePrefix + rel;
            item.data = data;
            items.push_back(item);
            ++result.originals;
        }
    }

    QDir().mkpath(QFileInfo(config.outputArchive).absolutePath());
    QString error;
    result.ok = DatWriter::write(config.outputArchive, formatOf(config), items, &error);
    if (!result.ok)
        result.error = error;
    return result;
}

}
