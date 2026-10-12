// Offscreen smoke test of the whole application window with a small fake Fallout data set.
// Set BABYLON_SHOT_DIR to also write screenshots there.

#include "ui/MainWindow.h"
#include "ui/MsgTableModel.h"
#include "ui/ProjectTree.h"
#include "ui/SciEdit.h"
#include "ui/Theme.h"
#include "ui/TodoPanel.h"
#include "ui/TranslationEditor.h"
#include "core/VirtualFileSystem.h"

#include <QDir>
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
            if (dialogFile.isEmpty() && Project::kindOf(f) == FileKind::Dialog && f.contains("acb"))
                dialogFile = f;
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
        qInfo() << "two files opened in ms" << timer.elapsed();
        QTest::qWait(500);
        const QByteArray shotDir = qgetenv("BABYLON_SHOT_DIR");
        if (!shotDir.isEmpty())
            window.grab().save(QString::fromLocal8Bit(shotDir) + "/real.png");
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
