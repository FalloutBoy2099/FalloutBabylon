#include "MainWindow.h"

#include "DialogTreePanel.h"
#include "GlossaryPanel.h"
#include "ImportTranslationDialog.h"
#include "PipboyPanel.h"
#include "SlowGuard.h"
#include "MsgTableModel.h"
#include "ProjectDialog.h"
#include "ProjectTree.h"
#include "QualityPanel.h"
#include "SearchPanel.h"
#include "SettingsDialog.h"
#include "StatsPanel.h"
#include "TranslationService.h"
#include "TodoPanel.h"
#include <algorithm>
#include "Theme.h"
#include "TranslationEditor.h"
#include "core/DatWriter.h"
#include "core/ProjectStore.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QThreadPool>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct UiLanguage {
    const char *code;
    const char *name;
};
const UiLanguage kUiLanguages[] = {
    {"en", "English"}, {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"}, {"pt", "Português"},
    {"de", "Deutsch"}, {"pl", "Polski"}, {"cs", "Čeština"}, {"ru", "Русский"},
    {"zh", "中文"},
};

}

MainWindow::MainWindow()
{
    m_service = new TranslationService(this);
    setWindowTitle(QStringLiteral("Fallout Babylon"));
    resize(1500, 900);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);

    auto *welcome = new QWidget;
    auto *wl = new QVBoxLayout(welcome);
    wl->addStretch();
    auto *title = new QLabel(tr("<h2>Fallout Babylon</h2><p>Translation editor for Fallout 1 and Fallout 2 mods.</p>"));
    title->setAlignment(Qt::AlignCenter);
    wl->addWidget(title);
    auto *b1 = new QPushButton(tr("Open a game folder..."));
    auto *b2 = new QPushButton(tr("New project..."));
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(b1);
    row->addWidget(b2);
    row->addStretch();
    wl->addLayout(row);
    wl->addStretch();
    connect(b1, &QPushButton::clicked, this, [this] { openGameFolder(); });
    connect(b2, &QPushButton::clicked, this, [this] { newProject(); });

    m_stack = new QStackedWidget;
    m_stack->addWidget(welcome);
    m_stack->addWidget(m_tabs);
    setCentralWidget(m_stack);

    createDocks();
    createActions();
    createMenus();

    m_statusLabel = new QLabel;
    m_progress = new QProgressBar;
    m_progress->setMaximumWidth(180);
    m_progress->setTextVisible(false);
    m_progress->hide();
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progress);

    m_dialogTimer.setSingleShot(true);
    m_dialogTimer.setInterval(250);
    connect(&m_dialogTimer, &QTimer::timeout, this, [this] { refreshDialogTree(); });
    m_todoTimer.setSingleShot(true);
    m_todoTimer.setInterval(300);
    connect(&m_todoTimer, &QTimer::timeout, this, [this] { refreshTodo(); });
    m_glossaryTimer.setSingleShot(true);
    m_glossaryTimer.setInterval(1500);
    connect(&m_glossaryTimer, &QTimer::timeout, this, [this] {
        const QStringList ids = m_glossaryPending;
        m_glossaryPending.clear();
        for (const QString &id : ids)
            if (m_sessions.contains(id))
                startAnalysis(id, true, false);
    });

    m_archiveTimer.setSingleShot(true);
    m_archiveTimer.setInterval(1000);
    connect(&m_archiveTimer, &QTimer::timeout, this, [this] {
        const QSet<QString> ids = m_archivePending;
        for (const QString &id : ids)
            if (!m_archiveRunning.contains(id)) {
                m_archivePending.remove(id);
                buildArchive(id);
            }
        if (!m_archivePending.isEmpty())
            m_archiveTimer.start();     // a build is still running
    });

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (TranslationEditor *e = currentEditor()) {
            setActiveProject(projectIdOf(e));
            m_statsPanel->setCurrentFile(e->relPath());
        }
        m_dialogTimer.start();
        m_pipboyPanel->setEditor(currentEditor());
        if (currentEditor())
            m_pipboyPanel->showId(currentEditor()->currentId());
        updateUi();
    });

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    updateUi();
}

MainWindow::~MainWindow()
{
    for (auto &s : m_sessions)
        s.cancelAll();
    QThreadPool::globalInstance()->waitForDone();
}

// ----------------------------------------------------------------------------- UI construction

