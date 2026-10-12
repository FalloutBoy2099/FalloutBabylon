#pragma once

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

// Returns false when a token was lost or invented by the translation (the text is still restored as far as possible).
bool restore(const QString &translatedXml, const QStringList &tokens, QString *result);

}
