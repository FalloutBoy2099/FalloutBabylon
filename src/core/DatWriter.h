#pragma once

#include "DatArchive.h"

// One file to put in a new archive: either in-memory data, or an entry copied from an existing archive
// (copied as-is, without recompression, when both archives use the same format).
struct DatWriteItem {
    QString path;             // '\' or '/' separated, case kept
    QByteArray data;
    const DatArchive *source = nullptr;
    int sourceIndex = -1;
};

namespace DatWriter {

// Writes a Fallout 1 (files stored uncompressed) or Fallout 2 (zlib) archive.
// The archive is written to a temporary file and moved over `outPath` on success.
bool write(const QString &outPath, DatArchive::Format format, const QVector<DatWriteItem> &items,
           QString *error = nullptr);

}