void MainWindow::createDocks()
{
    m_tree = new ProjectTree;
    auto *projectDock = new QDockWidget(tr("Projects"), this);
    projectDock->setObjectName(QStringLiteral("dockProjects"));
    projectDock->setWidget(m_tree);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    m_glossaryPanel = new GlossaryPanel;
    auto *glossaryDock = new QDockWidget(tr("Glossary"), this);
    glossaryDock->setObjectName(QStringLiteral("dockGlossary"));
    glossaryDock->setWidget(m_glossaryPanel);
    addDockWidget(Qt::RightDockWidgetArea, glossaryDock);

    m_statsPanel = new StatsPanel;
    auto *statsDock = new QDockWidget(tr("Statistics"), this);
    statsDock->setObjectName(QStringLiteral("dockStats"));
    statsDock->setWidget(m_statsPanel);
    addDockWidget(Qt::RightDockWidgetArea, statsDock);
    m_todoPanel = new TodoPanel;
    auto *todoDock = new QDockWidget(tr("To review"), this);
    todoDock->setObjectName(QStringLiteral("dockTodo"));
    todoDock->setWidget(m_todoPanel);
    addDockWidget(Qt::RightDockWidgetArea, todoDock);
    m_dialogPanel = new DialogTreePanel;
    auto *dialogDock = new QDockWidget(tr("Dialogue tree"), this);
    dialogDock->setObjectName(QStringLiteral("dockDialogTree"));
    dialogDock->setWidget(m_dialogPanel);
    addDockWidget(Qt::RightDockWidgetArea, dialogDock);
    connect(m_dialogPanel, &DialogTreePanel::idActivated, this, [this](int id) {
        if (TranslationEditor *e = currentEditor())
            e->selectId(id);
    });
    tabifyDockWidget(glossaryDock, statsDock);
    tabifyDockWidget(statsDock, todoDock);
    m_pipboyPanel = new PipboyPanel;
    auto *pipboyDock = new QDockWidget(tr("Pip-Boy holodisk"), this);
    pipboyDock->setObjectName(QStringLiteral("dockPipboy"));
    pipboyDock->setWidget(m_pipboyPanel);
    addDockWidget(Qt::RightDockWidgetArea, pipboyDock);
    tabifyDockWidget(todoDock, dialogDock);
    tabifyDockWidget(dialogDock, pipboyDock);
    glossaryDock->raise();
    connect(m_todoPanel, &TodoPanel::itemActivated, this, [this](const QString &rel, int id) {
        openFile(m_activeProject, rel);
        if (TranslationEditor *e = currentEditor())
            e->selectId(id);
    });

    m_searchPanel = new SearchPanel;
    m_searchDock = new QDockWidget(tr("Search and replace"), this);
    m_searchDock->setObjectName(QStringLiteral("dockSearch"));
    m_searchDock->setWidget(m_searchPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);
    m_searchDock->hide();
    connect(m_searchPanel, &SearchPanel::openRequested, this, [this](const QString &rel, int id) {
        openFile(m_activeProject, rel);
        if (TranslationEditor *e = currentEditor())
            e->selectId(id);
    });
    connect(m_searchPanel, &SearchPanel::replaceRequested, this, &MainWindow::replaceInProject);

    m_qualityPanel = new QualityPanel;
    m_qualityDock = new QDockWidget(tr("Quality report"), this);
    m_qualityDock->setObjectName(QStringLiteral("dockQuality"));
    m_qualityDock->setWidget(m_qualityPanel);
    addDockWidget(Qt::BottomDockWidgetArea, m_qualityDock);
    tabifyDockWidget(m_searchDock, m_qualityDock);
    m_qualityDock->hide();
    connect(m_qualityPanel, &QualityPanel::openRequested, this, [this](const QString &rel, int id) {
        openFile(m_activeProject, rel);
        if (TranslationEditor *e = currentEditor())
            e->selectId(id);
    });

    resizeDocks({projectDock, glossaryDock}, {430, 380}, Qt::Horizontal);

    connect(m_tree, &ProjectTree::fileActivated, this, &MainWindow::openFile);
    connect(m_tree, &ProjectTree::refreshRequested, this, [this](const QString &id) { startAnalysis(id, false, true); });
    connect(m_tree, &ProjectTree::rebuildGlossaryRequested, this, [this](const QString &id) { startAnalysis(id, true, false); });
    connect(m_tree, &ProjectTree::settingsRequested, this, &MainWindow::editProjectSettings);
    connect(m_tree, &ProjectTree::closeRequested, this, [this](const QString &id) { closeProject(id); });
    connect(m_tree, &ProjectTree::exportRequested, this, &MainWindow::exportToDat);
    connect(m_tree, &ProjectTree::openFolderRequested, this, &MainWindow::openOutputFolder);
    connect(m_statsPanel, &StatsPanel::fileActivated, this, [this](const QString &rel) {
        if (!m_activeProject.isEmpty())
            openFile(m_activeProject, rel);
    });
    connect(m_glossaryPanel, &GlossaryPanel::insertRequested, this, [this](const QString &text) {
        if (TranslationEditor *e = currentEditor())
            e->insertText(text);
    });
    connect(m_glossaryPanel, &GlossaryPanel::customTermsChanged, this, [this](const QVector<CustomTerm> &terms) {
        const auto it = m_sessions.constFind(m_activeProject);
        if (it == m_sessions.constEnd())
            return;
        it->project->setCustomTerms(terms);
        startAnalysis(m_activeProject, true, false);   // the terms are added to the glossary
    });

    m_dockToggles << projectDock->toggleViewAction() << glossaryDock->toggleViewAction() << statsDock->toggleViewAction()
                  << todoDock->toggleViewAction() << dialogDock->toggleViewAction() << pipboyDock->toggleViewAction()
                  << m_searchDock->toggleViewAction() << m_qualityDock->toggleViewAction();
}

