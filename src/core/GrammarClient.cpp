#include "GrammarClient.h"

#include "Languages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString joinUrl(const QString &base, const QString &path)
{
    QString b = base.trimmed();
    while (b.endsWith(QLatin1Char('/')))
        b.chop(1);
    return b + path;
}

}

GrammarClient::GrammarClient(QObject *parent)
    : QObject(parent)
{
    m_network.setTransferTimeout(15000);
}

bool GrammarClient::engineFor(const QString &language, const QString &grammalecteUrl, const QString &languageToolUrl,
                              Engine *engine, QString *url)
{
    if (Languages::isFrench(language) && !grammalecteUrl.trimmed().isEmpty()) {
        *engine = Grammalecte;
        *url = grammalecteUrl.trimmed();
        return true;
    }
    if (!languageToolUrl.trimmed().isEmpty() && !Languages::languageTool(language).isEmpty()) {
        *engine = LanguageTool;
        *url = languageToolUrl.trimmed();
        return true;
    }
    return false;
}

void GrammarClient::post(const QString &url, const QByteArray &body,
                         std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_network.post(request, body);
    QPointer<GrammarClient> self(this);
    connect(reply, &QNetworkReply::finished, this, [reply, done, self] {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = (status >= 200 && status < 300) ? QString()
            : status ? tr("Server error %1").arg(status) : reply->errorString();
        reply->deleteLater();
        if (self)
            done(data, error);
    });
}

void GrammarClient::check(Engine engine, const QString &serverUrl, const QString &gameLanguage, const QString &text,
                          Callback done)
{
    if (text.trimmed().isEmpty()) {
        done({}, QString());
        return;
    }
    // application/x-www-form-urlencoded, built by hand: every byte of the text is percent-encoded ('+' included).
    QByteArray form = "text=" + QUrl::toPercentEncoding(text);
    if (engine == Grammalecte) {
        post(joinUrl(serverUrl, QStringLiteral("/gc_text/fr")), form,
             [done, text](const QByteArray &data, const QString &error) {
                 done(error.isEmpty() ? parseGrammalecte(data, text) : QVector<GrammarIssue>(), error);
             });
        return;
    }
    const QString code = Languages::languageTool(gameLanguage);
    if (code.isEmpty()) {
        done({}, tr("LanguageTool does not support %1.").arg(gameLanguage));
        return;
    }
    form += "&language=" + QUrl::toPercentEncoding(code);
    post(joinUrl(serverUrl, QStringLiteral("/v2/check")), form,
         [done](const QByteArray &data, const QString &error) {
             done(error.isEmpty() ? parseLanguageTool(data) : QVector<GrammarIssue>(), error);
         });
}

QVector<GrammarIssue> GrammarClient::parseGrammalecte(const QByteArray &json, const QString &text)
{
    QVector<GrammarIssue> issues;
    const QJsonObject root = QJsonDocument::fromJson(json).object();

    // Offsets are relative to the paragraph (a line of the text).
    QVector<int> lineStart;
    int pos = 0;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        lineStart << pos;
        pos += line.size() + 1;
    }

    for (const QJsonValue &pv : root.value(QStringLiteral("data")).toArray()) {
        const QJsonObject paragraph = pv.toObject();
        const int index = paragraph.value(QStringLiteral("iParagraph")).toInt(1) - 1;
        const int base = lineStart.value(qMax(0, index), 0);

        for (const QJsonValue &ev : paragraph.value(QStringLiteral("lGrammarErrors")).toArray()) {
            const QJsonObject e = ev.toObject();
            GrammarIssue issue;
            issue.kind = e.value(QStringLiteral("sType")).toString() == QLatin1String("typo") ? GrammarIssue::Style
                                                                                              : GrammarIssue::Grammar;
            issue.start = base + e.value(QStringLiteral("nStart")).toInt();
            issue.length = e.value(QStringLiteral("nEnd")).toInt() - e.value(QStringLiteral("nStart")).toInt();
            issue.message = e.value(QStringLiteral("sMessage")).toString();
            issue.ruleId = e.value(QStringLiteral("sRuleId")).toString();
            for (const QJsonValue &s : e.value(QStringLiteral("aSuggestions")).toArray())
                issue.suggestions << s.toString();
            if (issue.length > 0)
                issues.push_back(issue);
        }
        for (const QJsonValue &ev : paragraph.value(QStringLiteral("lSpellingErrors")).toArray()) {
            const QJsonObject e = ev.toObject();
            GrammarIssue issue;
            issue.kind = GrammarIssue::Spelling;
            issue.start = base + e.value(QStringLiteral("nStart")).toInt();
            issue.length = e.value(QStringLiteral("nEnd")).toInt() - e.value(QStringLiteral("nStart")).toInt();
            issue.message = QObject::tr("Unknown word: %1").arg(e.value(QStringLiteral("sValue")).toString());
            issue.ruleId = QStringLiteral("spelling");
            if (issue.length > 0)
                issues.push_back(issue);
        }
    }
    return issues;
}

QVector<GrammarIssue> GrammarClient::parseLanguageTool(const QByteArray &json)
{
    QVector<GrammarIssue> issues;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for (const QJsonValue &mv : root.value(QStringLiteral("matches")).toArray()) {
        const QJsonObject m = mv.toObject();
        GrammarIssue issue;
        const QJsonObject rule = m.value(QStringLiteral("rule")).toObject();
        const QString type = rule.value(QStringLiteral("issueType")).toString();
        issue.kind = type == QLatin1String("misspelling") ? GrammarIssue::Spelling
            : (type == QLatin1String("style") || type == QLatin1String("typographical")) ? GrammarIssue::Style
                                                                                         : GrammarIssue::Grammar;
        issue.start = m.value(QStringLiteral("offset")).toInt();
        issue.length = m.value(QStringLiteral("length")).toInt();
        issue.message = m.value(QStringLiteral("message")).toString();
        issue.ruleId = rule.value(QStringLiteral("id")).toString();
        for (const QJsonValue &r : m.value(QStringLiteral("replacements")).toArray()) {
            issue.suggestions << r.toObject().value(QStringLiteral("value")).toString();
            if (issue.suggestions.size() >= 6)
                break;
        }
        if (issue.length > 0)
            issues.push_back(issue);
    }
    return issues;
}
