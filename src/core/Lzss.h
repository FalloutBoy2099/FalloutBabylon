#pragma once

#include <QByteArray>

namespace Lzss {

// Fallout 1 DAT compression: a sequence of blocks, each prefixed by a big-endian int16.
// Negative size = that many raw bytes, positive size = LZSS block (4096 window, 3..18 matches,
// dictionary reset to spaces for every block), zero = end.
// Returns an empty array and fills `error` when the data is damaged.
QByteArray decompress(const QByteArray &source, int unpackedSize, QString *error = nullptr);

}
