#pragma once

#include <QString>
#include <QVector>

// A term added by hand to the glossary of a project (names, jargon, "do not translate" words...).
struct CustomTerm {
    QString source;
    QString target;       // equals `source` when `keep` is set
    bool keep = false;    // do not translate: the term must appear unchanged in the translation
};

namespace CustomGlossary {

// "term;translation;keep" lines (UTF-8 with a header), as exchanged with spreadsheets.
QString toCsv(const QVector<CustomTerm> &terms);

// Accepts comma, semicolon or tab separated text, with or without a header; the third column
// ("keep", "1", "yes", "x"...) marks a do-not-translate term. A line with only a term is a do-not-translate term.
QVector<CustomTerm> fromCsv(const QString &text, int *skipped = nullptr);

// Imported terms replace those with the same term (compared ignoring case and extra spaces).
QVector<CustomTerm> merge(const QVector<CustomTerm> &existing, const QVector<CustomTerm> &imported);

QString keyOf(const QString &source);

}
