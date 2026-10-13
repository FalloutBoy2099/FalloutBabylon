#pragma once

#include "core/CustomGlossary.h"
#include "core/Glossary.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class GlossaryTableModel;

// Dock panel listing the glossary terms (names found in the /game files and terms entered by hand)
// and their translations.
class GlossaryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GlossaryPanel(QWidget *parent = nullptr);

    void setGlossary(const QVector<GlossaryTerm> &terms);
    void showBuilding();

    // Terms entered by hand (the project's); the panel edits its copy and reports changes.
    void setCustomTerms(const QVector<CustomTerm> &terms);
    QVector<CustomTerm> customTerms() const { return m_custom; }
    void setEditable(bool editable);

    // Programmatic edits (used by the dialogs, the context menu and the tests)
    void addOrReplaceTerm(const CustomTerm &term);
    void removeTerm(const QString &source);
    void importTerms(const QVector<CustomTerm> &terms);

signals:
    void insertRequested(const QString &text);
    void customTermsChanged(const QVector<CustomTerm> &terms);

private:
    void contextMenu(const QPoint &pos);
    void editDialog(const CustomTerm &initial, bool isNew);
    void editSelected();
    void deleteSelected();
    void importFile();
    void exportFile();
    const GlossaryTerm *selectedTerm() const;
    void commit(const QVector<CustomTerm> &terms);

    GlossaryTableModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QTableView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_add = nullptr, *m_edit = nullptr, *m_delete = nullptr, *m_import = nullptr, *m_export = nullptr;
    QVector<CustomTerm> m_custom;
};
