// Offscreen smoke test of the whole application window with a small fake Fallout data set.
// Set BABYLON_SHOT_DIR to also write screenshots there.

#include "ui/MainWindow.h"
#include "core/AppSettings.h"
#include "core/DialogGraph.h"
#include "core/Int2Ssl.h"
#include "core/ProjectStore.h"
#include "ui/DialogTreePanel.h"
#include "core/DatArchive.h"
#include "core/ProjectSearch.h"
#include "core/QualityCheck.h"
#include "ui/GlossaryPanel.h"
#include "ui/ImportTranslationDialog.h"
#include "ui/MassTranslateDialog.h"
#include "ui/QualityPanel.h"
#include "ui/SearchPanel.h"
#include "ui/MsgTableModel.h"
#include "ui/PipboyPanel.h"
#include "ui/ProjectDialog.h"
#include "ui/ProjectTree.h"
#include "ui/SciEdit.h"
#include "ui/Theme.h"
#include "ui/TodoPanel.h"
#include "ui/TranslationService.h"
#include "ui/TranslationEditor.h"
#include "core/VirtualFileSystem.h"

#include <QDir>
#include <QDockWidget>
#include <QLineEdit>
#include <QSettings>
#include <QFile>
#include <QStandardPaths>
#include <QTranslator>
#include <QTableView>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

}

class GuiSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("FalloutBabylonTest"));
        QCoreApplication::setApplicationName(QStringLiteral("FalloutBabylonTest"));
        // BABYLON_QM=path	oabylon_fr.qm shows the interface in that language.
        const QString qm = qEnvironmentVariable("BABYLON_QM");
        if (!qm.isEmpty()) {
            auto *translator = new QTranslator(qApp);
            QVERIFY(translator->load(qm));
            QVERIFY(qApp->installTranslator(translator));
        }
    }

    void defaultOutputFolder()
    {
        QTemporaryDir dir;
        const QString inst = dir.path() + "/F2";
        for (const char *sub : {"master.dat", "critter.dat", "data", "mods/x.dat", "ModFolder"})
            QVERIFY(QDir().mkpath(inst + "/" + sub));
        auto norm = [](const QString &p) { return QDir::cleanPath(p); };

        // an installation: the data folder, never an extracted archive folder or the last mod
        const QStringList install = VirtualFileSystem::discoverSources(inst);
        QCOMPARE(norm(ProjectDialog::defaultOutputFor(install, "x")), norm(inst + "/data"));
        // data folder wins even when a mod comes after it
        QCOMPARE(norm(ProjectDialog::defaultOutputFor({inst + "/master.dat", inst + "/data", inst + "/mods/x.dat"}, "x")),
                 norm(inst + "/data"));
        // only archive folders: a folder of its own, not the archive
        const QString own = ProjectDialog::defaultOutputFor({inst + "/master.dat", inst + "/critter.dat"}, "My mod");
        QVERIFY(own.endsWith("FalloutBabylon/My mod/data"));
        // a plain mod folder is used as is
        QCOMPARE(norm(ProjectDialog::defaultOutputFor({inst + "/master.dat", inst + "/ModFolder"}, "x")), norm(inst + "/ModFolder"));

        // the dialog itself, on a real installation when one is given
        const QString real = qEnvironmentVariable("BABYLON_SAMPLE_INSTALL");
        const QByteArray shotDir = qgetenv("BABYLON_SHOT_DIR");
        if (!real.isEmpty() && !shotDir.isEmpty()) {
            ProjectDialog dialog(ProjectConfig(), true);
            dialog.addInstallation(real);
            dialog.show();
            QTest::qWait(300);
            dialog.grab().save(QString::fromLocal8Bit(shotDir) + "/dialog.png");
            QCOMPARE(norm(dialog.config().outputDir), norm(real + "/data"));
        }
    }

    void pipboyPanelWorkflow()
    {
        QTemporaryDir dir;
        const QString mod = dir.path() + "/mod";
        write(mod + "/text/english/game/pipboy.msg",
              "{100}{}{Back}\r\n{101}{}{More}\r\n"
              "{300}{}{Disk title}\r\n{301}{}{First line of the disk, cut by hand}\r\n{302}{}{**END-PAR**}\r\n"
              "{303}{}{Second paragraph.}\r\n{304}{}{**END-DISK**}\r\n"
              "{310}{}{Unrelated label}\r\n");
        ProjectConfig cfg;
        cfg.id = "pipboy1";
        cfg.name = "Pipboy";
        cfg.sources = {mod};
        cfg.outputDir = dir.path() + "/out";
        cfg.sourceLanguage = "english";
        cfg.targetLanguage = "french";
        QFile::remove(ProjectStore::stateFile(cfg.id));
        AppSettings::setPipboyMaxWidth(374);
        AppSettings::setPipboyFallbackChars(53);   // no font in the fake data: 53 characters per line

        Theme::apply(Theme::Mode::Light);
        MainWindow window;
        window.resize(1500, 900);
        window.show();
        QVERIFY(window.openProject(cfg));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 15000);
        window.openFile(cfg.id, "game/pipboy.msg");
        auto *editor = window.findChild<TranslationEditor *>();
        auto *panel = window.findChild<PipboyPanel *>();
        QVERIFY(editor && panel);

        // not a holodisk line: the panel stays idle
        editor->selectId(100);
        QTest::qWait(100);
        QVERIFY(!panel->hasBlock());
        // a holodisk line: the panel loads the whole body as paragraphs
        editor->selectId(301);
        QTRY_VERIFY_WITH_TIMEOUT(panel->hasBlock(), 3000);
        QVERIFY(panel->targetEdit()->plainText().isEmpty());   // nothing translated yet

        auto *model = editor->model();
        const QString paragraph1 = QString::fromUtf8("Voici la première ligne du disque, qui est assez longue pour dépasser largement une seule ligne du Pip-Boy, "
                                                     "et qu'il faut donc couper automatiquement aux bons endroits.");
        const QString paragraph2 = QString::fromUtf8("Deuxième paragraphe.");
        panel->targetEdit()->insertAtCaret(paragraph1 + "\n\n" + paragraph2);
        panel->applyNow();
        QStringList entries = panel->entries();
        QVERIFY(entries.size() > 5);                              // more entries than the source disk had
        QCOMPARE(entries.last(), QString("**END-DISK**"));
        QVERIFY(entries.contains("**END-PAR**"));
        for (const QString &e : entries)
            if (!e.startsWith("**"))
                QVERIFY2(e.size() <= 53, qPrintable(e));          // fallback width: 53 characters
        const int last = 300 + int(entries.size()) - 1;
        QVERIFY(last < 310);
        for (int i = 0; i < entries.size(); ++i) {
            const int row = model->rowForId(300 + i);
            QVERIFY2(row >= 0, qPrintable(QString::number(300 + i)));
            QCOMPARE(model->row(row).target, entries.at(i));
        }
        QVERIFY(!model->row(model->rowForId(last)).inSource);      // extra records were added
        QVERIFY(editor->isDirty());

        // too long for the free ids (record 310 is something else): nothing is written
        const QStringList before = entries;
        panel->targetEdit()->insertAtCaret(QString("\n\n") + QString("mot ").repeated(400));
        panel->applyNow();
        QCOMPARE(model->row(model->rowForId(300)).target, before.first());
        QVERIFY(model->rowForId(310) >= 0);
        QCOMPARE(model->row(model->rowForId(310)).target, QString());   // untouched

        // a shorter text again: surplus source records keep their text (counted as done), extra ones disappear
        panel->targetEdit()->setPlainText("Court. Fin.");
        panel->applyNow();
        QCOMPARE(panel->entries(), (QStringList{"Court. Fin.", "**END-DISK**"}));
        QCOMPARE(model->row(model->rowForId(300)).target, QString("Court. Fin."));
        QCOMPARE(model->row(model->rowForId(301)).target, QString("**END-DISK**"));
        QCOMPARE(model->row(model->rowForId(303)).target, model->row(model->rowForId(303)).source);
        QVERIFY(model->row(model->rowForId(303)).confirmed);
        QCOMPARE(model->rowForId(last), -1);                       // orphans removed

        // saved file: the engine reads ids 300.. until END-DISK
        QVERIFY(editor->save());
        QFile out(cfg.outputDir + "/text/french/game/pipboy.msg");
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QByteArray saved = out.readAll();
        QVERIFY(saved.contains("{300}{}{Court. Fin.}"));
        QVERIFY(saved.contains("{301}{}{**END-DISK**}"));
        QVERIFY(saved.contains("{310}{}{Unrelated label}"));
    }

    void reviewToolsWorkflow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mod = dir.path() + "/mod";
        const QString old = dir.path() + "/old";
        write(mod + "/text/english/game/pro_item.msg", "{100}{}{Stimpak}\r\n{200}{}{Leather Armor}\r\n");
        write(mod + "/text/french/game/pro_item.msg", "{100}{}{Stimpak}\r\n{200}{}{Armure en cuir}\r\n");
        write(mod + "/text/english/game/quests.msg",
              "{100}{}{Find the water chip in the vault and bring it back to the elder before the water runs out for good.}\r\n");
        write(mod + "/text/french/game/quests.msg",
              "{100}{}{Trouvez la puce \xE0 eau dans l'abri et rapportez-la \xE0 l'ancien avant que l'eau ne soit " "\xE9puis\xE9" "e.}\r\n");
        write(mod + "/text/english/dialog/abel.msg",
              "{100}{}{Hello @Name@, need some Stimpaks?}\r\n"
              "{101}{}{I can sell you Leather Armor.}\r\n"
              "{102}{}{Goodbye.}\r\n");
        write(mod + "/text/french/dialog/abel.msg",
              "{100}{}{Bonjour, besoin de Stimpaks ?}\r\n"
              "{101}{}{Je peux te vendre une cuirasse.}\r\n");
        write(old + "/text/english/dialog/abel.msg", "{102}{}{Goodbye.}\r\n");
        write(old + "/text/french/dialog/abel.msg", "{102}{}{Au revoir.}\r\n");

        ProjectConfig cfg;
        cfg.id = "tools1";
        cfg.name = "Tools";
        cfg.sources = {mod};
        cfg.outputDir = dir.path() + "/out";
        cfg.sourceLanguage = "english";
        cfg.targetLanguage = "french";
        QFile::remove(ProjectStore::stateFile(cfg.id));

        Theme::apply(Theme::Mode::Light);
        MainWindow window;
        window.resize(1500, 900);
        window.show();
        QVERIFY(window.openProject(cfg));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 15000);
        window.openFile(cfg.id, "dialog/abel.msg");
        auto *editor = window.findChild<TranslationEditor *>();
        QVERIFY(editor);
        auto *model = editor->model();
        auto *table = editor->findChild<QTableView *>();
        QVERIFY(table);

        // --- file-level undo / redo
        table->selectRow(0);
        editor->toggleReview();
        QVERIFY(model->row(0).review);
        QVERIFY(model->canUndo());
        QVERIFY(editor->undoOperation());
        QVERIFY(!model->row(0).review);
        QVERIFY(model->canRedo());
        QVERIFY(editor->redoOperation());
        QVERIFY(model->row(0).review);
        QVERIFY(editor->undoOperation());

        // --- replace in an open file is one undoable step
        ProjectSearch::Options replace;
        replace.text = "Stimpaks";
        QCOMPARE(editor->replaceInTarget(replace, "Seringues", {}), 1);
        QVERIFY(model->row(0).target.contains("Seringues"));
        QVERIFY(editor->undoOperation());
        QVERIFY(model->row(0).target.contains("Stimpaks"));

        // --- validation, comment and the filters that use them
        table->selectRow(1);
        editor->toggleValidated();
        QVERIFY(model->row(1).validated != 0);
        QVERIFY(!model->row(1).changedSinceValidation());
        model->setComment(1, "check the tone");
        MsgFilterProxy proxy;
        proxy.setSourceModel(model);
        proxy.setStatusFilter(MsgFilterProxy::Validated);
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setStatusFilter(MsgFilterProxy::WithComment);
        QCOMPARE(proxy.rowCount(), 1);
        model->setTarget(1, "Je peux te vendre une armure en cuir.");
        editor->refreshCurrent();     // the text editor shows the model's text again
        QVERIFY(model->row(1).changedSinceValidation());
        proxy.setStatusFilter(MsgFilterProxy::ChangedSinceValidation);
        QCOMPARE(proxy.rowCount(), 1);
        proxy.setStatusFilter(MsgFilterProxy::Validated);
        QCOMPARE(proxy.rowCount(), 0);
        QVERIFY(editor->save());
        {
            Project reopened(cfg);
            reopened.setStateFile(ProjectStore::stateFile(cfg.id));
            reopened.reload();
            const TranslationDoc doc = reopened.loadDocument("dialog/abel.msg");
            QCOMPARE(doc.rows[1].comment, QString("check the tone"));
            QVERIFY(doc.rows[1].changedSinceValidation());
        }

        // --- search panel
        auto *search = window.findChild<SearchPanel *>();
        QVERIFY(search);
        const auto edits = search->findChildren<QLineEdit *>();
        QVERIFY(!edits.isEmpty());
        edits.first()->setText("stimpak");
        search->searchNow();
        QTRY_VERIFY_WITH_TIMEOUT(search->resultCount() > 0, 5000);
        QVERIFY(search->hits().first().inTarget);

        // --- quality report: the lost @Name@ is found
        auto *quality = window.findChild<QualityPanel *>();
        QVERIFY(quality);
        quality->runNow();
        QTRY_VERIFY_WITH_TIMEOUT(!quality->running() && quality->totalCount() > 0, 8000);
        bool tokenIssue = false;
        for (const auto &issue : quality->issues())
            tokenIssue = tokenIssue || (issue.kind == QualityCheck::Tokens && issue.id == 100);
        QVERIFY(tokenIssue);

        // --- manual glossary term: stored with the project and added to the rebuilt glossary
        auto *glossary = window.findChild<GlossaryPanel *>();
        QVERIFY(glossary);
        glossary->addOrReplaceTerm({"Abel", "Abel", true});
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 10000);
        QCOMPARE(editor->sharedProject()->customTerms().size(), 1);
        bool found = false;
        for (const GlossaryTerm &t : editor->sharedProject()->glossary().terms())
            found = found || (t.custom && t.keep && t.source == "Abel");
        QVERIFY(found);
        glossary->removeTerm("abel");
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 10000);
        QVERIFY(editor->sharedProject()->customTerms().isEmpty());

        // --- quests.msg: preview of the wrapping done by the engine
        window.openFile(cfg.id, "game/quests.msg");
        auto *pipboy = window.findChild<PipboyPanel *>();
        QVERIFY(pipboy);
        QTRY_VERIFY_WITH_TIMEOUT(pipboy->questMode() && !pipboy->previewText().isEmpty(), 3000);
        QVERIFY(pipboy->previewText().contains("Trouvez"));
        QVERIFY(pipboy->previewText().count('\n') >= 1);      // wrapped on several lines

        // --- reuse of an old translation, through the dialog
        auto project = editor->sharedProject();
        ImportTranslationDialog importDialog(project);
        importDialog.show();
        importDialog.setFolder(old);
        importDialog.startNow();
        QTRY_VERIFY_WITH_TIMEOUT(importDialog.imported(), 8000);
        QCOMPARE(importDialog.report().exact, 1);
    }

    void archiveOutputWorkflow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mod = dir.path() + "/mod";
        write(mod + "/text/english/dialog/abel.msg", "{100}{}{Hello there.}\r\n{101}{}{Goodbye.}\r\n");

        ProjectConfig cfg;
        cfg.id = "archive1";
        cfg.name = "Archive";
        cfg.sources = {mod};
        cfg.outputArchive = dir.path() + "/out/french.dat";
        cfg.outputDir = ProjectStore::stagingDir(cfg.id);
        cfg.sourceLanguage = "english";
        cfg.targetLanguage = "french";
        QFile::remove(ProjectStore::stateFile(cfg.id));
        QDir(cfg.outputDir).removeRecursively();

        // the settings dialog keeps the archive mode and uses the application's working folder
        {
            ProjectDialog dialog(cfg, false);
            const ProjectConfig back = dialog.config();
            QCOMPARE(QDir::cleanPath(back.outputArchive), QDir::cleanPath(cfg.outputArchive));
            QCOMPARE(back.outputArchiveFormat, QString("dat2"));
            QVERIFY(back.archiveWithSource);
            QCOMPARE(QDir::cleanPath(back.outputDir), QDir::cleanPath(cfg.outputDir));
        }

        Theme::apply(Theme::Mode::Light);
        MainWindow window;
        window.resize(1500, 900);
        window.show();
        QSignalSpy built(&window, &MainWindow::archiveBuilt);
        QVERIFY(window.openProject(cfg));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 15000);
        window.openFile(cfg.id, "dialog/abel.msg");
        auto *editor = window.findChild<TranslationEditor *>();
        QVERIFY(editor);
        editor->model()->setTarget(0, "Bonjour.");
        editor->refreshCurrent();
        QVERIFY(editor->save());

        QTRY_VERIFY_WITH_TIMEOUT(built.count() >= 1, 8000);
        QVERIFY(built.last().at(1).toBool());
        const auto dat = DatArchive::open(cfg.outputArchive);
        QVERIFY(dat);
        const int fr = dat->indexOf("text/french/dialog/abel.msg");
        QVERIFY(fr >= 0);
        QVERIFY(dat->read(fr).contains("{100}{}{Bonjour.}"));
        QVERIFY(dat->indexOf("text/english/dialog/abel.msg") >= 0);   // the reference copy
        QVERIFY(!QFileInfo::exists(dir.path() + "/out/text"));         // no loose files next to the archive
    }

    void servicesWorkflow()
    {
        // Needs the real services: BABYLON_GRAMMALECTE_URL, BABYLON_DEEPL_KEY (kept in the environment only).
        const QString grammalecte = qEnvironmentVariable("BABYLON_GRAMMALECTE_URL");
        const QString deeplKey = qEnvironmentVariable("BABYLON_DEEPL_KEY");
        if (grammalecte.isEmpty() && deeplKey.isEmpty())
            QSKIP("BABYLON_GRAMMALECTE_URL / BABYLON_DEEPL_KEY not set");

        QTemporaryDir dir;
        const QString mod = dir.path() + "/mod";
        write(mod + "/text/english/dialog/trader.msg",
              "{100}{}{Hello, stranger. Need some Stimpaks?}\r\n"
              "{101}{}{I can sell you Leather Armor, cheap.}\r\n"
              "{102}{}{Take this, @Name@. It costs %d caps.}\r\n"
              "{103}{}{Goodbye.}\r\n");
        write(mod + "/text/english/game/pro_item.msg", "{100}{}{Stimpak}\r\n{200}{}{Leather Armor}\r\n");
        write(mod + "/text/french/game/pro_item.msg", "{100}{}{Stimpak}\r\n{200}{}{Armure en cuir}\r\n");

        ProjectConfig cfg;
        cfg.id = "services1";
        cfg.name = "Services";
        cfg.sources = {mod};
        cfg.outputDir = dir.path() + "/out";
        cfg.sourceLanguage = "english";
        cfg.targetLanguage = "french";
        QFile::remove(ProjectStore::stateFile(cfg.id));

        AppSettings::setGrammalecteUrl(grammalecte);
        AppSettings::setDeeplKey(deeplKey);
        AppSettings::setDeeplMarkReview(true);
        AppSettings::setCheckEnabled(true);
        struct Cleanup {   // never leave the key or the test servers in the settings store
            ~Cleanup()
            {
                AppSettings::setDeeplKey({});
                AppSettings::setGrammalecteUrl({});
                QSettings().remove(QStringLiteral("deepl/glossary/services1"));
            }
        } cleanup;

        Theme::apply(Theme::Mode::Light);
        MainWindow window;
        window.resize(1500, 900);
        window.show();
        QVERIFY(window.openProject(cfg));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 15000);
        window.openFile(cfg.id, "dialog/trader.msg");
        auto *editor = window.findChild<TranslationEditor *>();
        QVERIFY(editor);

        if (!grammalecte.isEmpty()) {
            editor->model()->setTarget(0, QStringLiteral("Je suis aller au marché."));
            editor->refreshCurrent();
            QTRY_VERIFY_WITH_TIMEOUT(editor->grammarIssueCount() > 0, 8000);
            qInfo() << "grammar issues underlined:" << editor->grammarIssueCount();
            editor->model()->setTarget(0, QString());
            editor->refreshCurrent();
        }

        if (!deeplKey.isEmpty()) {
            // one line: row 1 mentions the glossary terms
            auto *table = editor->findChild<QTableView *>();
            table->selectRow(1);
            editor->translateCurrent();
            QTRY_VERIFY_WITH_TIMEOUT(!editor->model()->row(1).target.isEmpty(), 20000);
            qInfo() << "DeepL line:" << editor->model()->row(1).target;
            QVERIFY(editor->model()->row(1).review);   // machine translation is flagged

            // mass translation of every untranslated line, on a model of its own
            auto project = std::make_shared<Project>(cfg);
            project->reload();
            project->setGlossary(project->buildGlossary());
            MsgTableModel model(project, project->loadDocument("dialog/trader.msg"));
            TranslationService service;
            MassTranslateDialog mass(&service, project, &model, {});
            mass.show();
            mass.run(false);
            QTRY_VERIFY_WITH_TIMEOUT(!mass.isRunning(), 60000);
            const int applied = mass.applyProposals();
            qInfo() << "mass translation applied" << applied << "lines";
            QCOMPARE(applied, 4);
            for (int i = 0; i < 4; ++i)
                QVERIFY(!model.row(i).target.isEmpty());
            QVERIFY(model.row(2).target.contains("@Name@"));    // placeholders survive
            QVERIFY(model.row(2).target.contains("%d"));
            QVERIFY(model.row(2).review);
            qInfo() << "line 102:" << model.row(2).target;
        }
    }

    void realInstall()
    {
        // BABYLON_SAMPLE_INSTALL=C:\Fallout2\F2_RPU_2.4.34 : opens the whole game and screenshots the result.
        const QString install = qEnvironmentVariable("BABYLON_SAMPLE_INSTALL");
        if (install.isEmpty())
            QSKIP("BABYLON_SAMPLE_INSTALL not set");
        QTemporaryDir out;
        ProjectConfig cfg;
        cfg.id = "real1";
        cfg.name = "RPU";
        cfg.sources = VirtualFileSystem::discoverSources(install);
        cfg.outputDir = out.path();
        cfg.sourceLanguage = "english";
        cfg.targetLanguage = "french";

        Theme::apply(Theme::Mode::Light);
        MainWindow window;
        window.resize(1600, 950);
        window.show();
        QElapsedTimer timer;
        timer.start();
        QVERIFY(window.openProject(cfg));
        qInfo() << "project opened in ms" << timer.elapsed();
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 60000);
        qInfo() << "analysis finished after ms" << timer.elapsed();

        timer.restart();
        Project probe(cfg);
        probe.reload();
        QString dialogFile;
        for (const QString &f : probe.messageFiles())
            if (dialogFile.isEmpty() && Project::kindOf(f) == FileKind::Dialog
                && (f.endsWith("/abel.msg") || f.endsWith("/cassidy.msg") || f.endsWith("/myron.msg")))
                dialogFile = f;
        if (dialogFile.isEmpty()) {
            // Fall back on the first dialogue whose script is in the sources and talks.
            int tried = 0;
            for (const QString &f : probe.messageFiles()) {
                if (Project::kindOf(f) != FileKind::Dialog || tried >= 80)
                    continue;
                const QByteArray script = probe.readDialogScript(f);
                if (script.isEmpty())
                    continue;
                ++tried;
                if (DialogGraph::parseSsl(Int2Ssl::decompile(script)).hasDialogue()) {
                    dialogFile = f;
                    break;
                }
            }
        }
        qInfo() << "dialog file used:" << dialogFile;
        QVERIFY(!dialogFile.isEmpty());
        {
            // Where does the time go when a file is opened?
            auto project = std::make_shared<Project>(cfg);
            project->reload();
            QElapsedTimer t;
            t.start();
            TranslationDoc doc = project->loadDocument("game/pro_item.msg");
            qInfo() << "loadDocument ms" << t.restart() << "rows" << doc.rows.size();
            project->setGlossary(project->buildGlossary());
            qInfo() << "buildGlossary ms" << t.restart();
            MsgTableModel model(project, doc);
            qInfo() << "MsgTableModel ms" << t.restart();
            TranslationEditor editor(project, doc);
            qInfo() << "TranslationEditor ms" << t.restart();
        }
        window.openFile(cfg.id, "game/pro_item.msg");
        qInfo() << "openFile pro_item ms" << timer.restart();
        window.openFile(cfg.id, dialogFile);
        qInfo() << "openFile dialog ms" << timer.restart();
        auto *tree = window.findChild<DialogTreePanel *>();
        QVERIFY(tree);
        QTest::qWait(1500);
        qInfo() << "tree status:" << tree->statusText();
        QTRY_VERIFY_WITH_TIMEOUT(tree->nodeCount() > 0, 3000);
        qInfo() << "dialogue tree:" << tree->nodeCount() << "nodes," << tree->statusText();
        qInfo() << "two files opened in ms" << timer.elapsed();
        QTest::qWait(500);
        if (auto *dock = window.findChild<QDockWidget *>("dockDialogTree"))
            dock->raise();
        QTest::qWait(300);
        const QByteArray shotDir = qgetenv("BABYLON_SHOT_DIR");
        if (!shotDir.isEmpty())
            window.grab().save(QString::fromLocal8Bit(shotDir) + "/real.png");

        // Pip-Boy holodisk panel on the real game files (font1.aaf, 374 px)
        if (probe.messageFiles().contains("game/pipboy.msg")) {
            window.openFile(cfg.id, "game/pipboy.msg");
            auto *pipboy = window.findChild<PipboyPanel *>();
            TranslationEditor *current = nullptr;
            for (auto *e : window.findChildren<TranslationEditor *>())
                if (e->relPath() == "game/pipboy.msg")
                    current = e;
            QVERIFY(current && pipboy);
            current->selectId(1001);
            QTRY_VERIFY_WITH_TIMEOUT(pipboy->hasBlock(), 3000);
            pipboy->targetEdit()->insertAtCaret(QString::fromUtf8(
                "Ce holodisque indique l'emplacement de l'Abri 13. L'emplacement a été marqué sur votre carte du monde.\n\n"
                "Bonne chance, étranger : le chemin sera long et dangereux, mais l'avenir de votre communauté en dépend."));
            QTest::qWait(900);
            qInfo() << "pipboy entries:" << pipboy->entries();
            if (auto *dock = window.findChild<QDockWidget *>("dockPipboy"))
                dock->raise();
            QTest::qWait(300);
            if (!shotDir.isEmpty())
                window.grab().save(QString::fromLocal8Bit(shotDir) + "/real_pipboy.png");
        }
    }

    void fullWorkflow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mod = dir.path() + "/mod";

        write(mod + "/text/english/game/pro_item.msg",
              "{100}{}{Stimpak}\r\n{101}{}{A stimpak is a healing syringe.}\r\n{200}{}{Leather Armor}\r\n"
              "{201}{}{Armor made of leather.}\r\n{300}{}{Pip-Boy 2000}\r\n{301}{}{A wrist computer.}\r\n");
        write(mod + "/text/french/game/pro_item.msg",
              "{100}{}{Stimpak}\r\n{101}{}{Un stimpak est une seringue de soin.}\r\n{200}{}{Armure en cuir}\r\n"
              "{201}{}{Une armure en cuir.}\r\n{300}{}{Pip-Boy 2000}\r\n{301}{}{Un ordinateur de poignet.}\r\n");
        write(mod + "/text/english/game/skill.msg",
              "{100}{}{Small Guns}\r\n{101}{}{Big Guns}\r\n{102}{}{Speech}\r\n{200}{}{Skill description.}\r\n");
        write(mod + "/text/french/game/skill.msg",
              "{100}{}{Armes l\xE9g\xE8res}\r\n{101}{}{Armes lourdes}\r\n{102}{}{\xC9loquence}\r\n{200}{}{Description.}\r\n");
        write(mod + "/text/english/dialog/abel.msg",
              "# Abel, trader of Klamath\r\n"
              "{100}{}{Hello, stranger. Need some Stimpaks?}\r\n"
              "{101}{}{I can sell you Leather Armor, cheap.}\r\n"
              "{102}{}{Your Small Guns skill looks weak, friend.}\r\n"
              "{103}{}{Goodbye.}\r\n"
              "{104}{}{A Pip-Boy 2000? Where did you find that?}\r\n");
        write(mod + "/text/french/dialog/abel.msg",
              "# Abel, trader of Klamath\r\n"
              "{100}{}{Bonjour, \xE9tranger. Besoin de Stimpaks ?}\r\n"
              "{101}{}{Je peux te vendre une cuirasse, pas ch\xE8re.}\r\n"
              "{103}{}{Goodbye.}\r\n"
              "{104}{}{}\r\n");
        write(mod + "/text/english/dialog/cassidy.msg", "{100}{}{Welcome to Vault City.}\r\n{101}{}{No weapons inside.}\r\n");

        ProjectConfig cfg;
        cfg.id = "smoke1";
        cfg.name = "Smoke mod";
        cfg.sources = {mod};
        cfg.outputDir = dir.path() + "/out";
        cfg.sourceLanguage = "english";
        cfg.targetLanguage = "french";

        QFile::remove(ProjectStore::stateFile(cfg.id));   // flags saved by a previous run of this test
        Theme::apply(Theme::Mode::Light);
        MainWindow window;
        window.resize(1500, 900);
        window.show();
        QVERIFY(window.openProject(cfg));
        QTRY_VERIFY_WITH_TIMEOUT(!window.analysisRunning(), 15000);

        window.openFile(cfg.id, "dialog/abel.msg");
        QTest::qWait(300);

        auto editors = window.findChildren<TranslationEditor *>();
        QCOMPARE(editors.size(), 1);
        TranslationEditor *editor = editors.first();
        const FileStats stats = editor->stats();
        QCOMPARE(stats.lines, 5);
        QCOMPARE(stats.untranslated, 3); // 102 (absent), 103 (== source), 104 (empty)

        auto *table = editor->findChild<QTableView *>();
        QVERIFY(table);
        QCOMPARE(table->model()->rowCount(), 5);

        // glossary alert on row 2 ("cuirasse" instead of "Armure en cuir")
        auto *model = editor->findChild<MsgTableModel *>();
        QVERIFY(model);
        QCOMPARE(model->issueCount(1), 1);
        QCOMPARE(model->issueCount(0), 0);

        // type a translation in the target editor
        table->selectRow(2);
        QTest::qWait(50);
        SciEdit *target = editor->targetEditor();
        target->setFocus();
        target->insertAtCaret(QString::fromUtf8("Ta compétence Armes légères paraît faible, ami."));
        QTest::qWait(400);
        QVERIFY(editor->isDirty());
        QCOMPARE(model->row(2).target, QString::fromUtf8("Ta compétence Armes légères paraît faible, ami."));
        QCOMPARE(model->issueCount(2), 0);

        // "to review" shows up in the TODO panel, unsaved mark included
        auto *todo = window.findChild<TodoPanel *>();
        QVERIFY(todo);
        editor->toggleReview();
        QTRY_COMPARE_WITH_TIMEOUT(todo->itemCount(), 1, 3000);
        QCOMPARE(model->status(2), SegmentStatus::ToReview);

        const QByteArray shotDir = qgetenv("BABYLON_SHOT_DIR");
        auto shot = [&](const char *name) {
            if (!shotDir.isEmpty())
                window.grab().save(QString::fromLocal8Bit(shotDir) + "/" + name);
        };
        shot("light.png");

        QVERIFY(editor->save());
        QVERIFY(!editor->isDirty());
        QFile out(cfg.outputDir + "/text/french/dialog/abel.msg");
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QByteArray saved = out.readAll();
        QVERIFY(saved.contains("{102}{}{Ta comp\xE9tence Armes l\xE9g\xE8res para\xEEt faible, ami.}"));
        QVERIFY(saved.contains("{104}{}{A Pip-Boy 2000? Where did you find that?}")); // untranslated keeps the source
        QVERIFY(saved.startsWith("# Abel, trader of Klamath\r\n"));

        Theme::apply(Theme::Mode::Dark);
        window.applyThemeToAll();
        QTest::qWait(100);
        shot("dark.png");
    }
};

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    GuiSmoke test;
    return QTest::qExec(&test, argc, argv);
}

#include "gui_smoke.moc"
