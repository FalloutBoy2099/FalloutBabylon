#include "StatsPanel.h"

#include "core/Project.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {

enum CategoryColumn { CatLines, CatDone, CatReview, CatWordsLeft, CatCharsLeft, CatColumns };

QTableWidgetItem *numberItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QString percentText(const FileStats &s)
{
    return QStringLiteral("%1%").arg(s.percent(), 0, 'f', s.percent() < 10 || s.percent() > 90 ? 1 : 0);
}

}

StatsPanel::StatsPanel(QWidget *parent)
    : QWidget(parent)
{
    m_title = new QLabel;
    m_title->setWordWrap(true);

    m_categories = new QTableWidget(4, CatColumns);
    m_categories->setHorizontalHeaderLabels({tr("Lines"), tr("Done"), tr("To review"), tr("Words left"), tr("Chars left")});
    m_categories->setVerticalHeaderLabels({tr("Game files"), tr("Dialogues"), tr("Other"), tr("Total")});
    m_categories->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_categories->setSelectionMode(QAbstractItemView::NoSelection);
    m_categories->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_categories->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_categories->setMaximumHeight(m_categories->horizontalHeader()->height() + 4 * (fontMetrics().height() + 10) + 8);

    m_currentLabel = new QLabel;
    m_currentLabel->setWordWrap(true);
    m_currentLabel->setTextFormat(Qt::RichText);

    m_onlyIncomplete = new QCheckBox(tr("Only unfinished files"));
    m_onlyIncomplete->setChecked(true);
    m_files = new QTableWidget(0, 4);
    m_files->setHorizontalHeaderLabels({tr("File / character"), tr("Done"), tr("Lines left"), tr("Words left")});
    m_files->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_files->setSelectionMode(QAbstractItemView::SingleSelection);
    m_files->verticalHeader()->hide();
    m_files->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Fixed widths set once per refresh: automatic resizing re-measures the whole column for every added row.
    for (int c = 1; c < 4; ++c)
        m_files->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Fixed);
    m_files->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_files->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(60);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refreshNow(); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_title);
    layout->addWidget(m_categories);
    layout->addWidget(m_currentLabel);
    layout->addWidget(m_onlyIncomplete);
    layout->addWidget(m_files, 1);

    connect(m_onlyIncomplete, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(m_files, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        emit fileActivated(m_files->item(row, 0)->data(Qt::UserRole).toString());
    });
    clear();
}

void StatsPanel::clear()
{
    m_name.clear();
    m_stats.clear();
    m_current.clear();
    m_known = false;
    refresh();
}

void StatsPanel::setProject(const QString &name, const QVector<FileStats> &stats)
{
    m_name = name;
    m_stats.clear();
    for (const FileStats &s : stats)
        m_stats.insert(s.relPath, s);
    m_known = true;
    refresh();
}

void StatsPanel::updateFile(const FileStats &stats)
{
    m_stats.insert(stats.relPath, stats);
    refresh();
}

void StatsPanel::setCurrentFile(const QString &relPath)
{
    m_current = relPath;
    refresh();
}

QString StatsPanel::summary(const FileStats &s)
{
    return tr("<b>%1</b> — %2 done<br>%3 lines (%4 untranslated, %5 to review)<br>"
              "%6 words and %7 characters left (of %8 words)")
        .arg(s.displayName.toHtmlEscaped(), percentText(s))
        .arg(s.lines).arg(s.untranslated).arg(s.review)
        .arg(s.wordsRemaining).arg(s.charsRemaining).arg(s.words);
}

void StatsPanel::refresh()
{
    m_refreshTimer.start();
}

void StatsPanel::refreshNow()
{
    if (m_name.isEmpty()) {
        m_title->setText(tr("No project open."));
        m_categories->setVisible(false);
        m_currentLabel->clear();
        m_files->setRowCount(0);
        return;
    }
    m_categories->setVisible(true);
    m_title->setText(m_known ? tr("<b>%1</b>").arg(m_name.toHtmlEscaped()) : tr("<b>%1</b> — computing...").arg(m_name.toHtmlEscaped()));

    FileStats byKind[3], total;
    for (const FileStats &s : std::as_const(m_stats)) {
        byKind[qBound(0, s.kind, 2)].add(s);
        total.add(s);
    }
    const FileStats rows[4] = {byKind[0], byKind[1], byKind[2], total};
    for (int r = 0; r < 4; ++r) {
        m_categories->setItem(r, CatLines, numberItem(QString::number(rows[r].lines)));
        m_categories->setItem(r, CatDone, numberItem(rows[r].lines ? percentText(rows[r]) : QStringLiteral("-")));
        m_categories->setItem(r, CatReview, numberItem(QString::number(rows[r].review)));
        m_categories->setItem(r, CatWordsLeft, numberItem(QString::number(rows[r].wordsRemaining)));
        m_categories->setItem(r, CatCharsLeft, numberItem(QString::number(rows[r].charsRemaining)));
    }

    const auto it = m_stats.constFind(m_current);
    m_currentLabel->setText(it == m_stats.constEnd() ? QString() : summary(it.value()));

    QList<FileStats> list = m_stats.values();
    if (m_onlyIncomplete->isChecked())
        list.erase(std::remove_if(list.begin(), list.end(), [](const FileStats &s) { return s.isComplete(); }), list.end());
    std::sort(list.begin(), list.end(), [](const FileStats &a, const FileStats &b) {
        return a.wordsRemaining != b.wordsRemaining ? a.wordsRemaining > b.wordsRemaining : a.relPath < b.relPath;
    });
    if (list.size() > 300)
        list.erase(list.begin() + 300, list.end());
    m_files->setUpdatesEnabled(false);
    m_files->setRowCount(int(list.size()));
    for (int r = 0; r < list.size(); ++r) {
        const FileStats &s = list.at(r);
        auto *name = new QTableWidgetItem(s.displayName);
        name->setData(Qt::UserRole, s.relPath);
        name->setToolTip(s.relPath);
        m_files->setItem(r, 0, name);
        m_files->setItem(r, 1, numberItem(percentText(s)));
        m_files->setItem(r, 2, numberItem(QString::number(s.untranslated)));
        m_files->setItem(r, 3, numberItem(QString::number(s.wordsRemaining)));
    }
    for (int c = 1; c < 4; ++c)
        m_files->resizeColumnToContents(c);
    m_files->setUpdatesEnabled(true);
}
