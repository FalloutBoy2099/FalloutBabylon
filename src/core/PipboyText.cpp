#include "PipboyText.h"

#include "TextCodec.h"

#include <QtEndian>

namespace PipboyText {

namespace {

bool isSingleByte(const QString &encoding)
{
    return encoding == QLatin1String("cp1252") || encoding == QLatin1String("cp1250") || encoding == QLatin1String("cp1251");
}

bool isBlank(const QString &s)
{
    return s.trimmed().isEmpty();
}

QString firstWord(const QString &line)
{
    const QString t = line.trimmed();
    const int space = t.indexOf(QLatin1Char(' '));
    return space < 0 ? t : t.left(space);
}

}

const QString &endDisk()
{
    static const QString s = QStringLiteral("**END-DISK**");
    return s;
}

const QString &endPar()
{
    static const QString s = QStringLiteral("**END-PAR**");
    return s;
}

// ----------------------------------------------------------------------------- font

Font Font::fromAaf(const QByteArray &data)
{
    Font font;
    // "AAFF", then big endian: max height, letter spacing, word spacing, line spacing, 256 x (width, height, offset).
    if (data.size() < 2060 || data.left(4) != "AAFF")
        return font;
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    font.m_letterSpacing = qFromBigEndian<qint16>(p + 6);
    font.m_wordSpacing = qFromBigEndian<qint16>(p + 8);
    for (int i = 0; i < 256; ++i)
        font.m_glyphWidth[i] = qFromBigEndian<qint16>(p + 12 + i * 8);
    font.m_valid = true;
    return font;
}

int Font::width(const QByteArray &encoded) const
{
    int total = 0;
    for (uchar ch : encoded)
        total += (ch == ' ' ? m_wordSpacing : m_glyphWidth[ch]) + m_letterSpacing;
    return total;
}

// ----------------------------------------------------------------------------- measure

bool Measure::exact() const
{
    return font && font->isValid() && isSingleByte(encoding);
}

int Measure::width(const QString &text) const
{
    if (exact())
        return font->width(TextCodec::encode(text, encoding));
    // Without the font: the configured number of characters fills the line.
    return int(qint64(text.size()) * maxWidth / qMax(1, fallbackChars));
}

// ----------------------------------------------------------------------------- blocks

QVector<Block> findHolodisks(const QMap<int, QString> &texts)
{
    QVector<Block> blocks;
    int runStart = 0;
    int previous = 0;
    bool inRun = false;
    for (auto it = texts.cbegin(); it != texts.cend(); ++it) {
        const int id = it.key();
        if (!inRun || id != previous + 1) {
            runStart = id;
            inRun = true;
        }
        previous = id;
        if (it.value() == endDisk()) {
            blocks.push_back({runStart, id});
            runStart = id + 1;   // a following disk may start right after
        }
    }
    return blocks;
}

// ----------------------------------------------------------------------------- reflow

QString reflow(const QStringList &entries, const Measure &measure)
{
    // Split into paragraphs (END-PAR or blank entries), then rebuild each one.
    QVector<QStringList> paragraphs(1);
    for (const QString &entry : entries) {
        if (entry == endDisk())
            break;
        if (entry == endPar() || isBlank(entry)) {
            if (!paragraphs.last().isEmpty())
                paragraphs.push_back({});
            continue;
        }
        paragraphs.last() << entry.trimmed();
    }
    if (paragraphs.last().isEmpty())
        paragraphs.removeLast();

    QStringList out;
    for (const QStringList &lines : std::as_const(paragraphs)) {
        QString text;
        for (int i = 0; i < lines.size(); ++i) {
            text += lines.at(i);
            if (i + 1 == lines.size())
                break;
            // The line was cut by the layout when the next word would not have fitted: join them again.
            const bool wrapped = measure.width(lines.at(i) + QLatin1Char(' ') + firstWord(lines.at(i + 1))) > measure.maxWidth;
            text += wrapped ? QStringLiteral(" ") : QStringLiteral("\n");
        }
        out << text;
    }
    return out.join(QStringLiteral("\n\n"));
}

// ----------------------------------------------------------------------------- layout

namespace {

// Greedy wrap of one hard line.
QStringList wrapLine(const QString &line, const Measure &m)
{
    QStringList lines;
    QString current;
    const QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString word : words) {
        // A word wider than the screen is cut character by character.
        while (m.width(word) > m.maxWidth && word.size() > 1) {
            int cut = word.size() - 1;
            while (cut > 1 && m.width(word.left(cut)) > m.maxWidth)
                --cut;
            if (!current.isEmpty()) {
                lines << current;
                current.clear();
            }
            lines << word.left(cut);
            word = word.mid(cut);
        }
        if (current.isEmpty()) {
            current = word;
        } else if (m.width(current + QLatin1Char(' ') + word) <= m.maxWidth) {
            current += QLatin1Char(' ') + word;
        } else {
            lines << current;
            current = word;
        }
    }
    if (!current.isEmpty())
        lines << current;
    return lines;
}

}

QStringList layout(const QString &rawText, const Measure &measure)
{
    QString text = rawText;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    // Paragraphs are separated by blank lines.
    QVector<QStringList> paragraphs(1);
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (isBlank(line)) {
            if (!paragraphs.last().isEmpty())
                paragraphs.push_back({});
        } else {
            paragraphs.last() << line;
        }
    }
    if (paragraphs.last().isEmpty())
        paragraphs.removeLast();

    QStringList entries;
    for (int p = 0; p < paragraphs.size(); ++p) {
        if (p > 0)
            entries << endPar();
        for (const QString &hardLine : paragraphs.at(p))
            entries += wrapLine(hardLine, measure);
    }
    entries << endDisk();
    return entries;
}

QVector<int> tooWide(const QStringList &entries, const Measure &measure)
{
    QVector<int> bad;
    for (int i = 0; i < entries.size(); ++i)
        if (entries.at(i) != endPar() && entries.at(i) != endDisk() && measure.width(entries.at(i)) > measure.maxWidth)
            bad << i;
    return bad;
}

}
