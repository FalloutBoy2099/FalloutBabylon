#include "DatArchive.h"

#include "Lzss.h"

#include <QFile>
#include <QtEndian>

namespace {

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

bool readExact(QFile &file, char *buffer, qint64 size)
{
    return file.read(buffer, size) == size;
}

bool readBE32(QFile &file, quint32 &value)
{
    uchar b[4];
    if (!readExact(file, reinterpret_cast<char *>(b), 4))
        return false;
    value = qFromBigEndian<quint32>(b);
    return true;
}

bool readName(QFile &file, QString &name)
{
    uchar length;
    if (!readExact(file, reinterpret_cast<char *>(&length), 1))
        return false;
    QByteArray bytes = file.read(length);
    if (bytes.size() != length)
        return false;
    name = QString::fromLatin1(bytes);
    return true;
}

}

QString DatArchive::normalizePath(const QString &path)
{
    QString result = path;
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (result.startsWith(QLatin1Char('/')))
        result.remove(0, 1);
    return result.toLower();
}

void DatArchive::add(const QString &rawPath, bool compressed, quint32 unpacked, quint32 packed, quint32 offset)
{
    DatEntry entry;
    entry.rawPath = rawPath;
    entry.path = normalizePath(rawPath);
    entry.compressed = compressed;
    entry.unpackedSize = unpacked;
    entry.packedSize = packed;
    entry.offset = offset;
    m_index.insert(entry.path, m_entries.size());
    m_entries.push_back(entry);
}

std::unique_ptr<DatArchive> DatArchive::open(const QString &filePath, QString *error)
{
    std::unique_ptr<DatArchive> archive(new DatArchive);
    archive->m_filePath = filePath;
    QFile &file = archive->m_file;
    file.setFileName(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return nullptr;
    }
    const qint64 length = file.size();
    if (length < 16) {
        setError(error, QStringLiteral("The file is too small to be a DAT archive."));
        return nullptr;
    }

    // Fallout 2: the last 8 bytes are [tree size][archive size], and the archive size is the file size.
    uchar footer[8];
    file.seek(length - 8);
    if (readExact(file, reinterpret_cast<char *>(footer), 8)
        && qFromLittleEndian<quint32>(footer + 4) == quint64(length)) {
        archive->m_format = Format::Fallout2;
        const qint64 treeSize = qFromLittleEndian<quint32>(footer);
        if (!archive->readFallout2(file, treeSize, length, error))
            return nullptr;
        return archive;
    }

    // Fallout 1: big endian header starting with the directory count.
    archive->m_format = Format::Fallout1;
    if (!archive->readFallout1(file, error))
        return nullptr;
    return archive;
}

bool DatArchive::readFallout2(QFile &file, qint64 treeSize, qint64 archiveEnd, QString *error)
{
    if (treeSize < 4 || treeSize + 8 > archiveEnd) {
        setError(error, QStringLiteral("The directory tree of the DAT file is invalid."));
        return false;
    }
    m_dataLimit = archiveEnd - treeSize - 8;
    file.seek(m_dataLimit);
    const QByteArray tree = file.read(treeSize);
    if (tree.size() != treeSize) {
        setError(error, QStringLiteral("The DAT file is truncated."));
        return false;
    }
    const auto *t = reinterpret_cast<const uchar *>(tree.constData());
    const qint32 fileCount = qFromLittleEndian<qint32>(t);
    qint64 pos = 4;
    for (qint32 i = 0; i < fileCount; ++i) {
        if (pos + 4 > treeSize)
            break;
        const qint32 nameLength = qFromLittleEndian<qint32>(t + pos);
        if (nameLength <= 0 || pos + 4 + nameLength + 13 > treeSize)
            break;
        pos += 4;
        const QString name = QString::fromLatin1(tree.constData() + pos, nameLength);
        pos += nameLength;
        bool compressed = t[pos++] == 1;
        const quint32 unpacked = qFromLittleEndian<quint32>(t + pos);
        const quint32 packed = qFromLittleEndian<quint32>(t + pos + 4);
        const quint32 offset = qFromLittleEndian<quint32>(t + pos + 8);
        pos += 12;
        if (!compressed && unpacked != packed)
            compressed = true; // some tools forget the flag
        add(name, compressed, unpacked, packed, offset);
    }
    if (m_entries.size() != fileCount) {
        setError(error, QStringLiteral("The directory tree of the DAT file is damaged."));
        return false;
    }
    return true;
}

