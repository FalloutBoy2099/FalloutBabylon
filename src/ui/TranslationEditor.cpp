#include "TranslationEditor.h"

#include "MsgTableModel.h"
#include "SciEdit.h"
#include "Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QTableView>
#include <QToolTip>
#include <QVBoxLayout>

TranslationEditor::TranslationEditor(std::shared_ptr<Project> project, TranslationDoc doc, QWidget *parent)
    : QWidget(parent)
    , m_project(std::move(project))
    , m_doc(doc)
{
    m_model = new MsgTableModel(m_project, std::move(doc), this);
    m_proxy = new MsgFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setDynamicSortFilter(false); // a row must not vanish from a filtered list while it is being edited

    // --- list of records
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search in this file (text or ID)..."));
    m_search->setClearButtonEnabled(true);
    m_filter = new QComboBox;
    m_filter->addItem(tr("All lines"), int(MsgFilterProxy::AllRows));
    m_filter->addItem(tr("Untranslated"), int(MsgFilterProxy::Untranslated));
    m_filter->addItem(tr("Translated"), int(MsgFilterProxy::Translated));
    m_filter->addItem(tr("To review"), int(MsgFilterProxy::ToReview));
    m_filter->addItem(tr("Glossary alerts"), int(MsgFilterProxy::WithIssues));
    m_info = new QLabel;

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_search, 1);
    bar->addWidget(m_filter);
    bar->addWidget(m_info);

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    auto *header = m_table->horizontalHeader();
    header->setSectionResizeMode(MsgTableModel::ColId, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(MsgTableModel::ColSource, QHeaderView::Stretch);
    header->setSectionResizeMode(MsgTableModel::ColTarget, QHeaderView::Stretch);
    header->setSectionResizeMode(MsgTableModel::ColStatus, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(MsgTableModel::ColIssues, QHeaderView::ResizeToContents);

    auto *listPane = new QWidget;
    auto *listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addLayout(bar);
    listLayout->addWidget(m_table, 1);

    // --- editors
    m_source = new SciEdit(true);
    m_target = new SciEdit(false);
    m_issues = new QLabel;
    m_issues->setWordWrap(true);
    m_issues->setTextFormat(Qt::RichText);
    m_issues->hide();

    auto makePane = [](const QString &title, QWidget *editor, QWidget *extra = nullptr) {
        auto *pane = new QWidget;
        auto *l = new QVBoxLayout(pane);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(2);
        auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
        l->addWidget(label);
        l->addWidget(editor, 1);
        if (extra)
            l->addWidget(extra);
        return pane;
    };
    auto *editors = new QSplitter(Qt::Horizontal);
    editors->addWidget(makePane(tr("Source (%1)").arg(m_project->config().sourceLanguage), m_source));
    editors->addWidget(makePane(tr("Translation (%1)").arg(m_project->config().targetLanguage), m_target, m_issues));
    editors->setChildrenCollapsible(false);

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(listPane);
    splitter->addWidget(editors);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setChildrenCollapsible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(splitter);

    // --- wiring
    m_highlightTimer.setSingleShot(true);
    m_highlightTimer.setInterval(220);
    connect(&m_highlightTimer, &QTimer::timeout, this, [this] {
        updateHighlights();
        updateIssueLabel();
    });

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { onCurrentChanged(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilters(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
    connect(m_target, &SciEdit::userEdited, this, &TranslationEditor::onTargetEdited);

    connect(m_model, &MsgTableModel::changed, this, [this] {
        const bool dirty = m_model->isDirty();
        if (dirty != m_lastDirty) {
            m_lastDirty = dirty;
            emit dirtyChanged(dirty);
        }
        emit statsChanged(m_model->stats());
    });

    connect(m_source, &SciEdit::hoverAt, this, [this](int i, const QPoint &g) { showTermTooltip(m_source, true, i, g); });
    connect(m_target, &SciEdit::hoverAt, this, [this](int i, const QPoint &g) { showTermTooltip(m_target, false, i, g); });
    connect(m_source, &SciEdit::hoverEnded, this, [] { QToolTip::hideText(); });
    connect(m_target, &SciEdit::hoverEnded, this, [] { QToolTip::hideText(); });

    // Glossary actions of the context menus
    connect(m_source, &SciEdit::contextMenuAboutToShow, this, [this](QMenu *menu, int charIndex) {
        if (m_currentRow < 0)
            return;
        for (const auto &hit : m_project->glossary().findInSource(m_model->row(m_currentRow).source)) {
            if (charIndex < hit.match.start || charIndex > hit.match.start + hit.match.length)
                continue;
            menu->addSeparator();
            if (hit.term->targets.isEmpty()) {
                menu->addAction(tr("\"%1\": not translated in the game files yet").arg(hit.term->source))->setEnabled(false);
            }
            for (const QString &t : hit.term->targets)
                menu->addAction(tr("Insert \"%1\" in the translation").arg(t), [this, t] { insertText(t); });
            break;
        }
    });
    connect(m_target, &SciEdit::contextMenuAboutToShow, this, [this](QMenu *menu, int) {
        if (m_currentRow < 0)
            return;
        const auto hits = m_project->glossary().findInSource(m_model->row(m_currentRow).source);
        QStringList done;
        bool first = true;
        for (const auto &hit : hits) {
            if (hit.term->targets.isEmpty() || done.contains(hit.term->source))
                continue;
            done << hit.term->source;
            if (first) {
                menu->addSeparator();
                first = false;
            }
            const QString t = hit.term->targets.first();
            menu->addAction(tr("Glossary: %1 → %2").arg(hit.term->source, t), [this, t] { insertText(t); });
            if (done.size() >= 8)
                break;
        }
    });

    applyTheme();
    emit statsChanged(m_model->stats());
    if (m_proxy->rowCount() > 0)
        m_table->selectRow(0);
    m_info->setText(tr("%1 lines").arg(m_model->rowCount()));
}

QVector<TodoItem> TranslationEditor::reviewItems() const
{
    QVector<TodoItem> items;
    for (const TranslationRow &row : m_model->doc().rows)
        if (row.review)
            items.push_back({m_doc.relPath, row.id, row.source, row.target});
    return items;
}

bool TranslationEditor::isDirty() const
{
    return m_model->isDirty();
}

FileStats TranslationEditor::stats() const
{
    return m_model->stats();
}

void TranslationEditor::applyTheme()
{
    m_source->applyTheme();
    m_target->applyTheme();
    m_table->viewport()->update();
    updateHighlights();
}

void TranslationEditor::refreshGlossary()
{
    m_model->refreshIssues();
    updateHighlights();
    updateIssueLabel();
}

int TranslationEditor::currentSourceRow() const
{
    const QModelIndex idx = m_table->currentIndex();
    return idx.isValid() ? m_proxy->mapToSource(idx).row() : -1;
}

void TranslationEditor::onCurrentChanged()
{
    const int row = currentSourceRow();
    if (row == m_currentRow)
        return;
    m_currentRow = row;
    m_loading = true;
    if (row < 0) {
        m_source->setPlainText(QString());
        m_target->setPlainText(QString());
    } else {
        const TranslationRow &r = m_model->row(row);
        m_source->setPlainText(r.source);
        m_target->setPlainText(r.target);
        emit currentIdChanged(r.id);
    }
    m_loading = false;
    updateHighlights();
    updateIssueLabel();
}

void TranslationEditor::onTargetEdited()
{
    if (m_loading || m_currentRow < 0)
        return;
    m_model->setTarget(m_currentRow, m_target->plainText());
    m_highlightTimer.start();
}

void TranslationEditor::updateHighlights()
{
    m_source->clearAllSpans();
    m_target->clearAllSpans();
    if (m_currentRow < 0)
        return;
    const TranslationRow &r = m_model->row(m_currentRow);
    const Glossary &glossary = m_project->glossary();

    QVector<SciEdit::Span> exact, fuzzy, issues;
    for (const auto &hit : glossary.findInSource(r.source))
        (hit.match.fuzzy ? fuzzy : exact).push_back({hit.match.start, hit.match.length});
    m_source->setSpans(SciEdit::TermIndicator, exact);
    m_source->setSpans(SciEdit::FuzzyTermIndicator, fuzzy);
    for (const auto &issue : glossary.checkConsistency(r.source, m_target->plainText()))
        issues.push_back({issue.sourceMatch.start, issue.sourceMatch.length});
    m_source->setSpans(SciEdit::IssueIndicator, issues);

    exact.clear();
    fuzzy.clear();
    for (const auto &hit : glossary.findInTarget(m_target->plainText()))
        (hit.match.fuzzy ? fuzzy : exact).push_back({hit.match.start, hit.match.length});
    m_target->setSpans(SciEdit::TermIndicator, exact);
    m_target->setSpans(SciEdit::FuzzyTermIndicator, fuzzy);
}

void TranslationEditor::updateIssueLabel()
{
    if (m_currentRow < 0) {
        m_issues->hide();
        return;
    }
    const QStringList messages = m_model->issueMessages(m_currentRow);
    if (messages.isEmpty()) {
        m_issues->hide();
        return;
    }
    QStringList lines;
    for (const QString &m : messages)
        lines << m.toHtmlEscaped();
    m_issues->setText(QStringLiteral("<span style='color:%1'>⚠ %2</span> %3")
                          .arg(Theme::colors().issue.name(), tr("Glossary:"), lines.join(QStringLiteral(" &nbsp;|&nbsp; "))));
    m_issues->show();
}

QString TranslationEditor::termHtml(const GlossaryTerm &term) const
{
    QString html = QStringLiteral("<b>%1</b> <i>(%2)</i><br>").arg(term.source.toHtmlEscaped(), term.category.toHtmlEscaped());
    if (term.targets.isEmpty()) {
        html += tr("<i>Not translated in the game files yet</i>");
    } else {
        html += QStringLiteral("→ ");
        QStringList targets;
        for (const QString &t : term.targets)
            targets << QStringLiteral("<b>%1</b>").arg(t.toHtmlEscaped());
        html += targets.join(QStringLiteral(" / "));
        if (term.conflict())
            html += QStringLiteral("<br><span style='color:%1'>⚠ %2</span>")
                        .arg(Theme::colors().issue.name(), tr("Inconsistent translations in the game files"));
    }
    if (!term.origins.isEmpty())
        html += QStringLiteral("<br><small>%1</small>").arg(term.origins.mid(0, 3).join(QStringLiteral(", ")).toHtmlEscaped());
    return html;
}

void TranslationEditor::showTermTooltip(SciEdit *edit, bool inSource, int charIndex, const QPoint &globalPos)
{
    if (m_currentRow < 0)
        return;
    const Glossary &glossary = m_project->glossary();
    const QString text = inSource ? m_model->row(m_currentRow).source : edit->plainText();
    const auto hits = inSource ? glossary.findInSource(text) : glossary.findInTarget(text);
    for (const auto &hit : hits) {
        if (charIndex >= hit.match.start && charIndex <= hit.match.start + hit.match.length) {
            QToolTip::showText(globalPos, termHtml(*hit.term), edit);
            return;
        }
    }
}

void TranslationEditor::applyFilters()
{
    m_proxy->setSearchText(m_search->text());
    m_proxy->setStatusFilter(m_filter->currentData().toInt());
    m_info->setText(tr("%1 / %2 lines").arg(m_proxy->rowCount()).arg(m_model->rowCount()));
}

void TranslationEditor::selectId(int id)
{
    for (int i = 0; i < m_proxy->rowCount(); ++i) {
        const int srcRow = m_proxy->mapToSource(m_proxy->index(i, 0)).row();
        if (m_model->row(srcRow).id == id) {
            m_table->selectRow(i);
            m_table->scrollTo(m_proxy->index(i, 0), QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

bool TranslationEditor::save()
{
    // Make sure the last keystrokes are in the model.
    if (m_currentRow >= 0 && !m_loading)
        m_model->setTarget(m_currentRow, m_target->plainText());

    QString error, unencodable;
    if (!m_project->saveDocument(m_model->doc(), &error, &unencodable)) {
        QMessageBox::critical(this, tr("Save"), tr("Cannot save %1:\n%2").arg(m_doc.relPath, error));
        return false;
    }
    if (!unencodable.isEmpty()) {
        QMessageBox::warning(this, tr("Save"),
                             tr("Some characters cannot be represented in the encoding of the target files (%1) and were "
                                "replaced by '?':\n\n%2\n\nChoose another encoding in the project settings or reword the text.")
                                 .arg(m_project->config().targetEncoding, unencodable));
    }
    m_model->markSaved();
    m_lastDirty = false;
    emit dirtyChanged(false);
    return true;
}

// ----------------------------------------------------------------------------- commands

void TranslationEditor::toggleReview()
{
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    // If every selected row is already flagged, clear the flag; otherwise flag them all.
    bool allFlagged = true;
    for (const QModelIndex &idx : rows)
        allFlagged = allFlagged && m_model->row(m_proxy->mapToSource(idx).row()).review;
    for (const QModelIndex &idx : rows)
        m_model->setReview(m_proxy->mapToSource(idx).row(), !allFlagged);
}

void TranslationEditor::copySourceToTarget()
{
    if (m_currentRow < 0)
        return;
    const QString source = m_model->row(m_currentRow).source;
    m_loading = true;
    m_target->setPlainText(source);
    m_loading = false;
    m_model->setTarget(m_currentRow, source);
    m_highlightTimer.start();
    m_target->setFocus();
}

void TranslationEditor::confirmCurrent()
{
    if (m_currentRow < 0)
        return;
    m_model->confirm(m_currentRow);
    if (m_target->plainText().isEmpty()) {
        m_loading = true;
        m_target->setPlainText(m_model->row(m_currentRow).target);
        m_loading = false;
    }
}

void TranslationEditor::goToRow(int delta)
{
    const int count = m_proxy->rowCount();
    if (!count)
        return;
    int row = m_table->currentIndex().isValid() ? m_table->currentIndex().row() + delta : 0;
    row = qBound(0, row, count - 1);
    m_table->selectRow(row);
    m_table->scrollTo(m_proxy->index(row, 0));
}

void TranslationEditor::goToNextUntranslated(bool forward)
{
    const int count = m_proxy->rowCount();
    const int start = m_table->currentIndex().isValid() ? m_table->currentIndex().row() : (forward ? -1 : count);
    for (int step = 1; step <= count; ++step) {
        const int row = ((forward ? start + step : start - step) % count + count) % count;
        const int src = m_proxy->mapToSource(m_proxy->index(row, 0)).row();
        if (m_model->status(src) == SegmentStatus::Untranslated) {
            m_table->selectRow(row);
            m_table->scrollTo(m_proxy->index(row, 0));
            m_target->setFocus();
            return;
        }
    }
}

void TranslationEditor::insertText(const QString &text)
{
    m_target->insertAtCaret(text);
}

void TranslationEditor::focusSearch()
{
    m_search->setFocus();
    m_search->selectAll();
}