void MainWindow::createActions()
{
    m_actSave = new QAction(tr("&Save"), this);
    m_actSave->setShortcut(QKeySequence::Save);
    connect(m_actSave, &QAction::triggered, this, [this] {
        if (TranslationEditor *e = currentEditor())
            e->save();
    });
    m_actSaveAll = new QAction(tr("Save &all"), this);
    m_actSaveAll->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(m_actSaveAll, &QAction::triggered, this, [this] { saveAll(); });
    m_actCloseTab = new QAction(tr("&Close file"), this);
    m_actCloseTab->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(m_actCloseTab, &QAction::triggered, this, [this] {
        if (m_tabs->currentIndex() >= 0)
            closeTab(m_tabs->currentIndex());
    });

    auto editorAction = [this](const QString &text, const QKeySequence &key, auto fn) {
        auto *a = new QAction(text, this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, fn] {
            if (TranslationEditor *e = currentEditor())
                fn(e);
        });
        return a;
    };
    m_actReview = editorAction(tr("Mark as &to review"), QKeySequence(Qt::CTRL | Qt::Key_R),
                               [](TranslationEditor *e) { e->toggleReview(); });
    m_actValidate = editorAction(tr("&Validate (or remove the validation)"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V),
                                 [](TranslationEditor *e) { e->toggleValidated(); });
    m_actCopySource = editorAction(tr("Copy the &source"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                                   [](TranslationEditor *e) { e->copySourceToTarget(); });
    m_actConfirm = editorAction(tr("&Accept as is"), QKeySequence(Qt::CTRL | Qt::Key_Return),
                                [](TranslationEditor *e) { e->confirmCurrent(); });
    m_actNext = editorAction(tr("&Next line"), QKeySequence(Qt::CTRL | Qt::Key_Down), [](TranslationEditor *e) { e->goToRow(1); });
    m_actPrev = editorAction(tr("&Previous line"), QKeySequence(Qt::CTRL | Qt::Key_Up), [](TranslationEditor *e) { e->goToRow(-1); });
    m_actNextUntranslated = editorAction(tr("Next &untranslated line"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down),
                                         [](TranslationEditor *e) { e->goToNextUntranslated(true); });
    m_actPrevUntranslated = editorAction(tr("Previous untranslated line"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up),
                                         [](TranslationEditor *e) { e->goToNextUntranslated(false); });
    m_actFind = editorAction(tr("&Find in this file"), QKeySequence::Find, [](TranslationEditor *e) { e->focusSearch(); });
    m_actUndoOp = editorAction(tr("Undo the last operation"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z),
                               [](TranslationEditor *e) { e->undoOperation(); });
    m_actRedoOp = editorAction(tr("Redo the operation"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Y),
                               [](TranslationEditor *e) { e->redoOperation(); });

    m_actTranslateLine = editorAction(tr("&Translate this line with DeepL"), QKeySequence(Qt::CTRL | Qt::Key_T),
                                      [](TranslationEditor *e) { e->translateCurrent(); });
    m_actMassTranslate = editorAction(tr("&Mass translation with DeepL..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                                      [](TranslationEditor *e) { e->massTranslate(); });
    m_actAppSettings = new QAction(tr("&Settings..."), this);
    connect(m_actAppSettings, &QAction::triggered, this, [this] {
        SettingsDialog dialog(m_service, this);
        if (dialog.exec() == QDialog::Accepted)
            for (TranslationEditor *e : editors())
                e->setService(m_service);   // re-check the current line with the new servers
    });

    m_actSearchProject = new QAction(tr("Search and replace in the &project..."), this);
    m_actSearchProject->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(m_actSearchProject, &QAction::triggered, this, [this] {
        m_searchDock->show();
        m_searchDock->raise();
        m_searchPanel->focusQuery();
    });
    m_actQuality = new QAction(tr("&Quality report..."), this);
    connect(m_actQuality, &QAction::triggered, this, [this] {
        m_qualityDock->show();
        m_qualityDock->raise();
    });
    m_actImport = new QAction(tr("&Import an existing translation..."), this);
    connect(m_actImport, &QAction::triggered, this, [this] { importTranslation(m_activeProject); });

    m_actSettings = new QAction(tr("Project &settings..."), this);
    connect(m_actSettings, &QAction::triggered, this, [this] { editProjectSettings(m_activeProject); });
    m_actExport = new QAction(tr("&Export to a DAT archive..."), this);
    connect(m_actExport, &QAction::triggered, this, [this] { exportToDat(m_activeProject); });
    m_actBuildArchive = new QAction(tr("&Update the output DAT archive now"), this);
    connect(m_actBuildArchive, &QAction::triggered, this, [this] {
        if (!saveAll())
            return;
        m_archivePending.remove(m_activeProject);
        buildArchive(m_activeProject);
    });
    m_actRefresh = new QAction(tr("Refresh &statistics"), this);
    m_actRefresh->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_actRefresh, &QAction::triggered, this, [this] { startAnalysis(m_activeProject, false, true); });
    m_actRebuild = new QAction(tr("Rebuild the &glossary"), this);
    connect(m_actRebuild, &QAction::triggered, this, [this] { startAnalysis(m_activeProject, true, false); });
    m_actCloseProject = new QAction(tr("Close the pro&ject"), this);
    connect(m_actCloseProject, &QAction::triggered, this, [this] { closeProject(m_activeProject); });
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New project..."), QKeySequence::New, this, &MainWindow::newProject);
    file->addAction(tr("Open a game &folder..."), QKeySequence::Open, this, [this] { openGameFolder(); });
    m_recentMenu = file->addMenu(tr("Recent &folders"));
    m_savedMenu = file->addMenu(tr("&Saved projects"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        const QStringList recent = ProjectStore::recentFolders();
        for (const QString &f : recent)
            m_recentMenu->addAction(f, this, [this, f] { newProjectFromFolder(f); });
        if (recent.isEmpty())
            m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
    });
    connect(m_savedMenu, &QMenu::aboutToShow, this, [this] {
        m_savedMenu->clear();
        bool any = false;
        for (const ProjectConfig &c : ProjectStore::loadAll()) {
            if (m_sessions.contains(c.id))
                continue;
            any = true;
            m_savedMenu->addAction(QStringLiteral("%1  (%2 → %3)").arg(c.name, c.sourceLanguage, c.targetLanguage),
                                   this, [this, c] { openProject(c); });
        }
        if (!any)
            m_savedMenu->addAction(tr("(none)"))->setEnabled(false);
    });
    file->addSeparator();
    file->addAction(m_actSave);
    file->addAction(m_actSaveAll);
    file->addAction(m_actCloseTab);
    file->addSeparator();
    file->addAction(m_actSettings);
    file->addAction(m_actExport);
    file->addAction(m_actBuildArchive);
    file->addAction(m_actImport);
    file->addAction(m_actCloseProject);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actUndoOp);
    edit->addAction(m_actRedoOp);
    edit->addSeparator();
    edit->addAction(m_actNext);
    edit->addAction(m_actPrev);
    edit->addAction(m_actNextUntranslated);
    edit->addAction(m_actPrevUntranslated);
    edit->addSeparator();
    edit->addAction(m_actCopySource);
    edit->addAction(m_actConfirm);
    edit->addAction(m_actReview);
    edit->addAction(m_actValidate);
    edit->addSeparator();
    edit->addAction(m_actFind);
    edit->addAction(m_actSearchProject);

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(m_actTranslateLine);
    tools->addAction(m_actMassTranslate);
    tools->addSeparator();
    tools->addAction(m_actQuality);
    tools->addAction(m_actRefresh);
    tools->addAction(m_actRebuild);
    tools->addSeparator();
    tools->addAction(m_actAppSettings);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    for (QAction *a : std::as_const(m_dockToggles))
        view->addAction(a);
    view->addSeparator();
    QMenu *themeMenu = view->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    const struct { Theme::Mode mode; QString text; } themes[] = {
        {Theme::Mode::System, tr("System")}, {Theme::Mode::Light, tr("Light")}, {Theme::Mode::Dark, tr("Dark")}};
    for (const auto &t : themes) {
        QAction *a = themeMenu->addAction(t.text);
        a->setCheckable(true);
        a->setChecked(Theme::savedMode() == t.mode);
        themeGroup->addAction(a);
        const Theme::Mode mode = t.mode;
        connect(a, &QAction::triggered, this, [this, mode] {
            Theme::saveMode(mode);
            Theme::apply(mode);
            applyThemeToAll();
        });
    }

    QMenu *langMenu = view->addMenu(tr("&Language"));
    auto *langGroup = new QActionGroup(this);
    const QString saved = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("auto")).toString();
    QAction *autoAction = langMenu->addAction(tr("Automatic (system)"));
    autoAction->setCheckable(true);
    autoAction->setChecked(saved == QLatin1String("auto"));
    langGroup->addAction(autoAction);
    connect(autoAction, &QAction::triggered, this, [this] {
        QSettings().setValue(QStringLiteral("ui/language"), QStringLiteral("auto"));
        QMessageBox::information(this, tr("Language"), tr("The language will change the next time the application starts."));
    });
    for (const UiLanguage &l : kUiLanguages) {
        const QString code = QString::fromLatin1(l.code);
        // English is the source language of the interface; the others need a compiled translation.
        if (code != QLatin1String("en")) {
            QTranslator probe;
            if (!probe.load(QStringLiteral(":/i18n/babylon_%1.qm").arg(code)) || probe.isEmpty())
                continue; // no finished translation for this language yet
        }
        QAction *a = langMenu->addAction(QString::fromUtf8(l.name));
        a->setCheckable(true);
        a->setChecked(saved == code);
        langGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, code] {
            QSettings().setValue(QStringLiteral("ui/language"), code);
            QMessageBox::information(this, tr("Language"), tr("The language will change the next time the application starts."));
        });
    }

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About..."), this, &MainWindow::showAbout);
    help->addAction(tr("About &Qt..."), this, [] { QApplication::aboutQt(); });
}

