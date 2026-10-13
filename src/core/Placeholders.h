#pragma once

#include "Glossary.h"

#include <QString>
#include <QStringList>

// Game texts hold tokens that must reach the translation untouched (@Name@, %s, %d...).
// protect() turns them into <x>n</x> markers (and XML-escapes the rest) for a translation service used in
// XML mode with <x> ignored; restore() puts the original tokens back.
namespace Placeholders {

struct Protected {
    QString xml;
    QStringList tokens;
};

Protected protect(const QString &text);

// Same, and additionally protects the glossary terms found in `text` as if they were tokens whose "original text"
// is the project's translation: the translation service leaves them alone and restore() writes the
// translation of the project. Only safe matches are used (exact form, not a common word at the start of
// a sentence, a single translation). This stands in for a DeepL glossary when none can be used.
Protected protect(const QString &text, const Glossary *glossary);

// Returns false when a token was lost or invented by the translation (the text is still restored as far as possible).
bool restore(const QString &translatedXml, const QStringList &tokens, QString *result);

}
