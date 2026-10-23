#include "SuggestionPanel.h"

#include "core/Statistics.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { ColScore, ColTranslation, ColSource, ColOrigin, ColCount };

QString oneLine(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('\n'), QStringLiteral(" ⏎ "));
    return t;
}
}

SuggestionPanel::SuggestionPanel(QWidget *parent)
    : QWidget(parent)
{
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_minimum = new QSpinBox;
    m_minimum->setRange(40, 100);
    m_minimum->setSuffix(QStringLiteral(" %"));
    m_minimum->setValue(QSettings().value(QStringLiteral("suggestions/minimum"), 70).toInt());
    m_minimum->setToolTip(tr("Minimum similarity of the texts offered"));

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({tr("%"), tr("Translation"), tr("Similar text"), tr("Origin")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColScore, 44);
    m_table->setColumnWidth(ColTranslation, 240);
    m_table->setColumnWidth(ColSource, 200);

    auto *top = new QHBoxLayout;
    top->addWidget(m_status, 1);
    top->addWidget(new QLabel(tr("Minimum:")));
    top->addWidget(m_minimum);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(top);
    layout->addWidget(m_table, 1);

    connect(m_minimum, &QSpinBox::valueChanged, this, [this](int v) {
        QSettings().setValue(QStringLiteral("suggestions/minimum"), v);
        refresh();
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { useRow(row); });
    connect(m_table, &QTableWidget::itemActivated, this, [this](QTableWidgetItem *item) { useRow(item->row()); });
    clear();
}

void SuggestionPanel::setIndex(const std::shared_ptr<TranslationIndex> &index)
{
    m_index = index;
    refresh();
}

void SuggestionPanel::showFor(const QString &relPath, int id, const QString &source)
{
    m_relPath = relPath;
    m_id = id;
    m_source = source;
    refresh();
}

void SuggestionPanel::clear()
{
    m_relPath.clear();
    m_source.clear();
    m_id = -1;
    refresh();
}

int SuggestionPanel::suggestionCount() const
{
    return m_table->rowCount();
}

void SuggestionPanel::refresh()
{
    m_matches.clear();
    m_table->setRowCount(0);
    if (m_id < 0 || m_source.trimmed().isEmpty()) {
        m_status->setText(tr("Select a line to see what was already translated for the same or a similar text."));
        return;
    }
    if (!m_index) {
        m_status->setText(tr("Building the translation memory of the project..."));
        return;
    }
    if (!Statistics::isTranslatable(m_source)) {
        m_status->setText(tr("This line has nothing to translate."));
        return;
    }
    m_matches = m_index->find(m_source, 8, m_minimum->value() / 100.0, m_relPath, m_id);
    m_table->setRowCount(int(m_matches.size()));
    for (int r = 0; r < m_matches.size(); ++r) {
        const TranslationIndex::Match &m = m_matches.at(r);
        auto *score = new QTableWidgetItem(QString::number(m.score >= 1.0 ? 100 : int(m.score * 100)));
        score->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, ColScore, score);
        auto *target = new QTableWidgetItem(oneLine(m.target));
        target->setToolTip(m.target);
        m_table->setItem(r, ColTranslation, target);
        auto *source = new QTableWidgetItem(oneLine(m.source));
        source->setToolTip(m.source);
        m_table->setItem(r, ColSource, source);
        QString origin = Statistics::displayNameFor(m.relPath) + QStringLiteral("#") + QString::number(m.id);
        if (m.count > 1)
            origin += QStringLiteral(" (×%1)").arg(m.count);
        m_table->setItem(r, ColOrigin, new QTableWidgetItem(origin));
    }
    m_status->setText(m_matches.isEmpty() ? tr("No similar translated text.")
                                          : tr("%1 suggestion(s): double-click to use one.").arg(m_matches.size()));
}

void SuggestionPanel::useRow(int row)
{
    if (row < 0 || row >= m_matches.size())
        return;
    emit useRequested(m_matches.at(row).target, m_matches.at(row).score);
}