// ----------------------------------------------------------------------------- projects

void MainWindow::newProject()
{
    ProjectConfig c;
    ProjectDialog dialog(c, true, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ProjectConfig config = dialog.config();
    ProjectStore::save(config);
    openProject(config);
}

void MainWindow::openGameFolder()
{
    const QString start = ProjectStore::recentFolders().value(0);
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open a Fallout game or mod folder"), start);
    if (!folder.isEmpty())
        newProjectFromFolder(folder);
}

void MainWindow::newProjectFromFolder(const QString &folder)
{
    ProjectDialog dialog(ProjectConfig(), true, this);
    dialog.addInstallation(folder);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ProjectConfig config = dialog.config();
    ProjectStore::save(config);
    openProject(config);
}

bool MainWindow::openProject(const ProjectConfig &config)
{
    if (m_sessions.contains(config.id)) {
        setActiveProject(config.id);
        return false;
    }
    auto project = std::make_shared<Project>(config);
    project->setStateFile(ProjectStore::stateFile(config.id));
    project->setStatsCacheFile(ProjectStore::statsCacheFile(config.id));

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList errors;
    // An existing output archive restores the working folder on a new computer / after switching to an archive.
    QString seedError;
    OutputArchive::seedWorkingFolder(config, &seedError);
    if (!seedError.isEmpty())
        errors << seedError;
    project->reload(&errors);
    QApplication::restoreOverrideCursor();
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Project"), tr("Some sources could not be read:\n\n%1").arg(errors.join(QLatin1Char('\n'))));

    const QStringList files = project->messageFiles();
    if (files.isEmpty())
        QMessageBox::information(this, tr("Project"),
                                 tr("No .msg file found under text/%1 or text/%2 in the sources of \"%3\".")
                                     .arg(config.sourceLanguage, config.targetLanguage, config.name));

    Session session;
    session.project = project;
    m_sessions.insert(config.id, session);
    m_order << config.id;
    m_tree->addProject(config.id, config.name, files);
    setActiveProject(config.id);
    persistOpenProjects();
    startAnalysis(config.id, true, true);
    updateUi();
    return true;
}

void MainWindow::editProjectSettings(const QString &id)
{
    if (!m_sessions.contains(id))
        return;
    for (TranslationEditor *e : editorsOf(id))
        if (e->isDirty()) {
            QMessageBox::information(this, tr("Project settings"),
                                     tr("Save or close the modified files of this project first."));
            return;
        }
    ProjectDialog dialog(m_sessions.value(id).project->config(), false, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ProjectConfig config = dialog.config();
    ProjectStore::save(config);
    if (!closeProject(id))
        return;
    openProject(config);
}

bool MainWindow::closeProject(const QString &id)
{
    if (!m_sessions.contains(id))
        return true;
    const QList<TranslationEditor *> open = editorsOf(id);
    if (!confirmClose(open))
        return false;
    for (TranslationEditor *e : open) {
        m_tabs->removeTab(m_tabs->indexOf(e));
        e->deleteLater();
    }
    m_sessions[id].cancelAll();
    flushArchive(id);
    m_sessions.remove(id);
    m_order.removeAll(id);
    m_tree->removeProject(id);
    if (m_activeProject == id)
        setActiveProject(m_order.value(m_order.size() - 1));
    persistOpenProjects();
    updateProgress();
    updateUi();
    return true;
}

void MainWindow::persistOpenProjects()
{
    ProjectStore::setOpenProjectIds(m_order);
}

void MainWindow::restoreSession()
{
    const QStringList ids = ProjectStore::openProjectIds();
    const QVector<ProjectConfig> all = ProjectStore::loadAll();
    for (const QString &id : ids)
        for (const ProjectConfig &c : all)
            if (c.id == id)
                openProject(c);
}

void MainWindow::setActiveProject(const QString &id)
{
    m_activeProject = id;
    const auto it = m_sessions.constFind(id);
    m_searchPanel->setProject(it == m_sessions.constEnd() ? nullptr : it->project);
    m_qualityPanel->setProject(it == m_sessions.constEnd() ? nullptr : it->project);
    m_glossaryPanel->setEditable(it != m_sessions.constEnd());
    m_glossaryPanel->setCustomTerms(it == m_sessions.constEnd() ? QVector<CustomTerm>() : it->project->customTerms());
    if (it == m_sessions.constEnd()) {
        m_statsPanel->clear();
        m_glossaryPanel->setGlossary({});
        m_todoTimer.start();
        updateUi();
        return;
    }
    m_todoTimer.start();
    const QString name = it->project->config().name;
    m_glossaryPanel->setGlossary(it->project->glossary().terms());
    m_statsPanel->setProject(name, it->stats.values());
    if (TranslationEditor *e = currentEditor())
        m_statsPanel->setCurrentFile(e->relPath());
    updateUi();
}

void MainWindow::refreshDialogTree()
{
    TranslationEditor *e = currentEditor();
    const auto it = e ? m_sessions.constFind(projectIdOf(e)) : m_sessions.constEnd();
    if (!e || it == m_sessions.constEnd()) {
        m_dialogPanel->clear();
        return;
    }
    m_dialogPanel->showFile(it->project, e->relPath(), e->dialogLines());
}

void MainWindow::refreshTodo()
{
    const auto it = m_sessions.constFind(m_activeProject);
    if (it == m_sessions.constEnd()) {
        m_todoPanel->setItems({});
        return;
    }
    // Open files hold the freshest state (unsaved marks included); the others come from the saved flags.
    QSet<QString> open;
    QVector<TodoItem> items;
    for (TranslationEditor *e : editorsOf(m_activeProject)) {
        open.insert(e->relPath());
        items += e->reviewItems();
    }
    items += it->project->reviewItems(open);
    std::stable_sort(items.begin(), items.end(), [](const TodoItem &a, const TodoItem &b) {
        return a.relPath != b.relPath ? a.relPath < b.relPath : a.id < b.id;
    });
    m_todoPanel->setItems(items);
}

QString MainWindow::activeProjectName() const
{
    const auto it = m_sessions.constFind(m_activeProject);
    return it == m_sessions.constEnd() ? QString() : it->project->config().name;
}

// ----------------------------------------------------------------------------- background analysis

void MainWindow::startAnalysis(const QString &id, bool glossary, bool stats)
{
    auto it = m_sessions.find(id);
    if (it == m_sessions.end())
        return;
    Session &s = it.value();
    const std::shared_ptr<Project> project = s.project;

    auto restart = [this](Job &job) {
        job.cancel->store(true);                       // stop the previous run of this job
        job.cancel = std::make_shared<std::atomic_bool>(false);
        job.generation = ++m_generationCounter;
        job.running = true;
    };

    if (glossary) {
        restart(s.glossaryJob);
        if (id == m_activeProject)
            m_glossaryPanel->showBuilding();
        const std::shared_ptr<std::atomic_bool> cancel = s.glossaryJob.cancel;
        const int generation = s.glossaryJob.generation;
        QThreadPool::globalInstance()->start([this, id, project, cancel, generation] {
            auto g = std::make_shared<Glossary>(project->buildGlossary(cancel.get()));
            if (cancel->load())
                return;
            QMetaObject::invokeMethod(this, [this, id, generation, g] { onGlossaryReady(id, generation, g); },
                                      Qt::QueuedConnection);
        });
    }

    if (stats) {
        restart(s.statsJob);
        s.statsDone = 0;
        s.statsTotal = 0;
        const std::shared_ptr<std::atomic_bool> cancel = s.statsJob.cancel;
        const int generation = s.statsJob.generation;
        QThreadPool::globalInstance()->start([this, id, project, cancel, generation] {
            QVector<FileStats> batch;
            int lastDone = 0, lastTotal = 0;
            auto flush = [&] {
                QMetaObject::invokeMethod(this, [this, id, generation, batch, lastDone, lastTotal] {
                    onStatsBatch(id, generation, batch, lastDone, lastTotal);
                }, Qt::QueuedConnection);
                batch.clear();
            };
            project->computeStatistics([&](int done, int total, const FileStats &fs) {
                batch.push_back(fs);
                lastDone = done;
                lastTotal = total;
                if (batch.size() >= 25)
                    flush();
            }, cancel.get());
            if (!cancel->load())
                flush();
        });
    }
    updateProgress();
}

void MainWindow::updateProgress()
{
    bool busy = false;
    QString text;
    int done = 0, total = 0;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        busy = busy || it->glossaryJob.running || it->statsJob.running;
        if (it->statsJob.running && it->statsTotal > 0 && total == 0) {
            done = it->statsDone;
            total = it->statsTotal;
            text = tr("Analysing \"%1\": %2 / %3 files").arg(it->project->config().name).arg(done).arg(total);
        } else if (text.isEmpty() && (it->glossaryJob.running || it->statsJob.running)) {
            text = tr("Analysing \"%1\"...").arg(it->project->config().name);
        }
    }
    m_progress->setVisible(busy);
    if (total > 0) {
        m_progress->setRange(0, total);
        m_progress->setValue(done);
    } else {
        m_progress->setRange(0, 0);
    }
    m_statusLabel->setText(busy ? text : QString());
}

void MainWindow::onGlossaryReady(const QString &id, int generation, const std::shared_ptr<Glossary> &glossary)
{
    auto it = m_sessions.find(id);
    if (it == m_sessions.end() || it->glossaryJob.generation != generation)
        return;
    it->glossaryJob.running = false;
    it->project->setGlossary(std::move(*glossary));
    if (id == m_activeProject)
        m_glossaryPanel->setGlossary(it->project->glossary().terms());
    for (TranslationEditor *e : editorsOf(id))
        e->refreshGlossary();
    updateProgress();
}

void MainWindow::onStatsBatch(const QString &id, int generation, const QVector<FileStats> &batch, int done, int total)
{
    auto it = m_sessions.find(id);
    if (it == m_sessions.end() || it->statsJob.generation != generation)
        return;
    // Files being edited are more up to date than what is on disk.
    QVector<FileStats> accepted;
    for (const FileStats &fs : batch) {
        bool open = false;
        for (TranslationEditor *e : editorsOf(id))
            open = open || e->relPath() == fs.relPath;
        if (open)
            continue;
        it->stats.insert(fs.relPath, fs);
        accepted.push_back(fs);
    }
    {
        SlowGuard g("tree.setStats");
        m_tree->setStats(id, accepted);
    }
    if (id == m_activeProject) {
        SlowGuard g("statsPanel.setProject");
        m_statsPanel->setProject(it->project->config().name, it->stats.values());
    }

    it->statsDone = done;
    it->statsTotal = total;
    if (done >= total)
        it->statsJob.running = false;
    updateProgress();
}

bool MainWindow::analysisRunning() const
{
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it)
        if (it->glossaryJob.running || it->statsJob.running)
            return true;
    return false;
}

