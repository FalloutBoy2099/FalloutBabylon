#include "GlossaryPanel.h"

#include "Theme.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStringConverter>
#include <QTableView>
#include <QVBoxLayout>

class GlossaryTableModel : public QAbstractTableModel
{
public:

    enum Column { ColSource, ColTarget, ColCategory, ColOrigin, ColumnCount };

    void setTerms(const QVector<GlossaryTerm> &terms)
    {
        beginResetModel();
        m_terms = terms;
        endResetModel();
    }
    const GlossaryTerm &term(int row) const { return m_terms.at(row); }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_terms.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid())
            return {};
        const GlossaryTerm &t = m_terms.at(index.row());
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColSource: return t.source;
            case ColTarget:
                return t.targets.isEmpty() ? QString() : t.targets.join(QStringLiteral(" / "))
                        + (t.conflict() ? QStringLiteral("  ⚠") : QString());
            case ColCategory: return t.category;
            case ColOrigin: return t.origins.value(0) + (t.origins.size() > 1 ? QStringLiteral(" +%1").arg(t.origins.size() - 1) : QString());
            }
        } else if (role == Qt::FontRole) {
            if (t.custom) {
                QFont f = QApplication::font();
                f.setBold(true);
                return f;
            }
        } else if (role == Qt::ForegroundRole) {
            if (t.conflict() && index.column() == ColTarget)
                return Theme::colors().issue;
            if (t.targets.isEmpty())
                return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
        } else if (role == Qt::ToolTipRole) {
            QStringList lines{t.source + QStringLiteral(" → ") + t.targets.join(QStringLiteral(" / "))};
            lines << t.origins.mid(0, 8);
            if (t.keep)
                lines << QCoreApplication::translate("GlossaryPanel", "Do not translate: this term must stay unchanged.");
            if (t.conflict())
                lines << QCoreApplication::translate("GlossaryPanel", "⚠ Several different translations exist in the game files.");
            if (t.targets.isEmpty())
                lines << QCoreApplication::translate("GlossaryPanel", "Not translated yet: translate the matching /game file first.");
            return lines.join(QLatin1Char('\n'));
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation o, int role) const override
    {
        if (o != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case ColSource: return QCoreApplication::translate("GlossaryPanel", "Term");
        case ColTarget: return QCoreApplication::translate("GlossaryPanel", "Translation");
        case ColCategory: return QCoreApplication::translate("GlossaryPanel", "Type");
        case ColOrigin: return QCoreApplication::translate("GlossaryPanel", "Origin");
        }
        return {};
    }

private:
    QVector<GlossaryTerm> m_terms;
};

GlossaryPanel::GlossaryPanel(QWidget *parent)
    : QWidget(parent)
{
    m_model = new GlossaryTableModel;
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_model->setParent(m_proxy);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search the glossary..."));
    m_search->setClearButtonEnabled(true);
    m_summary = new QLabel;

    m_view = new QTableView;
    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setColumnWidth(GlossaryTableModel::ColSource, 150);
    m_view->setColumnWidth(GlossaryTableModel::ColTarget, 150);
    m_view->setColumnWidth(GlossaryTableModel::ColCategory, 70);

    m_add = new QPushButton(tr("Add..."));
    m_edit = new QPushButton(tr("Edit..."));
    m_delete = new QPushButton(tr("Delete"));
    m_import = new QPushButton(tr("Import..."));
    m_export = new QPushButton(tr("Export..."));
    m_add->setToolTip(tr("Add a term of your own (a name to keep, a word with a fixed translation...)"));
    m_import->setToolTip(tr("Import terms from a CSV / TSV file (term; translation; keep)"));
    m_export->setToolTip(tr("Export your own terms to a CSV file"));
    auto *buttons = new QHBoxLayout;
    for (QPushButton *b : {m_add, m_edit, m_delete, m_import, m_export})
        buttons->addWidget(b);
    buttons->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(m_search);
    layout->addLayout(buttons);
    layout->addWidget(m_summary);
    layout->addWidget(m_view, 1);

    connect(m_add, &QPushButton::clicked, this, [this] { editDialog({}, true); });
    connect(m_edit, &QPushButton::clicked, this, [this] { editSelected(); });
    connect(m_delete, &QPushButton::clicked, this, [this] { deleteSelected(); });
    connect(m_import, &QPushButton::clicked, this, [this] { importFile(); });
    connect(m_export, &QPushButton::clicked, this, [this] { exportFile(); });
    setEditable(false);

    connect(m_search, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex &idx) {
        const GlossaryTerm &t = m_model->term(m_proxy->mapToSource(idx).row());
        if (!t.targets.isEmpty())
            emit insertRequested(t.targets.first());
    });
    connect(m_view, &QTableView::customContextMenuRequested, this, &GlossaryPanel::contextMenu);
    setGlossary({});
}

