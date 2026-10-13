#include "SearchPanel.h"

#include "core/Statistics.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QThreadPool>
#include <QVBoxLayout>

namespace {
enum Column { ColCheck, ColFile, ColId, ColText, ColAfter, ColCount };
constexpr int HitRole = Qt::UserRole + 1;

QString oneLine(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
    return t;
}
}

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent)
{
    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Search in the whole project..."));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit;
    m_replace->setPlaceholderText(tr("Replace with (in the translation)..."));
    m_scope = new QComboBox;
    m_scope->addItem(tr("In the translation"), 0);
    m_scope->addItem(tr("In the source"), 1);
    m_scope->addItem(tr("In both"), 2);
    m_regex = new QCheckBox(tr("Regular expression"));
    m_case = new QCheckBox(tr("Match case"));
    m_word = new QCheckBox(tr("Whole words"));
    m_searchButton = new QPushButton(tr("Search"));
    m_replaceChecked = new QPushButton(tr("Replace the checked lines"));
    m_replaceAll = new QPushButton(tr("Replace all"));
    m_status = new QLabel;
    m_status->setWordWrap(true);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QString(), tr("File"), tr("ID"), tr("Text"), tr("After replacement")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColCheck, 28);
    m_table->setColumnWidth(ColFile, 110);
    m_table->setColumnWidth(ColId, 50);
    m_table->setColumnWidth(ColText, 260);

    auto *optionsRow = new QHBoxLayout;
    optionsRow->addWidget(m_scope);
    optionsRow->addWidget(m_regex);
    optionsRow->addWidget(m_case);
    optionsRow->addWidget(m_word);
    optionsRow->addStretch();
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_searchButton);
    buttons->addStretch();
    buttons->addWidget(m_replaceChecked);
    buttons->addWidget(m_replaceAll);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_find);
    layout->addWidget(m_replace);
    layout->addLayout(optionsRow);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addWidget(m_table, 1);

    connect(m_find, &QLineEdit::returnPressed, this, [this] { searchNow(); });
    connect(m_replace, &QLineEdit::returnPressed, this, [this] { searchNow(); });
    connect(m_searchButton, &QPushButton::clicked, this, [this] { searchNow(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *file = m_table->item(row, ColFile);
        if (file)
            emit openRequested(file->data(Qt::UserRole).toString(), file->data(Qt::UserRole + 1).toInt());
    });
    connect(m_replaceChecked, &QPushButton::clicked, this, [this] {
        emit replaceRequested(options(), replacement(), checkedHits());
    });
    connect(m_replaceAll, &QPushButton::clicked, this, [this] {
        QVector<ProjectSearch::Hit> all;
        for (const auto &h : std::as_const(m_hits))
            if (h.inTarget)
                all.push_back(h);
        emit replaceRequested(options(), replacement(), all);
    });
    m_replaceChecked->setEnabled(false);
    m_replaceAll->setEnabled(false);
    m_status->setText(tr("No project open."));
}

SearchPanel::~SearchPanel()
{
    m_cancel->store(true);
}

ProjectSearch::Options SearchPanel::options() const
{
    ProjectSearch::Options o;
    o.text = m_find->text();
    const int scope = m_scope->currentData().toInt();
    o.inTarget = scope == 0 || scope == 2;
    o.inSource = scope == 1 || scope == 2;
    o.regex = m_regex->isChecked();
    o.caseSensitive = m_case->isChecked();
    o.wholeWord = m_word->isChecked();
    return o;
}

QString SearchPanel::replacement() const
{
    return m_replace->text();
}

void SearchPanel::setProject(const std::shared_ptr<Project> &project)
{
    if (project == m_project)
        return;
    m_cancel->store(true);
    m_project = project;
    m_hits.clear();
    m_table->setRowCount(0);
    m_replaceChecked->setEnabled(false);
    m_replaceAll->setEnabled(false);
    m_status->setText(project ? tr("Type a text and press Enter.") : tr("No project open."));
}

void SearchPanel::focusQuery()
{
    m_find->setFocus();
    m_find->selectAll();
}

int SearchPanel::resultCount() const
{
    return m_table->rowCount();
}

void SearchPanel::searchNow()
{
    if (!m_project)
        return;
    const ProjectSearch::Options opts = options();
    const QRegularExpression re = opts.expression();
    if (opts.text.isEmpty()) {
        m_status->setText(tr("Type a text to search."));
        return;
    }
    if (!re.isValid()) {
        m_status->setText(tr("Invalid regular expression: %1").arg(re.errorString()));
        return;
    }
    m_cancel->store(true);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const int generation = ++m_generation;
    m_status->setText(tr("Searching..."));
    m_searchButton->setEnabled(false);

    const auto project = m_project;
    const auto cancel = m_cancel;
    const QString repl = m_replace->text();
    QThreadPool::globalInstance()->start([this, project, cancel, opts, repl, generation] {
        bool truncated = false;
        // an empty replacement field still previews "replace by nothing" only when the user filled the field
        const QVector<ProjectSearch::Hit> hits =
            ProjectSearch::search(*project, opts, repl.isEmpty() ? QString() : repl, {}, cancel.get(), 5000, &truncated);
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(this, [this, generation, hits, truncated] { showResults(generation, hits, truncated); },
                                  Qt::QueuedConnection);
    });
}

void SearchPanel::showResults(int generation, const QVector<ProjectSearch::Hit> &hits, bool truncated)
{
    if (generation != m_generation)
        return;
    m_searchButton->setEnabled(true);
    m_hits = hits;
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(int(hits.size()));
    int targetHits = 0, occurrences = 0;
    for (int r = 0; r < hits.size(); ++r) {
        const ProjectSearch::Hit &h = hits.at(r);
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(h.inTarget ? Qt::Checked : Qt::Unchecked);
        if (!h.inTarget)
            check->setFlags(Qt::NoItemFlags);
        check->setData(HitRole, r);
        m_table->setItem(r, ColCheck, check);
        auto *file = new QTableWidgetItem(Statistics::displayNameFor(h.relPath) + (h.inTarget ? QString() : tr(" (source)")));
        file->setData(Qt::UserRole, h.relPath);
        file->setData(Qt::UserRole + 1, h.id);
        file->setToolTip(h.relPath);
        m_table->setItem(r, ColFile, file);
        m_table->setItem(r, ColId, new QTableWidgetItem(QString::number(h.id)));
        auto *text = new QTableWidgetItem(oneLine(h.text));
        text->setToolTip(h.text);
        m_table->setItem(r, ColText, text);
        m_table->setItem(r, ColAfter, new QTableWidgetItem(oneLine(h.after)));
        targetHits += h.inTarget ? 1 : 0;
        occurrences += h.occurrences;
    }
    m_table->setUpdatesEnabled(true);
    m_replaceChecked->setEnabled(targetHits > 0);
    m_replaceAll->setEnabled(targetHits > 0);
    QString status = hits.isEmpty() ? tr("No result.") : tr("%1 line(s), %2 occurrence(s)").arg(hits.size()).arg(occurrences);
    if (truncated)
        status += QStringLiteral("  ⚠ ") + tr("only the first results are shown");
    m_status->setText(status);
}

QVector<ProjectSearch::Hit> SearchPanel::checkedHits() const
{
    QVector<ProjectSearch::Hit> checked;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem *c = m_table->item(r, ColCheck);
        if (c && c->checkState() == Qt::Checked)
            checked.push_back(m_hits.at(c->data(HitRole).toInt()));
    }
    return checked;
}
