#pragma once

#include "core/DeepLClient.h"
#include "core/GrammarClient.h"
#include "core/Project.h"

#include <QObject>
#include <memory>

// Shared access to DeepL and to the correction servers for the whole application:
// reads the settings, keeps one DeepL glossary per project up to date, routes grammar checks.
class TranslationService : public QObject
{
    Q_OBJECT

public:
    explicit TranslationService(QObject *parent = nullptr);

    DeepLClient *deepl() { return &m_deepl; }
    GrammarClient *grammar() { return &m_grammar; }

    bool deeplConfigured() const;
    // DeepL options for a project (key, languages, formality); glossaryId is filled by ensureGlossary().
    DeepLClient::Options deeplOptions(const Project &project) const;

    // Makes sure a DeepL glossary matching the project glossary exists (created when the glossary changed),
    // then calls back with its id (empty without glossary; `warning` explains a failure, translation can go on).
    void ensureGlossary(const std::shared_ptr<Project> &project, std::function<void(const QString &id, const QString &warning)> done);

    // Grammar check of text in the target language of `project`; the callback is not called when no
    // server is configured for that language (returns false).
    bool checkText(const Project &project, const QString &text, GrammarClient::Callback done);
    bool checkAvailable(const Project &project) const;

private:
    // Creates a DeepL glossary; when the account's single free glossary is taken, offers to replace it.
    void createOrReplace(const QString &key, const QString &name, const QString &source, const QString &target,
                         const QVector<QPair<QString, QString>> &entries, bool mayReplace, DeepLClient::GlossaryCallback done);

    DeepLClient m_deepl;
    GrammarClient m_grammar;
};
