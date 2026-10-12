#include "TodoPanel.h"

#include "core/Statistics.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString oneLine(const QString &text)
{
    QString s = text.simplified();
    return s.size() > 160 ? s.left(160) + QStringLiteral("…") : s;
}

}

TodoPanel::TodoPanel(QWidget *parent)
    : QWidget(parent)
{
    m_summary = new QLabel;
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({tr("File"), tr("ID"), tr("Source"), tr("Translation")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 110);
    m_table->setColumnWidth(1, 50);
    m_table->setColumnWidth(2, 220);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_summary);
    layout->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::cellActivated, this, [this](int row, int) {
        const QTableWidgetItem *file = m_table->item(row, 0);
        if (file)
            emit itemActivated(file->data(Qt::UserRole).toString(), file->data(Qt::UserRole + 1).toInt());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, m_table, &QTableWidget::cellActivated);
    setItems({});
}

int TodoPanel::itemCount() const
{
    return m_table->rowCount();
}

void TodoPanel::setItems(const QVector<TodoItem> &items)
{
    m_summary->setText(items.isEmpty() ? tr("Nothing to review. Mark lines with Ctrl+R.")
                                       : tr("%1 line(s) to review — double-click to open").arg(items.size()));
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(int(items.size()));
    for (int r = 0; r < items.size(); ++r) {
        const TodoItem &t = items.at(r);
        auto *file = new QTableWidgetItem(Statistics::displayNameFor(t.relPath));
        file->setData(Qt::UserRole, t.relPath);
        file->setData(Qt::UserRole + 1, t.id);
        file->setToolTip(t.relPath);
        m_table->setItem(r, 0, file);
        auto *id = new QTableWidgetItem(QString::number(t.id));
        id->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(r, 1, id);
        m_table->setItem(r, 2, new QTableWidgetItem(oneLine(t.source)));
        m_table->setItem(r, 3, new QTableWidgetItem(oneLine(t.target)));
        m_table->item(r, 2)->setToolTip(t.source);
        m_table->item(r, 3)->setToolTip(t.target);
    }
    m_table->setUpdatesEnabled(true);
}
