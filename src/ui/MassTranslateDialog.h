#pragma once

#include <QDialog>
#include <QVector>
#include <memory>

class MsgTableModel;
class Project;
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class TranslationService;

// Mass translation of a file with DeepL: choose the lines, translate, review the proposals in a table
// (they can be edited or unchecked), then apply the accepted ones to the file.
class MassTranslateDialog : public QDialog
{
    Q_OBJECT

public:
    MassTranslateDialog(TranslationService *service, std::shared_ptr<Project> project, MsgTableModel *model,
                        const QVector<int> &selectedRows, QWidget *parent = nullptr);
    ~MassTranslateDialog() override;

    // Starts the translation of the chosen lines (asks for confirmation unless told otherwise).
    void run(bool confirm = true);
    bool isRunning() const { return m_running; }
    // Writes the checked proposals into the model; returns how many were applied.
    int applyProposals();

private:
    QVector<int> candidateRows() const;
    void updateEstimate();
    void pump();
    void finish();
    void stop();
    void apply();

    TranslationService *m_service;
    std::shared_ptr<Project> m_project;
    MsgTableModel *m_model;
    QVector<int> m_selected;

    QRadioButton *m_untranslated = nullptr;
    QRadioButton *m_selection = nullptr;
    QRadioButton *m_all = nullptr;
    QCheckBox *m_overwrite = nullptr;
    QLabel *m_estimate = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_translate = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_apply = nullptr;

    // run state
    QVector<int> m_queue;        // model rows still to translate
    int m_next = 0;
    int m_inFlight = 0;
    int m_done = 0;
    int m_failed = 0;
    bool m_running = false;
    bool m_stopRequested = false;
    QString m_glossaryId;
    qint64 m_quotaLeft = -1;
    int m_generation = 0;
};
