#pragma once

#include "TermMatcher.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

// One glossary term: a source expression with its translation(s) found in the game files.
struct GlossaryTerm {
    QString source;
    QStringList targets;     // distinct translations, most frequent first (empty = not translated yet)
    QString category;        // "Item", "Skill", "Location"...
    QStringList origins;     // "pro_item.msg#100"
    bool custom = false;     // entered by hand (it overrides what the game files say)
    bool keep = false;       // custom term that must stay unchanged in the translation
    bool conflict() const { return targets.size() > 1; }
};

// Terms extracted from the /game message files, plus the matchers that spot them in dialogue text.
class Glossary
{
public:
    struct Hit {
        TermMatcher::Match match;
        const GlossaryTerm *term = nullptr;
    };
    struct Issue {
        const GlossaryTerm *term = nullptr;
        TermMatcher::Match sourceMatch;
        QString message;
    };

    Glossary() = default;

    void clear();
    void add(const QString &source, const QString &target, const QString &category, const QString &origin);
    // A term entered by hand: its translation wins over the ones found in the game files.
    void addCustom(const QString &source, const QString &target, bool keep, const QString &category);
    // Groups duplicates and builds the matchers.
    void finalize(const QString &sourceLanguage, const QString &targetLanguage);

    const QVector<GlossaryTerm> &terms() const { return m_terms; }
    bool isEmpty() const { return m_terms.isEmpty(); }
    int conflictCount() const;

    QVector<Hit> findInSource(const QString &text) const;
    QVector<Hit> findInTarget(const QString &text) const;
    // Terms present in `source` whose known translation is absent from `target`.
    QVector<Issue> checkConsistency(const QString &source, const QString &target) const;

private:
    struct Raw {
        QString source, target, category, origin;
        bool custom = false, keep = false;
    };
    QVector<Raw> m_raw;
    QVector<GlossaryTerm> m_terms;
    std::unique_ptr<TermMatcher> m_sourceMatcher;
    std::unique_ptr<TermMatcher> m_targetMatcher;
};
