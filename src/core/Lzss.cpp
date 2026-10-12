#include "Lzss.h"

#include <QString>
#include <algorithm>
#include <cstdint>

namespace Lzss {

namespace {
constexpr int kDictionary = 4096;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 18;
}

QByteArray decompress(const QByteArray &source, int unpackedSize, QString *error)
{
    auto fail = [&](const char *msg) {
        if (error)
            *error = QString::fromLatin1(msg);
        return QByteArray();
    };

    // A match of 18 bytes costs 2 bytes plus a flag bit: data cannot expand more than ~8.5 times.
    if (unpackedSize < 0 || qint64(unpackedSize) > qint64(source.size()) * 10 + 64)
        return fail("The announced size of the file is impossible.");

    const auto *src = reinterpret_cast<const uint8_t *>(source.constData());
    const int srcSize = int(source.size());

    QByteArray out(unpackedSize, Qt::Uninitialized);
    auto *dst = reinterpret_cast<uint8_t *>(out.data());
    uint8_t dict[kDictionary];

    int outPos = 0;
    int pos = 0;
    while (pos + 2 <= srcSize && outPos < unpackedSize) {
        const int blockSize = int(int16_t((src[pos] << 8) | src[pos + 1]));
        pos += 2;
        if (blockSize == 0)
            break;

        if (blockSize < 0) {
            const int length = std::min({-blockSize, srcSize - pos, unpackedSize - outPos});
            std::copy(src + pos, src + pos + length, dst + outPos);
            outPos += length;
            pos += -blockSize;
            continue;
        }

        const int blockEnd = std::min(pos + blockSize, srcSize);
        std::fill(dict, dict + kDictionary, uint8_t(0x20));
        int dictPos = kDictionary - kMaxMatch;

        while (pos < blockEnd && outPos < unpackedSize) {
            int flags = src[pos++];
            for (int bit = 0; bit < 8 && pos < blockEnd && outPos < unpackedSize; ++bit, flags >>= 1) {
                if (flags & 1) {
                    const uint8_t value = src[pos++];
                    dst[outPos++] = value;
                    dict[dictPos++ % kDictionary] = value;
                } else {
                    if (pos + 1 >= blockEnd) {
                        pos = blockEnd;
                        break;
                    }
                    const int low = src[pos++];
                    const int high = src[pos++];
                    const int offset = low | ((high & 0xF0) << 4);
                    const int length = (high & 0x0F) + kMinMatch;
                    for (int i = 0; i < length && outPos < unpackedSize; ++i) {
                        const uint8_t value = dict[(offset + i) % kDictionary];
                        dst[outPos++] = value;
                        dict[dictPos++ % kDictionary] = value;
                    }
                }
            }
        }
        pos = blockEnd;
    }

    if (outPos != unpackedSize)
        return fail("The compressed data of the file is truncated.");
    return out;
}

}
