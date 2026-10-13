#include "ProjectDialog.h"

#include "core/OutputArchive.h"
#include "core/ProjectStore.h"
#include "core/TextCodec.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const QStringList kLanguages = {
    QStringLiteral("english"), QStringLiteral("french"), QStringLiteral("german"), QStringLiteral("spanish"),
    QStringLiteral("italian"), QStringLiteral("portuguese"), QStringLiteral("polish"), QStringLiteral("czech"),
    QStringLiteral("russian"), QStringLiteral("chinese"),
};

void fillEncodings(QComboBox *box)
{
    for (const auto &info : TextCodec::available())
        box->addItem(info.label, info.id);
}

void selectData(QComboBox *box, const QString &value)
{
    const int i = box->findData(value);
    if (i >= 0)
        box->setCurrentIndex(i);
}

}

ProjectDialog::ProjectDialog(const ProjectConfig &config, bool isNew, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_isNew(isNew)
{
    setWindowTitle(isNew ? tr("New translation project") : tr("Project settings"));
    resize(640, 520);

    m_name = new QLineEdit(config.name);
    m_name->setPlaceholderText(tr("e.g. RPU — French"));

    m_sources = new QListWidget;
    m_sources->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sources->setToolTip(tr("Lowest priority first: a later source overrides an earlier one, like in the game."));
    for (const QString &s : config.sources)
        m_sources->addItem(s);

    auto *addInstall = new QPushButton(tr("Game folder..."));
    addInstall->setToolTip(tr("Adds a Fallout folder: its DAT archives and its data folder are detected."));
    auto *addFolder = new QPushButton(tr("Mod folder..."));
    auto *addDat = new QPushButton(tr("DAT archive..."));
    auto *recent = new QPushButton(tr("Recent"));
    auto *up = new QPushButton(tr("Up"));
    auto *down = new QPushButton(tr("Down"));
    auto *remove = new QPushButton(tr("Remove"));
    auto *buttons = new QVBoxLayout;
    for (QPushButton *b : {addInstall, addFolder, addDat, recent, up, down, remove})
        buttons->addWidget(b);
    buttons->addStretch();
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_sources, 1);
    sourceRow->addLayout(buttons);

    m_sourceLanguage = new QComboBox;
    m_sourceLanguage->setEditable(true);
    m_sourceLanguage->addItems(kLanguages);
    m_sourceLanguage->setCurrentText(config.sourceLanguage);
    m_targetLanguage = new QComboBox;
    m_targetLanguage->setEditable(true);
    m_targetLanguage->addItems(kLanguages);
    m_targetLanguage->setCurrentText(config.targetLanguage);
    m_sourceEncoding = new QComboBox;
    m_targetEncoding = new QComboBox;
    fillEncodings(m_sourceEncoding);
    fillEncodings(m_targetEncoding);
    selectData(m_sourceEncoding, config.sourceEncoding);
    selectData(m_targetEncoding, config.targetEncoding);

    const bool archiveMode = OutputArchive::enabled(config);
    m_outputMode = new QComboBox;
    m_outputMode->addItem(tr("Folder of loose files"), 0);
    m_outputMode->addItem(tr("DAT archive"), 1);
    m_outputMode->setCurrentIndex(archiveMode ? 1 : 0);
    m_output = new QLineEdit(archiveMode ? QString() : config.outputDir);
    auto *browseOutput = new QPushButton(tr("Browse..."));
    m_folderRow = new QWidget;
    auto *outputRow = new QHBoxLayout(m_folderRow);
    outputRow->setContentsMargins(0, 0, 0, 0);
    outputRow->addWidget(m_output, 1);
    outputRow->addWidget(browseOutput);

    m_archive = new QLineEdit(config.outputArchive);
    m_archive->setPlaceholderText(tr("e.g. french.dat"));
    auto *browseArchive = new QPushButton(tr("Browse..."));
    m_archiveFormat = new QComboBox;
    m_archiveFormat->addItem(tr("Fallout 2 (zlib)"), QStringLiteral("dat2"));
    m_archiveFormat->addItem(tr("Fallout 1"), QStringLiteral("dat1"));
    selectData(m_archiveFormat, config.outputArchiveFormat);
    m_archiveSource = new QCheckBox(tr("Also store the original texts (reference files)"));
    m_archiveSource->setChecked(config.archiveWithSource);
    m_archiveSource->setToolTip(tr("Adds text/<source language>/ copies of the translated files, so that the reference of each "
                                   "translation is in the archive. Turn it off if a later mod must keep providing the original texts."));
    m_archiveRow = new QWidget;
    auto *archiveLayout = new QVBoxLayout(m_archiveRow);
    archiveLayout->setContentsMargins(0, 0, 0, 0);
    auto *archiveTop = new QHBoxLayout;
    archiveTop->addWidget(m_archive, 1);
    archiveTop->addWidget(browseArchive);
    auto *archiveOptions = new QHBoxLayout;
    archiveOptions->addWidget(new QLabel(tr("Format:")));
    archiveOptions->addWidget(m_archiveFormat);
    archiveOptions->addStretch();
    archiveLayout->addLayout(archiveTop);
    archiveLayout->addLayout(archiveOptions);
    archiveLayout->addWidget(m_archiveSource);

    m_hint = new QLabel;
    m_hint->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Sources"), sourceRow);
    form->addRow(tr("Source language"), m_sourceLanguage);
    form->addRow(tr("Source encoding"), m_sourceEncoding);
    form->addRow(tr("Target language"), m_targetLanguage);
    form->addRow(tr("Target encoding"), m_targetEncoding);
    form->addRow(tr("Output"), m_outputMode);
    form->addRow(tr("Output folder"), m_folderRow);
    form->addRow(tr("Output archive"), m_archiveRow);
    m_outputForm = form;
    m_outputHint = new QLabel;
    m_outputHint->setWordWrap(true);
    form->addRow(QString(), m_outputHint);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_hint);
    layout->addStretch();
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(addInstall, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Fallout folder"));
        if (!dir.isEmpty())
            addInstallation(dir);
    });
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Mod folder (containing text/ or data/)"));
        if (!dir.isEmpty())
            addSource(dir);
    });
    connect(addDat, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("DAT archives"), QString(),
                                                                tr("DAT archives (*.dat);;All files (*)"));
        for (const QString &f : files)
            addSource(f);
    });
    connect(recent, &QPushButton::clicked, this, [this, recent] {
        QMenu menu(this);
        const QStringList folders = ProjectStore::recentFolders();
        for (const QString &f : folders)
            menu.addAction(f, [this, f] { addInstallation(f); });
        if (folders.isEmpty())
            menu.addAction(tr("(none)"))->setEnabled(false);
        menu.exec(recent->mapToGlobal(QPoint(0, recent->height())));
    });
    connect(up, &QPushButton::clicked, this, [this] {
        const int r = m_sources->currentRow();
        if (r > 0) {
            m_sources->insertItem(r - 1, m_sources->takeItem(r));
            m_sources->setCurrentRow(r - 1);
        }
    });
    connect(down, &QPushButton::clicked, this, [this] {
        const int r = m_sources->currentRow();
        if (r >= 0 && r < m_sources->count() - 1) {
            m_sources->insertItem(r + 1, m_sources->takeItem(r));
            m_sources->setCurrentRow(r + 1);
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        delete m_sources->takeItem(m_sources->currentRow());
        detectLanguages();
    });
    connect(browseOutput, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Output folder"), m_output->text());
        if (!dir.isEmpty()) {
            m_output->setText(dir);
            m_outputEdited = true;
        }
    });
    connect(browseArchive, &QPushButton::clicked, this, [this] {
        const QString start = m_archive->text().isEmpty() ? defaultArchiveFor() : m_archive->text();
        const QString file = QFileDialog::getSaveFileName(this, tr("Output archive"), start, tr("DAT archives (*.dat)"),
                                                         nullptr, QFileDialog::DontConfirmOverwrite);
        if (!file.isEmpty())
            m_archive->setText(QDir::toNativeSeparators(file.endsWith(QLatin1String(".dat"), Qt::CaseInsensitive) ? file : file + QStringLiteral(".dat")));
    });
    connect(m_outputMode, &QComboBox::currentIndexChanged, this, [this] { updateOutputMode(); });
    connect(m_archive, &QLineEdit::textChanged, this, [this] { updateOutputHint(); });
    connect(m_archiveSource, &QCheckBox::toggled, this, [this] { updateOutputHint(); });
    connect(m_output, &QLineEdit::textEdited, this, [this] { m_outputEdited = true; });
    connect(m_output, &QLineEdit::textChanged, this, [this] { updateOutputHint(); });
    connect(m_name, &QLineEdit::textChanged, this, [this] { updateOutputDefault(); });
    connect(m_targetLanguage, &QComboBox::currentTextChanged, this, [this] {
        updateTargetEncoding();
        updateOutputHint();
    });
    connect(m_sourceLanguage, &QComboBox::currentTextChanged, this, [this](const QString &lang) {
        selectData(m_sourceEncoding, TextCodec::defaultFor(lang));
    });

    m_outputEdited = !isNew && !archiveMode;
    detectLanguages();
    if (archiveMode)
        updateOutputDefault();
    updateOutputMode();
}

