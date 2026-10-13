#include "TranslationMemory.h"

#include "MsgFile.h"
#include "TextCodec.h"
#include "VirtualFileSystem.h"

#include <QHash>

namespace TranslationMemory {

namespace {

QString key(const QString &text)
{
    return text.simplified();
}

MsgFile loadMsg(const VirtualFileSystem &vfs, const QString &path, const QString &encoding, bool *found)
{
    if (!vfs.exists(path)) {
        *found = false;
        return {};
    }
    *found = true;
    return MsgFile::parse(TextCodec::decode(vfs.read(path), encoding));
}

}

Report import(Project &project, const Memory &memory, const Options &options, const ProgressFn &progress,
              const std::atomic_bool *cancel)
{
    Report report;
    VirtualFileSystem mem;
    QStringList errors;
    mem.setSources(memory.sources, &errors);
    report.errors = errors;

    const QString srcPrefix = QStringLiteral("text/%1/").arg(memory.sourceLanguage.toLower());
    const QString dstPrefix = QStringLiteral("text/%1/").arg(memory.targetLanguage.toLower());

    // Text index of the whole memory: normalised source text -> translation (the most frequent one wins).
    QHash<QString, QHash<QString, int>> byText;
    if (options.matchByText) {
        for (const QString &path : mem.list(srcPrefix, QStringLiteral(".msg"))) {
            if (cancel && cancel->load())
                return report;
            const QString rel = path.mid(srcPrefix.size());
            bool haveSrc = false, haveDst = false;
            const MsgFile src = loadMsg(mem, path, memory.sourceEncoding, &haveSrc);
            const MsgFile dst = loadMsg(mem, dstPrefix + rel, memory.targetEncoding, &haveDst);
            if (!haveDst)
                continue;
            const QHash<int, int> dstIndex = dst.indexById();
            for (const MsgEntry &s : src.entries) {
                const int di = dstIndex.value(s.id, -1);
                if (di < 0)
                    continue;
                const QString &t = dst.entries.at(di).text;
                if (t.trimmed().isEmpty() || t == s.text || !Statistics::isTranslatable(s.text))
                    continue;
                byText[key(s.text)][t] += 1;
            }
        }
    }

    const QStringList files = project.messageFiles();
    for (int fi = 0; fi < files.size(); ++fi) {
        if (cancel && cancel->load())
            break;
        const QString &rel = files.at(fi);
        if (progress)
            progress(fi, int(files.size()));

        TranslationDoc doc = project.loadDocument(rel);
        bool haveSrc = false, haveDst = false;
        const MsgFile mSrc = loadMsg(mem, srcPrefix + rel, memory.sourceEncoding, &haveSrc);
        const MsgFile mDst = loadMsg(mem, dstPrefix + rel, memory.targetEncoding, &haveDst);
        const QHash<int, int> srcIndex = mSrc.indexById();
        const QHash<int, int> dstIndex = mDst.indexById();
        ++report.files;

        bool modified = false;
        for (TranslationRow &row : doc.rows) {
            if (!row.inSource || !Statistics::isTranslatable(row.source))
                continue;
            ++report.examined;
            if (!options.overwrite && Project::statusOf(row) != SegmentStatus::Untranslated) {
                ++report.kept;
                continue;
            }

            const int si = srcIndex.value(row.id, -1);
            const int di = dstIndex.value(row.id, -1);
            const QString oldSource = si >= 0 ? mSrc.entries.at(si).text : QString();
            const QString oldTarget = di >= 0 ? mDst.entries.at(di).text : QString();
            const bool oldTranslated = !oldTarget.trimmed().isEmpty() && oldTarget != oldSource;

            if (haveSrc && haveDst && si >= 0 && oldTranslated) {
                if (key(oldSource) == key(row.source)) {
                    row.target = oldTarget;
                    row.review = false;
                    ++report.exact;
                    modified = true;
                    continue;
                }
                if (options.changedAsReview) {
                    row.target = oldTarget;
                    row.review = true;
                    row.previous = oldTarget;
                    row.comment = QStringLiteral("Imported translation: the source text has changed. Previous source: %1")
                                      .arg(oldSource.simplified());
                    ++report.changed;
                    modified = true;
                    continue;
                }
            }
            if (options.matchByText) {
                const auto it = byText.constFind(key(row.source));
                if (it != byText.constEnd()) {
                    QString best;
                    int bestCount = 0;
                    for (auto t = it->constBegin(); t != it->constEnd(); ++t)
                        if (t.value() > bestCount || (t.value() == bestCount && t.key() < best)) {
                            best = t.key();
                            bestCount = t.value();
                        }
                    row.target = best;
                    row.review = false;
                    ++report.byText;
                    modified = true;
                }
            }
        }

        if (modified) {
            QString error, unencodable;
            if (project.saveDocument(doc, &error, &unencodable, false))
                ++report.saved;
            else
                report.errors << QStringLiteral("%1: %2").arg(rel, error);
            if (!unencodable.isEmpty())
                report.errors << QStringLiteral("%1: characters not representable in the target encoding: %2").arg(rel, unencodable);
        }
    }
    project.saveState();
    if (progress)
        progress(int(files.size()), int(files.size()));
    return report;
}

}
