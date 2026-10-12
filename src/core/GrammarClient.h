#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

// One problem found by a grammar / spelling checker, as a range of the checked text.
struct GrammarIssue {
    enum Kind { Spelling, Grammar, Style };
    Kind kind = Grammar;
    int start = 0;           // UTF-16 offset in the checked text
    int length = 0;
    QString message;
    QString ruleId;
    QStringList suggestions;
};

// Asynchronous client of a checker server reached over HTTP(S): Grammalecte (French) or LanguageTool.
class GrammarClient : public QObject
{
    Q_OBJECT

public:
    enum Engine { Grammalecte, LanguageTool };
    using Callback = std::function<void(const QVector<GrammarIssue> &issues, const QString &error)>;

    explicit GrammarClient(QObject *parent = nullptr);

    // Which server serves a target language: Grammalecte for French when it is configured, LanguageTool
    // otherwise. Returns false when no configured server handles the language.
    static bool engineFor(const QString &gameLanguage, const QString &grammalecteUrl, const QString &languageToolUrl,
                          Engine *engine, QString *url);

    void check(Engine engine, const QString &serverUrl, const QString &gameLanguage, const QString &text, Callback done);

    // Parsers, public for the tests.
    static QVector<GrammarIssue> parseGrammalecte(const QByteArray &json, const QString &text);
    static QVector<GrammarIssue> parseLanguageTool(const QByteArray &json);

private:
    void post(const QString &url, const QByteArray &body, std::function<void(const QByteArray &, const QString &)> done);

    QNetworkAccessManager m_network;
};
