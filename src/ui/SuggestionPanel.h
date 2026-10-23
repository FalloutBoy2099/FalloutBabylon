#pragma once

#include "core/TranslationIndex.h"

#include <QWidget>
#include <memory>

class QLabel;
class QSpinBox;
class QTableWidget;

// Translation memory suggestions for the line being translated: what was already done for the same or a
// close text in this project. Double-click (or Enter) uses a suggestion.
class SuggestionPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SuggestionPanel(QWidget *parent = nullptr);

    // nullptr while the index of the project is being built.
    void setIndex(const std::shared_ptr<TranslationIndex> &index);
    // The line to translate: its file, id and source text.
    void showFor(const QString &relPath, int id, const QString &source);
    void clear();

    // For the tests
    int suggestionCount() const;
    QVector<TranslationIndex::Match> suggestions() const { return m_matches; }
    void useRow(int row);

signals:
    // `score` below 1 means the text is not the same: the line should be reviewed.
    void useRequested(const QString &translation, double score);

private:
    void refresh();

    std::shared_ptr<TranslationIndex> m_index;
    QString m_relPath, m_source;
    int m_id = -1;
    QLabel *m_status = nullptr;
    QSpinBox *m_minimum = nullptr;
    QTableWidget *m_table = nullptr;
    QVector<TranslationIndex::Match> m_matches;
};
