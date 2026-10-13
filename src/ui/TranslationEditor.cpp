#include "TranslationEditor.h"

#include "MassTranslateDialog.h"
#include "MsgTableModel.h"
#include "SciEdit.h"
#include "Theme.h"
#include "TranslationService.h"
#include "core/AppSettings.h"
#include "core/Statistics.h"

#include <QPointer>
#include <QPushButton>

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
    m_filter->addItem(tr("Validated"), int(MsgFilterProxy::Validated));
    m_filter->addItem(tr("Not validated"), int(MsgFilterProxy::NotValidated));
    m_filter->addItem(tr("Changed since validation"), int(MsgFilterProxy::ChangedSinceValidation));
    m_filter->addItem(tr("With a comment"), int(MsgFilterProxy::WithComment));
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
    header->setSectionResizeMode(MsgTableModel::ColNote, QHeaderView::ResizeToContents);

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
    m_comment = new QLineEdit;
    m_comment->setPlaceholderText(tr("Comment on this line (kept in the project, not in the game files)..."));
    m_comment->setClearButtonEnabled(true);
    m_previous = new QLabel;
    m_previous->setWordWrap(true);
    m_previous->setTextFormat(Qt::PlainText);
    m_restorePrevious = new QPushButton(tr("Restore"));
    m_restorePrevious->setToolTip(tr("Put the previous translation back"));
    m_previousRow = new QWidget;
    auto *previousLayout = new QHBoxLayout(m_previousRow);
    previousLayout->setContentsMargins(0, 0, 0, 0);
    previousLayout->addWidget(m_previous, 1);
    previousLayout->addWidget(m_restorePrevious);
    m_previousRow->hide();
    auto *extras = new QWidget;
    auto *extrasLayout = new QVBoxLayout(extras);
    extrasLayout->setContentsMargins(0, 0, 0, 0);
    extrasLayout->setSpacing(2);
    extrasLayout->addWidget(m_issues);
    extrasLayout->addWidget(m_previousRow);
    extrasLayout->addWidget(m_comment);

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
    editors->addWidget(makePane(tr("Translation (%1)").arg(m_project->config().targetLanguage), m_target, extras));
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

    m_checkTimer.setSingleShot(true);
    m_checkTimer.setInterval(700);
    connect(&m_checkTimer, &QTimer::timeout, this, [this] { runCheck(); });

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { onCurrentChanged(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilters(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { applyFilters(); });
    connect(m_target, &SciEdit::userEdited, this, &TranslationEditor::onTargetEdited);
    connect(m_comment, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!m_loading && m_currentRow >= 0)
            m_model->setComment(m_currentRow, text);
    });
    connect(m_restorePrevious, &QPushButton::clicked, this, [this] {
        if (m_currentRow < 0 || m_model->row(m_currentRow).previous.isEmpty())
            return;
        {
            MsgTableModel::Operation op(m_model, tr("Previous translation restored"));
            m_model->setTarget(m_currentRow, m_model->row(m_currentRow).previous);
        }
        refreshCurrent();
    });

    connect(m_model, &MsgTableModel::historyChanged, this, &TranslationEditor::historyChanged);
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
    connect(m_target, &SciEdit::contextMenuAboutToShow, this, [this](QMenu *menu, int charIndex) {
        if (m_currentRow < 0)
            return;
        if (const GrammarIssue *issue = issueAt(charIndex)) {
            menu->addSeparator();
            menu->addAction(issue->message)->setEnabled(false);
            const int start = issue->start, length = issue->length;
            for (const QString &s : issue->suggestions)
                menu->addAction(tr("Replace with \"%1\"").arg(s), [this, start, length, s] { m_target->replaceRange(start, length, s); });
        }
        if (m_service) {
            menu->addSeparator();
            menu->addAction(tr("Translate this line with DeepL"), [this] { translateCurrent(); });
        }
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

QHash<int, DialogLine> TranslationEditor::dialogLines() const
{
    QHash<int, DialogLine> lines;
    for (const TranslationRow &row : m_model->doc().rows)
        lines.insert(row.id, {row.source, row.target, Project::statusOf(row)});
    return lines;
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
    m_grammarIssues.clear();
    m_checkTimer.start(150);
}

void TranslationEditor::onTargetEdited()
{
    if (m_loading || m_currentRow < 0)
        return;
    m_model->setTarget(m_currentRow, m_target->plainText());
    m_highlightTimer.start();
    m_grammarIssues.clear();     // their offsets no longer match the text
    m_checkTimer.start(700);
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
    applyIssueSpans();
}

void TranslationEditor::applyIssueSpans()
{
    QVector<SciEdit::Span> spelling, grammar;
    for (const GrammarIssue &g : std::as_const(m_grammarIssues))
        (g.kind == GrammarIssue::Spelling ? spelling : grammar).push_back({g.start, g.length});
    m_target->setSpans(SciEdit::SpellingIndicator, spelling);
    m_target->setSpans(SciEdit::GrammarIndicator, grammar);
}

const GrammarIssue *TranslationEditor::issueAt(int charIndex) const
{
    for (const GrammarIssue &g : m_grammarIssues)
        if (charIndex >= g.start && charIndex <= g.start + g.length)
            return &g;
    return nullptr;
}

void TranslationEditor::runCheck()
{
    if (!m_service || m_currentRow < 0 || !m_service->checkAvailable(*m_project)) {
        if (!m_grammarIssues.isEmpty()) {
            m_grammarIssues.clear();
            applyIssueSpans();
        }
        return;
    }
    const QString text = m_target->plainText();
    if (text.trimmed().isEmpty()) {
        m_grammarIssues.clear();
        applyIssueSpans();
        return;
    }
    const int serial = ++m_checkSerial;
    QPointer<TranslationEditor> self(this);
    m_service->checkText(*m_project, text, [self, serial, text](const QVector<GrammarIssue> &issues, const QString &error) {
        // Drop answers for text that has changed since (or for another line).
        if (!self || serial != self->m_checkSerial || self->m_target->plainText() != text)
            return;
        if (!error.isEmpty()) {
            self->m_grammarIssues.clear();
            self->applyIssueSpans();
            return;
        }
        self->m_grammarIssues = issues;
        self->applyIssueSpans();
    });
}

void TranslationEditor::showMessage(const QString &html)
{
    m_issues->setText(html);
    m_issues->show();
}

void TranslationEditor::setService(TranslationService *service)
{
    m_service = service;
    m_checkTimer.start(150);
}

int TranslationEditor::currentId() const
{
    return m_currentRow < 0 ? -1 : m_model->row(m_currentRow).id;
}

void TranslationEditor::refreshCurrent()
{
    if (m_currentRow < 0)
        return;
    m_loading = true;
    m_target->setPlainText(m_model->row(m_currentRow).target);
    m_loading = false;
    updateHighlights();
    updateIssueLabel();
    m_grammarIssues.clear();
    m_checkTimer.start(150);
}

int TranslationEditor::replaceInTarget(const ProjectSearch::Options &options, const QString &replacement, const QSet<int> &ids)
{
    int total = 0;
    MsgTableModel::Operation op(m_model, tr("Replace"));
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const TranslationRow &r = m_model->row(row);
        if (!ids.isEmpty() && !ids.contains(r.id))
            continue;
        QString text = r.target;
        const int n = ProjectSearch::replaceIn(text, options, replacement);
        if (n > 0 && text != r.target) {
            m_model->setTarget(row, text);
            total += n;
        }
    }
    if (total)
        refreshCurrent();
    return total;
}

QString TranslationEditor::contextFor(const TranslationDoc &doc, int row)
{
    QStringList parts;
    parts << QStringLiteral("Dialogue line from the video game Fallout (%1).").arg(Statistics::displayNameFor(doc.relPath));
    auto line = [&](int i) {
        return i >= 0 && i < doc.rows.size() ? doc.rows.at(i).source.simplified().left(160) : QString();
    };
    const QString before = (line(row - 2) + QLatin1Char(' ') + line(row - 1)).trimmed();
    const QString after = line(row + 1);
    if (!before.isEmpty())
        parts << QStringLiteral("Previous lines: ") + before;
    if (!after.isEmpty())
        parts << QStringLiteral("Next line: ") + after;
    return parts.join(QLatin1Char('\n')).left(700);
}

void TranslationEditor::translateCurrent()
{
    if (m_currentRow < 0 || !m_service || m_translating)
        return;
    if (!m_service->deeplConfigured()) {
        QMessageBox::information(this, tr("DeepL"), tr("Enter your DeepL API key in Tools > Settings first."));
        return;
    }
    const int row = m_currentRow;
    const QString source = m_model->row(row).source;
    if (!Statistics::isTranslatable(source))
        return;
    m_translating = true;
    showMessage(tr("Translating with DeepL..."));
    QPointer<TranslationEditor> self(this);
    m_service->ensureGlossary(m_project, [self, row, source](const QString &glossaryId, const QString &warning) {
        if (!self)
            return;
        DeepLClient::Options options = self->m_service->deeplOptions(*self->m_project);
        options.glossaryId = glossaryId;
        const QString context = AppSettings::deeplUseContext() ? contextFor(self->m_model->doc(), row) : QString();
        self->m_service->deepl()->translate(source, context, options, [self, row, warning](const QString &text, const QString &error) {
            if (!self)
                return;
            self->m_translating = false;
            if (text.isEmpty()) {
                self->showMessage(QStringLiteral("<span style='color:%1'>✖ %2</span>")
                                      .arg(Theme::colors().spelling.name(), error.toHtmlEscaped()));
                return;
            }
            {
                MsgTableModel::Operation op(self->m_model, tr("DeepL translation"));
                self->m_model->setTarget(row, text);
                if (AppSettings::deeplMarkReview())
                    self->m_model->setReview(row, true);
            }
            if (row == self->m_currentRow)
                self->refreshCurrent();
            const QString note = !error.isEmpty() ? error : warning;
            if (!note.isEmpty())
                self->showMessage(QStringLiteral("<span style='color:%1'>⚠ %2</span>")
                                      .arg(Theme::colors().issue.name(), note.toHtmlEscaped()));
        });
    });
}

void TranslationEditor::massTranslate()
{
    if (!m_service)
        return;
    QVector<int> selected;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        selected << m_proxy->mapToSource(idx).row();
    MassTranslateDialog dialog(m_service, m_project, m_model, selected, this);
    {
        MsgTableModel::Operation op(m_model, tr("Mass translation"));
        dialog.exec();
    }
    refreshCurrent();
}

bool TranslationEditor::undoOperation()
{
    if (!m_model->undo())
        return false;
    if (m_currentRow >= 0 && m_currentRow < m_model->rowCount())
        refreshCurrent();
    return true;
}

bool TranslationEditor::redoOperation()
{
    if (!m_model->redo())
        return false;
    if (m_currentRow >= 0 && m_currentRow < m_model->rowCount())
        refreshCurrent();
    return true;
}

void TranslationEditor::updateIssueLabel()
{
    // comment and previous translation of the current line
    {
        const bool valid = m_currentRow >= 0 && m_currentRow < m_model->rowCount();
        const QString comment = valid ? m_model->row(m_currentRow).comment : QString();
        const QString previous = valid ? m_model->row(m_currentRow).previous : QString();
        if (m_comment->text() != comment)
            m_comment->setText(comment);
        m_comment->setEnabled(valid);
        m_previousRow->setVisible(!previous.isEmpty() && previous != m_model->row(m_currentRow).target);
        m_previous->setText(tr("Previous translation: %1").arg(previous));
    }
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
    if (!inSource) {
        if (const GrammarIssue *issue = issueAt(charIndex)) {
            QString html = QStringLiteral("<b>%1</b>").arg(issue->message.toHtmlEscaped());
            if (!issue->suggestions.isEmpty())
                html += QStringLiteral("<br>→ ") + issue->suggestions.mid(0, 4).join(QStringLiteral(" / ")).toHtmlEscaped();
            QToolTip::showText(globalPos, html, edit);
            return;
        }
    }
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
    MsgTableModel::Operation op(m_model, tr("Review mark"));
    bool allFlagged = true;
    for (const QModelIndex &idx : rows)
        allFlagged = allFlagged && m_model->row(m_proxy->mapToSource(idx).row()).review;
    for (const QModelIndex &idx : rows)
        m_model->setReview(m_proxy->mapToSource(idx).row(), !allFlagged);
}

void TranslationEditor::toggleValidated()
{
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    MsgTableModel::Operation op(m_model, tr("Validation"));
    // If every selected line is already validated (and unchanged), remove the mark; otherwise validate them all.
    bool allValid = true;
    for (const QModelIndex &idx : rows) {
        const TranslationRow &r = m_model->row(m_proxy->mapToSource(idx).row());
        allValid = allValid && r.validated && !r.changedSinceValidation();
    }
    for (const QModelIndex &idx : rows)
        m_model->setValidated(m_proxy->mapToSource(idx).row(), !allValid);
}

void TranslationEditor::copySourceToTarget()
{
    if (m_currentRow < 0)
        return;
    MsgTableModel::Operation op(m_model, tr("Copy the source"));
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
    MsgTableModel::Operation op(m_model, tr("Accept as is"));
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
