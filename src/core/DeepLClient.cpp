#include "DeepLClient.h"

#include "Languages.h"
#include "Placeholders.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kMaxAttempts = 4;
}

DeepLClient::DeepLClient(QObject *parent)
    : QObject(parent)
{
    m_network.setTransferTimeout(30000);
}

QString DeepLClient::baseUrlFor(const QString &apiKey)
{
    return apiKey.trimmed().endsWith(QLatin1String(":fx")) ? QStringLiteral("https://api-free.deepl.com")
                                                          : QStringLiteral("https://api.deepl.com");
}

void DeepLClient::send(const QString &apiKey, const QByteArray &verb, const QString &path, const QJsonObject &body,
                       JsonCallback done, int attempt)
{
    const QString key = apiKey.trimmed();
    if (key.isEmpty()) {
        done({}, tr("No DeepL API key. Enter it in Tools > Settings."));
        return;
    }
    const QString base = m_baseUrl.isEmpty() ? baseUrlFor(key) : m_baseUrl;
    QNetworkRequest request(QUrl(base + path));
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + key.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "FalloutBabylon/0.1");

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = verb == "GET" ? m_network.get(request)
        : verb == "DELETE"              ? m_network.deleteResource(request)
                                         : m_network.post(request, payload);
    QPointer<DeepLClient> self(this);
    connect(reply, &QNetworkReply::finished, this, [=]() mutable {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString netText = reply->errorString();
        reply->deleteLater();
        if (!self)
            return;

        // Rate limit / transient server trouble: wait and try again.
        if ((status == 429 || status == 503 || status == 529) && attempt + 1 < kMaxAttempts) {
            QTimer::singleShot(600 * (attempt + 1), self, [=] { self->send(apiKey, verb, path, body, done, attempt + 1); });
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(data).object();
        if (status >= 200 && status < 300) {
            done(json, QString());
            return;
        }
        QString message = json.value(QStringLiteral("message")).toString();
        if (status == 403)
            message = tr("DeepL rejected the API key.");
        else if (status == 456)
            message = tr("The DeepL quota is exhausted.");
        else if (message.isEmpty())
            message = status ? tr("DeepL error %1").arg(status) : netText;
        Q_UNUSED(netError)
        done(json, message);
    });
}

void DeepLClient::translate(const QString &text, const QString &context, const Options &o, TranslateCallback done)
{
    const QString source = Languages::deeplSource(o.sourceLanguage);
    const QString target = Languages::deeplTarget(o.targetLanguage);
    if (source.isEmpty() || target.isEmpty()) {
        done({}, tr("DeepL does not support %1.").arg(source.isEmpty() ? o.sourceLanguage : o.targetLanguage));
        return;
    }
    const Placeholders::Protected p =
        Placeholders::protect(text, o.glossaryId.isEmpty() ? o.localGlossary.get() : nullptr);

    QJsonObject body;
    body.insert(QStringLiteral("text"), QJsonArray{p.xml});
    body.insert(QStringLiteral("source_lang"), source);
    body.insert(QStringLiteral("target_lang"), target);
    body.insert(QStringLiteral("tag_handling"), QStringLiteral("xml"));
    body.insert(QStringLiteral("ignore_tags"), QJsonArray{QStringLiteral("x")});
    body.insert(QStringLiteral("split_sentences"), QStringLiteral("nonewlines"));
    body.insert(QStringLiteral("preserve_formatting"), true);
    if (!context.trimmed().isEmpty())
        body.insert(QStringLiteral("context"), context);
    if (!o.glossaryId.isEmpty())
        body.insert(QStringLiteral("glossary_id"), o.glossaryId);
    if (!o.formality.isEmpty())
        body.insert(QStringLiteral("formality"), o.formality);

    const QStringList tokens = p.tokens;
    send(o.apiKey, "POST", QStringLiteral("/v2/translate"), body,
         [done, tokens](const QJsonObject &json, const QString &error) {
             if (!error.isEmpty()) {
                 done({}, error);
                 return;
             }
             const QJsonArray list = json.value(QStringLiteral("translations")).toArray();
             if (list.isEmpty()) {
                 done({}, QObject::tr("Empty answer from DeepL."));
                 return;
             }
             QString result;
             const bool intact = Placeholders::restore(list.first().toObject().value(QStringLiteral("text")).toString(),
                                                       tokens, &result);
             done(result, intact ? QString() : QObject::tr("A placeholder (@...@, %s...) was altered: check this line."));
         });
}

void DeepLClient::usage(const QString &apiKey, UsageCallback done)
{
    send(apiKey, "GET", QStringLiteral("/v2/usage"), {}, [done](const QJsonObject &json, const QString &error) {
        done(qint64(json.value(QStringLiteral("character_count")).toDouble()),
             qint64(json.value(QStringLiteral("character_limit")).toDouble()), error);
    });
}

void DeepLClient::createGlossary(const QString &apiKey, const QString &name, const QString &sourceLanguage,
                                 const QString &targetLanguage, const QVector<QPair<QString, QString>> &entries,
                                 GlossaryCallback done)
{
    const QString source = Languages::deeplGlossary(sourceLanguage);
    const QString target = Languages::deeplGlossary(targetLanguage);
    if (source.isEmpty() || target.isEmpty() || entries.isEmpty()) {
        done({}, tr("No glossary can be created for this language pair."));
        return;
    }
    QStringList lines;
    for (const auto &e : entries)
        lines << e.first + QLatin1Char('\t') + e.second;

    QJsonObject dictionary;
    dictionary.insert(QStringLiteral("source_lang"), source);
    dictionary.insert(QStringLiteral("target_lang"), target);
    dictionary.insert(QStringLiteral("entries"), lines.join(QLatin1Char('\n')));
    dictionary.insert(QStringLiteral("entries_format"), QStringLiteral("tsv"));
    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("dictionaries"), QJsonArray{dictionary});
    send(apiKey, "POST", QStringLiteral("/v3/glossaries"), body, [done](const QJsonObject &json, const QString &error) {
        QString message = error;
        // 456 on this call means the glossary allowance is used up (the free plan holds a single glossary).
        if (!error.isEmpty() && error == QObject::tr("The DeepL quota is exhausted."))
            message = glossaryLimitMessage();
        done(json.value(QStringLiteral("glossary_id")).toString(), message);
    });
}

QString DeepLClient::glossaryLimitMessage()
{
    return QObject::tr("DeepL refused the glossary: the free plan allows a single glossary (delete the existing "
                       "one in your DeepL account to use the project glossary).");
}

void DeepLClient::listGlossaries(const QString &apiKey, GlossaryListCallback done)
{
    send(apiKey, "GET", QStringLiteral("/v3/glossaries"), {}, [done](const QJsonObject &json, const QString &error) {
        QVector<QPair<QString, QString>> list;
        for (const QJsonValue &v : json.value(QStringLiteral("glossaries")).toArray())
            list.push_back({v.toObject().value(QStringLiteral("glossary_id")).toString(),
                            v.toObject().value(QStringLiteral("name")).toString()});
        done(list, error);
    });
}

void DeepLClient::deleteGlossary(const QString &apiKey, const QString &glossaryId, DoneCallback done)
{
    send(apiKey, "DELETE", QStringLiteral("/v3/glossaries/") + glossaryId, {},
         [done](const QJsonObject &, const QString &error) { done(error); });
}
