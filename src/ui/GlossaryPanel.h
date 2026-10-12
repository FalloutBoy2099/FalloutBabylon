#pragma once

#include "core/Glossary.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableView;
class GlossaryTableModel;

// Dock panel listing the glossary terms (names found in the /game files) and their translations.
class GlossaryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GlossaryPanel(QWidget *parent = nullptr);

    void setGlossary(const QVector<GlossaryTerm> &terms);
    void showBuilding();

signals:
    void insertRequested(const QString &text);

private:
    void contextMenu(const QPoint &pos);

    GlossaryTableModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QTableView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_summary = nullptr;
};
