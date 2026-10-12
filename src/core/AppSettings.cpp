#include "AppSettings.h"

#include <QSettings>

namespace AppSettings {

namespace {

QString str(const char *key, const QString &fallback = {})
{
    return QSettings().value(QString::fromLatin1(key), fallback).toString();
}

bool flag(const char *key, bool fallback)
{
    return QSettings().value(QString::fromLatin1(key), fallback).toBool();
}

void put(const char *key, const QVariant &value)
{
    QSettings().setValue(QString::fromLatin1(key), value);
}

}

QString deeplKey() { return str("deepl/key"); }
void setDeeplKey(const QString &key) { put("deepl/key", key.trimmed()); }

bool deeplUseGlossary() { return flag("deepl/useGlossary", true); }
void setDeeplUseGlossary(bool on) { put("deepl/useGlossary", on); }
bool deeplUseContext() { return flag("deepl/useContext", true); }
void setDeeplUseContext(bool on) { put("deepl/useContext", on); }
bool deeplMarkReview() { return flag("deepl/markReview", true); }
void setDeeplMarkReview(bool on) { put("deepl/markReview", on); }
QString deeplFormality() { return str("deepl/formality"); }
void setDeeplFormality(const QString &value) { put("deepl/formality", value); }

QString grammalecteUrl() { return str("check/grammalecteUrl"); }
void setGrammalecteUrl(const QString &url) { put("check/grammalecteUrl", url.trimmed()); }
QString languageToolUrl() { return str("check/languageToolUrl"); }
void setLanguageToolUrl(const QString &url) { put("check/languageToolUrl", url.trimmed()); }
bool checkEnabled() { return flag("check/enabled", true); }
void setCheckEnabled(bool on) { put("check/enabled", on); }

int pipboyMaxWidth() { return QSettings().value(QStringLiteral("pipboy/maxWidth"), 374).toInt(); }
void setPipboyMaxWidth(int px) { put("pipboy/maxWidth", px); }
int pipboyFallbackChars() { return QSettings().value(QStringLiteral("pipboy/fallbackChars"), 53).toInt(); }
void setPipboyFallbackChars(int n) { put("pipboy/fallbackChars", n); }

}