void GlossaryPanel::showBuilding()
{
    m_summary->setText(tr("Building the glossary..."));
}

void GlossaryPanel::setGlossary(const QVector<GlossaryTerm> &terms)
{
    m_model->setTerms(terms);
    int conflicts = 0, untranslated = 0;
    for (const GlossaryTerm &t : terms) {
        conflicts += t.conflict() ? 1 : 0;
        untranslated += t.targets.isEmpty() ? 1 : 0;
    }
    if (terms.isEmpty())
        m_summary->setText(tr("No term yet. The glossary is built from the /game message files."));
    else
        m_summary->setText(tr("%1 terms · %2 without translation · %3 inconsistent")
                               .arg(terms.size()).arg(untranslated).arg(conflicts));
}

void GlossaryPanel::setEditable(bool editable)
{
    for (QPushButton *b : {m_add, m_edit, m_delete, m_import})
        b->setEnabled(editable);
    m_export->setEnabled(editable);
}

void GlossaryPanel::setCustomTerms(const QVector<CustomTerm> &terms)
{
    m_custom = terms;
}

const GlossaryTerm *GlossaryPanel::selectedTerm() const
{
    const QModelIndex idx = m_view->currentIndex();
    return idx.isValid() ? &m_model->term(m_proxy->mapToSource(idx).row()) : nullptr;
}

void GlossaryPanel::commit(const QVector<CustomTerm> &terms)
{
    m_custom = terms;
    emit customTermsChanged(m_custom);
}

void GlossaryPanel::addOrReplaceTerm(const CustomTerm &term)
{
    if (term.source.simplified().isEmpty())
        return;
    commit(CustomGlossary::merge(m_custom, {term}));
}

void GlossaryPanel::removeTerm(const QString &source)
{
    QVector<CustomTerm> terms = m_custom;
    const QString key = CustomGlossary::keyOf(source);
    for (int i = terms.size() - 1; i >= 0; --i)
        if (CustomGlossary::keyOf(terms.at(i).source) == key)
            terms.removeAt(i);
    if (terms.size() != m_custom.size())
        commit(terms);
}

void GlossaryPanel::importTerms(const QVector<CustomTerm> &terms)
{
    if (!terms.isEmpty())
        commit(CustomGlossary::merge(m_custom, terms));
}

