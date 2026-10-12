#include "PipboyPanel.h"

#include "MsgTableModel.h"
#include "SciEdit.h"
#include "Theme.h"
#include "TranslationEditor.h"
#include "core/AppSettings.h"
#include "core/Statistics.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

PipboyPanel::PipboyPanel(QWidget *parent)
    : QWidget(parent)
{
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_mode = new QLabel;
    m_mode->setWordWrap(true);
    m_status = new QLabel;
    m_status->setWordWrap(true);

    m_source = new SciEdit(true);
    m_target = new SciEdit(false);
    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_width = new QSpinBox;
    m_width->setRange(100, 1000);
    m_width->setSuffix(tr(" px"));
    m_width->setValue(AppSettings::pipboyMaxWidth());
    m_width->setToolTip(tr("Width of the Pip-Boy text area (374 px in the game)."));
    m_chars = new QSpinBox;
    m_chars->setRange(20, 200);
    m_chars->setValue(AppSettings::pipboyFallbackChars());
    m_chars->setToolTip(tr("Characters per line, used when the game font cannot be read."));
    m_reload = new QPushButton(tr("Reload from the lines"));
    m_reload->setToolTip(tr("Rebuild the text of the translation from the records of the file."));

    auto *options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Width:")));
    options->addWidget(m_width);
    options->addWidget(new QLabel(tr("Characters (fallback):")));
    options->addWidget(m_chars);
    options->addStretch();
    options->addWidget(m_reload);

    auto makePane = [](const QString &title, QWidget *editor) {
        auto *pane = new QWidget;
        auto *l = new QVBoxLayout(pane);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(2);
        l->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped())));
        l->addWidget(editor, 1);
        return pane;
    };
    auto *texts = new QSplitter(Qt::Vertical);
    texts->addWidget(makePane(tr("Source (paragraphs)"), m_source));
    texts->addWidget(makePane(tr("Translation (write freely: a blank line starts a paragraph)"), m_target));
    texts->setChildrenCollapsible(false);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(texts);
    split->addWidget(makePane(tr("Lines written to the file"), m_preview));
    split->setChildrenCollapsible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_info);
    layout->addWidget(m_mode);
    layout->addLayout(options);
    layout->addWidget(split, 1);
    layout->addWidget(m_status);

    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(600);
    connect(&m_applyTimer, &QTimer::timeout, this, [this] { applyNow(); });
    connect(m_target, &SciEdit::userEdited, this, [this] { scheduleApply(); });
    connect(m_width, &QSpinBox::valueChanged, this, [this](int v) {
        AppSettings::setPipboyMaxWidth(v);
        if (m_hasBlock)
            scheduleApply();
    });
    connect(m_chars, &QSpinBox::valueChanged, this, [this](int v) {
        AppSettings::setPipboyFallbackChars(v);
        if (m_hasBlock)
            scheduleApply();
    });
    connect(m_reload, &QPushButton::clicked, this, [this] { reloadFromLines(); });
    setIdle(tr("Open game/pipboy.msg and select a holodisk line."));
}

void PipboyPanel::setIdle(const QString &message)
{
    m_applyTimer.stop();
    m_hasBlock = false;
    m_entries.clear();
    m_loading = true;
    m_source->setPlainText(QString());
    m_target->setPlainText(QString());
    m_loading = false;
    m_preview->clear();
    m_info->setText(message);
    m_mode->clear();
    m_status->clear();
    m_target->setEnabled(false);
    m_reload->setEnabled(false);
}

void PipboyPanel::setEditor(TranslationEditor *editor)
{
    if (m_hasBlock && m_applyTimer.isActive())
        applyNow();   // do not lose the last keystrokes of the previous file
    if (editor && editor->relPath() == QLatin1String("game/pipboy.msg")) {
        m_editor = editor;
        m_model = editor->model();
        m_project = editor->sharedProject();
    } else {
        m_editor = nullptr;
        m_model = nullptr;
        m_project.reset();
        setIdle(tr("The holodisk editor works on game/pipboy.msg."));
    }
}

PipboyText::Measure PipboyPanel::measure() const
{
    PipboyText::Measure m;
    m.font = m_font.isValid() ? &m_font : nullptr;
    m.encoding = m_project ? m_project->config().targetEncoding : QString();
    m.maxWidth = m_width->value();
    m.fallbackChars = m_chars->value();
    return m;
}

void PipboyPanel::showId(int id)
{
    if (!m_editor || !m_model || !m_project)
        return;
    if (m_hasBlock && m_block.contains(id))
        return;   // still the same holodisk
    if (m_hasBlock && m_applyTimer.isActive())
        applyNow();

    QMap<int, QString> texts;
    for (const TranslationRow &row : m_model->doc().rows)
        if (row.inSource)
            texts.insert(row.id, row.source);
    for (const PipboyText::Block &block : PipboyText::findHolodisks(texts))
        if (block.contains(id)) {
            loadBlock(block);
            return;
        }
    setIdle(tr("This line is not part of a holodisk body (a holodisk body ends with **END-DISK**)."));
}

