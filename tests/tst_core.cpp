#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/CustomGlossary.h"
#include "core/DatArchive.h"
#include "core/DatWriter.h"
#include "core/DialogGraph.h"
#include "core/GameReport.h"
#include "core/Glossary.h"
#include "core/Int2Ssl.h"
#include "core/IntStrings.h"
#include "core/Lzss.h"
#include "core/MsgFile.h"
#include "core/Placeholders.h"
#include "core/OutputArchive.h"
#include "core/PipboyText.h"
#include "core/Project.h"
#include "core/ProjectSearch.h"
#include "core/QualityCheck.h"
#include "core/ScriptStrings.h"
#include "core/TermMatcher.h"
#include "core/TextCodec.h"
#include "core/TranslationIndex.h"
#include "core/TranslationMemory.h"
#include "core/VirtualFileSystem.h"

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    void lzssLiteralsAndMatch()
    {
        // 3 literals then a match (offset 4078 = start of the ring buffer, length 3).
        QByteArray block;
        block.append(char(0x07));
        block.append("abc");
        block.append(char(0xEE));
        block.append(char(0xF0));
        QByteArray stream;
        stream.append(char(0));
        stream.append(char(block.size()));
        stream.append(block);
        QString error;
        QCOMPARE(Lzss::decompress(stream, 6, &error), QByteArray("abcabc"));

        // Raw block (negative size)
        QByteArray raw;
        raw.append(char(0xFF));
        raw.append(char(0xFD)); // -3
        raw.append("xyz");
        QCOMPARE(Lzss::decompress(raw, 3), QByteArray("xyz"));

        // Truncated input is an error, not a crash
        QVERIFY(Lzss::decompress(stream, 7, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void msgRoundTrip_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("simple") << QStringLiteral("{100}{}{Hello}\r\n{101}{audio}{World}\r\n");
        QTest::newRow("comments") << QStringLiteral("# header\r\n\r\n{1}{}{a}\r\n# middle\r\n{2}{}{b}\r\n\r\n");
        QTest::newRow("multiline") << QStringLiteral("{5}{}{line one\r\nline two}\r\n");
        QTest::newRow("lf") << QStringLiteral("{5}{}{x}\n{6}{}{y}\n");
        QTest::newRow("spaces") << QStringLiteral("{7}{n}{ padded }\r\n");
    }
    void msgRoundTrip()
    {
        QFETCH(QString, text);
        const MsgFile f = MsgFile::parse(text);
        QVERIFY(f.warnings.isEmpty());
        QVERIFY(!f.entries.isEmpty());
        QCOMPARE(f.serialize(), text);
    }

    void msgParsing()
    {
        const MsgFile f = MsgFile::parse(QStringLiteral("junk\r\n{10}{}{a\r\nb}\r\n{20}{au}{c}\r\n{10}{}{dup}"));
        QCOMPARE(f.entries.size(), 3);
        QCOMPARE(f.entries.at(0).text, QStringLiteral("a\nb"));
        QCOMPARE(f.entries.at(1).audio, QStringLiteral("au"));
        QCOMPARE(f.indexById().value(10), 2); // last duplicate wins, like the game
        QVERIFY(f.crlf);
    }

    void msgBadRecordStops()
    {
        const MsgFile f = MsgFile::parse(QStringLiteral("{1}{}{ok}\r\n{2}{broken"));
        QCOMPARE(f.entries.size(), 1);
        QVERIFY(!f.warnings.isEmpty());
    }

    void codecsRoundTrip_data()
    {
        QTest::addColumn<QString>("id");
        QTest::newRow("cp1252") << "cp1252";
        QTest::newRow("cp1250") << "cp1250";
        QTest::newRow("cp1251") << "cp1251";
    }
    void codecsRoundTrip()
    {
        QFETCH(QString, id);
        QByteArray all;
        for (int i = 0; i < 256; ++i)
            all.append(char(i));
        const QString text = TextCodec::decode(all, id);
        QCOMPARE(text.size(), 256);
        QString bad;
        QCOMPARE(TextCodec::encode(text, id, &bad), all);
        QVERIFY(bad.isEmpty());
    }
    void codecsKnownValues()
    {
        QCOMPARE(TextCodec::decode(QByteArray("\xE9\x80", 2), QStringLiteral("cp1252")), QString::fromUtf8("é€"));
        QCOMPARE(TextCodec::decode(QByteArray("\xCF\xF0\xE8\xE2\xE5\xF2"), QStringLiteral("cp1251")),
                 QString::fromUtf8("Привет"));
        QCOMPARE(TextCodec::decode(QByteArray("\xA3\xB3\xBF"), QStringLiteral("cp1250")), QString::fromUtf8("Łłż"));
        QString bad;
        QCOMPARE(TextCodec::encode(QString::fromUtf8("a€Ж"), QStringLiteral("cp1252"), &bad), QByteArray("a\x80?"));
        QCOMPARE(bad, QString::fromUtf8("Ж"));
    }
    void codecDefaults()
    {
        QCOMPARE(TextCodec::defaultFor(QStringLiteral("russian")), QStringLiteral("cp1251"));
        QCOMPARE(TextCodec::defaultFor(QStringLiteral("french")), QStringLiteral("cp1252"));
        QCOMPARE(TextCodec::defaultFor(QStringLiteral("polish")), QStringLiteral("cp1250"));
    }

    void datRoundTrip_data()
    {
        QTest::addColumn<int>("format");
        QTest::newRow("dat2") << int(DatArchive::Format::Fallout2);
        QTest::newRow("dat1") << int(DatArchive::Format::Fallout1);
    }
    void datRoundTrip()
    {
        QFETCH(int, format);
        const auto fmt = DatArchive::Format(format);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QByteArray big;
        for (int i = 0; i < 2000; ++i)
            big += "{100}{}{Some quite repetitive text}\r\n";
        QVector<DatWriteItem> items;
        items.push_back({QStringLiteral("text\\english\\game\\skill.msg"), big, nullptr, -1});
        items.push_back({QStringLiteral("text/english/dialog/abel.msg"), QByteArray("{1}{}{Hi}"), nullptr, -1});
        items.push_back({QStringLiteral("root.txt"), QByteArray(), nullptr, -1});

        const QString path = dir.filePath(QStringLiteral("test.dat"));
        QString error;
        QVERIFY2(DatWriter::write(path, fmt, items, &error), qPrintable(error));

        auto archive = DatArchive::open(path, &error);
        QVERIFY2(archive, qPrintable(error));
        QCOMPARE(archive->format(), fmt);
        QCOMPARE(archive->entries().size(), 3);
        int i = archive->indexOf(QStringLiteral("text/english/game/skill.msg"));
        QVERIFY(i >= 0);
        QCOMPARE(archive->read(i, &error), big);
        i = archive->indexOf(QStringLiteral("text/english/dialog/abel.msg"));
        QCOMPARE(archive->read(i), QByteArray("{1}{}{Hi}"));
        QVERIFY(archive->indexOf(QStringLiteral("root.txt")) >= 0);
        if (fmt == DatArchive::Format::Fallout2)
            QVERIFY(archive->entries().at(archive->indexOf(QStringLiteral("text/english/game/skill.msg"))).compressed);

        // Copy entries across archives (same format: raw copy; different format: transcode).
        for (auto target : {DatArchive::Format::Fallout1, DatArchive::Format::Fallout2}) {
            QVector<DatWriteItem> copy;
            for (int k = 0; k < archive->entries().size(); ++k) {
                DatWriteItem item;
                item.path = archive->entries().at(k).rawPath;
                item.source = archive.get();
                item.sourceIndex = k;
                copy.push_back(item);
            }
            const QString copyPath = dir.filePath(QStringLiteral("copy.dat"));
            QVERIFY2(DatWriter::write(copyPath, target, copy, &error), qPrintable(error));
            auto again = DatArchive::open(copyPath, &error);
            QVERIFY2(again, qPrintable(error));
            QCOMPARE(again->format(), target);
            QCOMPARE(again->read(again->indexOf(QStringLiteral("text/english/game/skill.msg"))), big);
        }
    }

    void datRejectsGarbage()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bad.dat"));
        writeFile(path, QByteArray(100, 'x'));
        QString error;
        QVERIFY(!DatArchive::open(path, &error));
        QVERIFY(!error.isEmpty());
    }

    void realDatIfAvailable()
    {
        const QByteArray env = qgetenv("BABYLON_SAMPLE_DAT");
        if (env.isEmpty())
            QSKIP("BABYLON_SAMPLE_DAT not set");
        QString error;
        auto archive = DatArchive::open(QString::fromLocal8Bit(env), &error);
        QVERIFY2(archive, qPrintable(error));
        QVERIFY(!archive->entries().isEmpty());
        int compressed = 0;
        for (const DatEntry &e : archive->entries())
            compressed += e.compressed ? 1 : 0;
        qInfo() << "DAT format" << (archive->format() == DatArchive::Format::Fallout1 ? "Fallout 1" : "Fallout 2")
                << "entries" << archive->entries().size() << "compressed" << compressed;
        int ok = 0;
        const int limit = qEnvironmentVariableIntValue("BABYLON_SAMPLE_DAT_LIMIT") > 0 ? qEnvironmentVariableIntValue("BABYLON_SAMPLE_DAT_LIMIT") : 200;
        for (int i = 0; i < archive->entries().size() && ok < limit; ++i, ++ok) {
            const QByteArray data = archive->read(i, &error);
            QVERIFY2(data.size() == int(archive->entries().at(i).unpackedSize), qPrintable(archive->entries().at(i).rawPath + error));
        }
    }

    void realMsgRoundTripIfAvailable()
    {
        // Point BABYLON_SAMPLE_FOLDER at a folder holding text/<language>/**.msg: every file must survive
        // decode -> parse -> serialize -> encode byte for byte (when its line endings are consistent).
        const QString folder = qEnvironmentVariable("BABYLON_SAMPLE_FOLDER");
        if (folder.isEmpty())
            QSKIP("BABYLON_SAMPLE_FOLDER not set");
        VirtualFileSystem vfs;
        vfs.setSources({folder});
        const QStringList files = vfs.list("text/", ".msg");
        QVERIFY(!files.isEmpty());
        int checked = 0;
        for (const QString &path : files) {
            const QByteArray bytes = vfs.read(path);
            const QString lang = path.section('/', 1, 1);
            const QString codec = TextCodec::defaultFor(lang);
            const MsgFile parsed = MsgFile::parse(TextCodec::decode(bytes, codec));
            QVERIFY2(parsed.warnings.isEmpty(), qPrintable(path + ": " + parsed.warnings.join("; ")));
            const int crlf = int(bytes.count("\r\n")), lf = int(bytes.count('\n'));
            const bool mixed = crlf != 0 && crlf != lf;
            if (!mixed && !bytes.startsWith("\xEF\xBB\xBF"))
                QVERIFY2(TextCodec::encode(parsed.serialize(), codec) == bytes, qPrintable(path));
            ++checked;
        }
        qInfo() << "round-tripped" << checked << "message files";
    }

    void realInstallIfAvailable()
    {
        // Point BABYLON_SAMPLE_INSTALL at a Fallout folder (master.dat, critter.dat, patches...). Read only.
        const QString folder = qEnvironmentVariable("BABYLON_SAMPLE_INSTALL");
        if (folder.isEmpty())
            QSKIP("BABYLON_SAMPLE_INSTALL not set");
        const QString target = qEnvironmentVariable("BABYLON_SAMPLE_TARGET", "french");

        QElapsedTimer timer;
        timer.start();
        ProjectConfig c;
        c.id = "real";
        c.name = "real";
        c.sources = VirtualFileSystem::discoverSources(folder);
        c.outputDir = QDir::tempPath() + "/babylon_real_out";
        c.sourceLanguage = "english";
        c.targetLanguage = target;
        c.targetEncoding = TextCodec::defaultFor(target);
        Project p(c);
        QStringList errors;
        p.reload(&errors);
        qInfo() << "sources" << c.sources << "errors" << errors << "open ms" << timer.restart();
        QVERIFY(errors.isEmpty());
        qInfo() << "languages" << p.availableLanguages();

        const QStringList files = p.messageFiles();
        int game = 0, dialog = 0;
        for (const QString &f : files)
            (Project::kindOf(f) == FileKind::Game ? game : dialog) += Project::kindOf(f) == FileKind::Other ? 0 : 1;
        qInfo() << "message files" << files.size() << "game" << game << "dialog" << dialog;
        qInfo() << "first files:" << files.mid(0, 4) << "sample dialogs:" << files.mid(60, 8);
        QVERIFY(game > 0 && dialog > 0);

        timer.restart();
        const Glossary g = p.buildGlossary();
        qInfo() << "glossary terms" << g.terms().size() << "conflicts" << g.conflictCount() << "ms" << timer.restart();
        QVERIFY(!g.isEmpty());
        for (int i = 0; i < qMin<qsizetype>(12, g.terms().size()); ++i)
            qInfo() << "  " << g.terms().at(i).source << "->" << g.terms().at(i).targets << g.terms().at(i).category;

        timer.restart();
        FileStats total;
        int count = 0;
        p.computeStatistics([&](int, int, const FileStats &s) { total.add(s); ++count; });
        qInfo() << "stats files" << count << "lines" << total.lines << "untranslated" << total.untranslated
                << "words left" << total.wordsRemaining << "ms" << timer.elapsed();
        QVERIFY(total.lines > 1000);
    }

    void translationMemoryRealIfAvailable()
    {
        // BABYLON_TM_NEW / BABYLON_TM_OLD: two installs of the mod; the French of the old one is imported into the new
        // one (written to a temporary output folder; the installs are only read).
        const QString newInstall = qEnvironmentVariable("BABYLON_TM_NEW");
        const QString oldInstall = qEnvironmentVariable("BABYLON_TM_OLD");
        if (newInstall.isEmpty() || oldInstall.isEmpty())
            QSKIP("BABYLON_TM_NEW / BABYLON_TM_OLD not set");
        QTemporaryDir out;
        ProjectConfig c;
        c.id = "tmreal";
        c.name = "tmreal";
        c.sources = VirtualFileSystem::discoverSources(newInstall);
        c.outputDir = out.path();
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(out.path() + "/state.json");
        p.reload();

        FileStats before;
        for (const FileStats &s : p.computeStatistics())
            before.add(s);
        TranslationMemory::Memory memory;
        memory.sources = VirtualFileSystem::discoverSources(oldInstall);
        QElapsedTimer timer;
        timer.start();
        const auto report = TranslationMemory::import(p, memory, {});
        qInfo() << "memory import ms" << timer.elapsed() << "files" << report.files << "examined" << report.examined
                << "exact" << report.exact << "changed" << report.changed << "byText" << report.byText
                << "kept" << report.kept << "saved" << report.saved << "errors" << report.errors.mid(0, 3);
        Project q(c);
        q.setStateFile(out.path() + "/state.json");
        q.reload();
        FileStats after;
        for (const FileStats &s : q.computeStatistics())
            after.add(s);
        qInfo() << "untranslated before" << before.untranslated << "after" << after.untranslated << "of" << after.lines
                << "to review" << after.review;
        QVERIFY(after.untranslated < before.untranslated);
        QVERIFY(report.exact > 1000);
    }

    void translationMemoryImport()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        // the new version of the mod (English only)
        writeFile(root + "/new/text/english/dialog/a.msg",
                  "{1}{}{Hello there}\r\n{2}{}{Bye now}\r\n{5}{}{Thanks}\r\n{6}{}{Brand new line}\r\n{7}{}{Already done}\r\n");
        writeFile(root + "/new/text/french/dialog/a.msg", "{7}{}{D\xE9j\xE0 fait}\r\n");
        // the old version, translated
        writeFile(root + "/old/text/english/dialog/a.msg",
                  "{1}{}{Hello there}\r\n{2}{}{Bye}\r\n{9}{}{Thanks}\r\n{7}{}{Already done}\r\n");
        writeFile(root + "/old/text/french/dialog/a.msg",
                  "{1}{}{Bonjour}\r\n{2}{}{Salut}\r\n{9}{}{Merci}\r\n{7}{}{Ancienne version}\r\n");

        ProjectConfig c;
        c.id = "tm";
        c.name = "tm";
        c.sources = {root + "/new"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();

        TranslationMemory::Memory memory;
        memory.sources = {root + "/old"};
        const TranslationMemory::Report report = TranslationMemory::import(p, memory, {});
        QVERIFY2(report.errors.isEmpty(), qPrintable(report.errors.join("; ")));
        QCOMPARE(report.exact, 1);     // id 1
        QCOMPARE(report.changed, 1);   // id 2: source changed
        QCOMPARE(report.byText, 1);    // id 5 found under id 9
        QCOMPARE(report.kept, 1);      // id 7 already translated
        QCOMPARE(report.saved, 1);

        Project q(c);
        q.setStateFile(root + "/state.json");
        q.reload();
        const TranslationDoc doc = q.loadDocument("dialog/a.msg");
        auto row = [&](int id) -> const TranslationRow & {
            for (const TranslationRow &r : doc.rows)
                if (r.id == id)
                    return r;
            static TranslationRow none;
            return none;
        };
        QCOMPARE(row(1).target, QString("Bonjour"));
        QVERIFY(!row(1).review);
        QCOMPARE(row(2).target, QString("Salut"));
        QVERIFY(row(2).review);
        QCOMPARE(row(2).previous, QString("Salut"));
        QVERIFY(row(2).comment.contains("Bye"));       // the previous source is in the comment
        QCOMPARE(row(5).target, QString("Merci"));
        QCOMPARE(Project::statusOf(row(6)), SegmentStatus::Untranslated);   // nothing known: still to do
        QCOMPARE(row(7).target, QString::fromLatin1("D\xE9j\xE0 fait"));   // kept

        // overwrite mode replaces it
        TranslationMemory::Options overwrite;
        overwrite.overwrite = true;
        const auto again = TranslationMemory::import(p, memory, overwrite);
        QCOMPARE(again.kept, 0);
        QCOMPARE(q.loadDocument("dialog/a.msg").rows.size(), 5);
        Project r(c);
        r.setStateFile(root + "/state.json");
        r.reload();
        for (const TranslationRow &x : r.loadDocument("dialog/a.msg").rows)
            if (x.id == 7)
                QCOMPARE(x.target, QString("Ancienne version"));
    }

    void rowCommentsAndValidation()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg", "{1}{}{One}\r\n{2}{}{Two}\r\n");
        ProjectConfig c;
        c.id = "cv";
        c.name = "cv";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();
        TranslationDoc doc = p.loadDocument("dialog/a.msg");
        doc.rows[0].target = "Un";
        doc.rows[0].comment = "check the tone";
        doc.rows[0].validated = Project::textHash("Un");
        doc.rows[1].target = "Deux";
        doc.rows[1].validated = Project::textHash("Deux");
        QVERIFY(!doc.rows[0].changedSinceValidation());
        QVERIFY(p.saveDocument(doc));

        Project q(c);
        q.setStateFile(root + "/state.json");
        q.reload();
        TranslationDoc again = q.loadDocument("dialog/a.msg");
        QCOMPARE(again.rows[0].comment, QString("check the tone"));
        QVERIFY(!again.rows[0].changedSinceValidation());
        again.rows[1].target = "Deux, modifié";
        QVERIFY(again.rows[1].changedSinceValidation());
        QVERIFY(Project::textHash("a") != Project::textHash("b"));
        QCOMPARE(Project::textHash("same"), Project::textHash("same"));
    }

    void projectSearchAndReplace()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg",
                  "{1}{}{The Vault Dweller}\r\n{2}{}{Vault 13}\r\n{3}{}{Nothing}\r\n");
        writeFile(root + "/m/text/french/dialog/a.msg",
                  "{1}{}{L'Habitant de l'Abri}\r\n{2}{}{Abri 13 et abri 8}\r\n{3}{}{Rien du tout}\r\n");
        writeFile(root + "/m/text/english/dialog/b.msg", "{1}{}{Cat}\r\n");
        writeFile(root + "/m/text/french/dialog/b.msg", "{1}{}{Concatener le chat}\r\n");
        ProjectConfig c;
        c.id = "search";
        c.name = "search";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();

        ProjectSearch::Options o;
        o.text = "abri";
        // case-insensitive by default: three hits in two lines of a.msg
        auto hits = ProjectSearch::search(p, o, "Refuge");
        QCOMPARE(hits.size(), 2);
        int occurrences = 0;
        for (const auto &h : hits) {
            QVERIFY(h.inTarget);
            QVERIFY(!h.after.contains("bri", Qt::CaseInsensitive));
            occurrences += h.occurrences;
        }
        QCOMPARE(occurrences, 3);

        o.caseSensitive = true;
        QCOMPARE(ProjectSearch::search(p, o).size(), 1);          // only the lowercase one

        // whole words
        ProjectSearch::Options w;
        w.text = "chat";
        QCOMPARE(ProjectSearch::search(p, w).size(), 1);
        w.wholeWord = true;
        QCOMPARE(ProjectSearch::search(p, w).size(), 1);
        w.text = "cat";
        w.wholeWord = false;
        w.inSource = true;
        w.inTarget = true;
        QCOMPARE(ProjectSearch::search(p, w).size(), 2);          // "Cat" (source) and "Concatener" (target)
        w.wholeWord = true;
        const auto words = ProjectSearch::search(p, w);
        QCOMPARE(words.size(), 1);
        QVERIFY(!words.first().inTarget);

        // regex with a back-reference, and a plain replacement that contains a backslash and a dollar
        ProjectSearch::Options r;
        r.text = "(\\w+) (\\d+)";
        r.regex = true;
        QString text = "Abri 13";
        QCOMPARE(ProjectSearch::replaceIn(text, r, "\\2 \\1"), 1);
        QCOMPARE(text, QString("13 Abri"));
        ProjectSearch::Options plain;
        plain.text = "13";
        text = "Abri 13";
        ProjectSearch::replaceIn(text, plain, "\\1$1");
        QCOMPARE(text, QString("Abri \\1$1"));
        ProjectSearch::Options bad;
        bad.text = "(";
        bad.regex = true;
        QVERIFY(ProjectSearch::search(p, bad).isEmpty());

        // replacing in a document changes only the requested records
        TranslationDoc doc = p.loadDocument("dialog/a.msg");
        ProjectSearch::Options again;
        again.text = "abri";
        QCOMPARE(ProjectSearch::replaceInDocument(doc, again, "Refuge", {2}), 2);
        for (const TranslationRow &row : doc.rows) {
            if (row.id == 1)
                QCOMPARE(row.target, QString("L'Habitant de l'Abri"));
            if (row.id == 2)
                QCOMPARE(row.target, QString("Refuge 13 et Refuge 8"));
        }
        QVERIFY(p.saveDocument(doc));
        Project q(c);
        q.setStateFile(root + "/state.json");
        q.reload();
        QVERIFY(ProjectSearch::search(q, again, {}).size() == 1);

        // truncation
        bool truncated = false;
        ProjectSearch::Options any;
        any.text = "e";
        any.inSource = true;
        QCOMPARE(ProjectSearch::search(q, any, {}, {}, nullptr, 2, &truncated).size(), 2);
        QVERIFY(truncated);
    }

    // A tiny compiled script: no procedure, no identifier, two strings, code that pushes both.
    static QByteArray tinyScript(const QByteArray &first, const QByteArray &second)
    {
        auto be32 = [](quint32 v) {
            QByteArray b(4, '\0');
            qToBigEndian<quint32>(v, reinterpret_cast<uchar *>(b.data()));
            return b;
        };
        auto be16 = [](quint16 v) {
            QByteArray b(2, '\0');
            qToBigEndian<quint16>(v, reinterpret_cast<uchar *>(b.data()));
            return b;
        };
        auto entry = [&](const QByteArray &text) {
            const int allocated = (text.size() + 2) & ~1;
            return be16(quint16(allocated)) + text + QByteArray(allocated - text.size(), '\0');
        };
        QByteArray table = entry(first) + entry(second);
        const quint32 bodyOffset = 42 + 4 + 24 + 4 + 4 + 4 + quint32(table.size()) + 4;
        QByteArray out(42, '\0');
        out += be32(1);                       // one procedure: name, type, time, condition, body offset, arguments
        out += be32(0) + be32(0) + be32(0) + be32(0) + be32(bodyOffset) + be32(0);
        out += be32(0) + be32(0xFFFFFFFFu);   // identifiers (empty) and terminator
        out += be32(quint32(table.size())) + table + be32(0xFFFFFFFFu);
        const quint32 firstOperand = 4 + 2;                                   // from the size field
        const quint32 secondOperand = 4 + entry(first).size() + 2;
        out += be16(0x9001) + be32(firstOperand) + be16(0x9001) + be32(secondOperand) + be16(0x9001) + be32(firstOperand);
        out += be16(0x8019);                  // some operator without operand
        return out;
    }

    void intStringsParseAndPatch()
    {
        const QByteArray script = tinyScript("You see Sammy.", "Hurt dude");
        IntStrings::Table t = IntStrings::parse(script);
        QVERIFY2(t.valid, qPrintable(t.error));
        QVERIFY(t.codeDecoded);
        QCOMPARE(t.entries.size(), 2);
        QCOMPARE(t.entries[0].text, QByteArray("You see Sammy."));
        QCOMPARE(t.entries[0].capacity(), 15);       // 14 characters + NUL + padding = 16 bytes
        QCOMPARE(t.entries[1].text, QByteArray("Hurt dude"));
        QCOMPARE(t.usedOperands.size(), 2);
        QVERIFY(t.usedOperands.contains(t.entries[0].operand) && t.usedOperands.contains(t.entries[1].operand));
        QVERIFY(IntStrings::looksTranslatable("You see Sammy."));
        QVERIFY(!IntStrings::looksTranslatable("obj_name"));
        QVERIFY(!IntStrings::looksTranslatable("art/critters/hmwarr"));

        // fits in place: the file keeps its size and its layout
        QHash<quint32, QByteArray> texts;
        texts.insert(t.entries[0].operand, "Vous voyez Sammy.");        // 17 characters: too long for 15
        IntStrings::PatchResult r = IntStrings::patch(script, texts, false);
        QCOMPARE(r.data, script);
        QCOMPARE(r.problems.size(), 1);
        texts.insert(t.entries[0].operand, "C'est Sammy.");
        r = IntStrings::patch(script, texts, false);
        QCOMPARE(r.inPlace, 1);
        QCOMPARE(r.data.size(), script.size());
        t = IntStrings::parse(r.data);
        QVERIFY(t.valid);
        QCOMPARE(t.entries[0].text, QByteArray("C'est Sammy."));
        QCOMPARE(t.entries[1].text, QByteArray("Hurt dude"));
        QCOMPARE(t.usedOperands.size(), 2);

        // longer: appended at the end, both pushes of that string redirected
        texts.clear();
        texts.insert(t.entries[0].operand, "Vous voyez Sammy, la fille du forgeron.");
        r = IntStrings::patch(script, texts, true);
        QCOMPARE(r.enlarged, 1);
        QVERIFY(r.data.size() > script.size());
        const IntStrings::Table moved = IntStrings::parse(r.data);
        QVERIFY(moved.valid && moved.codeDecoded);
        QCOMPARE(moved.entries.size(), 2);                          // the table itself is unchanged
        QCOMPARE(moved.usedOperands.size(), 2);                     // both pushes of the first string moved together
        quint32 newOperand = 0;
        for (quint32 op : moved.usedOperands)
            if (op != moved.entries[1].operand)
                newOperand = op;
        QVERIFY(newOperand > moved.entries[1].operand);
        // the new entry sits right at the end of the file and is read like any other
        const int pos = int(moved.tablePos) + int(newOperand);
        QCOMPARE(r.data.mid(pos, 39), QByteArray("Vous voyez Sammy, la fille du forgeron."));
        QCOMPARE(r.data.at(pos + 39), '\0');
    }

    void scriptStringsProject()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg", "{1}{}{Hello}\r\n");
        writeFile(root + "/m/scripts/sammy.int", tinyScript("You see Sammy.", "Hurt dude"));
        ProjectConfig c;
        c.id = "ss";
        c.name = "ss";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state/ss.state.json");
        p.reload();

        QCOMPARE(p.scriptNames(), QStringList{"sammy"});
        QVector<ScriptStrings::Text> texts = ScriptStrings::scan(p);
        QCOMPARE(texts.size(), 2);
        QCOMPARE(texts[0].source, QString("You see Sammy."));
        QCOMPARE(texts[0].capacity, 15);

        // one text fits, one does not
        QVector<ScriptTranslation> translations;
        translations.push_back({"sammy", texts[0].operand, texts[0].source, "C'est Sammy."});
        translations.push_back({"sammy", texts[1].operand, texts[1].source, "Il s'est fait mal, le pauvre."});
        p.setScriptTranslations(translations);
        ScriptStrings::ApplyResult r = ScriptStrings::apply(p, p.scriptTranslations(), false);
        QCOMPARE(r.scripts, 1);
        QCOMPARE(r.inPlace, 1);
        QCOMPARE(r.problems.size(), 1);
        QVERIFY(QFileInfo::exists(root + "/out/scripts/sammy.int"));
        QVERIFY(QFileInfo::exists(root + "/state/ss.scripts/sammy.int"));      // the original is kept
        QFile patched(root + "/out/scripts/sammy.int");
        QVERIFY(patched.open(QIODevice::ReadOnly));
        const IntStrings::Table t = IntStrings::parse(patched.readAll());
        patched.close();                    // Windows cannot delete an open file
        QCOMPARE(t.entries[0].text, QByteArray("C'est Sammy."));
        QCOMPARE(t.entries[1].text, QByteArray("Hurt dude"));

        // the translations survive a reload of the project, and the original is still what is scanned
        Project q(c);
        q.setStateFile(root + "/state/ss.state.json");
        q.reload();
        QCOMPARE(q.scriptTranslations().size(), 2);
        QCOMPARE(q.scriptTranslations()[0].target, QString("C'est Sammy."));
        QCOMPARE(ScriptStrings::scan(q)[0].source, QString("You see Sammy."));

        // the output archive carries the translated script
        c.outputArchive = root + "/delivery/french.dat";
        Project d(c);
        d.setStateFile(root + "/state/ss.state.json");
        d.reload();
        const OutputArchive::Result built = OutputArchive::build(d);
        QVERIFY2(built.ok, qPrintable(built.error));
        QCOMPARE(built.scripts, 1);
        const auto dat = DatArchive::open(c.outputArchive);
        QVERIFY(dat && dat->indexOf("scripts/sammy.int") >= 0);

        // nothing left to say: the translated script goes away
        r = ScriptStrings::apply(p, {}, false);
        QCOMPARE(r.scripts, 0);
        QVERIFY(!QFileInfo::exists(root + "/out/scripts/sammy.int"));
    }

    void qualityAndSuggestionsRealIfAvailable()
    {
        // BABYLON_TM_OLD = an installation with a French translation (read only): quality report with its cache,
        // translation memory, texts of the scripts.
        const QString install = qEnvironmentVariable("BABYLON_TM_OLD");
        if (install.isEmpty())
            QSKIP("BABYLON_TM_OLD not set");
        QTemporaryDir dir;
        ProjectConfig c;
        c.id = "realqa";
        c.name = "realqa";
        c.sources = VirtualFileSystem::discoverSources(install);
        c.outputDir = dir.path() + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(dir.path() + "/state.json");
        p.reload();

        QElapsedTimer timer;
        timer.start();
        const auto glossary = std::make_shared<Glossary>(p.buildGlossary());
        qInfo() << "glossary ms" << timer.restart() << "terms" << glossary->terms().size();
        QualityCheck::Options o;
        const QString cacheFile = dir.path() + "/qa.json";
        const QString key = QualityCheck::cacheKey(p, glossary.get(), o);
        int hits = 0;
        QualityCheck::Cache first;
        first.open(cacheFile, key);
        const auto issues = QualityCheck::run(p, glossary.get(), o, {}, nullptr, &first, &hits);
        qInfo() << "quality, first run ms" << timer.restart() << "issues" << issues.size() << "hits" << hits;
        QCOMPARE(hits, 0);
        QualityCheck::Cache second;
        second.open(cacheFile, key);
        const auto again = QualityCheck::run(p, glossary.get(), o, {}, nullptr, &second, &hits);
        qInfo() << "quality, cached run ms" << timer.restart() << "issues" << again.size() << "hits" << hits;
        QCOMPARE(again.size(), issues.size());
        QVERIFY(hits > 100);

        const auto index = TranslationIndex::build(p);
        qInfo() << "translation memory ms" << timer.restart() << "lines" << index->size();
        QVERIFY(index->size() > 1000);
        const auto found = index->find("Welcome to Vault City. How may I help you today?", 5, 0.6);
        qInfo() << "query ms" << timer.restart() << "matches" << found.size();
        if (!found.isEmpty())
            qInfo() << "best:" << found.first().score << found.first().source << "->" << found.first().target;

        const auto scripts = ScriptStrings::scan(p);
        qInfo() << "script texts ms" << timer.restart() << "texts" << scripts.size();
    }

    void intStringsRealScripts()
    {
        const QString folder = qEnvironmentVariable("BABYLON_F1_SCRIPTS");
        if (folder.isEmpty())
            QSKIP("BABYLON_F1_SCRIPTS not set");
        const QFileInfoList files = QDir(folder).entryInfoList({"*.int", "*.INT"}, QDir::Files);
        QVERIFY(!files.isEmpty());
        int withStrings = 0, candidates = 0, outsideTable = 0, badBody = 0;
        QString sample;
        for (const QFileInfo &fi : files) {
            QFile f(fi.absoluteFilePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray bytes = f.readAll();
            const IntStrings::Table t = IntStrings::parse(bytes);
            QVERIFY2(t.valid, qPrintable(fi.fileName() + ": " + t.error));
            QVERIFY2(t.codeDecoded, qPrintable(fi.fileName() + ": code does not decode"));
            QSet<quint32> known;
            for (const auto &e : t.entries)
                known.insert(e.operand);
            for (quint32 used : t.usedOperands)
                if (!known.contains(used))
                    ++outsideTable;
            if (t.bodyPos < t.codePos || t.bodyPos > bytes.size()) {
                ++badBody;
                qInfo() << "odd body start in" << fi.fileName() << t.bodyPos << t.codePos << bytes.size();
            }
            if (!t.entries.isEmpty())
                ++withStrings;
            for (const auto &e : t.entries)
                if (IntStrings::looksTranslatable(e.text)) {
                    ++candidates;
                    if (sample.isEmpty())
                        sample = fi.fileName() + ": " + QString::fromLatin1(e.text);
                }
            // an unchanged text leaves the file as it was
            QHash<quint32, QByteArray> same;
            for (const auto &e : t.entries)
                same.insert(e.operand, e.text);
            QCOMPARE(IntStrings::patch(bytes, same, true).data, bytes);
        }
        qInfo() << "scripts" << files.size() << "with strings" << withStrings << "texts" << candidates << "e.g." << sample;
        QCOMPARE(badBody, 0);
        QCOMPARE(outsideTable, 0);                  // in the procedure bodies, every push designates a string of the table
        QVERIFY(candidates > 20);

        // end to end with the real decompiler: the patched script says what was written, in place or lengthened
        const QString path = QDir(folder).filePath("BARSTOW.INT");
        if (QFileInfo::exists(path) && !Int2Ssl::findExecutable().isEmpty()) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray original = f.readAll();
            const IntStrings::Table t = IntStrings::parse(original);
            QHash<quint32, QByteArray> texts;
            QByteArray shortOne, longOne;
            for (const auto &e : t.entries) {
                if (!IntStrings::looksTranslatable(e.text) || !t.usedOperands.contains(e.operand))
                    continue;
                if (longOne.isEmpty()) {
                    longOne = "Vous voyez Zack Barstow, un initie des Enfants de la Cathedrale, qui vous regarde avec mefiance.";
                    texts.insert(e.operand, longOne);
                } else if (shortOne.isEmpty() && e.capacity() >= 10) {
                    shortOne = "Bonjour !";
                    texts.insert(e.operand, shortOne);
                }
            }
            QVERIFY(!longOne.isEmpty() && !shortOne.isEmpty());
            QString error;
            const QString before = Int2Ssl::decompile(original, &error);

            // in place only: the real decompiler reads the new text, the long one is refused
            IntStrings::PatchResult r = IntStrings::patch(original, texts, false);
            QCOMPARE(r.inPlace, 1);
            QCOMPARE(r.enlarged, 0);
            QCOMPARE(r.problems.size(), 1);
            QCOMPARE(r.data.size(), original.size());
            QString after = Int2Ssl::decompile(r.data, &error);
            QVERIFY2(!after.isEmpty(), qPrintable(error));
            QVERIFY(after.contains(QString::fromLatin1(shortOne)));
            QVERIFY(!after.contains(QString::fromLatin1(longOne)));
            QVERIFY(!before.contains(QString::fromLatin1(shortOne)));

            // lengthened: appended after the code (tools that read the whole file as code, int2ssl included,
            // do not like that; the game only follows the pushes)
            r = IntStrings::patch(original, texts, true);
            QCOMPARE(r.enlarged, 1);
            const IntStrings::Table moved = IntStrings::parse(r.data);
            QVERIFY(moved.valid);
            QCOMPARE(moved.entries.size(), t.entries.size());
            const qint64 tail = r.data.size() - (2 + ((longOne.size() + 2) & ~1));
            QCOMPARE(r.data.mid(tail + 2, longOne.size()), longOne);
        }
    }

    void gameReportRemarks()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/abel.msg", "{100}{}{Hello there.}\r\n{101}{}{You are not welcome.}\r\n");
        writeFile(root + "/m/text/french/dialog/abel.msg", "{100}{}{Bonjour.}\r\n{101}{}{Vous n'etes pas le bienvenu.}\r\n");
        writeFile(root + "/m/text/english/game/misc.msg", "{200}{}{Hello there.}\r\n");
        writeFile(root + "/m/text/french/game/misc.msg", "{200}{}{Salut.}\r\n");
        ProjectConfig c;
        c.id = "gr";
        c.name = "gr";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();

        const QString report =
            "# written by the game\r\n"
            "dialog/abel.msg 101 the sentence is too rude\r\n"
            "abel.msg#100\r\n"
            "text/french/game/misc.msg:200 wrong word\r\n"
            "Vous n'etes pas le bienvenu\r\n"
            "Hello there\r\n"
            "dialog/abel.msg 999 no such line\r\n"
            "A text that exists nowhere\r\n"
            "\r\n";
        const auto remarks = GameReport::parse(report);
        QCOMPARE(remarks.size(), 7);
        QCOMPARE(remarks[0].relPath, QString("dialog/abel.msg"));
        QCOMPARE(remarks[0].id, 101);
        QCOMPARE(remarks[0].note, QString("the sentence is too rude"));
        QCOMPARE(remarks[1].relPath, QString("abel.msg"));
        QCOMPARE(remarks[2].relPath, QString("game/misc.msg"));        // text/<language>/ prefix dropped
        QVERIFY(remarks[3].relPath.isEmpty());                          // free text

        const auto outcome = GameReport::resolve(p, remarks, 1);
        QStringList found;
        for (const auto &r : outcome.found)
            found << QString("%1:%2").arg(r.relPath).arg(r.id);
        found.sort();
        // 101 and 100 by id, misc 200 by id, the rude sentence found by its text (translation)
        QVERIFY(found.contains("dialog/abel.msg:101"));
        QVERIFY(found.contains("dialog/abel.msg:100"));
        QVERIFY(found.contains("game/misc.msg:200"));
        QCOMPARE(outcome.notFound.size(), 2);                           // id 999, the unknown text
        QCOMPARE(outcome.ambiguous.size(), 1);                          // "Hello there" is the source of two lines (> 1)

        // the marks only touch the saved state: no .msg file is written
        QVector<ReviewMark> marks;
        for (const auto &r : outcome.found)
            marks.push_back({r.relPath, r.id, r.comment});
        QVERIFY(p.markForReview(marks) > 0);
        QCOMPARE(p.markForReview(marks), 0);                            // idempotent
        QVERIFY(!QFileInfo::exists(root + "/out/text/french/dialog/abel.msg"));
        const TranslationDoc doc = p.loadDocument("dialog/abel.msg");
        QVERIFY(doc.rows[1].review);
        QVERIFY(doc.rows[1].comment.contains("too rude"));
        QVERIFY(doc.rows[1].comment.contains("Game report"));
        Project again(c);
        again.setStateFile(root + "/state.json");
        again.reload();
        QVERIFY(again.loadDocument("dialog/abel.msg").rows[1].review);   // persisted
    }

    void translationIndexSuggestions()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg",
                  "{1}{}{You need a Stimpak to heal your wounds.}\r\n"
                  "{2}{}{The Vault Dweller returns home.}\r\n"
                  "{3}{}{Goodbye.}\r\n"
                  "{4}{}{Nothing yet.}\r\n");
        writeFile(root + "/m/text/french/dialog/a.msg",
                  "{1}{}{Il vous faut un Stimpak pour soigner vos blessures.}\r\n"
                  "{2}{}{L'Habitant de l'Abri rentre chez lui.}\r\n"
                  "{3}{}{Au revoir.}\r\n");
        writeFile(root + "/m/text/english/dialog/b.msg",
                  "{1}{}{You need a Stimpak to heal your wounds.}\r\n"
                  "{2}{}{You need a Super Stimpak to heal your wounds.}\r\n"
                  "{3}{}{Goodbye.}\r\n");
        writeFile(root + "/m/text/french/dialog/b.msg", "{1}{}{Il vous faut un Stimpak pour soigner vos blessures.}\r\n");
        ProjectConfig c;
        c.id = "ti";
        c.name = "ti";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();

        const auto index = TranslationIndex::build(p);
        QCOMPARE(index->size(), 4);       // a.msg 1-3 and b.msg 1 (untranslated lines are not indexed)

        // the same text elsewhere: an exact suggestion; the line itself is not offered
        auto found = index->find("You need a Stimpak to heal your wounds.", 5, 0.7, "dialog/b.msg", 2);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().score, 1.0);
        QCOMPARE(found.first().target, QString("Il vous faut un Stimpak pour soigner vos blessures."));
        QCOMPARE(found.first().count, 2);       // translated the same way in two files

        // a close text: a fuzzy suggestion below 100 %
        found = index->find("You need a Super Stimpak to heal your wounds.", 5, 0.7, "dialog/b.msg", 2);
        QVERIFY(!found.isEmpty());
        QVERIFY(found.first().score < 1.0 && found.first().score > 0.7);
        QCOMPARE(found.first().source, QString("You need a Stimpak to heal your wounds."));

        // nothing similar, and a line asking about itself
        QVERIFY(index->find("Completely different sentence about robots.", 5, 0.7).isEmpty());
        QVERIFY(index->find("The Vault Dweller returns home.", 5, 0.7, "dialog/a.msg", 2).isEmpty());

        // a saved file is taken into account without rebuilding everything
        TranslationDoc doc = p.loadDocument("dialog/a.msg");
        doc.rows[3].target = "Rien pour l'instant.";
        index->updateFile(doc);
        QCOMPARE(index->size(), 5);
        found = index->find("Nothing yet.", 5, 0.7);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().target, QString("Rien pour l'instant."));
        doc.rows[3].target.clear();
        index->updateFile(doc);
        QVERIFY(index->find("Nothing yet.", 5, 0.7).isEmpty());

        QCOMPARE(TranslationIndex::similarity("abc", "abc"), 1.0);
        QVERIFY(TranslationIndex::similarity("kitten", "sitting") < 0.6);
    }

    void msgRepair()
    {
        const QString broken =
            "# header\r\n"
            "{100}{}{Fine line}\r\n"
            "{101}{Two fields}\r\n"
            "{102}{}{Missing the closing brace\r\n"
            "{103}{}Missing the opening brace}\r\n"
            "{abc}{}{Not a number}\r\n"
            "\r\n"
            "{104}{}{Multi\r\nline is fine}\r\n"
            "{105}{}{Last one}\r\n";
        const MsgFile before = MsgFile::parse(broken);
        QVERIFY(!before.warnings.isEmpty());

        const MsgRepair::Result r = MsgRepair::repair(broken);
        QVERIFY(r.changed());
        QCOMPARE(r.unrepaired, 0);
        QCOMPARE(r.fixes.size(), 4);
        const MsgFile after = MsgFile::parse(r.text);
        QVERIFY2(after.warnings.isEmpty(), qPrintable(after.warnings.join("; ")));
        QVector<int> ids;
        for (const MsgEntry &e : after.entries)
            ids << e.id;
        QCOMPARE(ids, (QVector<int>{100, 101, 102, 103, 104, 105}));
        auto text = [&](int id) {
            for (const MsgEntry &e : after.entries)
                if (e.id == id)
                    return e.text;
            return QString("<none>");
        };
        QCOMPARE(text(101), QString("Two fields"));
        QCOMPARE(text(102), QString("Missing the closing brace"));
        QCOMPARE(text(103), QString("Missing the opening brace"));
        QCOMPARE(text(104), QString("Multi\nline is fine"));
        QVERIFY(r.text.contains("# [abc][][Not a number]"));   // kept, out of the game's way
        QVERIFY(r.text.contains("\r\n"));                       // line endings preserved
        QVERIFY(r.text.startsWith("# header"));

        // a healthy file is left alone, byte for byte
        const QString fine = "{1}{}{One}\r\n\r\n# note\r\n{2}{}{Two\r\nlines}\r\n";
        const MsgRepair::Result same = MsgRepair::repair(fine);
        QVERIFY(!same.changed());
        QVERIFY(same.fixes.isEmpty());
        QCOMPARE(same.text, fine);

        // through a project: the corrected translation is written next to the sources and the original kept
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg", "{1}{}{One}\r\n{2}{}{Two}\r\n{3}{}{Three}\r\n");
        writeFile(root + "/out/text/french/dialog/a.msg", "{1}{}{Un}\r\n{2}{Deux}\r\n{3}{}{Trois}\r\n");
        ProjectConfig c;
        c.id = "rep";
        c.name = "rep";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();
        TranslationDoc doc = p.loadDocument("dialog/a.msg");
        QVERIFY(!doc.targetWarnings.isEmpty());
        QCOMPARE(doc.rows[2].target, QString());                // record 3 is lost behind the broken one
        QString error;
        const MsgRepair::Result fixed = p.repairTargetFile("dialog/a.msg", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(fixed.fixes.size(), 1);
        QVERIFY(QFileInfo::exists(root + "/out/text/french/dialog/a.msg.bak"));
        doc = p.loadDocument("dialog/a.msg");
        QVERIFY(doc.targetWarnings.isEmpty());
        QCOMPARE(doc.rows[1].target, QString("Deux"));
        QCOMPARE(doc.rows[2].target, QString("Trois"));
    }

    void outputArchiveBuild()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg", "{1}{}{Hello}\r\n{2}{}{Bye}\r\n");
        writeFile(root + "/m/text/english/dialog/b.msg", "{1}{}{Other}\r\n");
        ProjectConfig c;
        c.id = "oa";
        c.name = "oa";
        c.sources = {root + "/m"};
        c.outputDir = root + "/work";
        c.outputArchive = root + "/out/french.dat";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();

        // nothing saved yet: an empty archive is still valid
        QVERIFY(OutputArchive::problemWith(c).isEmpty());
        TranslationDoc doc = p.loadDocument("dialog/a.msg");
        doc.rows[0].target = "Bonjour";
        doc.rows[1].target = "Au revoir";
        QVERIFY(p.saveDocument(doc));

        // with the original texts (default): translated file + its reference, but not the untouched b.msg
        OutputArchive::Result r = OutputArchive::build(p);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.translated, 1);
        QCOMPARE(r.originals, 1);
        {
            const auto dat = DatArchive::open(c.outputArchive);
            QVERIFY(dat);
            QCOMPARE(int(dat->format()), int(DatArchive::Format::Fallout2));
            QCOMPARE(dat->entries().size(), 2);
            const int fr = dat->indexOf("text/french/dialog/a.msg");
            const int en = dat->indexOf("text/english/dialog/a.msg");
            QVERIFY(fr >= 0 && en >= 0);
            QVERIFY(dat->read(fr).contains("{1}{}{Bonjour}"));
            QVERIFY(dat->read(en).contains("{1}{}{Hello}"));
            QCOMPARE(dat->indexOf("text/english/dialog/b.msg"), -1);
        }

        // without the originals, in the Fallout 1 format
        ProjectConfig c1 = c;
        c1.archiveWithSource = false;
        c1.outputArchiveFormat = "dat1";
        c1.outputArchive = root + "/out/french1.dat";
        Project p1(c1);
        p1.setStateFile(root + "/state1.json");
        p1.reload();
        r = OutputArchive::build(p1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.originals, 0);
        {
            const auto dat = DatArchive::open(c1.outputArchive);
            QVERIFY(dat);
            QCOMPARE(int(dat->format()), int(DatArchive::Format::Fallout1));
            QCOMPARE(dat->entries().size(), 1);
            QVERIFY(dat->read(dat->indexOf("text/french/dialog/a.msg")).contains("{2}{}{Au revoir}"));
        }
        QCOMPARE(int(OutputArchive::detectFormat({c1.outputArchive})), int(DatArchive::Format::Fallout1));
        QCOMPARE(int(OutputArchive::detectFormat({root + "/m"})), int(DatArchive::Format::Fallout2));

        // a new computer: the working folder is restored from the archive (translations only, never overwritten)
        ProjectConfig again = c;
        again.outputDir = root + "/work2";
        QCOMPARE(OutputArchive::seedWorkingFolder(again), 1);
        QVERIFY(QFileInfo::exists(root + "/work2/text/french/dialog/a.msg"));
        QVERIFY(!QFileInfo::exists(root + "/work2/text/english"));
        QCOMPARE(OutputArchive::seedWorkingFolder(again), 0);

        // the archive can never be one of the sources
        ProjectConfig bad = c;
        bad.sources = {c.outputArchive};
        QVERIFY(!OutputArchive::problemWith(bad).isEmpty());
        bad = c;
        bad.outputArchive = root + "/out/french.txt";
        QVERIFY(!OutputArchive::problemWith(bad).isEmpty());
    }

    void customGlossary()
    {
        // CSV round trip, with quotes, semicolons and a do-not-translate term
        QVector<CustomTerm> terms;
        terms.push_back({"Vault Dweller", "Habitant de l'Abri", false});
        terms.push_back({"Brotherhood; of Steel", "Confr\xC3\xA9rie \"Acier\"", false});
        terms.push_back({"Pip-Boy", "Pip-Boy", true});
        const QString csv = CustomGlossary::toCsv(terms);
        int skipped = -1;
        const QVector<CustomTerm> back = CustomGlossary::fromCsv(csv, &skipped);
        QCOMPARE(back.size(), 3);
        QCOMPARE(skipped, 0);
        QCOMPARE(back[1].source, QString("Brotherhood; of Steel"));
        QCOMPARE(back[1].target, QString::fromUtf8("Confr\xC3\xA9rie \"Acier\""));
        QVERIFY(back[2].keep);

        // other separators, no header, a lone term, a blank line
        const auto tabs = CustomGlossary::fromCsv("Nuka-Cola\tNuka-Cola\nStimpak\tStimpak\n\nCaps\tcapsules\nRad-X", &skipped);
        QCOMPARE(tabs.size(), 4);
        QVERIFY(tabs[0].keep);
        QVERIFY(!tabs[2].keep);
        QVERIFY(tabs[3].keep);
        const auto commas = CustomGlossary::fromCsv("term,translation\nCaps,capsules\n", nullptr);
        QCOMPARE(commas.size(), 1);

        // merging replaces by term, ignoring case and spaces
        const auto merged = CustomGlossary::merge(terms, {{"vault  dweller", "Habitant du Vault", false}, {"Ghoul", "Goule", false}});
        QCOMPARE(merged.size(), 4);
        QCOMPARE(merged[0].target, QString("Habitant du Vault"));

        // the terms are saved with the project and beat the translations of the game files
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/game/pro_item.msg", "{100}{}{Stimpak}\r\n{101}{}{Super Stimpak}\r\n");
        writeFile(root + "/m/text/french/game/pro_item.msg", "{100}{}{Stimpak}\r\n{101}{}{Super Stimpak}\r\n");
        ProjectConfig c;
        c.id = "cg";
        c.name = "cg";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();
        p.setCustomTerms({{"Stimpak", "Seringue", false}, {"Radscorpion", "Radscorpion", true}});

        Project q(c);
        q.setStateFile(root + "/state.json");
        q.reload();
        QCOMPARE(q.customTerms().size(), 2);
        const Glossary g = q.buildGlossary();
        const GlossaryTerm *stimpak = nullptr, *scorpion = nullptr;
        for (const GlossaryTerm &t : g.terms()) {
            if (t.source == "Stimpak")
                stimpak = &t;
            if (t.source == "Radscorpion")
                scorpion = &t;
        }
        QVERIFY(stimpak && scorpion);
        QVERIFY(stimpak->custom);
        QCOMPARE(stimpak->targets, QStringList{"Seringue"});
        QVERIFY(!stimpak->conflict());
        QVERIFY(scorpion->keep);
        // the consistency check uses the custom translation
        QCOMPARE(g.checkConsistency("Use a Stimpak", "Utilisez un Stimpak").size(), 1);
        QCOMPARE(g.checkConsistency("Use a Stimpak", "Utilisez une seringue").size(), 0);
        QCOMPARE(g.checkConsistency("A Radscorpion", "Un scorpion").size(), 1);
    }

    void localGlossarySubstitution()
    {
        Glossary g;
        g.add("Vault Dweller", "Habitant de l'Abri", "Misc", "a#1");
        g.add("Stimpak", "Stimpak", "Item", "b#1");
        g.add("Water", "Eau", "Item", "b#2");
        g.add("Chip", "Puce", "Item", "b#3");
        g.add("Chip", "Jeton", "Item", "b#4");       // two translations: never forced
        g.addCustom("caps", "capsules", false, "Custom");
        g.finalize("english", "french");

        // multi-word names, capitalised names (not at the start of a sentence) and custom terms are protected
        const auto p = Placeholders::protect("The Vault Dweller found a Stimpak and 5 caps for @Name@. Water is rare.", &g);
        QVERIFY(p.tokens.contains("Habitant de l'Abri"));
        QVERIFY(p.tokens.contains("Stimpak"));
        QVERIFY(p.tokens.contains("capsules"));
        QVERIFY(p.tokens.contains("@Name@"));
        QVERIFY(!p.tokens.contains("Eau"));            // common word at the start of a sentence
        QVERIFY(!p.tokens.contains("Puce") && !p.tokens.contains("Jeton"));
        QVERIFY(p.xml.contains("Water is rare"));

        // what the service sends back is restored with the translations of the project
        QString restored;
        QString answer = p.xml;
        QVERIFY(Placeholders::restore(answer, p.tokens, &restored));
        QVERIFY(restored.contains("Habitant de l'Abri"));
        QVERIFY(restored.contains("5 capsules"));
        QVERIFY(restored.contains("@Name@"));

        // without a glossary only the real tokens are protected
        QCOMPARE(Placeholders::protect("The Vault Dweller has %d caps", nullptr).tokens, QStringList{"%d"});

        // exact form only: a plural is left to the service
        const auto plural = Placeholders::protect("Two Stimpaks here", &g);
        QVERIFY(plural.tokens.isEmpty());
    }

    void qualityChecks()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/m/text/english/dialog/a.msg",
                  "{1}{}{Hello @Name@, you have %d caps}\r\n"
                  "{2}{}{Plain sentence here}\r\n"
                  "{3}{}{Short}\r\n"
                  "{4}{}{This quest is really difficult to finish}\r\n"
                  "{5}{}{This quest is really difficult to finish}\r\n"
                  "{6}{}{Untouched line}\r\n");
        writeFile(root + "/m/text/french/dialog/a.msg",
                  "{1}{}{Bonjour, vous avez %d capsules}\r\n"
                  "{2}{}{Phrase  simple ici }\r\n"
                  "{3}{}{Court}\r\n"
                  "{4}{}{Cette qu\xEAte est vraiment difficile \xE0 terminer}\r\n"
                  "{5}{}{Cette mission est vraiment difficile \xE0 terminer}\r\n");
        ProjectConfig c;
        c.id = "qa";
        c.name = "qa";
        c.sources = {root + "/m"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        p.reload();

        QualityCheck::Options o;
        const auto issues = QualityCheck::run(p, nullptr, o);
        auto has = [&](int id, QualityCheck::Kind kind) {
            for (const auto &i : issues)
                if (i.id == id && i.kind == kind)
                    return true;
            return false;
        };
        QVERIFY(has(1, QualityCheck::Tokens));        // @Name@ lost
        QVERIFY(!has(2, QualityCheck::Tokens));
        QVERIFY(has(2, QualityCheck::Spacing));       // double and trailing space
        QVERIFY(!has(3, QualityCheck::Encoding));
        QVERIFY(has(4, QualityCheck::Duplicates) != has(5, QualityCheck::Duplicates));   // one of the two is the odd one

        // a character typed in the editor that cp1252 cannot hold
        TranslationDoc doc = p.loadDocument("dialog/a.msg");
        for (TranslationRow &row : doc.rows)
            if (row.id == 3)
                row.target = QString::fromUtf8("Court \xE2\x89\xA0");   // not equal sign
        bool found = false;
        for (const auto &i : QualityCheck::checkDocument(doc, p.config(), nullptr, o))
            found = found || (i.id == 3 && i.kind == QualityCheck::Encoding);
        QVERIFY(found);
        for (const auto &i : issues)
            QVERIFY(i.id != 6);                        // untranslated lines are not reported here

        // the cache: a second run reads nothing, and gives the same answer
        {
            const QString cacheFile = root + "/qa.cache.json";
            const QString key = QualityCheck::cacheKey(p, nullptr, o);
            int hits = -1;
            QualityCheck::Cache first;
            first.open(cacheFile, key);
            const auto fresh = QualityCheck::run(p, nullptr, o, {}, nullptr, &first, &hits);
            QCOMPARE(hits, 0);
            QCOMPARE(fresh.size(), issues.size());

            QualityCheck::Cache second;
            second.open(cacheFile, key);
            QVERIFY(second.size() > 0);
            const auto cached = QualityCheck::run(p, nullptr, o, {}, nullptr, &second, &hits);
            QCOMPARE(hits, 1);                       // the project has a single message file
            QCOMPARE(cached.size(), fresh.size());
            for (int i = 0; i < cached.size(); ++i) {
                QCOMPARE(cached[i].id, fresh[i].id);
                QCOMPARE(int(cached[i].kind), int(fresh[i].kind));
                QCOMPARE(cached[i].message, fresh[i].message);
                QCOMPARE(cached[i].target, fresh[i].target);
            }

            // a changed file is read again
            TranslationDoc edited = p.loadDocument("dialog/a.msg");
            edited.rows[1].target = "Phrase simple ici";
            QVERIFY(p.saveDocument(edited));
            QualityCheck::Cache third;
            third.open(cacheFile, key);
            QualityCheck::run(p, nullptr, o, {}, nullptr, &third, &hits);
            QCOMPARE(hits, 0);

            // other checks, other cache
            QualityCheck::Options narrow = o;
            narrow.kinds = QualityCheck::Tokens;
            QualityCheck::Cache other;
            other.open(cacheFile, QualityCheck::cacheKey(p, nullptr, narrow));
            QCOMPARE(other.size(), 0);
        }

        // a restricted selection
        QualityCheck::Options only;
        only.kinds = QualityCheck::Tokens;
        for (const auto &i : QualityCheck::run(p, nullptr, only))
            QCOMPARE(int(i.kind), int(QualityCheck::Tokens));
    }

    void statisticsCache()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        writeFile(root + "/mod/text/english/dialog/a.msg", "{1}{}{Hello there}\r\n{2}{}{Bye}\r\n");
        writeFile(root + "/mod/text/english/dialog/b.msg", "{1}{}{Another one}\r\n");
        writeFile(root + "/mod/text/french/dialog/a.msg", "{1}{}{Bonjour}\r\n{2}{}{Salut}\r\n");
        ProjectConfig c;
        c.id = "cache";
        c.name = "cache";
        c.sources = {root + "/mod"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";

        {
            Project p(c);
            p.setStatsCacheFile(root + "/stats.json");
            p.reload();
            const auto first = p.computeStatistics();
            QCOMPARE(first.size(), 2);
            QCOMPARE(p.statsCacheHits(), 0);
        }
        {
            Project p(c);
            p.setStatsCacheFile(root + "/stats.json");
            p.reload();
            const auto second = p.computeStatistics();
            QCOMPARE(p.statsCacheHits(), 2);   // nothing changed: both files come from the cache
            QCOMPARE(second.first().lines + second.last().lines, 3);
        }
        // a file changes: only that one is recomputed
        QTest::qWait(30);
        writeFile(root + "/mod/text/french/dialog/a.msg", "{1}{}{Bonjour}\r\n{2}{}{Salut toi, c'est long}\r\n");
        {
            Project p(c);
            p.setStatsCacheFile(root + "/stats.json");
            p.reload();
            p.computeStatistics();
            QCOMPARE(p.statsCacheHits(), 1);
        }
        // another encoding invalidates everything
        c.targetEncoding = "cp1250";
        {
            Project p(c);
            p.setStatsCacheFile(root + "/stats.json");
            p.reload();
            p.computeStatistics();
            QCOMPARE(p.statsCacheHits(), 0);
        }
    }

    void installWithExtractedArchives()
    {
        // master.dat / critter.dat extracted into folders of the same name must be used like archive files
        QTemporaryDir dir;
        const QString inst = dir.path() + "/F2";
        writeFile(inst + "/master.dat/text/english/game/skill.msg", "{100}{}{Small Guns}\r\n");
        writeFile(inst + "/master.dat/scripts/abel.int", "x");
        writeFile(inst + "/critter.dat/text/english/game/skill.msg", "{100}{}{Small Guns (critter)}\r\n");
        writeFile(inst + "/patch000.dat/text/english/game/pipboy.msg", "{100}{}{Back}\r\n");
        writeFile(inst + "/data/text/english/game/skill.msg", "{100}{}{Small Guns (patch)}\r\n");
        writeFile(inst + "/mods/mods_order.txt", "b.dat\r\na.dat\r\n");
        writeFile(inst + "/mods/a.dat/text/english/game/x.msg", "{1}{}{a}\r\n");
        writeFile(inst + "/mods/b.dat/text/english/game/x.msg", "{1}{}{b}\r\n");

        const QStringList sources = VirtualFileSystem::discoverSources(inst);
        auto name = [](const QString &p) { return QFileInfo(p).fileName(); };
        QStringList names;
        for (const QString &s : sources)
            names << name(s);
        QCOMPARE(names, (QStringList{"master.dat", "critter.dat", "patch000.dat", "data", "b.dat", "a.dat"}));

        VirtualFileSystem vfs;
        QStringList errors;
        vfs.setSources(sources, &errors);
        QVERIFY(errors.isEmpty());
        QVERIFY(vfs.exists("scripts/abel.int"));
        QVERIFY(vfs.exists("text/english/game/pipboy.msg"));
        // priority: master < critter < patch < data (the later one wins)
        QCOMPARE(QString::fromLatin1(vfs.read("text/english/game/skill.msg")), QString("{100}{}{Small Guns (patch)}\r\n"));
        // mods_order.txt: the last listed mod wins
        QCOMPARE(QString::fromLatin1(vfs.read("text/english/game/x.msg")), QString("{1}{}{a}\r\n"));
    }

    void dialogGraphFromSsl()
    {
        const QString ssl = QStringLiteral(
            "procedure start;\n"
            "procedure talk_p_proc;\n"
            "procedure Node00;\n procedure Node01;\n procedure Node02;\n procedure Combat;\n"
            "// a comment with procedure Fake begin\n"
            "procedure talk_p_proc\nbegin\n"
            "   start_gdialog(359, self_obj, 4, -1, -1);\n"
            "   gsay_start;\n"
            "   if (local_var(4) == 1) then begin\n      call Combat();\n   end\n"
            "   else begin\n      call Node00();\n   end\n"
            "   gsay_end;\n"
            "end\n"
            "procedure Node00\nbegin\n"
            "   gsay_reply(359, 102);\n"
            "   giq_option(-3, 359, 103, Node01, 0);\n"
            "   giq_option(4, 359, 104, Node02, 0);\n"
            "   giq_option(5, 359, 105, Combat, 0);\n"
            "end\n"
            "procedure Node01\nbegin\n"
            "   gsay_reply(359, 106 + 0);\n"
            "   gsay_reply(359, 107);\n"
            "   giq_option(-3, 359, 108, Node02, 0);\n"
            "end\n"
            "procedure Node02\nbegin\n"
            "   gsay_message(359, 110, 0);\n"
            "   call Combat();\n"
            "end\n"
            "procedure Combat\nbegin\n"
            "   field_change := \"begin end\";\n"
            "   call Combat();\n"
            "end\n");
        const DialogGraph g = DialogGraph::parseSsl(ssl);
        QVERIFY(g.hasDialogue());
        QCOMPARE(g.roots(), QStringList{"talk_p_proc"});
        QVERIFY(g.proc("Node00"));
        QCOMPARE(g.proc("Node00")->events.size(), 4);
        QCOMPARE(g.proc("Node00")->events.at(1).iq, -3);
        QCOMPARE(g.proc("Node00")->events.at(1).target, QStringLiteral("Node01"));
        QCOMPARE(g.proc("Node01")->events.at(0).msgId, -1); // not a literal: kept but unnumbered
        QVERIFY(g.proc("Combat")->events.size() == 1);       // strings do not break procedure boundaries
        QVERIFY(!g.proc("Fake"));

        const QVector<DialogNode> tree = g.buildTree();
        QCOMPARE(tree.size(), 1);
        const DialogNode &root = tree.first();
        QCOMPARE(root.proc, QStringLiteral("talk_p_proc"));
        // talk_p_proc -> call Combat, call Node00
        QCOMPARE(root.children.size(), 2);
        QCOMPARE(root.children.at(0).proc, QStringLiteral("Combat"));
        const DialogNode &node00 = root.children.at(1);   // Call Node00
        QCOMPARE(node00.type, DialogNode::Call);
        QCOMPARE(node00.children.size(), 1);               // its NPC reply (102)
        const DialogNode &reply = node00.children.first();
        QCOMPARE(reply.type, DialogNode::Npc);
        QCOMPARE(reply.msgId, 102);
        QCOMPARE(reply.children.size(), 3);                // 3 player options
        QCOMPARE(reply.children.at(0).type, DialogNode::Player);
        QCOMPARE(reply.children.at(0).msgId, 103);
        // option 103 -> Node01 (expanded here), whose option 108 -> Node02
        QVERIFY(!reply.children.at(0).children.isEmpty());
        // option 104 -> Node02, already expanded under 108: a reference
        QCOMPARE(reply.children.at(1).children.first().type, DialogNode::Reference);
        // option 105 -> Combat, already shown first: a reference
        QCOMPARE(reply.children.at(2).children.first().type, DialogNode::Reference);
        QCOMPARE(g.usedMessageIds(), (QVector<int>{102, 103, 104, 105, 107, 108, 110}));
    }

    void int2sslIfAvailable()
    {
        // BABYLON_SAMPLE_DAT=master.dat of a Fallout 2 install: decompiles scripts/abel.int and reads its tree.
        const QString datPath = qEnvironmentVariable("BABYLON_SAMPLE_DAT");
        if (datPath.isEmpty() || Int2Ssl::findExecutable().isEmpty())
            QSKIP("BABYLON_SAMPLE_DAT or int2ssl not available");
        QString error;
        auto archive = DatArchive::open(datPath, &error);
        QVERIFY2(archive, qPrintable(error));
        const int index = archive->indexOf(QStringLiteral("scripts/abel.int"));
        if (index < 0)
            QSKIP("scripts/abel.int is not in this archive");
        const QString ssl = Int2Ssl::decompile(archive->read(index), &error);
        QVERIFY2(!ssl.isEmpty(), qPrintable(error));
        const DialogGraph g = DialogGraph::parseSsl(ssl);
        QVERIFY(g.hasDialogue());
        QCOMPARE(g.roots(), QStringList{"talk_p_proc"});
        QVERIFY(g.usedMessageIds().contains(102));
        QVERIFY(g.usedMessageIds().contains(118));
        QVERIFY(!g.buildTree().isEmpty());
    }

    void fallout1ScriptsIfAvailable()
    {
        // BABYLON_F1_SCRIPTS = folder of Fallout 1 .int scripts (read only): decompile and read the dialogue trees.
        const QString folder = qEnvironmentVariable("BABYLON_F1_SCRIPTS");
        if (folder.isEmpty() || Int2Ssl::findExecutable().isEmpty())
            QSKIP("BABYLON_F1_SCRIPTS or int2ssl not available");
        const QFileInfoList files = QDir(folder).entryInfoList({"*.int", "*.INT"}, QDir::Files);
        QVERIFY(!files.isEmpty());
        int ok = 0, failed = 0, withDialogue = 0, fallout1Mode = 0, nodes = 0;
        QStringList failures;
        for (const QFileInfo &fi : files) {
            QFile f(fi.absoluteFilePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            QString error;
            bool f1 = false;
            const QString ssl = Int2Ssl::decompile(f.readAll(), &error, &f1);
            if (ssl.isEmpty()) {
                ++failed;
                if (failures.size() < 5)
                    failures << fi.fileName() + ": " + error.left(80);
                continue;
            }
            ++ok;
            fallout1Mode += f1 ? 1 : 0;
            const DialogGraph g = DialogGraph::parseSsl(ssl);
            if (g.hasDialogue()) {
                ++withDialogue;
                nodes += int(g.buildTree().size());
            }
        }
        qInfo() << "F1 scripts:" << files.size() << "decompiled" << ok << "failed" << failed << "fallout1-mode" << fallout1Mode
                << "with dialogue" << withDialogue << "root nodes" << nodes << failures;
        QVERIFY(ok > files.size() / 2);
        QVERIFY(withDialogue > 20);
    }

    void pipboyLayoutWithSyntheticFont()
    {
        // font: lower case 6 px, upper case 7 px, others 5 px; letter spacing 1, word spacing 4 (space = 5 px)
        QByteArray aaf("AAFF");
        auto be16 = [&](int v) { aaf.append(char((v >> 8) & 0xFF)); aaf.append(char(v & 0xFF)); };
        be16(9); be16(1); be16(4); be16(1);
        for (int ch = 0; ch < 256; ++ch) {
            be16(ch >= 'a' && ch <= 'z' ? 6 : ch >= 'A' && ch <= 'Z' ? 7 : 5);
            be16(8);
            aaf.append(QByteArray(4, '\0'));
        }
        aaf.append(QByteArray(100, '\0'));
        const PipboyText::Font font = PipboyText::Font::fromAaf(aaf);
        QVERIFY(font.isValid());
        QCOMPARE(font.width("ab"), 14);
        QCOMPARE(font.width(" "), 5);
        QVERIFY(!PipboyText::Font::fromAaf("nope").isValid());

        PipboyText::Measure m;
        m.font = &font;
        m.encoding = "cp1252";
        m.maxWidth = 70;
        QVERIFY(m.exact());
        QCOMPARE(m.width("aaaa"), 28);

        // greedy wrap: "aaaa bbbb" = 61 px fits, adding " cccc" (94) does not
        QCOMPARE(PipboyText::layout("aaaa bbbb cccc dddd", m),
                 (QStringList{"aaaa bbbb", "cccc dddd", "**END-DISK**"}));
        // blank line = paragraph (END-PAR); single line break = hard break
        QCOMPARE(PipboyText::layout("aaaa\n\nbbbb", m), (QStringList{"aaaa", "**END-PAR**", "bbbb", "**END-DISK**"}));
        QCOMPARE(PipboyText::layout("aaaa\r\nbbbb", m), (QStringList{"aaaa", "bbbb", "**END-DISK**"}));
        QCOMPARE(PipboyText::layout("  \n\n", m), (QStringList{"**END-DISK**"}));
        // a word wider than the screen is cut
        const QStringList cut = PipboyText::layout("aaaaaaaaaaaaaaaaaaaa", m);   // 20 x 7 = 140 px
        QVERIFY(cut.size() >= 3);
        QVERIFY(PipboyText::tooWide(cut, m).isEmpty());

        // reflow undoes layout
        const QStringList entries{"aaaa bbbb", "cccc dddd", "**END-PAR**", "eeee", "**END-DISK**"};
        const QString text = PipboyText::reflow(entries, m);
        QCOMPARE(text, QString("aaaa bbbb cccc dddd\n\neeee"));
        QCOMPARE(PipboyText::layout(text, m), entries);
        // a short line that was not cut by the layout stays a separate line (poems, lists...)
        QCOMPARE(PipboyText::reflow({"Short", "line two", "**END-DISK**"}, m), QString("Short\nline two"));
        // blank entries separate paragraphs like END-PAR
        QCOMPARE(PipboyText::reflow({"one", " ", "two"}, m), QString("one\n\ntwo"));

        // wider than the screen
        QCOMPARE(PipboyText::tooWide({"aaaaaaaaaaaaaaaa", "**END-PAR**", "bbb"}, m), (QVector<int>{0}));

        // 35 entries per page
        QCOMPARE(PipboyText::pageCount(35), 1);
        QCOMPARE(PipboyText::pageCount(36), 2);
        QCOMPARE(PipboyText::pageCount(0), 0);

        // fallback: characters per line
        PipboyText::Measure f;
        f.encoding = "cp1252";
        f.maxWidth = 374;
        f.fallbackChars = 53;
        QVERIFY(!f.exact());
        const QString fifty3(53, 'a'), fifty4(54, 'a');
        QVERIFY(f.width(fifty3) <= 374);
        QVERIFY(f.width(fifty4) > 374);
        const QStringList lines = PipboyText::layout((fifty3.left(26) + ' ' + fifty3.left(26) + ' ' + fifty3.left(10)), f);
        QCOMPARE(lines.first().size(), 53);
    }

    void pipboyFindsHolodisks()
    {
        QMap<int, QString> texts{{100, "Back"}, {101, "More"}, {300, "x"}, {301, "y"}, {302, "**END-DISK**"},
                                 {303, "z"}, {304, "**END-DISK**"}, {400, "label"}, {401, "**END-PAR**"}};
        const auto blocks = PipboyText::findHolodisks(texts);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(0).firstId, 300);
        QCOMPARE(blocks.at(0).lastId, 302);
        QCOMPARE(blocks.at(0).entryCount(), 3);
        QCOMPARE(blocks.at(1).firstId, 303);   // a disk may start right after the previous one
        QVERIFY(blocks.at(1).contains(304));
        QVERIFY(!blocks.at(1).contains(305));
    }

    void pipboyRealGameFiles()
    {
        // BABYLON_SAMPLE_INSTALL: the real font1.aaf and pipboy.msg must round-trip (lines -> text -> lines).
        const QString folder = qEnvironmentVariable("BABYLON_SAMPLE_INSTALL");
        if (folder.isEmpty())
            QSKIP("BABYLON_SAMPLE_INSTALL not set");
        ProjectConfig c;
        c.id = "pb";
        c.name = "pb";
        c.sources = VirtualFileSystem::discoverSources(folder);
        c.outputDir = QDir::tempPath() + "/babylon_pb_out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.reload();
        const QByteArray fontData = p.readInterfaceFont(1);
        QVERIFY2(!fontData.isEmpty(), "font1.aaf not found in the sources");
        const PipboyText::Font font = PipboyText::Font::fromAaf(fontData);
        QVERIFY(font.isValid());

        const TranslationDoc doc = p.loadDocument("game/pipboy.msg");
        QMap<int, QString> texts;
        for (const TranslationRow &r : doc.rows)
            texts.insert(r.id, r.source);
        const auto blocks = PipboyText::findHolodisks(texts);
        QVERIFY(blocks.size() > 5);

        PipboyText::Measure m;
        m.font = &font;
        m.encoding = "cp1252";
        int identical = 0, total = 0, overWide = 0;
        for (const auto &b : blocks) {
            QStringList entries;
            for (int id = b.firstId; id <= b.lastId; ++id)
                entries << texts.value(id);
            const QStringList again = PipboyText::layout(PipboyText::reflow(entries, m), m);
            ++total;
            // The game files pad lines with trailing/double spaces and use "" or END-PAR for a blank line: same screen.
            auto normalize = [](const QStringList &list) {
                QStringList out;
                for (const QString &e : list) {
                    const QString s = e.simplified();
                    out << (s.isEmpty() ? PipboyText::endPar() : s);
                }
                return out;
            };
            const bool same = normalize(again) == normalize(entries);
            identical += same ? 1 : 0;
            if (!same && total < 40) {
                int i = 0;
                while (i < entries.size() && i < again.size() && entries.at(i) == again.at(i))
                    ++i;
                qInfo() << "block" << b.firstId << "differs at entry" << i << "of" << entries.size() << "/" << again.size()
                        << "orig:" << entries.value(i) << "(" << m.width(entries.value(i)) << "px)"
                        << "new:" << again.value(i) << "(" << m.width(again.value(i)) << "px)"
                        << "next orig:" << entries.value(i + 1).left(30);
            }
            overWide += PipboyText::tooWide(again, m).isEmpty() ? 0 : 1;
        }
        qInfo() << "holodisks" << total << "round trip identical" << identical << "with a too wide line after layout" << overWide;
        QCOMPARE(overWide, 0);              // layout never exceeds the screen
        QVERIFY(identical * 10 >= total * 8);   // and rebuilds nearly every original body (poems/ASCII art may differ)
    }

    void stemmerAndMatcher()
    {
        TermMatcher m(QStringLiteral("english"));
        m.add(QStringLiteral("Stimpak"), 0);
        m.add(QStringLiteral("Leather Armor"), 1);
        m.add(QStringLiteral("Vault City"), 2);
        m.add(QStringLiteral("Vault"), 3);
        m.add(QStringLiteral("the"), 4); // stop word, ignored
        m.build();

        auto ids = [&](const QString &t) {
            QList<int> out;
            for (const auto &x : m.find(t))
                out << x.termId;
            return out;
        };
        QCOMPARE(ids(QStringLiteral("I need two Stimpaks and a stimpak.")), (QList<int>{0, 0}));
        QCOMPARE(ids(QStringLiteral("He wears the Leather Armor's collar")), (QList<int>{1}));
        QCOMPARE(ids(QStringLiteral("leather-armor")), (QList<int>{1}));
        QCOMPARE(ids(QStringLiteral("Welcome to Vault City, citizen of the Vault")), (QList<int>{2, 3}));
        QCOMPARE(ids(QStringLiteral("a Stimpack here")), (QList<int>{0})); // one-letter typo
        QVERIFY(m.find(QStringLiteral("a Stimpack here")).first().fuzzy);
        QCOMPARE(ids(QStringLiteral("leather and armor")), QList<int>{});

        const auto match = m.find(QStringLiteral("Take the Stimpaks!")).first();
        QCOMPARE(QStringLiteral("Take the Stimpaks!").mid(match.start, match.length), QStringLiteral("Stimpaks"));
    }

    void glossaryConsistency()
    {
        Glossary g;
        g.add(QStringLiteral("Stimpak"), QStringLiteral("Stimpak"), QStringLiteral("Item"), QStringLiteral("pro_item.msg#100"));
        g.add(QStringLiteral("Leather Armor"), QStringLiteral("Armure en cuir"), QStringLiteral("Item"), QStringLiteral("pro_item.msg#200"));
        g.add(QStringLiteral("Geckos"), QStringLiteral("Geckos"), QStringLiteral("Creature"), QStringLiteral("pro_crit.msg#100"));
        g.add(QStringLiteral("Pip-Boy"), QStringLiteral("Pip-Boy"), QStringLiteral("Item"), QStringLiteral("pro_item.msg#300"));
        g.add(QStringLiteral("Pip-Boy"), QStringLiteral("Pipboy"), QStringLiteral("Item"), QStringLiteral("misc#1"));
        g.finalize(QStringLiteral("english"), QStringLiteral("french"));

        QCOMPARE(g.terms().size(), 4);
        QCOMPARE(g.conflictCount(), 1);

        const auto ok = g.checkConsistency(QStringLiteral("Wear the Leather Armor."), QStringLiteral("Mets l'armure en cuir."));
        QVERIFY(ok.isEmpty());
        const auto bad = g.checkConsistency(QStringLiteral("Wear the Leather Armor."), QStringLiteral("Mets la cuirasse."));
        QCOMPARE(bad.size(), 1);
        QCOMPARE(bad.first().term->source, QStringLiteral("Leather Armor"));
        // untranslated target is not flagged
        QVERIFY(g.checkConsistency(QStringLiteral("Wear the Leather Armor."), QString()).isEmpty());
        // either variant of a conflicting term is accepted
        QVERIFY(g.checkConsistency(QStringLiteral("My Pip-Boy"), QStringLiteral("Mon Pipboy")).isEmpty());
        QVERIFY(g.checkConsistency(QStringLiteral("My Pip-Boy"), QStringLiteral("Mon Pip-Boy")).isEmpty());
        // case-only match is a weak hint, never an alert
        QVERIFY(g.checkConsistency(QStringLiteral("the geckos run"), QStringLiteral("les lézards courent")).isEmpty());
        QCOMPARE(g.findInTarget(QStringLiteral("Mon Pipboy")).size(), 1);
    }

    void projectWorkflow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        writeFile(root + "/mod/text/english/game/pro_item.msg",
                  "{100}{}{Stimpak}\r\n{101}{}{A healing item.}\r\n{200}{}{Leather Armor}\r\n");
        writeFile(root + "/mod/text/french/game/pro_item.msg",
                  "{100}{}{Stimpak}\r\n{101}{}{Un objet de soin.}\r\n{200}{}{Armure en cuir}\r\n");
        writeFile(root + "/mod/text/english/dialog/abel.msg",
                  "# Abel\r\n{100}{}{Hello there!}\r\n{101}{}{Take a Stimpak.}\r\n{102}{}{...}\r\n");

        ProjectConfig c;
        c.id = "t1";
        c.name = "Test";
        c.sources = {root + "/mod"};
        c.outputDir = root + "/out";
        c.sourceLanguage = "english";
        c.targetLanguage = "french";
        Project p(c);
        p.setStateFile(root + "/state.json");
        QStringList errors;
        p.reload(&errors);
        QVERIFY(errors.isEmpty());

        const QStringList files = p.messageFiles();
        QCOMPARE(files, (QStringList{"game/pro_item.msg", "dialog/abel.msg"})); // /game first
        QVERIFY(p.availableLanguages().contains("french"));

        // glossary from /game
        p.setGlossary(p.buildGlossary());
        QCOMPARE(p.glossary().terms().size(), 2); // names only, descriptions skipped
        const auto bad = p.glossary().checkConsistency("Take a Leather Armor", "Prends une cuirasse");
        QCOMPARE(bad.size(), 1);

        // statistics: pro_item is fully translated ("Stimpak" == "Stimpak" is the only ambiguous one)
        TranslationDoc game = p.loadDocument("game/pro_item.msg");
        FileStats gs = Project::statisticsOf(game);
        QCOMPARE(gs.lines, 3);
        QCOMPARE(gs.untranslated, 1); // identical to the source and not confirmed
        game.rows[0].confirmed = true;
        QCOMPARE(Project::statisticsOf(game).untranslated, 0);

        // dialog: nothing translated yet; "..." has nothing to translate
        TranslationDoc dlg = p.loadDocument("dialog/abel.msg");
        QVERIFY(!dlg.targetFileExists);
        FileStats ds = Project::statisticsOf(dlg);
        QCOMPARE(ds.lines, 3);
        QCOMPARE(ds.untranslated, 2);
        QCOMPARE(ds.wordsRemaining, 5);

        dlg.rows[0].target = QString::fromUtf8("Bonjour à toi !");
        dlg.rows[1].review = true;
        dlg.rows[1].target = QString::fromUtf8("Prends un Stimpak.");
        QString error, bad2;
        QVERIFY2(p.saveDocument(dlg, &error, &bad2), qPrintable(error));
        QVERIFY(bad2.isEmpty());

        const QString outFile = root + "/out/text/french/dialog/abel.msg";
        const QByteArray written = readFile(outFile);
        QVERIFY(written.startsWith("# Abel\r\n{100}{}{Bonjour \xE0 toi !}"));
        QVERIFY(written.contains("{102}{}{...}")); // untranslated line keeps the source text

        // reload from disk (+ persisted review flag)
        Project q(c);
        q.setStateFile(root + "/state.json");
        q.reload();
        const TranslationDoc again = q.loadDocument("dialog/abel.msg");
        QVERIFY(again.targetFileExists);
        QCOMPARE(again.rows.at(0).target, QString::fromUtf8("Bonjour à toi !"));
        QVERIFY(again.rows.at(1).review);
        QCOMPARE(Project::statusOf(again.rows.at(1)), SegmentStatus::ToReview);
        const QVector<TodoItem> todo = q.reviewItems();
        QCOMPARE(todo.size(), 1);
        QCOMPARE(todo.first().id, 101);
        QCOMPARE(todo.first().relPath, QStringLiteral("dialog/abel.msg"));
        QVERIFY(q.reviewItems({QStringLiteral("dialog/abel.msg")}).isEmpty());
        QCOMPARE(Project::statusOf(again.rows.at(0)), SegmentStatus::Translated);
        QCOMPARE(Project::statusOf(again.rows.at(2)), SegmentStatus::Translated);

        const auto all = q.computeStatistics();
        QCOMPARE(all.size(), 2);
    }
};

QTEST_APPLESS_MAIN(CoreTests)
#include "tst_core.moc"
