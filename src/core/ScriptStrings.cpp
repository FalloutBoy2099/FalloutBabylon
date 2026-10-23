#include "ScriptStrings.h"

#include "TextCodec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace ScriptStrings {

QVector<Text> scan(const Project &project, const ProgressFn &progress, const std::atomic_bool *cancel)
{
    QVector<Text> texts;
    const QStringList names = project.scriptNames();
    for (int i = 0; i < names.size(); ++i) {
        if (cancel && cancel->load())
            break;
        if (progress && i % 25 == 0)
            progress(i, int(names.size()));
        const QByteArray script = project.readScriptOriginal(names.at(i));
        if (script.isEmpty())
            continue;
        const IntStrings::Table table = IntStrings::parse(script);
        if (!table.valid)
            continue;
        for (const IntStrings::Entry &e : table.entries) {
            if (!table.usedOperands.contains(e.operand) || !IntStrings::looksTranslatable(e.text))
                continue;
            Text t;
            t.script = names.at(i);
            t.operand = e.operand;
            t.source = TextCodec::decode(e.text, project.config().sourceEncoding);
            t.capacity = e.capacity();
            texts.push_back(t);
        }
    }
    if (progress)
        progress(int(names.size()), int(names.size()));
    return texts;
}

ApplyResult apply(Project &project, const QVector<Translation> &translations, bool allowEnlarge)
{
    ApplyResult result;
    const ProjectConfig &config = project.config();
    const QString scriptsDir = QDir(config.outputDir).filePath(QStringLiteral("scripts"));

    QHash<QString, QVector<Translation>> byScript;
    for (const Translation &t : translations)
        if (!t.target.isEmpty() && t.target != t.source)
            byScript[t.script].push_back(t);

    // scripts that were written earlier and have nothing to say any more: back to the original
    const QString pristine = project.pristineScriptsDir();
    QStringList previous;
    if (!pristine.isEmpty())
        previous = QDir(pristine).entryList({QStringLiteral("*.int")}, QDir::Files);
    for (const QString &file : std::as_const(previous)) {
        const QString name = QFileInfo(file).completeBaseName();
        if (!byScript.contains(name))
            QFile::remove(QDir(scriptsDir).filePath(file));
    }

    for (auto it = byScript.cbegin(); it != byScript.cend(); ++it) {
        const QByteArray original = project.readScriptOriginal(it.key());
        if (original.isEmpty()) {
            result.problems << QStringLiteral("%1: script not found").arg(it.key());
            continue;
        }
        const IntStrings::Table table = IntStrings::parse(original);
        QHash<quint32, QByteArray> texts;
        for (const Translation &t : it.value()) {
            // the string must still be what was translated (the sources may have changed)
            bool found = false;
            for (const IntStrings::Entry &e : table.entries)
                if (e.operand == t.operand
                    && TextCodec::decode(e.text, config.sourceEncoding) == t.source) {
                    found = true;
                    break;
                }
            if (!found) {
                result.problems << QStringLiteral("%1: \"%2\" is not in the script any more").arg(it.key(), t.source.left(30));
                continue;
            }
            QString unencodable;
            texts.insert(t.operand, TextCodec::encode(t.target, config.targetEncoding, &unencodable));
            if (!unencodable.isEmpty())
                result.problems << QStringLiteral("%1: characters not available in %2: %3").arg(it.key(), config.targetEncoding, unencodable);
        }
        const IntStrings::PatchResult patched = IntStrings::patch(original, texts, allowEnlarge);
        for (const QString &p : patched.problems)
            result.problems << it.key() + QStringLiteral(": ") + p;
        const QString path = QDir(scriptsDir).filePath(it.key() + QStringLiteral(".int"));
        if (patched.inPlace + patched.enlarged == 0) {
            QFile::remove(path);                // nothing could be written: keep the original
            continue;
        }
        project.keepPristineScript(it.key(), original);
        QDir().mkpath(scriptsDir);
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(patched.data) != patched.data.size() || !out.commit()) {
            result.problems << it.key() + QStringLiteral(": ") + out.errorString();
            continue;
        }
        project.registerScriptFile(it.key(), path);
        ++result.scripts;
        result.inPlace += patched.inPlace;
        result.enlarged += patched.enlarged;
    }
    return result;
}

}