void PipboyPanel::loadBlock(const PipboyText::Block &block)
{
    // The font of the project (cached per project): the game draws the Pip-Boy with font1.aaf.
    if (m_fontProject != m_project->config().id) {
        m_font = PipboyText::Font::fromAaf(m_project->readInterfaceFont(1));
        m_fontProject = m_project->config().id;
    }
    m_block = block;
    m_hasBlock = true;
    m_target->setEnabled(true);
    m_reload->setEnabled(true);
    m_info->setText(tr("Holodisk: records %1 to %2 (%3 entries, %4 page(s) in the Pip-Boy).")
                        .arg(block.firstId).arg(block.lastId).arg(block.entryCount()).arg(PipboyText::pageCount(block.entryCount())));
    reloadFromLines();
}

void PipboyPanel::reloadFromLines()
{
    if (!m_hasBlock || !m_model)
        return;
    m_applyTimer.stop();
    const PipboyText::Measure m = measure();

    QStringList source, target;
    bool anyTranslated = false;
    for (int id = m_block.firstId; id <= m_block.lastId; ++id) {
        const int row = m_model->rowForId(id);
        if (row < 0)
            continue;
        const TranslationRow &r = m_model->row(row);
        source << r.source;
        target << r.target;
        anyTranslated = anyTranslated || m_model->status(row) != SegmentStatus::Untranslated;
    }
    // Records past the source block that an earlier edit added belong to the translation too.
    for (int id = m_block.lastId + 1;; ++id) {
        const int row = m_model->rowForId(id);
        if (row < 0 || m_model->row(row).inSource)
            break;
        target << m_model->row(row).target;
    }

    m_loading = true;
    m_source->setPlainText(PipboyText::reflow(source, m));
    m_target->setPlainText(anyTranslated ? PipboyText::reflow(target, m) : QString());
    m_loading = false;
    m_mode->setText(m.exact() ? tr("Measured with the game font (font1.aaf): lines are cut at %1 px.").arg(m.maxWidth)
                              : tr("Game font not available (or multi-byte encoding): lines are cut at about %1 characters.")
                                    .arg(m.fallbackChars));
    m_entries = anyTranslated ? PipboyText::layout(m_target->plainText(), m) : QStringList();
    updatePreview();
}

void PipboyPanel::scheduleApply()
{
    if (m_loading || !m_hasBlock)
        return;
    m_status->setText(tr("Editing..."));
    m_entries = PipboyText::layout(m_target->plainText(), measure());
    updatePreview();
    m_applyTimer.start();
}

void PipboyPanel::updatePreview()
{
    const PipboyText::Measure m = measure();
    const QVector<int> wide = PipboyText::tooWide(m_entries, m);
    QStringList lines;
    for (int i = 0; i < m_entries.size(); ++i) {
        const QString &entry = m_entries.at(i);
        const bool marker = entry == PipboyText::endPar() || entry == PipboyText::endDisk();
        const QString width = marker ? QStringLiteral("      ")
                                     : QStringLiteral("%1").arg(m.width(entry), 4) + (wide.contains(i) ? QStringLiteral("!") : QStringLiteral(" "));
        lines << QStringLiteral("%1  %2  %3").arg(m_block.firstId + i, 5).arg(width, entry);
    }
    m_preview->setPlainText(lines.join(QLatin1Char('\n')));

    const int needed = m_entries.size();
    QString status = tr("%1 entries, %2 page(s) in the Pip-Boy").arg(needed).arg(PipboyText::pageCount(needed));
    if (!wide.isEmpty())
        status += QStringLiteral("  ⚠ ") + tr("%1 line(s) wider than the screen").arg(wide.size());
    if (needed > 500)
        status += QStringLiteral("  ⚠ ") + tr("the game only reads 500 entries per holodisk");
    m_status->setText(status);
}

void PipboyPanel::applyNow()
{
    m_applyTimer.stop();
    if (!m_hasBlock || !m_model || m_loading)
        return;

    const QString text = m_target->plainText();
    if (text.trimmed().isEmpty())
        return;   // nothing written: the records stay as they are
    m_entries = PipboyText::layout(text, measure());
    const int needed = int(m_entries.size());
    const int available = m_block.entryCount();

    // Extra entries need free ids right after the block: stop if they belong to something else.
    for (int i = available; i < needed; ++i) {
        const int row = m_model->rowForId(m_block.firstId + i);
        if (row >= 0 && m_model->row(row).inSource) {
            m_status->setText(QStringLiteral("⚠ ") + tr("The translation needs %1 entries but only %2 ids are free before record %3. Shorten it.")
                                  .arg(needed).arg(i).arg(m_block.firstId + i));
            updatePreview();
            return;
        }
    }

    for (int i = 0; i < needed; ++i) {
        const int id = m_block.firstId + i;
        int row = m_model->rowForId(id);
        if (row < 0)
            row = m_model->addRow(id);
        m_model->setTarget(row, m_entries.at(i));
    }
    // Source records the translation no longer uses (it is shorter): they keep their text, counted as done.
    for (int i = needed; i < available; ++i) {
        const int row = m_model->rowForId(m_block.firstId + i);
        if (row < 0)
            continue;
        m_model->setTarget(row, m_model->row(row).source);
        m_model->confirm(row);
    }
    // Records an earlier, longer version had added past the source block.
    for (int id = m_block.firstId + qMax(needed, available);; ++id) {
        const int row = m_model->rowForId(id);
        if (row < 0 || m_model->row(row).inSource)
            break;
        m_model->removeOrphan(row);
    }
    if (m_editor)
        m_editor->refreshCurrent();
    updatePreview();
    m_status->setText(m_status->text() + QStringLiteral("  ✔ ") + tr("written to the file"));
}