void MainWindow::scheduleGlossaryRebuild(const QString &id)
{
    if (!m_glossaryPending.contains(id))
        m_glossaryPending << id;
    m_glossaryTimer.start();
}

// ----------------------------------------------------------------------------- files and tabs

TranslationEditor *MainWindow::currentEditor() const
{
    return qobject_cast<TranslationEditor *>(m_tabs->currentWidget());
}

QList<TranslationEditor *> MainWindow::editors() const
{
    QList<TranslationEditor *> list;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *e = qobject_cast<TranslationEditor *>(m_tabs->widget(i)))
            list << e;
    return list;
}

QList<TranslationEditor *> MainWindow::editorsOf(const QString &id) const
{
    QList<TranslationEditor *> list;
    for (TranslationEditor *e : editors())
        if (projectIdOf(e) == id)
            list << e;
    return list;
}

QString MainWindow::projectIdOf(const TranslationEditor *editor) const
{
    return editor->property("projectId").toString();
}

void MainWindow::openFile(const QString &id, const QString &relPath)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd())
        return;
    for (TranslationEditor *e : editorsOf(id))
        if (e->relPath() == relPath) {
            m_tabs->setCurrentWidget(e);
            return;
        }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    TranslationDoc doc = it->project->loadDocument(relPath, &error);
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Open"), tr("Cannot read %1:\n%2").arg(relPath, error));
        return;
    }
    if (doc.rows.isEmpty() && !doc.targetFileExists) {
        QMessageBox::information(this, tr("Open"), tr("%1 does not exist in the sources of this project.").arg(relPath));
        return;
    }
    if (!doc.warnings.isEmpty())
        QMessageBox::warning(this, tr("Open"),
                             tr("%1 contains malformed records; everything after the first one is ignored by the game:\n\n%2")
                                 .arg(relPath, doc.warnings.join(QLatin1Char('\n'))));

    SlowGuard openGuard("MainWindow::openFile (after load)");
    auto *editor = new TranslationEditor(it->project, std::move(doc));
    editor->setProperty("projectId", id);
    editor->setService(m_service);
    connect(editor, &TranslationEditor::dirtyChanged, this, [this, editor](bool dirty) {
        updateTabTitle(editor);
        m_tree->setFileDirty(projectIdOf(editor), editor->relPath(), dirty);
        if (!dirty && Project::kindOf(editor->relPath()) == FileKind::Game)
            scheduleGlossaryRebuild(projectIdOf(editor)); // names may have been translated
        if (!dirty)
            scheduleArchive(projectIdOf(editor));          // saved: the output archive follows
    });
    connect(editor, &TranslationEditor::statsChanged, this, [this, editor](const FileStats &s) { onEditorStats(editor, s); });
    connect(editor, &TranslationEditor::historyChanged, this, [this, editor] {
        if (editor == currentEditor())
            updateUi();
    });
    connect(editor, &TranslationEditor::currentIdChanged, this, [this, editor](int id) {
        if (editor == currentEditor()) {
            m_dialogPanel->selectId(id);
            m_pipboyPanel->showId(id);
        }
    });

    const int index = m_tabs->addTab(editor, QString());
    updateTabTitle(editor);
    m_tabs->setTabToolTip(index, it->project->config().name + QStringLiteral(" — ") + relPath);
    m_tabs->setCurrentIndex(index);
    onEditorStats(editor, editor->stats());
    updateUi();
}

