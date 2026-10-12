#include "DatWriter.h"

#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QtEndian>

namespace {

struct Prepared {
    QByteArray bytes;
    quint32 unpacked = 0;
    quint32 packed = 0;
    bool compressed = false;
};

struct Placed {
    const DatWriteItem *item = nullptr;
    QString name;
    Prepared prepared;
    quint32 offset = 0;
};

void put32le(QByteArray &out, quint32 v)
{
    uchar b[4];
    qToLittleEndian<quint32>(v, b);
    out.append(reinterpret_cast<const char *>(b), 4);
}

void put32be(QByteArray &out, quint32 v)
{
    uchar b[4];
    qToBigEndian<quint32>(v, b);
    out.append(reinterpret_cast<const char *>(b), 4);
}

QString upperAscii(QString s)
{
    for (QChar &c : s)
        if (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
            c = QChar(c.unicode() - 32);
    return s;
}

bool prepare(const DatWriteItem &item, DatArchive::Format format, Prepared &out, QString *error)
{
    const DatArchive *src = item.source;
    if (src && item.sourceIndex >= 0) {
        const DatEntry &entry = src->entries().at(item.sourceIndex);
        if (src->format() == format) {
            out.bytes = src->readRaw(item.sourceIndex, error);
            if (out.bytes.isEmpty() && (entry.compressed ? entry.packedSize : entry.unpackedSize) != 0)
                return false;
            out.unpacked = entry.unpackedSize;
            out.packed = entry.packedSize;
            out.compressed = entry.compressed;
            return true;
        }
        QByteArray data = src->read(item.sourceIndex, error);
        if (data.isEmpty() && entry.unpackedSize != 0)
            return false;
        DatWriteItem inMemory;
        inMemory.path = item.path;
        inMemory.data = data;
        return prepare(inMemory, format, out, error);
    }

    out.unpacked = quint32(item.data.size());
    if (format == DatArchive::Format::Fallout2 && !item.data.isEmpty()) {
        QByteArray z = qCompress(item.data, 9);
        z.remove(0, 4); // drop Qt's size prefix: what remains is a plain zlib stream
        if (quint32(z.size()) < out.unpacked) {
            out.bytes = z;
            out.packed = quint32(z.size());
            out.compressed = true;
            return true;
        }
    }
    out.bytes = item.data;
    out.packed = format == DatArchive::Format::Fallout1 ? 0 : out.unpacked;
    out.compressed = false;
    return true;
}

}

bool DatWriter::write(const QString &outPath, DatArchive::Format format, const QVector<DatWriteItem> &items,
                      QString *error)
{
    auto fail = [&](const QString &text) {
        if (error)
            *error = text;
        return false;
    };

    QSaveFile file(outPath);
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());

    if (format == DatArchive::Format::Fallout2) {
        QByteArray tree;
        put32le(tree, quint32(items.size()));
        for (const DatWriteItem &item : items) {
            Prepared p;
            if (!prepare(item, format, p, error))
                return false;
            const quint64 offset = quint64(file.pos());
            if (offset > 0xFFFFFFFFull)
                return fail(QStringLiteral("The archive would exceed 4 GB."));
            if (file.write(p.bytes) != p.bytes.size())
                return fail(file.errorString());
            QString raw = item.path;
            raw.replace(QLatin1Char('/'), QLatin1Char('\\'));
            const QByteArray name = raw.toLatin1();
            put32le(tree, quint32(name.size()));
            tree.append(name);
            tree.append(char(p.compressed ? 1 : 0));
            put32le(tree, p.unpacked);
            put32le(tree, p.packed);
            put32le(tree, quint32(offset));
        }
        const quint64 total = quint64(file.pos()) + quint64(tree.size()) + 8;
        if (total > 0xFFFFFFFFull)
            return fail(QStringLiteral("The archive would exceed 4 GB."));
        QByteArray footer;
        put32le(footer, quint32(tree.size()));
        put32le(footer, quint32(total));
        if (file.write(tree) != tree.size() || file.write(footer) != footer.size())
            return fail(file.errorString());
        return file.commit() ? true : fail(file.errorString());
    }

    // Fallout 1: [header][directory names][directory contents][data]; the index goes last because it holds offsets.
    QMap<QString, QVector<Placed>> directories;
    for (const DatWriteItem &item : items) {
        QString raw = item.path;
        raw.replace(QLatin1Char('/'), QLatin1Char('\\'));
        const int slash = raw.lastIndexOf(QLatin1Char('\\'));
        QString dir = upperAscii(slash < 0 ? QString() : raw.left(slash));
        if (dir.isEmpty())
            dir = QStringLiteral(".");
        Placed placed;
        placed.item = &item;
        placed.name = upperAscii(raw.mid(slash + 1));
        if (placed.name.size() > 255 || dir.size() > 255)
            return fail(QStringLiteral("Name too long for a Fallout 1 archive: %1").arg(raw));
        directories[dir].push_back(placed);
    }

    qint64 indexSize = 16;
    for (auto it = directories.cbegin(); it != directories.cend(); ++it) {
        indexSize += 1 + it.key().size() + 16;
        for (const Placed &p : it.value())
            indexSize += 1 + p.name.size() + 16;
    }
    if (file.write(QByteArray(int(indexSize), 0)) != indexSize)
        return fail(file.errorString());

    for (auto it = directories.begin(); it != directories.end(); ++it) {
        for (Placed &placed : it.value()) {
            if (!prepare(*placed.item, format, placed.prepared, error))
                return false;
            // Fallout 1 compression is not produced: raw data, no packed size.
            if (!placed.prepared.compressed)
                placed.prepared.packed = 0;
            const quint64 offset = quint64(file.pos());
            if (offset > 0xFFFFFFFFull)
                return fail(QStringLiteral("The archive would exceed 4 GB."));
            placed.offset = quint32(offset);
            if (file.write(placed.prepared.bytes) != placed.prepared.bytes.size())
                return fail(file.errorString());
        }
    }

    QByteArray index;
    put32be(index, quint32(directories.size()));
    put32be(index, quint32(directories.size()));
    put32be(index, 0);
    put32be(index, 0);
    for (auto it = directories.cbegin(); it != directories.cend(); ++it) {
        index.append(char(it.key().size()));
        index.append(it.key().toLatin1());
    }
    for (auto it = directories.cbegin(); it != directories.cend(); ++it) {
        put32be(index, quint32(it.value().size()));
        put32be(index, quint32(it.value().size()));
        put32be(index, 16);
        put32be(index, 0);
        for (const Placed &p : it.value()) {
            index.append(char(p.name.size()));
            index.append(p.name.toLatin1());
            put32be(index, p.prepared.compressed ? 0x40 : 0x20);
            put32be(index, p.offset);
            put32be(index, p.prepared.unpacked);
            put32be(index, p.prepared.packed);
        }
    }
    if (index.size() != indexSize)
        return fail(QStringLiteral("Internal error while building the index."));
    if (!file.seek(0) || file.write(index) != index.size())
        return fail(file.errorString());
    return file.commit() ? true : fail(file.errorString());
}