QString ProjectDialog::defaultArchiveFor() const
{
    QStringList sources;
    for (int i = 0; i < m_sources->count(); ++i)
        sources << m_sources->item(i)->text();
    // Next to the game's data folder (the game folder) or the mod folder.
    const QString folder = defaultOutputFor(sources, m_name->text());
    const QFileInfo fi(folder);
    const QString base = fi.fileName().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0 ? fi.absolutePath() : folder;
    const QString language = m_targetLanguage->currentText().trimmed().toLower();
    return QDir(base).filePath((language.isEmpty() ? QStringLiteral("translation") : language) + QStringLiteral(".dat"));
}

void ProjectDialog::updateOutputMode()
{
    const bool archive = m_outputMode->currentData().toInt() == 1;
    m_outputForm->setRowVisible(m_folderRow, !archive);
    m_outputForm->setRowVisible(m_archiveRow, archive);
    if (archive) {
        QStringList sources;
        for (int i = 0; i < m_sources->count(); ++i)
            sources << m_sources->item(i)->text();
        if (m_archive->text().trimmed().isEmpty()) {
            m_archive->setText(QDir::toNativeSeparators(defaultArchiveFor()));
            selectData(m_archiveFormat, OutputArchive::formatId(OutputArchive::detectFormat(sources)));
        }
    }
    updateOutputHint();
}