void GlossaryPanel::editDialog(const CustomTerm &initial, bool isNew)
{
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? tr("New glossary term") : tr("Edit the glossary term"));
    auto *source = new QLineEdit(initial.source);
    auto *target = new QLineEdit(initial.keep ? QString() : initial.target);
    auto *keep = new QCheckBox(tr("Do not translate (keep the term as is)"));
    keep->setChecked(initial.keep);
    source->setReadOnly(!isNew);
    target->setEnabled(!initial.keep);
    connect(keep, &QCheckBox::toggled, target, [target](bool on) { target->setEnabled(!on); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *form = new QFormLayout(&dialog);
    form->addRow(tr("Term (source language):"), source);
    form->addRow(tr("Translation:"), target);
    form->addRow(keep);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.setMinimumWidth(420);
    (isNew ? source : target)->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;
    CustomTerm term;
    term.source = source->text().simplified();
    term.keep = keep->isChecked();
    term.target = term.keep ? term.source : target->text().simplified();
    if (term.source.isEmpty() || term.target.isEmpty()) {
        QMessageBox::warning(this, dialog.windowTitle(), tr("Both the term and its translation are required."));
        return;
    }
    addOrReplaceTerm(term);
}

void GlossaryPanel::editSelected()
{
    const GlossaryTerm *t = selectedTerm();
    if (!t) {
        QMessageBox::information(this, tr("Glossary"), tr("Select a term first."));
        return;
    }
    // A term of the game files becomes a term of the project that overrides it.
    CustomTerm initial;
    initial.source = t->source;
    initial.target = t->targets.value(0);
    initial.keep = t->keep;
    editDialog(initial, !t->custom);
}

void GlossaryPanel::deleteSelected()
{
    const GlossaryTerm *t = selectedTerm();
    if (!t || !t->custom) {
        QMessageBox::information(this, tr("Glossary"), tr("Only the terms you added (in bold) can be deleted."));
        return;
    }
    removeTerm(t->source);
}

void GlossaryPanel::importFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import glossary terms"), QString(),
                                                      tr("Glossaries (*.csv *.tsv *.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Glossary"), tr("Cannot read %1.").arg(path));
        return;
    }
    const QByteArray bytes = file.readAll();
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(bytes);
    if (utf8.hasError())
        text = QString::fromLocal8Bit(bytes);
    int skipped = 0;
    const QVector<CustomTerm> terms = CustomGlossary::fromCsv(text, &skipped);
    if (terms.isEmpty()) {
        QMessageBox::information(this, tr("Glossary"), tr("No term found in this file."));
        return;
    }
    importTerms(terms);
    QMessageBox::information(this, tr("Glossary"),
                             tr("%1 term(s) imported, %2 line(s) ignored.").arg(terms.size()).arg(skipped));
}

void GlossaryPanel::exportFile()
{
    if (m_custom.isEmpty()) {
        QMessageBox::information(this, tr("Glossary"), tr("There is no term of your own to export yet."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export glossary terms"), QStringLiteral("glossary.csv"),
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Glossary"), tr("Cannot write %1.").arg(path));
        return;
    }
    file.write("\xEF\xBB\xBF");
    file.write(CustomGlossary::toCsv(m_custom).toUtf8());
}

void GlossaryPanel::contextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    if (!idx.isValid())
        return;
    const GlossaryTerm &t = m_model->term(m_proxy->mapToSource(idx).row());
    QMenu menu(this);
    if (m_add->isEnabled()) {
        menu.addAction(t.custom ? tr("Edit this term...") : tr("Override with my own translation..."), [this, t] {
            CustomTerm initial;
            initial.source = t.source;
            initial.target = t.targets.value(0);
            initial.keep = t.keep;
            editDialog(initial, !t.custom);
        });
        menu.addAction(tr("Keep this term as is (do not translate)"), [this, t] {
            addOrReplaceTerm({t.source, t.source, true});
        });
        if (t.custom)
            menu.addAction(tr("Delete this term"), [this, t] { removeTerm(t.source); });
        menu.addSeparator();
    }
    for (const QString &target : t.targets)
        menu.addAction(tr("Insert \"%1\"").arg(target), [this, target] { emit insertRequested(target); });
    menu.addAction(tr("Copy the term"), [t] { QApplication::clipboard()->setText(t.source); });
    if (!t.targets.isEmpty())
        menu.addAction(tr("Copy the translation"), [t] { QApplication::clipboard()->setText(t.targets.first()); });
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}
