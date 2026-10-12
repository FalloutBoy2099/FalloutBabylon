// Tests of the web service clients against a tiny local HTTP server (nothing leaves the machine).
// Optional real checks: BABYLON_GRAMMALECTE_URL, BABYLON_LT_URL, BABYLON_DEEPL_KEY.

#include "core/DeepLClient.h"
#include "core/GrammarClient.h"
#include "core/Languages.h"
#include "core/Placeholders.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace {

struct HttpRequest {
    QByteArray method, path, body;
    QMap<QByteArray, QByteArray> headers;
};
struct HttpResponse {
    int status = 200;
    QByteArray body;
};

// One-shot-per-connection HTTP/1.1 server driven by a handler function.
class MockServer : public QObject
{
public:
    using Handler = std::function<HttpResponse(const HttpRequest &)>;

    explicit MockServer(Handler handler) : m_handler(std::move(handler))
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const int headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    HttpRequest request;
                    const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                    const QList<QByteArray> first = lines.first().trimmed().split(' ');
                    request.method = first.value(0);
                    request.path = first.value(1);
                    for (int i = 1; i < lines.size(); ++i) {
                        const int colon = lines.at(i).indexOf(':');
                        if (colon > 0)
                            request.headers.insert(lines.at(i).left(colon).toLower(), lines.at(i).mid(colon + 1).trimmed());
                    }
                    const int length = request.headers.value("content-length").toInt();
                    if (buffer->size() < headerEnd + 4 + length)
                        return;
                    request.body = buffer->mid(headerEnd + 4, length);
                    requests << request;
                    const HttpResponse response = m_handler(request);
                    socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " X\r\nContent-Type: application/json\r\n"
                                  "Content-Length: " + QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n\r\n"
                                  + response.body);
                    socket->disconnectFromHost();
                    delete buffer;
                });
            }
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QList<HttpRequest> requests;

private:
    QTcpServer m_server;
    Handler m_handler;
};

template <typename Pred>
bool waitFor(Pred done, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QTest::qWait(10);
    return done();
}

}

class NetTests : public QObject
{
    Q_OBJECT

private slots:
    void languageMapping()
    {
        QCOMPARE(Languages::deeplSource("english"), QString("EN"));
        QCOMPARE(Languages::deeplTarget("english"), QString("EN-US"));
        QCOMPARE(Languages::deeplTarget("portuguese"), QString("PT-PT"));
        QCOMPARE(Languages::deeplTarget("cht"), QString("ZH-HANT"));
        QCOMPARE(Languages::deeplGlossary("french"), QString("fr"));
        QCOMPARE(Languages::languageTool("french"), QString("fr"));
        QVERIFY(Languages::languageTool("czech").isEmpty());
        QVERIFY(Languages::deeplTarget("vietnamese").isEmpty());
    }

    void placeholdersRoundTrip()
    {
        const QString text = QStringLiteral("Hello @Name@, you have %d caps & <b>{count}</b>.\nBye");
        const auto p = Placeholders::protect(text);
        QCOMPARE(p.tokens, (QStringList{"@Name@", "%d", "{count}"}));
        QVERIFY(!p.xml.contains('@'));
        QVERIFY(p.xml.contains("&amp;"));
        QString back;
        QVERIFY(Placeholders::restore(p.xml, p.tokens, &back));
        QCOMPARE(back, text);

        // a service that drops a marker or alters the order is detected
        QString lost;
        QVERIFY(!Placeholders::restore(QStringLiteral("Bonjour <x>0</x>"), p.tokens, &lost));
        QVERIFY(lost.contains("@Name@"));
        // reordering is fine when every marker is present
        QVERIFY(Placeholders::restore(QStringLiteral("<x>2</x> <x>1</x> <x>0</x>"), p.tokens, &back));
        QCOMPARE(back, QStringLiteral("{count} %d @Name@"));
    }

