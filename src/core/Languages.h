#pragma once

#include <QString>

// Maps the language folder names of the game (text/<language>/) to the codes of the web services.
namespace Languages {

// DeepL language codes ("EN", "FR"...). Empty when DeepL does not handle the language.
QString deeplSource(const QString &gameLanguage);
QString deeplTarget(const QString &gameLanguage);   // regional variants where DeepL requires them (EN-US, PT-PT)
QString deeplGlossary(const QString &gameLanguage);  // lower case base code used by glossaries ("en", "fr")

// LanguageTool language code ("fr", "en-US", "de-DE"...). Empty when unsupported.
QString languageTool(const QString &gameLanguage);

bool isFrench(const QString &gameLanguage);

}