void ProjectDialog::addSource(const QString &path)
{
    const QString clean = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    for (int i = 0; i < m_sources->count(); ++i)
        if (m_sources->item(i)->text() == clean)
            return;
    m_sources->addItem(clean);
    ProjectStore::addRecentFolder(clean);
    detectLanguages();
    updateOutputDefault();
}

void ProjectDialog::addInstallation(const QString &folder)
{
    for (const QString &s : VirtualFileSystem::discoverSources(folder))
        addSource(s);
    if (m_name->text().isEmpty())
        m_name->setText(QFileInfo(folder).fileName());
}

void ProjectDialog::detectLanguages()
{
    QStringList sources;
    for (int i = 0; i < m_sources->count(); ++i)
        sources << m_sources->item(i)->text();
    if (sources.isEmpty()) {
        m_hint->setText(tr("Add the game folder or the archives of the mod to translate."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    VirtualFileSystem vfs;
    QStringList errors;
    vfs.setSources(sources, &errors);
    QApplication::restoreOverrideCursor();

    const QStringList found = vfs.languages();
    QString text = found.isEmpty() ? tr("No text/<language>/ folder found in these sources.")
                                   : tr("Languages found: %1.").arg(found.join(QStringLiteral(", ")));
    if (!errors.isEmpty())
        text += QStringLiteral("\n") + errors.join(QLatin1Char('\n'));
    m_hint->setText(text);

    // Offer the detected languages first.
    auto prepend = [&](QComboBox *box) {
        const QString current = box->currentText();
        for (const QString &l : found)
            if (box->findText(l) < 0)
                box->insertItem(0, l);
        box->setCurrentText(current);
    };
    prepend(m_sourceLanguage);
    prepend(m_targetLanguage);
    if (m_isNew && !found.isEmpty() && !found.contains(m_sourceLanguage->currentText().toLower()))
        m_sourceLanguage->setCurrentText(found.contains(QStringLiteral("english")) ? QStringLiteral("english") : found.first());
}

void ProjectDialog::updateTargetEncoding()
{
    selectData(m_targetEncoding, TextCodec::defaultFor(m_targetLanguage->currentText()));
}

void ProjectDialog::updateOutputDefault()
{
    if (m_outputEdited)
        return;
    QStringList sources;
    for (int i = 0; i < m_sources->count(); ++i)
        sources << m_sources->item(i)->text();
    m_output->setText(QDir::toNativeSeparators(defaultOutputFor(sources, m_name->text())));
}

QString ProjectDialog::defaultOutputFor(const QStringList &sources, const QString &projectName)
{
    // 1. the game's data folder (master_patches): the folder loose files are read from, with the highest priority;
    // 2. otherwise the folder of a mod that is not an archive folder (not named *.dat);
    // 3. otherwise a folder of its own in Documents. An extracted master.dat/critter.dat/mod.dat folder is never used:
    //    it stands for an archive and must stay untouched.
    for (int i = int(sources.size()) - 1; i >= 0; --i) {
        const QFileInfo fi(sources.at(i));
        if (fi.isDir() && fi.fileName().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0)
            return fi.absoluteFilePath();
    }
    for (const QString &s : sources) {
        const QFileInfo fi(s);
        if (fi.isDir() && !fi.fileName().endsWith(QLatin1String(".dat"), Qt::CaseInsensitive))
            return VirtualFileSystem::resolveFolderRoot(fi.absoluteFilePath());
    }
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString name = projectName.trimmed().isEmpty() ? QStringLiteral("translation") : projectName.trimmed();
    return QDir(docs).filePath(QStringLiteral("FalloutBabylon/%1/data").arg(name));
}

void ProjectDialog::updateOutputHint()
{
    const QString language = m_targetLanguage->currentText().trimmed().toLower();
    if (m_outputMode->currentData().toInt() == 1) {
        QString text = tr("The translation is delivered in %1, rebuilt after each save. It contains text/%2/ with the translated files")
                           .arg(QDir::toNativeSeparators(m_archive->text().trimmed()), language.isEmpty() ? QStringLiteral("<language>") : language);
        text += m_archiveSource->isChecked() ? tr(", and the original texts under text/%1/.").arg(m_sourceLanguage->currentText().trimmed().toLower())
                                             : QStringLiteral(".");
        m_outputHint->setText(text);
        return;
    }
    m_outputHint->setText(tr("Translated files are written to %1 (this folder overrides the sources).")
                              .arg(QDir::toNativeSeparators(QDir(m_output->text().trimmed())
                                                                 .filePath(QStringLiteral("text/%1/").arg(language.isEmpty() ? QStringLiteral("<language>") : language)))));
}

ProjectConfig ProjectDialog::config() const
{
    ProjectConfig c = m_config;
    c.name = m_name->text().trimmed();
    c.sources.clear();
    for (int i = 0; i < m_sources->count(); ++i)
        c.sources << m_sources->item(i)->text();
    if (c.id.isEmpty())
        c.id = ProjectStore::newId();
    if (m_outputMode->currentData().toInt() == 1) {
        // The files are kept in a working folder of the application; the archive is built from it.
        c.outputArchive = QDir::fromNativeSeparators(m_archive->text().trimmed());
        c.outputArchiveFormat = m_archiveFormat->currentData().toString();
        c.archiveWithSource = m_archiveSource->isChecked();
        c.outputDir = ProjectStore::stagingDir(c.id);
    } else {
        c.outputArchive.clear();
        c.outputDir = QDir::fromNativeSeparators(m_output->text().trimmed());
    }
    c.sourceLanguage = m_sourceLanguage->currentText().trimmed().toLower();
    c.targetLanguage = m_targetLanguage->currentText().trimmed().toLower();
    c.sourceEncoding = m_sourceEncoding->currentData().toString();
    c.targetEncoding = m_targetEncoding->currentData().toString();
    return c;
}

void ProjectDialog::accept()
{
    const ProjectConfig c = config();
    QString problem;
    if (c.name.isEmpty())
        problem = tr("Give the project a name.");
    else if (c.sources.isEmpty())
        problem = tr("Add at least one source.");
    else if (c.sourceLanguage.isEmpty() || c.targetLanguage.isEmpty())
        problem = tr("Choose the source and the target language.");
    else if (c.sourceLanguage == c.targetLanguage)
        problem = tr("The source and target languages must differ.");
    else if (m_outputMode->currentData().toInt() == 0 && c.outputDir.isEmpty())
        problem = tr("Choose an output folder.");
    else if (m_outputMode->currentData().toInt() == 1 && c.outputArchive.isEmpty())
        problem = tr("Choose the output archive.");
    else if (!OutputArchive::problemWith(c).isEmpty())
        problem = OutputArchive::problemWith(c);
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), problem);
        return;
    }
    QDialog::accept();
}
