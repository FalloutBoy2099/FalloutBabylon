#include "Int2Ssl.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace Int2Ssl {

QString findExecutable()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("int2ssl.exe");
#else
    const QString name = QStringLiteral("int2ssl");
#endif
    const QString fromEnv = qEnvironmentVariable("BABYLON_INT2SSL");
    if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv))
        return fromEnv;
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {app.filePath(name), app.filePath(QStringLiteral("tools/") + name),
                                     app.filePath(QStringLiteral("../tools/") + name),
                                     app.filePath(QStringLiteral("../../tools/") + name)})
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    return {};
}

namespace {

QString run(const QString &exe, const QString &dir, const QStringList &options, QString *error)
{
    QFile::remove(QDir(dir).filePath(QStringLiteral("script.ssl")));
    QProcess process;
    process.setWorkingDirectory(dir);
    process.start(exe, options + QStringList{QStringLiteral("script.int"), QStringLiteral("script.ssl")});
    if (!process.waitForFinished(30000)) {
        process.kill();
        if (error)
            *error = QStringLiteral("int2ssl timed out");
        return {};
    }
    QFile out(QDir(dir).filePath(QStringLiteral("script.ssl")));
    if (process.exitCode() != 0 || !out.open(QIODevice::ReadOnly)) {
        if (error) {
            // int2ssl prints a banner and progress lines: the useful part is its "Error: ..." line.
            const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput() + process.readAllStandardError())
                                          .split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            QString message;
            for (const QString &line : lines)
                if (line.trimmed().startsWith(QLatin1String("Error"), Qt::CaseInsensitive))
                    message = line.trimmed();
            *error = message.isEmpty() ? (lines.isEmpty() ? QString() : lines.last().trimmed()) : message;
        }
        return {};
    }
    return QString::fromLatin1(out.readAll());
}

}

QString decompile(const QByteArray &intData, QString *error, bool *usedFallout1Mode)
{
    if (usedFallout1Mode)
        *usedFallout1Mode = false;
    const QString exe = findExecutable();
    if (exe.isEmpty()) {
        if (error)
            *error = QStringLiteral("int2ssl not found");
        return {};
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        if (error)
            *error = QStringLiteral("cannot create a temporary folder");
        return {};
    }
    QFile in(QDir(dir.path()).filePath(QStringLiteral("script.int")));
    if (!in.open(QIODevice::WriteOnly) || in.write(intData) != intData.size()) {
        if (error)
            *error = in.errorString();
        return {};
    }
    in.close();

    QString ssl = run(exe, dir.path(), {QStringLiteral("-a"), QStringLiteral("-b")}, error);
    if (ssl.isEmpty()) {
        ssl = run(exe, dir.path(), {QStringLiteral("-1"), QStringLiteral("-a"), QStringLiteral("-b")}, nullptr);
        if (usedFallout1Mode)
            *usedFallout1Mode = !ssl.isEmpty();
    }
    if (!ssl.isEmpty() && error)
        error->clear();
    return ssl;
}

}