void MainWindow::updateTabTitle(TranslationEditor *editor)
{
    const int index = m_tabs->indexOf(editor);
    if (index < 0)
        return;
    const auto it = m_sessions.constFind(projectIdOf(editor));
    const QString project = it == m_sessions.constEnd() ? QString() : it->project->config().name;
    const QString name = Statistics::displayNameFor(editor->relPath());
    m_tabs->setTabText(index, QStringLiteral("%1: %2%3").arg(project, name, editor->isDirty() ? QStringLiteral(" *") : QString()));
}

void MainWindow::onEditorStats(TranslationEditor *editor, const FileStats &stats)
{
    SlowGuard guard("MainWindow::onEditorStats");
    m_todoTimer.start();
    if (editor == currentEditor())
        m_dialogTimer.start();
    const QString id = projectIdOf(editor);
    auto it = m_sessions.find(id);
    if (it == m_sessions.end())
        return;
    it->stats.insert(stats.relPath, stats);
    {
        SlowGuard g("tree.updateStats");
        m_tree->updateStats(id, stats);
    }
    if (id == m_activeProject) {
        SlowGuard g("statsPanel.updateFile");
        m_statsPanel->updateFile(stats);
    }
}

bool MainWindow::closeTab(int index)
{
    auto *editor = qobject_cast<TranslationEditor *>(m_tabs->widget(index));
    if (!editor)
        return true;
    if (!confirmClose({editor}))
        return false;
    m_tabs->removeTab(index);
    m_todoTimer.start();
    m_tree->setFileDirty(projectIdOf(editor), editor->relPath(), false);
    editor->deleteLater();
    updateUi();
    return true;
}

