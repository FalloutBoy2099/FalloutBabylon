#include "MassTranslateDialog.h"

#include "MsgTableModel.h"
#include "TranslationEditor.h"
#include "TranslationService.h"
#include "core/AppSettings.h"
#include "core/Statistics.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int kConcurrency = 3;
enum Column { ColApply, ColId, ColSource, ColTranslation, ColNote, ColCount };
constexpr int RowRole = Qt::UserRole + 1;

}

MassTranslateDialog::MassTranslateDialog(TranslationService *service, std::shared_ptr<Project> project,
                                         MsgTableModel *model, const QVector<int> &selectedRows, QWidget *parent)
    : QDialog(parent)
    , m_service(service)
    , m_project(std::move(project))
    , m_model(model)
    , m_selected(selectedRows)
{
    setWindowTitle(tr("Mass translation with DeepL"));
    resize(980, 640);

    int untranslated = 0;
    for (int i = 0; i < m_model->rowCount(); ++i)
        untranslated += m_model->status(i) == SegmentStatus::Untranslated && Statistics::isTranslatable(m_model->row(i).source);

    m_untranslated = new QRadioButton(tr("Untranslated lines (%1)").arg(untranslated));
    m_selection = new QRadioButton(tr("Selected lines (%1)").arg(m_selected.size()));
    m_all = new QRadioButton(tr("All lines (%1)").arg(m_model->rowCount()));
    m_untranslated->setChecked(true);
    m_selection->setEnabled(!m_selected.isEmpty());
    m_overwrite = new QCheckBox(tr("Replace existing translations"));
    m_overwrite->setEnabled(false);
    m_estimate = new QLabel;
    m_status = new QLabel;

    auto *scope = new QHBoxLayout;
    scope->addWidget(m_untranslated);
    scope->addWidget(m_selection);
    scope->addWidget(m_all);
    scope->addWidget(m_overwrite);
    scope->addStretch();

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({tr("Apply"), tr("ID"), tr("Source"), tr("DeepL proposal (editable)"), tr("Note")});
    m_table->horizontalHeader()->setSectionResizeMode(ColApply, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColId, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColTranslation, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColNote, QHeaderView::Interactive);
    m_table->setColumnWidth(ColNote, 160);
    m_table->verticalHeader()->hide();
    m_table->setWordWrap(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_translate = new QPushButton(tr("Translate"));
    m_stop = new QPushButton(tr("Stop"));
    m_stop->setEnabled(false);
    m_apply = new QPushButton(tr("Apply the checked proposals"));
    m_apply->setEnabled(false);
    auto *close = new QPushButton(tr("Close"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_translate);
    buttons->addWidget(m_stop);
    buttons->addStretch();
    buttons->addWidget(m_apply);
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(scope);
    layout->addWidget(m_estimate);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(m_untranslated, &QRadioButton::toggled, this, [this] { updateEstimate(); });
    connect(m_selection, &QRadioButton::toggled, this, [this] { updateEstimate(); });
    connect(m_all, &QRadioButton::toggled, this, [this] {
        m_overwrite->setEnabled(m_all->isChecked() || m_selection->isChecked());
        updateEstimate();
    });
    connect(m_selection, &QRadioButton::toggled, this, [this] {
        m_overwrite->setEnabled(m_all->isChecked() || m_selection->isChecked());
    });
    connect(m_overwrite, &QCheckBox::toggled, this, [this] { updateEstimate(); });
    connect(m_translate, &QPushButton::clicked, this, [this] { run(true); });
    connect(m_stop, &QPushButton::clicked, this, [this] { stop(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    updateEstimate();
    if (m_service->deeplConfigured()) {
        QPointer<MassTranslateDialog> self(this);
        m_service->deepl()->usage(AppSettings::deeplKey(), [self](qint64 used, qint64 limit, const QString &error) {
            if (!self || !error.isEmpty() || limit <= 0)
                return;
            self->m_quotaLeft = limit - used;
            self->updateEstimate();
        });
    } else {
        m_status->setText(tr("No DeepL API key: enter it in Tools > Settings."));
        m_translate->setEnabled(false);
    }
}

MassTranslateDialog::~MassTranslateDialog()
{
    ++m_generation;   // late answers are ignored
}

QVector<int> MassTranslateDialog::candidateRows() const
{
    QVector<int> rows;
    const bool selection = m_selection->isChecked();
    const bool all = m_all->isChecked();
    const bool overwrite = m_overwrite->isChecked() && (selection || all);
    const QVector<int> base = [&] {
        if (selection)
            return m_selected;
        QVector<int> every;
        for (int i = 0; i < m_model->rowCount(); ++i)
            every << i;
        return every;
    }();
    for (int row : base) {
        const TranslationRow &r = m_model->row(row);
        if (!r.inSource || !Statistics::isTranslatable(r.source))
            continue;
        if (!overwrite && m_model->status(row) != SegmentStatus::Untranslated)
            continue;
        rows << row;
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MassTranslateDialog::updateEstimate()
{
    if (m_running)
        return;
    qint64 chars = 0;
    const QVector<int> rows = candidateRows();
    for (int row : rows)
        chars += m_model->row(row).source.size();
    QString text = tr("%1 line(s), about %2 characters to send to DeepL.").arg(rows.size()).arg(chars);
    if (m_quotaLeft >= 0) {
        text += tr(" Remaining quota: %L1 characters.").arg(m_quotaLeft);
        if (chars > m_quotaLeft)
            text += QStringLiteral(" <b>%1</b>").arg(tr("Not enough quota for everything."));
    }
    m_estimate->setText(text);
}

void MassTranslateDialog::run(bool confirm)
{
    m_queue = candidateRows();
    if (m_queue.isEmpty()) {
        m_status->setText(tr("Nothing to translate with this choice."));
        return;
    }
    qint64 chars = 0;
    for (int row : std::as_const(m_queue))
        chars += m_model->row(row).source.size();
    if (confirm
        && QMessageBox::question(this, tr("DeepL"),
                                 tr("Send %1 line(s) (about %2 characters) to DeepL?").arg(m_queue.size()).arg(chars))
            != QMessageBox::Yes)
        return;

    m_table->setRowCount(0);
    m_table->setRowCount(int(m_queue.size()));
    for (int i = 0; i < m_queue.size(); ++i) {
        const TranslationRow &r = m_model->row(m_queue.at(i));
        auto *apply = new QTableWidgetItem;
        apply->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        apply->setCheckState(Qt::Unchecked);
        apply->setData(RowRole, m_queue.at(i));
        m_table->setItem(i, ColApply, apply);
        auto *id = new QTableWidgetItem(QString::number(r.id));
        id->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(i, ColId, id);
        auto *source = new QTableWidgetItem(r.source);
        source->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(i, ColSource, source);
        auto *proposal = new QTableWidgetItem;
        proposal->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsSelectable);
        m_table->setItem(i, ColTranslation, proposal);
        auto *note = new QTableWidgetItem;
        note->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(i, ColNote, note);
    }

    m_next = m_inFlight = m_done = m_failed = 0;
    m_running = true;
    m_stopRequested = false;
    ++m_generation;
    m_translate->setEnabled(false);
    m_stop->setEnabled(true);
    m_apply->setEnabled(false);
    m_progress->setRange(0, int(m_queue.size()));
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_status->setText(tr("Preparing the glossary..."));

    QPointer<MassTranslateDialog> self(this);
    const int generation = m_generation;
    m_service->ensureGlossary(m_project, [self, generation](const QString &id, const QString &warning) {
        if (!self || self->m_generation != generation)
            return;
        self->m_glossaryId = id;
        self->m_status->setText(warning.isEmpty() ? tr("Translating...") : warning);
        self->pump();
    });
}

void MassTranslateDialog::pump()
{
    while (m_running && !m_stopRequested && m_inFlight < kConcurrency && m_next < m_queue.size()) {
        const int slot = m_next++;
        const int row = m_queue.at(slot);
        const QString source = m_model->row(row).source;
        DeepLClient::Options options = m_service->deeplOptions(*m_project);
        options.glossaryId = m_glossaryId;
        const QString context = AppSettings::deeplUseContext() ? TranslationEditor::contextFor(m_model->doc(), row) : QString();
        ++m_inFlight;

        QPointer<MassTranslateDialog> self(this);
        const int generation = m_generation;
        m_service->deepl()->translate(source, context, options, [self, generation, slot](const QString &text, const QString &error) {
            if (!self || self->m_generation != generation)
                return;
            --self->m_inFlight;
            QTableWidgetItem *proposal = self->m_table->item(slot, ColTranslation);
            QTableWidgetItem *note = self->m_table->item(slot, ColNote);
            QTableWidgetItem *apply = self->m_table->item(slot, ColApply);
            if (text.isEmpty()) {
                ++self->m_failed;
                note->setText(error);
                // a quota or key problem will hit every following line: stop instead of failing them all
                if (error.contains(QStringLiteral("quota"), Qt::CaseInsensitive) || error.contains(QStringLiteral("key"), Qt::CaseInsensitive))
                    self->m_stopRequested = true;
            } else {
                proposal->setText(text);
                apply->setCheckState(Qt::Checked);
                if (!error.isEmpty()) {
                    note->setText(error);          // altered placeholder: shown, left unchecked
                    apply->setCheckState(Qt::Unchecked);
                }
            }
            ++self->m_done;
            self->m_progress->setValue(self->m_done);
            self->pump();
        });
    }
    if (m_running && m_inFlight == 0 && (m_stopRequested || m_next >= m_queue.size()))
        finish();
}

void MassTranslateDialog::finish()
{
    m_running = false;
    m_translate->setEnabled(true);
    m_stop->setEnabled(false);
    m_progress->setVisible(false);
    bool any = false;
    for (int i = 0; i < m_table->rowCount() && !any; ++i)
        any = !m_table->item(i, ColTranslation)->text().isEmpty();
    m_apply->setEnabled(any);
    m_status->setText(m_stopRequested ? tr("Stopped: %1 of %2 line(s) translated, %3 failed.").arg(m_done - m_failed).arg(m_queue.size()).arg(m_failed)
                                      : tr("Done: %1 line(s) translated, %2 failed. Check the proposals, then apply.")
                                            .arg(m_done - m_failed).arg(m_failed));
    updateEstimate();
}

void MassTranslateDialog::stop()
{
    m_stopRequested = true;
    m_stop->setEnabled(false);
    m_status->setText(tr("Stopping..."));
    if (m_inFlight == 0)
        finish();
}

int MassTranslateDialog::applyProposals()
{
    int applied = 0;
    const bool review = AppSettings::deeplMarkReview();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *apply = m_table->item(i, ColApply);
        const QString text = m_table->item(i, ColTranslation)->text();
        if (apply->checkState() != Qt::Checked || text.isEmpty())
            continue;
        const int row = apply->data(RowRole).toInt();
        m_model->setTarget(row, text);
        if (review)
            m_model->setReview(row, true);
        ++applied;
    }
    return applied;
}

void MassTranslateDialog::apply()
{
    const int applied = applyProposals();
    QMessageBox::information(this, tr("DeepL"), tr("%1 translation(s) applied. Save the file to keep them.").arg(applied));
    accept();
}
