#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Legacy encodings used by Fallout localisations, converted to/from Unicode.
// Single byte code pages are table driven (identical on every platform); double byte ones
// (Big5, GBK, Shift-JIS, EUC-KR) rely on the platform converter.
namespace TextCodec {

struct Info {
    QString id;     // stable identifier stored in project files
    QString label;  // shown to the user
};

QList<Info> available();
bool isKnown(const QString &id);

// Sensible default for a game language directory name ("french" -> "cp1252", "russian" -> "cp1251"...).
QString defaultFor(const QString &language);

QString decode(const QByteArray &bytes, const QString &id);

// Characters the encoding cannot represent are replaced by '?' and collected in `unencodable` (if given).
QByteArray encode(const QString &text, const QString &id, QString *unencodable = nullptr);

}