bool MainWindow::confirmClose(const QList<TranslationEditor *> &list)
{
    QList<TranslationEditor *> dirty;
    for (TranslationEditor *e : list)
        if (e->isDirty())
            dirty << e;
    if (dirty.isEmpty())
        return true;

    QStringList names;
    for (TranslationEditor *e : dirty)
        names << e->relPath();
    QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                    tr("These files have unsaved changes:\n\n%1").arg(names.join(QLatin1Char('\n'))),
                    QMessageBox::NoButton, this);
    QPushButton *save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == save) {
        for (TranslationEditor *e : dirty)
            if (!e->save())
                return false;
        return true;
    }
    return box.clickedButton() == discard;
}

bool MainWindow::saveAll()
{
    bool ok = true;
    for (TranslationEditor *e : editors())
        if (e->isDirty())
            ok = e->save() && ok;
    return ok;
}

// ----------------------------------------------------------------------------- export

void MainWindow::importTranslation(const QString &id)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd())
        return;
    // The files are rewritten on disk: close the open ones first (after offering to save them).
    const QList<TranslationEditor *> open = editorsOf(id);
    if (!open.isEmpty()) {
        if (QMessageBox::question(this, tr("Import an existing translation"),
                                  tr("The files of this project that are open will be closed first. Continue?"))
            != QMessageBox::Yes)
            return;
        if (!confirmClose(open))
            return;
        for (TranslationEditor *e : open) {
            m_tabs->removeTab(m_tabs->indexOf(e));
            m_tree->setFileDirty(id, e->relPath(), false);
            e->deleteLater();
        }
    }
    const std::shared_ptr<Project> project = it->project;
    ImportTranslationDialog dialog(project, this);
    dialog.exec();
    if (dialog.imported()) {
        project->reload();                  // pick up the files written in the output folder
        scheduleArchive(id);
        startAnalysis(id, true, true);
        m_todoTimer.start();
        updateUi();
    }
}

void MainWindow::replaceInProject(const ProjectSearch::Options &options, const QString &replacement,
                                  const QVector<ProjectSearch::Hit> &hits)
{
    const auto it = m_sessions.constFind(m_activeProject);
    if (it == m_sessions.constEnd() || hits.isEmpty())
        return;
    QHash<QString, QSet<int>> byFile;
    for (const ProjectSearch::Hit &h : hits)
        if (h.inTarget)
            byFile[h.relPath].insert(h.id);
    if (byFile.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Replace"),
                              tr("Replace \"%1\" by \"%2\" in the translation of %3 line(s) of %4 file(s)?\n\n"
                                 "Files that are not open are saved immediately.")
                                  .arg(options.text, replacement)
                                  .arg(hits.size())
                                  .arg(byFile.size()))
        != QMessageBox::Yes)
        return;

    const std::shared_ptr<Project> project = it->project;
    int total = 0, files = 0;
    QStringList problems;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (auto f = byFile.cbegin(); f != byFile.cend(); ++f) {
        TranslationEditor *open = nullptr;
        for (TranslationEditor *e : editorsOf(m_activeProject))
            if (e->relPath() == f.key())
                open = e;
        if (open) {
            const int n = open->replaceInTarget(options, replacement, f.value());
            total += n;
            files += n > 0 ? 1 : 0;
            continue;
        }
        QString error;
        TranslationDoc doc = project->loadDocument(f.key(), &error);
        if (!error.isEmpty()) {
            problems << f.key() + QStringLiteral(": ") + error;
            continue;
        }
        const int n = ProjectSearch::replaceInDocument(doc, options, replacement, f.value());
        if (n == 0)
            continue;
        QString unencodable;
        if (!project->saveDocument(doc, &error, &unencodable)) {
            problems << f.key() + QStringLiteral(": ") + error;
            continue;
        }
        total += n;
        ++files;
        FileStats stats = Project::statisticsOf(doc);
        m_sessions[m_activeProject].stats.insert(stats.relPath, stats);
        m_tree->updateStats(m_activeProject, stats);
        m_statsPanel->updateFile(stats);
    }
    QApplication::restoreOverrideCursor();
    m_todoTimer.start();
    scheduleGlossaryRebuild(m_activeProject);
    scheduleArchive(m_activeProject);
    QString text = tr("%1 replacement(s) in %2 file(s).").arg(total).arg(files);
    if (!problems.isEmpty())
        text += QStringLiteral("\n\n") + problems.join(QLatin1Char('\n'));
    QMessageBox::information(this, tr("Replace"), text);
    m_searchPanel->searchNow();
}

