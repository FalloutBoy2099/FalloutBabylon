#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

// Holodisk texts of game/pipboy.msg.
//
// The engine shows one message entry per line of the Pip-Boy screen (no automatic wrapping): a holodisk body is a
// run of consecutive ids ending with an entry whose text is exactly "**END-DISK**"; "**END-PAR**" entries leave a
// blank line. The screen is 374 pixels wide and drawn with font1.aaf, so the translator's free text has to be cut
// into lines no wider than that. These functions do the conversion in both directions.
namespace PipboyText {

constexpr int kDefaultWidthPx = 374;      // width of the Pip-Boy text area (PIPBOY_WINDOW_CONTENT_VIEW_WIDTH)
constexpr int kDefaultFallbackChars = 53; // used when the font cannot be read: characters per line
constexpr int kQuestWidthPx = 350;        // the engine wraps a quest description (preceded by "N. ") at this width
constexpr int kEntriesPerPage = 35;      // the engine turns the page after 35 entries

const QString &endDisk();                 // "**END-DISK**"
const QString &endPar();                  // "**END-PAR**"

// Glyph widths of a Fallout interface font (.aaf).
class Font
{
public:
    static Font fromAaf(const QByteArray &data);
    bool isValid() const { return m_valid; }
    // Width in pixels of text already encoded in the font's character set.
    int width(const QByteArray &encoded) const;

private:
    bool m_valid = false;
    int m_letterSpacing = 0;
    int m_wordSpacing = 0;
    int m_glyphWidth[256] = {};
};

// How text is measured: with the real font when the encoding is single byte, otherwise by counting characters.
struct Measure {
    const Font *font = nullptr;
    QString encoding;                          // codec id of the target files (TextCodec)
    int maxWidth = kDefaultWidthPx;
    int fallbackChars = kDefaultFallbackChars;

    int width(const QString &text) const;
    bool exact() const;                        // true when the real font is used
};

// A holodisk body: consecutive ids from firstId to the END-DISK entry (included).
struct Block {
    int firstId = 0;
    int lastId = 0;                            // the END-DISK entry
    int entryCount() const { return lastId - firstId + 1; }
    bool contains(int id) const { return id >= firstId && id <= lastId; }
};

// Blocks found in the texts of a file (id -> text, any order). Runs of ids that do not end with END-DISK are labels.
QVector<Block> findHolodisks(const QMap<int, QString> &texts);

// Entries of a body (END-PAR/END-DISK included or not) -> free text: paragraphs separated by a blank line.
// Lines cut only because the next word did not fit are joined again; other line ends stay hard line breaks.
QString reflow(const QStringList &entries, const Measure &measure);

// Free text -> entries, ending with END-DISK, with END-PAR between paragraphs. Every line fits in measure.maxWidth.
QStringList layout(const QString &text, const Measure &measure);

// Lines wider than the screen in a list of entries (indexes).
QVector<int> tooWide(const QStringList &entries, const Measure &measure);

inline int pageCount(int entries) { return entries <= 0 ? 0 : (entries - 1) / kEntriesPerPage + 1; }

}
