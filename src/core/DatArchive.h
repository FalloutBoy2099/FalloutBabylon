#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>
#include <memory>


struct DatEntry {
    QString path;      // normalised: lower case, '/' separators
    QString rawPath;   // as stored in the archive ('\' separators, original case)
    bool compressed = false;
    quint32 unpackedSize = 0;
    quint32 packedSize = 0;
    quint32 offset = 0;
};

// Read-only access to a Fallout 1 (big endian, LZSS) or Fallout 2 (little endian, zlib) DAT archive.
// Immutable once opened; read()/readRaw() may be called from several threads (the archive file stays open and
// reads are serialised: reopening a 300 MB archive for every entry is slow, antivirus software scans each open).
class DatArchive
{
public:
    enum class Format { Fallout1, Fallout2 };

    static std::unique_ptr<DatArchive> open(const QString &filePath, QString *error = nullptr);

    Format format() const { return m_format; }
    const QString &filePath() const { return m_filePath; }
    const QVector<DatEntry> &entries() const { return m_entries; }
    int indexOf(const QString &normalizedPath) const { return m_index.value(normalizedPath, -1); }

    // Decompressed content of an entry.
    QByteArray read(int index, QString *error = nullptr) const;
    // Bytes as stored in the archive (used to copy entries without recompressing them).
    QByteArray readRaw(int index, QString *error = nullptr) const;

    static QString normalizePath(const QString &path);

private:
    DatArchive() = default;
    bool readFallout2(QFile &file, qint64 treeSize, qint64 archiveEnd, QString *error);
    bool readFallout1(QFile &file, QString *error);
    void add(const QString &rawPath, bool compressed, quint32 unpacked, quint32 packed, quint32 offset);

    QString m_filePath;
    mutable QFile m_file;
    mutable QMutex m_fileMutex;
    Format m_format = Format::Fallout2;
    QVector<DatEntry> m_entries;
    QHash<QString, int> m_index;
    qint64 m_dataLimit = 0;
};
