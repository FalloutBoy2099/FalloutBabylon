#include "QualityPanel.h"

#include "core/Statistics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QThreadPool>
#include <QVBoxLayout>

namespace {
enum Column { ColKind, ColFile, ColId, ColMessage, ColTarget, ColCount };

QString csv(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QLatin1Char('"') + t + QLatin1Char('"');
}
}

QualityPanel::QualityPanel(QWidget *parent)
    : QWidget(parent)
{
    const struct { QualityCheck::Kind kind; QString label; } kinds[] = {
        {QualityCheck::Tokens, tr("Lost tokens (@Name@, %d, {field})")},
        {QualityCheck::Spacing, tr("Spacing")},
        {QualityCheck::Encoding, tr("Characters the encoding cannot hold")},
        {QualityCheck::Length, tr("Too long")},
        {QualityCheck::Duplicates, tr("Inconsistent duplicates")},
        {QualityCheck::Glossary, tr("Glossary")},
    };
    auto *boxes = new QVBoxLayout;
    for (const auto &k : kinds) {
        auto *box = new QCheckBox(k.label);
        box->setChecked(true);
        m_kindBoxes.push_back(box);
        m_kindValues.push_back(k.kind);
        boxes->addWidget(box);
    }
    m_maxChars = new QSpinBox;
    m_maxChars->setRange(0, 5000);
    m_maxChars->setSpecialValueText(tr("no limit"));
    m_maxChars->setToolTip(tr("Maximum number of characters of a translation (0: only report translations much longer than the source)"));
    auto *limit = new QHBoxLayout;
    limit->addWidget(new QLabel(tr("Maximum length:")));
    limit->addWidget(m_maxChars);
    limit->addStretch();

    m_run = new QPushButton(tr("Run the checks"));
    m_run->setToolTip(tr("The checks apply to the saved files: save your changes first."));
    m_export = new QPushButton(tr("Export (CSV)..."));
    m_export->setEnabled(false);
    m_filter = new QComboBox;
    m_filter->addItem(tr("All problems"), 0);
    for (const auto &k : kinds)
        m_filter->addItem(QualityCheck::kindName(k.kind), int(k.kind));
    m_status = new QLabel(tr("No project open."));
    m_status->setWordWrap(true);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({tr("Check"), tr("File"), tr("ID"), tr("Problem"), tr("Translation")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColKind, 110);
    m_table->setColumnWidth(ColFile, 110);
    m_table->setColumnWidth(ColId, 50);
    m_table->setColumnWidth(ColMessage, 240);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_run);
    buttons->addWidget(m_filter);
    buttons->addStretch();
    buttons->addWidget(m_export);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(boxes);
    layout->addLayout(limit);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addWidget(m_table, 1);

    connect(m_run, &QPushButton::clicked, this, [this] { runNow(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { applyFilter(); });
    connect(m_export, &QPushButton::clicked, this, [this] { exportCsv(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *file = m_table->item(row, ColFile);
        if (file)
            emit openRequested(file->data(Qt::UserRole).toString(), file->data(Qt::UserRole + 1).toInt());
    });
    m_run->setEnabled(false);
}

QualityPanel::~QualityPanel()
{
    m_cancel->store(true);
}

void QualityPanel::setKinds(int kinds)
{
    for (int i = 0; i < m_kindBoxes.size(); ++i)
        m_kindBoxes[i]->setChecked(kinds & m_kindValues[i]);
}

QualityCheck::Options QualityPanel::options() const
{
    QualityCheck::Options o;
    o.kinds = 0;
    for (int i = 0; i < m_kindBoxes.size(); ++i)
        if (m_kindBoxes[i]->isChecked())
            o.kinds |= m_kindValues[i];
    o.maxChars = m_maxChars->value();
    return o;
}

void QualityPanel::setProject(const std::shared_ptr<Project> &project)
{
    if (project == m_project)
        return;
    m_cancel->store(true);
    m_running = false;
    m_project = project;
    m_issues.clear();
    m_table->setRowCount(0);
    m_run->setEnabled(project != nullptr);
    m_export->setEnabled(false);
    m_status->setText(project ? tr("Press \"Run the checks\".") : tr("No project open."));
}

int QualityPanel::resultCount() const
{
    return m_table->rowCount();
}

void QualityPanel::runNow()
{
    if (!m_project)
        return;
    const QualityCheck::Options opts = options();
    if (!opts.kinds) {
        m_status->setText(tr("Select at least one check."));
        return;
    }
    m_cancel->store(true);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const int generation = ++m_generation;
    m_running = true;
    m_run->setEnabled(false);
    m_status->setText(tr("Checking..."));

    const auto project = m_project;
    const auto cancel = m_cancel;
    // The glossary may be replaced on the UI thread while the checks run: work on a copy.
    const std::shared_ptr<const Glossary> glossary = project->glossarySnapshot();
    QThreadPool::globalInstance()->start([this, project, cancel, glossary, opts, generation] {
        const QVector<QualityCheck::Issue> issues = QualityCheck::run(
            *project, glossary.get(), opts,
            [this, cancel, generation](int done, int total) {
                if (cancel->load())
                    return;
                QMetaObject::invokeMethod(this, [this, generation, done, total] {
                    if (generation == m_generation)
                        m_status->setText(tr("Checking... %1 / %2 files").arg(done).arg(total));
                }, Qt::QueuedConnection);
            },
            cancel.get());
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(this, [this, generation, issues] { showResults(generation, issues); }, Qt::QueuedConnection);
    });
}

void QualityPanel::showResults(int generation, const QVector<QualityCheck::Issue> &issues)
{
    if (generation != m_generation)
        return;
    m_running = false;
    m_run->setEnabled(true);
    m_issues = issues;
    m_export->setEnabled(!issues.isEmpty());
    applyFilter();
}

void QualityPanel::applyFilter()
{
    const int kind = m_filter->currentData().toInt();
    QVector<const QualityCheck::Issue *> shown;
    for (const QualityCheck::Issue &i : std::as_const(m_issues))
        if (!kind || int(i.kind) == kind)
            shown.push_back(&i);
    constexpr int kMaxRows = 5000;
    const int rows = int(qMin<qsizetype>(shown.size(), kMaxRows));
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const QualityCheck::Issue &i = *shown.at(r);
        m_table->setItem(r, ColKind, new QTableWidgetItem(QualityCheck::kindName(i.kind)));
        auto *file = new QTableWidgetItem(Statistics::displayNameFor(i.relPath));
        file->setData(Qt::UserRole, i.relPath);
        file->setData(Qt::UserRole + 1, i.id);
        file->setToolTip(i.relPath);
        m_table->setItem(r, ColFile, file);
        m_table->setItem(r, ColId, new QTableWidgetItem(QString::number(i.id)));
        auto *message = new QTableWidgetItem(i.message);
        message->setToolTip(i.message);
        m_table->setItem(r, ColMessage, message);
        QString target = i.target;
        target.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
        auto *t = new QTableWidgetItem(target);
        t->setToolTip(i.source + QStringLiteral("\n→\n") + i.target);
        m_table->setItem(r, ColTarget, t);
    }
    m_table->setUpdatesEnabled(true);
    QString status = m_issues.isEmpty() ? tr("No problem found.") : tr("%1 problem(s)").arg(m_issues.size());
    if (kind)
        status += tr(" — %1 shown").arg(shown.size());
    if (shown.size() > kMaxRows)
        status += QStringLiteral("  ⚠ ") + tr("only the first %1 are listed (use the export for all)").arg(kMaxRows);
    m_status->setText(status);
}

void QualityPanel::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export the report"), QStringLiteral("quality-report.csv"),
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export the report"), tr("Cannot write %1.").arg(path));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out << "check,file,id,problem,source,translation\r\n";
    for (const QualityCheck::Issue &i : std::as_const(m_issues))
        out << csv(QualityCheck::kindName(i.kind)) << ',' << csv(i.relPath) << ',' << i.id << ',' << csv(i.message) << ','
            << csv(i.source) << ',' << csv(i.target) << "\r\n";
}
