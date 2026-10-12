#include "Languages.h"

#include <QHash>

namespace Languages {

namespace {

struct Entry {
    const char *deeplSource;
    const char *deeplTarget;
    const char *languageTool;
};

const QHash<QString, Entry> &table()
{
    static const QHash<QString, Entry> t = {
        {QStringLiteral("english"), {"EN", "EN-US", "en-US"}},
        {QStringLiteral("french"), {"FR", "FR", "fr"}},
        {QStringLiteral("german"), {"DE", "DE", "de-DE"}},
        {QStringLiteral("spanish"), {"ES", "ES", "es"}},
        {QStringLiteral("italian"), {"IT", "IT", "it"}},
        {QStringLiteral("portuguese"), {"PT", "PT-PT", "pt-PT"}},
        {QStringLiteral("brazilian"), {"PT", "PT-BR", "pt-BR"}},
        {QStringLiteral("polish"), {"PL", "PL", "pl-PL"}},
        {QStringLiteral("czech"), {"CS", "CS", ""}},
        {QStringLiteral("russian"), {"RU", "RU", "ru-RU"}},
        {QStringLiteral("hungarian"), {"HU", "HU", ""}},
        {QStringLiteral("swedish"), {"SV", "SV", "sv"}},
        {QStringLiteral("japanese"), {"JA", "JA", "ja-JP"}},
        {QStringLiteral("korean"), {"KO", "KO", ""}},
        {QStringLiteral("chinese"), {"ZH", "ZH-HANS", "zh-CN"}},
        {QStringLiteral("chs"), {"ZH", "ZH-HANS", "zh-CN"}},
        {QStringLiteral("cht"), {"ZH", "ZH-HANT", ""}},
    };
    return t;
}

QString lookup(const QString &language, const char *Entry::*member)
{
    const auto it = table().constFind(language.toLower());
    return it == table().constEnd() ? QString() : QString::fromLatin1(it.value().*member);
}

}

QString deeplSource(const QString &g)
{
    return lookup(g, &Entry::deeplSource);
}

QString deeplTarget(const QString &g)
{
    return lookup(g, &Entry::deeplTarget);
}

QString deeplGlossary(const QString &g)
{
    return deeplSource(g).toLower();
}

QString languageTool(const QString &g)
{
    return lookup(g, &Entry::languageTool);
}

bool isFrench(const QString &g)
{
    return g.compare(QLatin1String("french"), Qt::CaseInsensitive) == 0;
}

}
