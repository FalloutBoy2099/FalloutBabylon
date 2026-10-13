#include "ImportTranslationDialog.h"

#include "core/ProjectStore.h"
#include "core/TextCodec.h"
#include "core/VirtualFileSystem.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThreadPool>
#include <QVBoxLayout>

ImportTranslationDialog::ImportTranslationDialog(std::shared_ptr<Project> project, QWidget *parent)
    : QDialog(parent)
    , m_project(std::move(project))
{
    setWindowTitle(tr("Import an existing translation"));
    resize(640, 420);

    auto *intro = new QLabel(tr("Reuse the translation of another version of the mod (for instance an older installation "
                                "translated into %1). Only untranslated lines are filled in unless you ask to replace.")
                                 .arg(m_project->config().targetLanguage));
    intro->setWordWrap(true);

    m_path = new QLineEdit;
    m_path->setPlaceholderText(tr("Game or mod folder holding the translation"));
    auto *browse = new QPushButton(tr("Browse..."));
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);
    m_found = new QLabel;
    m_found->setWordWrap(true);

    m_sourceLanguage = new QComboBox;
    m_targetLanguage = new QComboBox;
    m_byText = new QCheckBox(tr("Also reuse translations of identical texts found under another id"));
    m_byText->setChecked(true);
    m_changed = new QCheckBox(tr("When the source text changed, import the old translation marked \"to review\""));
    m_changed->setChecked(true);
    m_overwrite = new QCheckBox(tr("Replace translations the project already has"));

    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_result = new QLabel;
    m_result->setWordWrap(true);
    m_start = new QPushButton(tr("Import"));
    m_start->setEnabled(false);
    m_cancel = new QPushButton(tr("Close"));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_start);
    buttons->addWidget(m_cancel);

    auto *form = new QFormLayout;
    form->addRow(tr("Folder"), pathRow);
    form->addRow(QString(), m_found);
    form->addRow(tr("Language of its source texts"), m_sourceLanguage);
    form->addRow(tr("Language of its translation"), m_targetLanguage);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(m_byText);
    layout->addWidget(m_changed);
    layout->addWidget(m_overwrite);
    layout->addStretch();
    layout->addWidget(m_progress);
    layout->addWidget(m_result);
    layout->addLayout(buttons);

    connect(browse, &QPushButton::clicked, this, [this] { chooseFolder(); });
    connect(m_path, &QLineEdit::editingFinished, this, [this] { scan(); });
    connect(m_start, &QPushButton::clicked, this, [this] { start(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { reject(); });
}

ImportTranslationDialog::~ImportTranslationDialog()
{
    m_stop = true;
    QThreadPool::globalInstance()->waitForDone();
}

void ImportTranslationDialog::chooseFolder()
{
    const QString start = ProjectStore::recentFolders().value(0);
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder holding the translation"), start);
    if (dir.isEmpty())
        return;
    m_path->setText(QDir::toNativeSeparators(dir));
    scan();
}

void ImportTranslationDialog::setFolder(const QString &folder)
{
    m_path->setText(QDir::toNativeSeparators(folder));
    scan();
}

void ImportTranslationDialog::scan()
{
    const QString folder = m_path->text().trimmed();
    m_start->setEnabled(false);
    if (folder.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_memory.sources = VirtualFileSystem::discoverSources(QDir::fromNativeSeparators(folder));
    VirtualFileSystem vfs;
    QStringList errors;
    vfs.setSources(m_memory.sources, &errors);
    QApplication::restoreOverrideCursor();

    const QStringList languages = vfs.languages();
    m_sourceLanguage->clear();
    m_targetLanguage->clear();
    m_sourceLanguage->addItems(languages);
    m_targetLanguage->addItems(languages);
    m_sourceLanguage->setCurrentText(languages.contains(m_project->config().sourceLanguage) ? m_project->config().sourceLanguage
                                                                                           : QStringLiteral("english"));
    m_targetLanguage->setCurrentText(m_project->config().targetLanguage);
    m_found->setText(languages.isEmpty() ? tr("No text/<language>/ folder found here.")
                                         : tr("%1 source(s) found; languages: %2.").arg(m_memory.sources.size()).arg(languages.join(QStringLiteral(", "))));
    if (!errors.isEmpty())
        m_found->setText(m_found->text() + QLatin1Char('\n') + errors.join(QLatin1Char('\n')));
    m_start->setEnabled(languages.contains(m_targetLanguage->currentText()));
}

void ImportTranslationDialog::start()
{
    m_memory.sourceLanguage = m_sourceLanguage->currentText();
    m_memory.targetLanguage = m_targetLanguage->currentText();
    m_memory.sourceEncoding = TextCodec::defaultFor(m_memory.sourceLanguage);
    m_memory.targetEncoding = TextCodec::defaultFor(m_memory.targetLanguage);
    TranslationMemory::Options options;
    options.matchByText = m_byText->isChecked();
    options.changedAsReview = m_changed->isChecked();
    options.overwrite = m_overwrite->isChecked();

    if (options.overwrite
        && QMessageBox::question(this, windowTitle(), tr("Replace the translations the project already has? This rewrites the files of the output folder."))
            != QMessageBox::Yes)
        return;

    m_running = true;
    m_stop = false;
    m_start->setEnabled(false);
    m_cancel->setText(tr("Cancel"));
    m_progress->setRange(0, 0);
    m_progress->setVisible(true);
    m_result->setText(tr("Importing..."));

    const std::shared_ptr<Project> project = m_project;
    const TranslationMemory::Memory memory = m_memory;
    QThreadPool::globalInstance()->start([this, project, memory, options] {
        const TranslationMemory::Report report = TranslationMemory::import(
            *project, memory, options,
            [this](int done, int total) {
                QMetaObject::invokeMethod(this, [this, done, total] {
                    m_progress->setRange(0, total);
                    m_progress->setValue(done);
                }, Qt::QueuedConnection);
            },
            &m_stop);
        QMetaObject::invokeMethod(this, [this, report] { finished(report); }, Qt::QueuedConnection);
    });
}

void ImportTranslationDialog::finished(const TranslationMemory::Report &report)
{
    m_running = false;
    m_imported = true;
    m_report = report;
    m_progress->setVisible(false);
    m_cancel->setText(tr("Close"));
    m_start->setEnabled(true);
    QString text = tr("%1 file(s) examined, %2 written.\n%3 line(s) with the same text, %4 with a changed source (to review), "
                      "%5 found under another id; %6 already translated and kept.")
                       .arg(report.files).arg(report.saved).arg(report.exact).arg(report.changed).arg(report.byText).arg(report.kept);
    if (!report.errors.isEmpty())
        text += QStringLiteral("\n⚠ ") + report.errors.mid(0, 5).join(QStringLiteral("\n⚠ "));
    m_result->setText(text);
}

void ImportTranslationDialog::reject()
{
    if (m_running) {
        m_stop = true;
        m_result->setText(tr("Cancelling..."));
        return;
    }
    QDialog::reject();
}
