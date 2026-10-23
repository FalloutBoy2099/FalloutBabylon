#include "ScriptStringsPanel.h"

#include "core/TextCodec.h"

#include <QBrush>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QThreadPool>
#include <QVBoxLayout>

namespace {
enum Column { ColScript, ColSource, ColTarget, ColRoom, ColCount };
constexpr int IndexRole = Qt::UserRole + 1;
}

ScriptStringsPanel::ScriptStringsPanel(QWidget *parent)
    : QWidget(parent)
{
    m_scan = new QPushButton(tr("Scan the scripts"));
    m_scan->setToolTip(tr("Looks for the texts written in the code of the scripts (not in the .msg files)."));
    m_apply = new QPushButton(tr("Write the translated scripts"));
    m_apply->setToolTip(tr("Writes scripts/<name>.int with the translations into the output (folder or archive)."));
    m_enlarge = new QCheckBox(tr("Allow longer texts (experimental)"));
    m_enlarge->setToolTip(tr("A text longer than the original is appended at the end of the script and the code is redirected to it. "
                             "The game reads strings through their offset and does not check where it points, but this is not "
                             "guaranteed: test it in the game. Without it, only translations that fit the room of the original are written."));
    m_enlarge->setChecked(QSettings().value(QStringLiteral("scriptStrings/enlarge"), false).toBool());
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter the texts..."));
    m_filter->setClearButtonEnabled(true);
    m_status = new QLabel(tr("No project open."));
    m_status->setWordWrap(true);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({tr("Script"), tr("Original text"), tr("Translation"), tr("Room")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(ColScript, 90);
    m_table->setColumnWidth(ColSource, 260);
    m_table->setColumnWidth(ColTarget, 260);

    auto *top = new QHBoxLayout;
    top->addWidget(m_scan);
    top->addWidget(m_apply);
    top->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(top);
    layout->addWidget(m_enlarge);
    layout->addWidget(m_filter);
    layout->addWidget(m_status);
    layout->addWidget(m_table, 1);

    connect(m_scan, &QPushButton::clicked, this, [this] { scanNow(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { emit applyRequested(translations(), allowEnlarge()); });
    connect(m_enlarge, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("scriptStrings/enlarge"), on);
        for (int r = 0; r < m_table->rowCount(); ++r)
            updateRowState(r);
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_filling || item->column() != ColTarget)
            return;
        const int index = item->data(IndexRole).toInt();
        const ScriptStrings::Text &t = m_texts.at(index);
        const QString text = item->text().trimmed();
        if (text.isEmpty())
            m_targets.remove(keyOf(t.script, t.operand));
        else
            m_targets.insert(keyOf(t.script, t.operand), text);
        updateRowState(item->row());
        emit translationsChanged(translations());
    });
    m_scan->setEnabled(false);
    m_apply->setEnabled(false);
}

ScriptStringsPanel::~ScriptStringsPanel()
{
    m_cancel->store(true);
}

QString ScriptStringsPanel::keyOf(const QString &script, quint32 operand)
{
    return script + QLatin1Char('#') + QString::number(operand);
}

bool ScriptStringsPanel::allowEnlarge() const
{
    return m_enlarge->isChecked();
}

void ScriptStringsPanel::setProject(const std::shared_ptr<Project> &project)
{
    if (project == m_project)
        return;
    m_cancel->store(true);
    m_scanning = false;
    m_project = project;
    m_texts.clear();
    m_targets.clear();
    m_table->setRowCount(0);
    m_scan->setEnabled(project != nullptr);
    m_apply->setEnabled(project != nullptr);
    if (project) {
        // translations already entered stay available without scanning again
        for (const ScriptTranslation &t : project->scriptTranslations())
            m_targets.insert(keyOf(t.script, t.operand), t.target);
        m_status->setText(project->scriptNames().isEmpty() ? tr("The sources of this project contain no compiled script.")
                                                           : tr("Press \"Scan the scripts\"."));
    } else {
        m_status->setText(tr("No project open."));
    }
}

int ScriptStringsPanel::rowCountShown() const
{
    int shown = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        shown += m_table->isRowHidden(r) ? 0 : 1;
    return shown;
}

void ScriptStringsPanel::scanNow()
{
    if (!m_project)
        return;
    m_cancel->store(true);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const int generation = ++m_generation;
    m_scanning = true;
    m_scan->setEnabled(false);
    m_status->setText(tr("Scanning the scripts..."));
    const auto project = m_project;
    const auto cancel = m_cancel;
    QThreadPool::globalInstance()->start([this, project, cancel, generation] {
        const QVector<ScriptStrings::Text> texts = ScriptStrings::scan(
            *project,
            [this, cancel, generation](int done, int total) {
                if (cancel->load())
                    return;
                QMetaObject::invokeMethod(this, [this, generation, done, total] {
                    if (generation == m_generation)
                        m_status->setText(tr("Scanning the scripts... %1 / %2").arg(done).arg(total));
                }, Qt::QueuedConnection);
            },
            cancel.get());
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(this, [this, generation, texts] {
            if (generation != m_generation)
                return;
            m_scanning = false;
            m_scan->setEnabled(true);
            m_texts = texts;
            fill();
        }, Qt::QueuedConnection);
    });
}

void ScriptStringsPanel::fill()
{
    m_filling = true;
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(int(m_texts.size()));
    for (int r = 0; r < m_texts.size(); ++r) {
        const ScriptStrings::Text &t = m_texts.at(r);
        auto *script = new QTableWidgetItem(t.script);
        script->setFlags(script->flags() & ~Qt::ItemIsEditable);
        script->setData(IndexRole, r);
        m_table->setItem(r, ColScript, script);
        auto *source = new QTableWidgetItem(t.source);
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);
        source->setToolTip(t.source);
        m_table->setItem(r, ColSource, source);
        auto *target = new QTableWidgetItem(m_targets.value(keyOf(t.script, t.operand)));
        target->setData(IndexRole, r);
        m_table->setItem(r, ColTarget, target);
        auto *room = new QTableWidgetItem;
        room->setFlags(room->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(r, ColRoom, room);
        updateRowState(r);
    }
    m_table->setUpdatesEnabled(true);
    m_filling = false;
    applyFilter();
    int translated = 0;
    for (const ScriptStrings::Text &t : std::as_const(m_texts))
        translated += m_targets.contains(keyOf(t.script, t.operand)) ? 1 : 0;
    m_status->setText(tr("%1 text(s) in the scripts, %2 translated.").arg(m_texts.size()).arg(translated));
}

void ScriptStringsPanel::updateRowState(int row)
{
    QTableWidgetItem *room = m_table->item(row, ColRoom);
    QTableWidgetItem *target = m_table->item(row, ColTarget);
    if (!room || !target || row >= m_texts.size())
        return;
    const ScriptStrings::Text &t = m_texts.at(row);
    const QString text = m_targets.value(keyOf(t.script, t.operand));
    const int length = text.isEmpty() ? 0 : int(TextCodec::encode(text, m_project ? m_project->config().targetEncoding : QStringLiteral("cp1252")).size());
    QString state = tr("%1 max").arg(t.capacity);
    QBrush brush;
    if (!text.isEmpty()) {
        if (length <= t.capacity) {
            state = QStringLiteral("%1 / %2").arg(length).arg(t.capacity);
        } else if (allowEnlarge()) {
            state = QStringLiteral("%1 / %2 · %3").arg(length).arg(t.capacity).arg(tr("will be appended"));
        } else {
            state = QStringLiteral("%1 / %2 · %3").arg(length).arg(t.capacity).arg(tr("too long"));
            brush = QBrush(QColor(200, 60, 60));
        }
    }
    room->setText(state);
    room->setForeground(brush);
    target->setForeground(brush);
}

void ScriptStringsPanel::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool show = needle.isEmpty();
        if (!show)
            for (int c : {int(ColScript), int(ColSource), int(ColTarget)})
                show = show || (m_table->item(r, c) && m_table->item(r, c)->text().contains(needle, Qt::CaseInsensitive));
        m_table->setRowHidden(r, !show);
    }
}

void ScriptStringsPanel::setTranslation(int textIndex, const QString &translation)
{
    if (textIndex < 0 || textIndex >= m_texts.size())
        return;
    // goes through the table so that the same signals fire as for an edit by the user
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *item = m_table->item(r, ColTarget);
        if (item && item->data(IndexRole).toInt() == textIndex) {
            item->setText(translation);
            return;
        }
    }
}

QVector<ScriptTranslation> ScriptStringsPanel::translations() const
{
    // Before any scan, what the project already holds is what there is.
    if (m_texts.isEmpty())
        return m_project ? m_project->scriptTranslations() : QVector<ScriptTranslation>();
    QVector<ScriptTranslation> out;
    for (const ScriptStrings::Text &t : m_texts) {
        const QString target = m_targets.value(keyOf(t.script, t.operand));
        if (!target.isEmpty())
            out.push_back({t.script, t.operand, t.source, target});
    }
    return out;
}