void MainWindow::openOutputFolder(const QString &id)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd())
        return;
    const ProjectConfig &config = it->project->config();
    const QString dir = OutputArchive::enabled(config) ? QFileInfo(config.outputArchive).absolutePath() : config.outputDir;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::exportToDat(const QString &id)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd())
        return;
    if (!saveAll())
        return;
    const ProjectConfig &config = it->project->config();
    const QString textDir = QDir(config.outputDir).filePath(QStringLiteral("text/%1").arg(config.targetLanguage));
    if (!QDir(textDir).exists()) {
        QMessageBox::information(this, tr("Export"),
                                 tr("Nothing to export yet: save some translated files first.\n(%1 does not exist)").arg(textDir));
        return;
    }

    const QString fo2 = tr("Fallout 2 archive (zlib) (*.dat)");
    const QString fo1 = tr("Fallout 1 archive (*.dat)");
    QString selected = fo2;
    const QString start = QDir(config.outputDir).filePath(config.targetLanguage + QStringLiteral(".dat"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export to a DAT archive"), start, fo2 + QStringLiteral(";;") + fo1, &selected);
    if (path.isEmpty())
        return;
    const auto format = selected == fo1 ? DatArchive::Format::Fallout1 : DatArchive::Format::Fallout2;

    QVector<DatWriteItem> items;
    const QDir root(config.outputDir);
    QDirIterator files(textDir, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString abs = files.next();
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        DatWriteItem item;
        item.path = root.relativeFilePath(abs);
        item.data = f.readAll();
        items.push_back(item);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = DatWriter::write(path, format, items, &error);
    QApplication::restoreOverrideCursor();
    if (ok)
        QMessageBox::information(this, tr("Export"), tr("%1 files written to\n%2").arg(items.size()).arg(path));
    else
        QMessageBox::critical(this, tr("Export"), tr("Cannot write the archive:\n%1").arg(error));
}

// ----------------------------------------------------------------------------- misc

void MainWindow::applyThemeToAll()
{
    for (TranslationEditor *e : editors())
        e->applyTheme();
    update();
}

void MainWindow::updateUi()
{
    const bool anyEditor = currentEditor() != nullptr;
    const bool anyProject = !m_sessions.isEmpty();
    m_stack->setCurrentIndex(anyEditor ? 1 : (m_tabs->count() > 0 ? 1 : 0));
    for (QAction *a : {m_actSave, m_actCloseTab, m_actReview, m_actValidate, m_actCopySource, m_actConfirm, m_actNext, m_actPrev,
                       m_actNextUntranslated, m_actPrevUntranslated, m_actFind, m_actTranslateLine, m_actMassTranslate})
        a->setEnabled(anyEditor);
    m_actSaveAll->setEnabled(anyEditor);
    {
        MsgTableModel *model = anyEditor ? currentEditor()->model() : nullptr;
        m_actUndoOp->setEnabled(model && model->canUndo());
        m_actRedoOp->setEnabled(model && model->canRedo());
        m_actUndoOp->setText(model && model->canUndo() ? tr("Undo the operation: %1").arg(model->undoLabel())
                                                       : tr("Undo the last operation"));
        m_actRedoOp->setText(model && model->canRedo() ? tr("Redo the operation: %1").arg(model->redoLabel())
                                                       : tr("Redo the operation"));
    }
    for (QAction *a : {m_actSettings, m_actExport, m_actImport, m_actRefresh, m_actRebuild, m_actCloseProject})
        a->setEnabled(anyProject && m_sessions.contains(m_activeProject));
    m_actBuildArchive->setEnabled(anyProject && m_sessions.contains(m_activeProject)
                                  && OutputArchive::enabled(m_sessions.value(m_activeProject).project->config()));

    QString title = QStringLiteral("Fallout Babylon");
    if (!m_activeProject.isEmpty() && anyProject)
        title = QStringLiteral("%1 — Fallout Babylon").arg(activeProjectName());
    setWindowTitle(title);

    if (anyEditor && m_sessions.contains(projectIdOf(currentEditor()))) {
        const ProjectConfig &c = m_sessions.value(projectIdOf(currentEditor())).project->config();
        statusBar()->showMessage(tr("%1 → %2  ·  encoding %3 / %4")
                                     .arg(c.sourceLanguage, c.targetLanguage, c.sourceEncoding, c.targetEncoding));
    } else {
        statusBar()->clearMessage();
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Fallout Babylon"),
                       tr("<h3>Fallout Babylon %1</h3>"
                          "<p>Translation editor for the dialogue and text files of Fallout 1 and Fallout 2 mods.</p>"
                          "<p>Built with Qt %2 and Scintilla.</p>")
                           .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmClose(editors())) {
        event->ignore();
        return;
    }
    for (auto &s : m_sessions)
        s.cancelAll();
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    persistOpenProjects();
    QThreadPool::globalInstance()->waitForDone();
    for (const QString &id : m_sessions.keys())
        flushArchive(id);
    event->accept();
}

// ----------------------------------------------------------------------------- output archive

void MainWindow::scheduleArchive(const QString &id)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd() || !OutputArchive::enabled(it->project->config()))
        return;
    m_archivePending.insert(id);
    m_archiveTimer.start();
}

void MainWindow::buildArchive(const QString &id)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd() || !OutputArchive::enabled(it->project->config()))
        return;
    if (m_archiveRunning.contains(id)) {
        m_archivePending.insert(id);      // changed while being written: once more afterwards
        return;
    }
    m_archiveRunning.insert(id);
    const std::shared_ptr<Project> project = it->project;
    statusBar()->showMessage(tr("Updating the DAT archive..."));
    QThreadPool::globalInstance()->start([this, id, project] {
        const OutputArchive::Result result = OutputArchive::build(*project);
        QMetaObject::invokeMethod(this, [this, id, result] { onArchiveBuilt(id, result); }, Qt::QueuedConnection);
    });
}

void MainWindow::onArchiveBuilt(const QString &id, const OutputArchive::Result &result)
{
    m_archiveRunning.remove(id);
    if (result.ok) {
        statusBar()->showMessage(tr("DAT archive updated: %1 translated file(s), %2 original(s).")
                                     .arg(result.translated).arg(result.originals), 8000);
    } else {
        m_archiveFailed.insert(id);
        statusBar()->showMessage(tr("DAT archive NOT updated: %1").arg(result.error));
    }
    if (result.ok)
        m_archiveFailed.remove(id);
    if (m_archivePending.contains(id) && result.ok)
        m_archiveTimer.start();
    emit archiveBuilt(id, result.ok);
}

void MainWindow::flushArchive(const QString &id)
{
    const auto it = m_sessions.constFind(id);
    if (it == m_sessions.constEnd() || !OutputArchive::enabled(it->project->config()))
        return;
    if (!m_archivePending.contains(id) && !m_archiveFailed.contains(id))
        return;
    m_archivePending.remove(id);
    // let a build in progress finish, then write the final state
    QThreadPool::globalInstance()->waitForDone();
    const OutputArchive::Result result = OutputArchive::build(*it->project);
    if (!result.ok)
        QMessageBox::warning(this, tr("DAT archive"), tr("The DAT archive could not be written:\n%1").arg(result.error));
    else
        m_archiveFailed.remove(id);
}
