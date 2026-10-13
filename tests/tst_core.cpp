#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/CustomGlossary.h"
#include "core/DatArchive.h"
#include "core/DatWriter.h"
#include "core/DialogGraph.h"
#include "core/Glossary.h"
#include "core/Int2Ssl.h"
#include "core/Lzss.h"
#include "core/MsgFile.h"
#include "core/Placeholders.h"
#include "core/OutputArchive.h"
#include "core/PipboyText.h"
#include "core/Project.h"
#include "core/ProjectSearch.h"
#include "core/QualityCheck.h"
#include "core/TermMatcher.h"
#include "core/TextCodec.h"
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
