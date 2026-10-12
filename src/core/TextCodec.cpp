#include "TextCodec.h"

#include <QHash>
#include <QStringConverter>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace TextCodec {

namespace {

// Upper half (0x80..0xFF) of the supported single byte code pages. 0 = same code point as the byte.
const ushort kCp1252[128] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

const ushort kCp1250[128] = {
    0x20AC, 0, 0x201A, 0, 0x201E, 0x2026, 0x2020, 0x2021, 0, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

const ushort kCp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

struct SingleByte {
    ushort toUnicode[256];
    QHash<ushort, uchar> fromUnicode;

    SingleByte(const ushort *upper)
    {
        for (int i = 0; i < 128; ++i)
            toUnicode[i] = ushort(i);
        for (int i = 0; i < 128; ++i) {
            // 0xA0..0xFF of Windows-1252 is Latin-1 (table entries left at 0 map to themselves)
            toUnicode[128 + i] = upper[i] ? upper[i] : ushort(128 + i);
        }
        for (int i = 0; i < 256; ++i)
            fromUnicode.insert(toUnicode[i], uchar(i));
    }
};

const SingleByte *singleByte(const QString &id)
{
    static const SingleByte cp1252(kCp1252), cp1250(kCp1250), cp1251(kCp1251);
    if (id == QLatin1String("cp1252"))
        return &cp1252;
    if (id == QLatin1String("cp1250"))
        return &cp1250;
    if (id == QLatin1String("cp1251"))
        return &cp1251;
    return nullptr;
}

struct MultiByte {
    const char *id;
    const char *label;
    unsigned windowsCodePage;
    const char *iconvName;
};

const MultiByte kMultiByte[] = {
    {"big5", "Big5 (Traditional Chinese)", 950, "Big5"},
    {"gbk", "GBK (Simplified Chinese)", 936, "GBK"},
    {"shift_jis", "Shift-JIS (Japanese)", 932, "Shift_JIS"},
    {"euc-kr", "EUC-KR / CP949 (Korean)", 949, "EUC-KR"},
};

const MultiByte *multiByte(const QString &id)
{
    for (const MultiByte &m : kMultiByte)
        if (id == QLatin1String(m.id))
            return &m;
    return nullptr;
}

}

QList<Info> available()
{
    QList<Info> list = {
        {QStringLiteral("cp1252"), QStringLiteral("CP1252 (Western European)")},
        {QStringLiteral("cp1250"), QStringLiteral("CP1250 (Central European)")},
        {QStringLiteral("cp1251"), QStringLiteral("CP1251 (Cyrillic)")},
    };
    for (const MultiByte &m : kMultiByte)
        list.push_back({QString::fromLatin1(m.id), QString::fromLatin1(m.label)});
    list.push_back({QStringLiteral("utf-8"), QStringLiteral("UTF-8")});
    return list;
}

bool isKnown(const QString &id)
{
    for (const Info &info : available())
        if (info.id == id)
            return true;
    return false;
}

QString defaultFor(const QString &language)
{
    const QString l = language.toLower();
    if (l == QLatin1String("russian") || l == QLatin1String("ukrainian") || l == QLatin1String("bulgarian"))
        return QStringLiteral("cp1251");
    if (l == QLatin1String("polish") || l == QLatin1String("czech") || l == QLatin1String("hungarian")
        || l == QLatin1String("slovak") || l == QLatin1String("romanian"))
        return QStringLiteral("cp1250");
    if (l == QLatin1String("chinese") || l == QLatin1String("tchinese") || l == QLatin1String("cht")
        || l == QLatin1String("traditional_chinese"))
        return QStringLiteral("big5");
    if (l == QLatin1String("schinese") || l == QLatin1String("chs") || l == QLatin1String("simplified_chinese"))
        return QStringLiteral("gbk");
    if (l == QLatin1String("japanese"))
        return QStringLiteral("shift_jis");
    if (l == QLatin1String("korean"))
        return QStringLiteral("euc-kr");
    return QStringLiteral("cp1252");
}

QString decode(const QByteArray &bytes, const QString &id)
{
    if (const SingleByte *sb = singleByte(id)) {
        QString out(bytes.size(), Qt::Uninitialized);
        QChar *dst = out.data();
        const auto *src = reinterpret_cast<const uchar *>(bytes.constData());
        for (qsizetype i = 0; i < bytes.size(); ++i)
            dst[i] = QChar(sb->toUnicode[src[i]]);
        return out;
    }
    if (id == QLatin1String("utf-8")) {
        QByteArray b = bytes;
        if (b.startsWith("\xEF\xBB\xBF"))
            b.remove(0, 3);
        return QString::fromUtf8(b);
    }
    if (const MultiByte *m = multiByte(id)) {
#ifdef Q_OS_WIN
        if (bytes.isEmpty())
            return {};
        const int n = MultiByteToWideChar(m->windowsCodePage, 0, bytes.constData(), int(bytes.size()), nullptr, 0);
        QString out(n, Qt::Uninitialized);
        MultiByteToWideChar(m->windowsCodePage, 0, bytes.constData(), int(bytes.size()),
                            reinterpret_cast<wchar_t *>(out.data()), n);
        return out;
#else
        QStringDecoder decoder(m->iconvName);
        if (decoder.isValid())
            return decoder.decode(bytes);
#endif
    }
    return QString::fromLatin1(bytes);
}

QByteArray encode(const QString &text, const QString &id, QString *unencodable)
{
    auto note = [&](QChar c) {
        if (unencodable && !unencodable->contains(c))
            unencodable->append(c);
    };

    if (const SingleByte *sb = singleByte(id)) {
        QByteArray out;
        out.reserve(text.size());
        for (QChar c : text) {
            const auto it = sb->fromUnicode.constFind(c.unicode());
            if (it != sb->fromUnicode.constEnd()) {
                out.append(char(it.value()));
            } else {
                out.append('?');
                note(c);
            }
        }
        return out;
    }
    if (id == QLatin1String("utf-8"))
        return text.toUtf8();
    if (const MultiByte *m = multiByte(id)) {
#ifdef Q_OS_WIN
        if (text.isEmpty())
            return {};
        BOOL usedDefault = FALSE;
        const wchar_t *wide = reinterpret_cast<const wchar_t *>(text.utf16());
        const int n = WideCharToMultiByte(m->windowsCodePage, 0, wide, int(text.size()), nullptr, 0, "?", &usedDefault);
        QByteArray out(n, Qt::Uninitialized);
        WideCharToMultiByte(m->windowsCodePage, 0, wide, int(text.size()), out.data(), n, "?", &usedDefault);
        if (usedDefault && unencodable) {
            // Report characters that do not survive a round trip.
            for (QChar c : text)
                if (c.unicode() > 127 && decode(encode(QString(c), id), id) != QString(c))
                    note(c);
        }
        return out;
#else
        QStringEncoder encoder(m->iconvName);
        if (encoder.isValid()) {
            QByteArray out = encoder.encode(text);
            if (encoder.hasError() && unencodable)
                *unencodable += QLatin1String("?");
            return out;
        }
#endif
    }
    return text.toLatin1();
}

}
