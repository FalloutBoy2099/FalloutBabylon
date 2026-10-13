#pragma once

#include "core/OutputArchive.h"
#include "core/Project.h"
#include "core/ProjectSearch.h"

#include <QHash>
#include <QSet>
#include <QMainWindow>
#include <QTimer>
#include <atomic>
#include <memory>

class DialogTreePanel;
class GlossaryPanel;
class PipboyPanel;
class ProjectTree;
class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QProgressBar;
class QStackedWidget;
class QualityPanel;
class QTabWidget;
class SearchPanel;
class StatsPanel;
class TodoPanel;
class TranslationService;
class TranslationEditor;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    // Reopens the projects that were open when the application was closed.
    void restoreSession();

    bool openProject(const ProjectConfig &config);
    void openFile(const QString &id, const QString &relPath);
    bool analysisRunning() const;
    void applyThemeToAll();

signals:
    // The output .dat archive of a project was rewritten (or failed).
    void archiveBuilt(const QString &id, bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // The glossary and the statistics are computed by independent background jobs, each with its own
    // cancellation flag and generation number (results of a superseded job are dropped).
    struct Job {
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
        int generation = 0;
        bool running = false;
    };
    struct Session {
        std::shared_ptr<Project> project;
        QHash<QString, FileStats> stats;
        Job glossaryJob, statsJob;
        int statsDone = 0, statsTotal = 0;
        void cancelAll()
        {
            glossaryJob.cancel->store(true);
            statsJob.cancel->store(true);
        }
    };

    void createActions();
    void createMenus();
    void createDocks();

    // projects
    void newProject();
    void openGameFolder();
    void newProjectFromFolder(const QString &folder);
    void editProjectSettings(const QString &id);
    bool closeProject(const QString &id);
    void persistOpenProjects();
    void setActiveProject(const QString &id);

    // background work: glossary and statistics are computed off the UI thread
    void startAnalysis(const QString &id, bool glossary, bool stats);
    void onGlossaryReady(const QString &id, int generation, const std::shared_ptr<Glossary> &glossary);
    void onStatsBatch(const QString &id, int generation, const QVector<FileStats> &batch, int done, int total);
    void scheduleGlossaryRebuild(const QString &id);
    void updateProgress();
    void refreshTodo();
    void refreshDialogTree();

    // files
    TranslationEditor *currentEditor() const;
    QList<TranslationEditor *> editors() const;
    QList<TranslationEditor *> editorsOf(const QString &id) const;
    QString projectIdOf(const TranslationEditor *editor) const;
    void updateTabTitle(TranslationEditor *editor);
    bool closeTab(int index);
    bool saveAll();
    bool confirmClose(const QList<TranslationEditor *> &list);
    void onEditorStats(TranslationEditor *editor, const FileStats &stats);

    // Output as a .dat archive: rebuilt shortly after the files were saved.
    void scheduleArchive(const QString &id);
    void buildArchive(const QString &id);
    void onArchiveBuilt(const QString &id, const OutputArchive::Result &result);
    void flushArchive(const QString &id);

    void exportToDat(const QString &id);
    void importTranslation(const QString &id);
    void replaceInProject(const ProjectSearch::Options &options, const QString &replacement,
                          const QVector<ProjectSearch::Hit> &hits);
    void openOutputFolder(const QString &id);
    void updateUi();
    void showAbout();
    QString activeProjectName() const;

    QHash<QString, Session> m_sessions;
    QStringList m_order;                 // open project ids, in opening order
    QString m_activeProject;
    int m_generationCounter = 0;

    QStackedWidget *m_stack = nullptr;
    QTabWidget *m_tabs = nullptr;
    ProjectTree *m_tree = nullptr;
    GlossaryPanel *m_glossaryPanel = nullptr;
    StatsPanel *m_statsPanel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    DialogTreePanel *m_dialogPanel = nullptr;
    PipboyPanel *m_pipboyPanel = nullptr;
    SearchPanel *m_searchPanel = nullptr;
    QDockWidget *m_searchDock = nullptr;
    QualityPanel *m_qualityPanel = nullptr;
    QDockWidget *m_qualityDock = nullptr;
    QTimer m_dialogTimer;
    TodoPanel *m_todoPanel = nullptr;
    QTimer m_todoTimer;
    QTimer m_glossaryTimer;
    QStringList m_glossaryPending;
    QTimer m_archiveTimer;
    QSet<QString> m_archivePending, m_archiveRunning, m_archiveFailed;

    QAction *m_actSave = nullptr, *m_actSaveAll = nullptr, *m_actCloseTab = nullptr;
    QAction *m_actReview = nullptr, *m_actCopySource = nullptr, *m_actConfirm = nullptr;
    QAction *m_actNext = nullptr, *m_actPrev = nullptr, *m_actNextUntranslated = nullptr, *m_actPrevUntranslated = nullptr;
    QAction *m_actFind = nullptr, *m_actSettings = nullptr, *m_actExport = nullptr, *m_actRefresh = nullptr;
    QAction *m_actRebuild = nullptr, *m_actCloseProject = nullptr;
    QAction *m_actImport = nullptr, *m_actBuildArchive = nullptr, *m_actSearchProject = nullptr, *m_actQuality = nullptr;
    QAction *m_actUndoOp = nullptr, *m_actRedoOp = nullptr, *m_actValidate = nullptr;
    QAction *m_actTranslateLine = nullptr, *m_actMassTranslate = nullptr, *m_actAppSettings = nullptr;
    TranslationService *m_service = nullptr;
    QList<QAction *> m_dockToggles;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_savedMenu = nullptr;
};