bool DatArchive::readFallout1(QFile &file, QString *error)
{
    m_dataLimit = file.size();
    file.seek(0);
    quint32 directoryCount = 0;
    if (!readBE32(file, directoryCount) || directoryCount == 0 || directoryCount > 0xFFFFF) {
        setError(error, QStringLiteral("The file is not a valid DAT archive."));
        return false;
    }
    file.seek(16);

    QStringList directories;
    for (quint32 i = 0; i < directoryCount; ++i) {
        QString name;
        if (!readName(file, name)) {
            setError(error, QStringLiteral("The DAT file is truncated."));
            return false;
        }
        while (name.startsWith(QLatin1Char('\\')))
            name.remove(0, 1);
        while (name.endsWith(QLatin1Char('\\')))
            name.chop(1);
        directories << (name == QLatin1String(".") || name.isEmpty() ? QString() : name + QLatin1Char('\\'));
    }

    for (const QString &directory : std::as_const(directories)) {
        quint32 fileCount = 0, skip = 0;
        if (!readBE32(file, fileCount) || !readBE32(file, skip) || !readBE32(file, skip) || !readBE32(file, skip)
            || fileCount > 0x7FFFFFFF) {
            setError(error, QStringLiteral("The directory contents of the DAT file are corrupted."));
            return false;
        }
        for (quint32 i = 0; i < fileCount; ++i) {
            QString name;
            quint32 attributes, offset, unpacked, packed;
            if (!readName(file, name) || !readBE32(file, attributes) || !readBE32(file, offset)
                || !readBE32(file, unpacked) || !readBE32(file, packed)) {
                setError(error, QStringLiteral("The DAT file is truncated."));
                return false;
            }
            add(directory + name, attributes == 0x40, unpacked, packed, offset);
        }
    }
    return true;
}

QByteArray DatArchive::readRaw(int index, QString *error) const
{
    if (index < 0 || index >= m_entries.size()) {
        setError(error, QStringLiteral("Invalid entry."));
        return {};
    }
    const DatEntry &e = m_entries.at(index);
    const quint32 size = e.compressed ? e.packedSize : e.unpackedSize;
    if (qint64(e.offset) + size > m_dataLimit) {
        setError(error, QStringLiteral("The data of '%1' is outside the archive.").arg(e.rawPath));
        return {};
    }
    QByteArray data;
    {
        QMutexLocker locker(&m_fileMutex);
        if (!m_file.seek(e.offset)) {
            setError(error, m_file.errorString());
            return {};
        }
        data = m_file.read(size);
    }
    if (quint32(data.size()) != size) {
        setError(error, QStringLiteral("The DAT file is truncated."));
        return {};
    }
    return data;
}

QByteArray DatArchive::read(int index, QString *error) const
{
    const QByteArray raw = readRaw(index, error);
    if (index < 0 || index >= m_entries.size())
        return {};
    const DatEntry &e = m_entries.at(index);
    if (!e.compressed)
        return raw;
    if (raw.isEmpty() && e.packedSize != 0)
        return {};

    if (m_format == Format::Fallout1)
        return Lzss::decompress(raw, int(e.unpackedSize), error);

    // zlib stream; qUncompress wants a big-endian size prefix.
    QByteArray prefixed;
    prefixed.reserve(raw.size() + 4);
    uchar size[4];
    qToBigEndian<quint32>(e.unpackedSize, size);
    prefixed.append(reinterpret_cast<const char *>(size), 4);
    prefixed.append(raw);
    QByteArray out = qUncompress(prefixed);
    if (quint32(out.size()) != e.unpackedSize) {
        setError(error, QStringLiteral("Cannot decompress '%1'.").arg(e.rawPath));
        return {};
    }
    return out;
}
