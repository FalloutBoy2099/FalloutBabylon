#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QVector>
#include <functional>

// Minimal asynchronous client of the DeepL REST API: translation (with context, glossary and protected
// placeholders), glossaries (v3 endpoints) and usage. Callbacks run on the thread that owns the client.
class DeepLClient : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString apiKey;
        QString sourceLanguage;   // game language folder names ("english"); converted to DeepL codes
        QString targetLanguage;
        QString glossaryId;       // optional
        QString formality;        // "", "prefer_more" or "prefer_less"
    };

    using TranslateCallback = std::function<void(const QString &text, const QString &error)>;
    using UsageCallback = std::function<void(qint64 used, qint64 limit, const QString &error)>;
    using GlossaryCallback = std::function<void(const QString &glossaryId, const QString &error)>;
    using DoneCallback = std::function<void(const QString &error)>;
    using GlossaryListCallback =
        std::function<void(const QVector<QPair<QString, QString>> &glossaries, const QString &error)>;   // id, name

    explicit DeepLClient(QObject *parent = nullptr);

    // Overrides the host derived from the key (free keys end with ":fx"); used by the tests.
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    static QString baseUrlFor(const QString &apiKey);

    // `context` is extra text that influences the translation but is not translated (may be empty).
    void translate(const QString &text, const QString &context, const Options &options, TranslateCallback done);
    void usage(const QString &apiKey, UsageCallback done);
    void createGlossary(const QString &apiKey, const QString &name, const QString &sourceLanguage,
                        const QString &targetLanguage, const QVector<QPair<QString, QString>> &entries,
                        GlossaryCallback done);
    // Glossaries of the account (the free plan holds a single one).
    void listGlossaries(const QString &apiKey, GlossaryListCallback done);
    // The error text createGlossary() reports when the account's glossary allowance is used up.
    static QString glossaryLimitMessage();
    void deleteGlossary(const QString &apiKey, const QString &glossaryId, DoneCallback done);

private:
    using JsonCallback = std::function<void(const QJsonObject &json, const QString &error)>;
    void send(const QString &apiKey, const QByteArray &verb, const QString &path, const QJsonObject &body,
              JsonCallback done, int attempt = 0);

    QNetworkAccessManager m_network;
    QString m_baseUrl;
};
