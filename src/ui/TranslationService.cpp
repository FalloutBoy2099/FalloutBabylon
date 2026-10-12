#include "TranslationService.h"

#include "core/AppSettings.h"
#include "core/Languages.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QSettings>

TranslationService::TranslationService(QObject *parent)
    : QObject(parent)
    , m_deepl(this)
    , m_grammar(this)
{
}

bool TranslationService::deeplConfigured() const
{
    return !AppSettings::deeplKey().isEmpty();
}

DeepLClient::Options TranslationService::deeplOptions(const Project &project) const
{
    DeepLClient::Options o;
    o.apiKey = AppSettings::deeplKey();
    o.sourceLanguage = project.config().sourceLanguage;
    o.targetLanguage = project.config().targetLanguage;
    o.formality = AppSettings::deeplFormality();
    return o;
}

void TranslationService::ensureGlossary(const std::shared_ptr<Project> &project,
                                        std::function<void(const QString &, const QString &)> done)
{
    const ProjectConfig &config = project->config();
    // term -> translation pairs of the project glossary (DeepL entries cannot hold tabs or line breaks)
    QVector<QPair<QString, QString>> entries;
    for (const GlossaryTerm &t : project->glossary().terms()) {
        if (t.targets.isEmpty())
            continue;
        const QString target = t.targets.first();
        if (t.source.contains(QLatin1Char('\t')) || target.contains(QLatin1Char('\t')) || t.source.contains(QLatin1Char('\n'))
            || target.contains(QLatin1Char('\n')))
            continue;
        entries.push_back({t.source, target});
    }
    if (!AppSettings::deeplUseGlossary() || entries.isEmpty()
        || Languages::deeplGlossary(config.sourceLanguage).isEmpty() || Languages::deeplGlossary(config.targetLanguage).isEmpty()) {
        done({}, QString());
        return;
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(config.sourceLanguage.toUtf8());
    hash.addData(config.targetLanguage.toUtf8());
    for (const auto &e : entries) {
        hash.addData(e.first.toUtf8());
        hash.addData("\t", 1);
        hash.addData(e.second.toUtf8());
        hash.addData("\n", 1);
    }
    const QString signature = QString::fromLatin1(hash.result().toHex());
    const QString settingsKey = QStringLiteral("deepl/glossary/") + config.id;
    const QStringList saved = QSettings().value(settingsKey).toString().split(QLatin1Char('|'));
    if (saved.size() == 2 && saved.at(0) == signature) {
        done(saved.at(1), QString());
        return;
    }

    const QString key = AppSettings::deeplKey();
    const QString oldId = saved.size() == 2 ? saved.at(1) : QString();
    createOrReplace(key, QStringLiteral("FalloutBabylon %1").arg(config.name), config.sourceLanguage, config.targetLanguage,
                    entries, true, [this, done, settingsKey, signature, key, oldId](const QString &id, const QString &error) {
                        if (!error.isEmpty() || id.isEmpty()) {
                            done({}, tr("DeepL glossary not used: %1").arg(error));
                            return;
                        }
                        QSettings().setValue(settingsKey, signature + QLatin1Char('|') + id);
                        if (!oldId.isEmpty())
                            m_deepl.deleteGlossary(key, oldId, [](const QString &) {});
                        done(id, QString());
                    });
}

void TranslationService::createOrReplace(const QString &key, const QString &name, const QString &source, const QString &target,
                                         const QVector<QPair<QString, QString>> &entries, bool mayReplace,
                                         DeepLClient::GlossaryCallback done)
{
    m_deepl.createGlossary(key, name, source, target, entries,
                           [=](const QString &id, const QString &error) {
        if (error != DeepLClient::glossaryLimitMessage() || !mayReplace) {
            done(id, error);
            return;
        }
        // The free plan holds one glossary and the account already has one: replace it, but only if the user agrees.
        m_deepl.listGlossaries(key, [=](const QVector<QPair<QString, QString>> &existing, const QString &listError) {
            if (!listError.isEmpty() || existing.isEmpty()) {
                done({}, error);
                return;
            }
            QStringList names;
            for (const auto &g : existing)
                names << QStringLiteral("• ") + g.second;
            const auto answer = QMessageBox::question(
                QApplication::activeWindow(), tr("DeepL glossary"),
                tr("Your DeepL account already contains a glossary:\n\n%1\n\nThe free plan allows a single glossary. "
                   "Delete it and replace it with the glossary of this project?\n\nThis cannot be undone.")
                    .arg(names.join(QLatin1Char('\n'))));
            if (answer != QMessageBox::Yes) {
                done({}, error);
                return;
            }
            auto remaining = std::make_shared<int>(int(existing.size()));
            auto failure = std::make_shared<QString>();
            for (const auto &g : existing)
                m_deepl.deleteGlossary(key, g.first, [=](const QString &deleteError) {
                    if (!deleteError.isEmpty())
                        *failure = deleteError;
                    if (--*remaining > 0)
                        return;
                    if (!failure->isEmpty())
                        done({}, *failure);
                    else
                        createOrReplace(key, name, source, target, entries, false, done);
                });
        });
    });
}

bool TranslationService::checkAvailable(const Project &project) const
{
    GrammarClient::Engine engine;
    QString url;
    return AppSettings::checkEnabled()
        && GrammarClient::engineFor(project.config().targetLanguage, AppSettings::grammalecteUrl(),
                                    AppSettings::languageToolUrl(), &engine, &url);
}

bool TranslationService::checkText(const Project &project, const QString &text, GrammarClient::Callback done)
{
    GrammarClient::Engine engine;
    QString url;
    if (!AppSettings::checkEnabled()
        || !GrammarClient::engineFor(project.config().targetLanguage, AppSettings::grammalecteUrl(),
                                     AppSettings::languageToolUrl(), &engine, &url))
        return false;
    m_grammar.check(engine, url, project.config().targetLanguage, text, std::move(done));
    return true;
}