    void deeplTranslate()
    {
        int calls = 0;
        MockServer server([&](const HttpRequest &r) {
            ++calls;
            HttpResponse resp;
            if (r.path == "/v2/translate") {
                const QJsonObject body = QJsonDocument::fromJson(r.body).object();
                if (r.headers.value("authorization") != "DeepL-Auth-Key test-key:fx") {
                    resp.status = 403;
                    return resp;
                }
                if (body.value("text").toString() == QLatin1String("quota")) {
                    resp.status = 456;
                    return resp;
                }
                const QString text = body.value("text").toArray().first().toString();
                QJsonObject out;
                out.insert("translations", QJsonArray{QJsonObject{{"text", "[FR] " + text}}});
                resp.body = QJsonDocument(out).toJson();
                // echo what was sent so the test can inspect it
                lastBody = body;
            }
            return resp;
        });
        DeepLClient client;
        client.setBaseUrl(server.url());
        DeepLClient::Options o;
        o.apiKey = "test-key:fx";
        o.sourceLanguage = "english";
        o.targetLanguage = "french";
        o.glossaryId = "g-1";
        o.formality = "prefer_more";

        QString result, error;
        bool done = false;
        client.translate("Take a Stimpak, @Name@ & go.", "Previous line: Hello.", o,
                         [&](const QString &t, const QString &e) { result = t; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result, QStringLiteral("[FR] Take a Stimpak, @Name@ & go."));
        QCOMPARE(lastBody.value("source_lang").toString(), QString("EN"));
        QCOMPARE(lastBody.value("target_lang").toString(), QString("FR"));
        QCOMPARE(lastBody.value("glossary_id").toString(), QString("g-1"));
        QCOMPARE(lastBody.value("context").toString(), QString("Previous line: Hello."));
        QCOMPARE(lastBody.value("tag_handling").toString(), QString("xml"));
        QCOMPARE(lastBody.value("ignore_tags").toArray().first().toString(), QString("x"));
        QCOMPARE(lastBody.value("formality").toString(), QString("prefer_more"));
        QVERIFY(lastBody.value("text").toArray().first().toString().contains("<x>0</x>"));

        // wrong key
        o.apiKey = "bad:fx";
        done = false;
        client.translate("Hi", {}, o, [&](const QString &, const QString &e) { error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QVERIFY(!error.isEmpty());

        // unsupported language: reported without any request
        const int before = calls;
        o.apiKey = "test-key:fx";
        o.targetLanguage = "vietnamese";
        done = false;
        client.translate("Hi", {}, o, [&](const QString &, const QString &e) { error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QVERIFY(!error.isEmpty());
        QCOMPARE(calls, before);
    }

    void deeplRetriesOnRateLimit()
    {
        int attempts = 0;
        MockServer server([&](const HttpRequest &) {
            HttpResponse r;
            if (++attempts < 2) {
                r.status = 429;
                return r;
            }
            r.body = R"({"translations":[{"text":"ok"}]})";
            return r;
        });
        DeepLClient client;
        client.setBaseUrl(server.url());
        DeepLClient::Options o{"k:fx", "english", "french", {}, {}};
        QString result, error;
        bool done = false;
        client.translate("x", {}, o, [&](const QString &t, const QString &e) { result = t; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }, 8000));
        QCOMPARE(result, QString("ok"));
        QCOMPARE(attempts, 2);
    }

    void deeplUsageAndGlossary()
    {
        MockServer server([&](const HttpRequest &r) {
            HttpResponse resp;
            if (r.path == "/v2/usage")
                resp.body = R"({"character_count":1234,"character_limit":500000})";
            else if (r.path == "/v3/glossaries" && r.method == "GET")
                resp.body = R"({"glossaries":[{"glossary_id":"old-1","name":"Boutique"}]})";
            else if (r.path == "/v3/glossaries" && r.method == "POST") {
                lastBody = QJsonDocument::fromJson(r.body).object();
                resp.body = R"({"glossary_id":"abc-123","name":"n"})";
            } else if (r.path == "/v3/glossaries/abc-123" && r.method == "DELETE")
                resp.status = 204;
            else
                resp.status = 404;
            return resp;
        });
        DeepLClient client;
        client.setBaseUrl(server.url());

        qint64 used = 0, limit = 0;
        QString error;
        bool done = false;
        client.usage("k:fx", [&](qint64 u, qint64 l, const QString &e) { used = u; limit = l; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QCOMPARE(used, qint64(1234));
        QCOMPARE(limit, qint64(500000));

        QVector<QPair<QString, QString>> existing;
        done = false;
        client.listGlossaries("k:fx", [&](const QVector<QPair<QString, QString>> &list, const QString &e) {
            existing = list;
            error = e;
            done = true;
        });
        QVERIFY(waitFor([&] { return done; }));
        QCOMPARE(existing.size(), 1);
        QCOMPARE(existing.first().first, QString("old-1"));
        QCOMPARE(existing.first().second, QString("Boutique"));

        QString id;
        done = false;
        client.createGlossary("k:fx", "FalloutBabylon test", "english", "french",
                              {{"Stimpak", "Stimpak"}, {"Leather Armor", "Armure en cuir"}},
                              [&](const QString &g, const QString &e) { id = g; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QCOMPARE(id, QString("abc-123"));
        const QJsonObject dict = lastBody.value("dictionaries").toArray().first().toObject();
        QCOMPARE(dict.value("source_lang").toString(), QString("en"));
        QCOMPARE(dict.value("target_lang").toString(), QString("fr"));
        QCOMPARE(dict.value("entries").toString(), QString("Stimpak\tStimpak\nLeather Armor\tArmure en cuir"));
        QCOMPARE(dict.value("entries_format").toString(), QString("tsv"));

        done = false;
        client.deleteGlossary("k:fx", "abc-123", [&](const QString &e) { error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void grammarParsers()
    {
        // answers captured from real Grammalecte 2.3.0 / LanguageTool 6.8 servers
        const QByteArray gram = R"({ "program": "grammalecte-fr", "version": "2.3.0", "lang": "fr", "error": "", "data" : [
{"iParagraph": 1, "lGrammarErrors": [{"nStart": 8, "nEnd": 13, "sLineId": "#33033", "sRuleId": "r1", "sType": "ppas", "sMessage": "Après être, un participe passé.", "aSuggestions": ["allé", "allée"], "URL": ""}], "lSpellingErrors": []},
{"iParagraph": 2, "lGrammarErrors": [], "lSpellingErrors": [{"nStart": 3, "nEnd": 8, "sValue": "marchè", "sType": "WORD"}]}
]})";
        const auto g = GrammarClient::parseGrammalecte(gram, QStringLiteral("Je suis aller au marché.\nUn marchè."));
        QCOMPARE(g.size(), 2);
        QCOMPARE(g.at(0).start, 8);
        QCOMPARE(g.at(0).length, 5);
        QCOMPARE(g.at(0).suggestions, (QStringList{"allé", "allée"}));
        QCOMPARE(g.at(1).kind, GrammarIssue::Spelling);
        QCOMPARE(g.at(1).start, QStringLiteral("Je suis aller au marché.\n").size() + 3); // second paragraph offset

        const QByteArray lt = R"({"matches":[{"message":"Un participe passé semble plus approprié.","replacements":[{"value":"allé"},{"value":"allés"}],"offset":8,"length":5,"rule":{"id":"ETRE_VPPA_OU_ADJ","issueType":"uncategorized"}},{"message":"Faute","replacements":[],"offset":0,"length":2,"rule":{"id":"MORFOLOGIK","issueType":"misspelling"}}]})";
        const auto l = GrammarClient::parseLanguageTool(lt);
        QCOMPARE(l.size(), 2);
        QCOMPARE(l.at(0).ruleId, QString("ETRE_VPPA_OU_ADJ"));
        QCOMPARE(l.at(0).suggestions.first(), QString("allé"));
        QCOMPARE(l.at(1).kind, GrammarIssue::Spelling);
    }

    void grammarEngineChoice()
    {
        GrammarClient::Engine e;
        QString url;
        QVERIFY(GrammarClient::engineFor("french", "http://g", "http://l", &e, &url));
        QCOMPARE(e, GrammarClient::Grammalecte);
        QVERIFY(GrammarClient::engineFor("french", "", "http://l", &e, &url));
        QCOMPARE(e, GrammarClient::LanguageTool);
        QVERIFY(GrammarClient::engineFor("german", "http://g", "http://l", &e, &url));
        QCOMPARE(e, GrammarClient::LanguageTool);
        QVERIFY(!GrammarClient::engineFor("german", "http://g", "", &e, &url));
        QVERIFY(!GrammarClient::engineFor("czech", "", "http://l", &e, &url)); // LanguageTool has no Czech
    }

    void grammarClientAgainstMock()
    {
        MockServer server([&](const HttpRequest &r) {
            HttpResponse resp;
            lastForm = r.body;
            lastPath = r.path;
            resp.body = R"({"matches":[{"message":"m","replacements":[{"value":"x"}],"offset":1,"length":2,"rule":{"id":"R","issueType":"grammar"}}]})";
            return resp;
        });
        GrammarClient client;
        QVector<GrammarIssue> issues;
        QString error;
        bool done = false;
        client.check(GrammarClient::LanguageTool, server.url() + "/", "german", QStringLiteral("a+b & c é"),
                     [&](const QVector<GrammarIssue> &i, const QString &e) { issues = i; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(issues.size(), 1);
        QCOMPARE(lastPath, QByteArray("/v2/check"));
        QCOMPARE(lastForm, QByteArray("text=a%2Bb%20%26%20c%20%C3%A9&language=de-DE"));
    }

    void realServersIfConfigured()
    {
        const QString g = qEnvironmentVariable("BABYLON_GRAMMALECTE_URL");
        const QString l = qEnvironmentVariable("BABYLON_LT_URL");
        if (g.isEmpty() && l.isEmpty())
            QSKIP("BABYLON_GRAMMALECTE_URL / BABYLON_LT_URL not set");
        GrammarClient client;
        for (auto [engine, url] : {std::pair{GrammarClient::Grammalecte, g}, std::pair{GrammarClient::LanguageTool, l}}) {
            if (url.isEmpty())
                continue;
            QVector<GrammarIssue> issues;
            QString error;
            bool done = false;
            client.check(engine, url, "french", QStringLiteral("Je suis aller au marché.\nLes chat mange."),
                         [&](const QVector<GrammarIssue> &i, const QString &e) { issues = i; error = e; done = true; });
            QVERIFY(waitFor([&] { return done; }, 15000));
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY2(issues.size() >= 2, qPrintable(url));
            qInfo() << url << "->" << issues.size() << "issues; first:" << issues.first().message << issues.first().suggestions;
            // the first issue covers "aller"
            QCOMPARE(QStringLiteral("Je suis aller au marché.").mid(issues.first().start, issues.first().length), QString("aller"));
        }
    }

    void realDeepLIfConfigured()
    {
        const QString key = qEnvironmentVariable("BABYLON_DEEPL_KEY");
        if (key.isEmpty())
            QSKIP("BABYLON_DEEPL_KEY not set");
        DeepLClient client;
        qint64 used = -1, limit = 0;
        QString error;
        bool done = false;
        client.usage(key, [&](qint64 u, qint64 li, const QString &e) { used = u; limit = li; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }, 15000));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        qInfo() << "DeepL usage" << used << "/" << limit;

        DeepLClient::Options o{key, "english", "french", {}, {}};
        QString result;
        done = false;
        client.translate("Take a Stimpak, @Name@. It will heal %d hit points.", "Dialogue of a trader in Fallout 2.", o,
                         [&](const QString &t, const QString &e) { result = t; error = e; done = true; });
        QVERIFY(waitFor([&] { return done; }, 20000));
        qInfo() << "DeepL result:" << result << "error:" << error;
        QVERIFY(result.contains("@Name@"));
        QVERIFY(result.contains("%d"));
        QVERIFY(result != QLatin1String("Take a Stimpak, @Name@. It will heal %d hit points."));
    }

private:
    QJsonObject lastBody;
    QByteArray lastForm, lastPath;
};

QTEST_MAIN(NetTests)
#include "tst_net.moc"
