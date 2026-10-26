#include "texture_decoder.h"
#include <QtEndian>
#include <limits>
#include <stdexcept>

namespace nature {
namespace {
[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }
void requireRange(const QByteArray &bytes, qsizetype offset, qsizetype size) {
    if (offset < 0 || size < 0 || offset > bytes.size() || size > bytes.size() - offset)
        fail("Truncated texture container.");
}
quint16 read16(const QByteArray &bytes, qsizetype offset) {
    requireRange(bytes, offset, 2);
    return qFromLittleEndian<quint16>(bytes.constData() + offset);
}
quint32 read32(const QByteArray &bytes, qsizetype offset) {
    requireRange(bytes, offset, 4);
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
qsizetype pixelSize(const TextureBlock &block) {
    if (!block.widthWords || !block.height || block.widthWords > 1024 || block.height > 512)
        fail("Texture dimensions must fit the PS1 VRAM extent (1024 words by 512 rows).");
    return qsizetype(block.widthWords) * 2 * block.height;
}
}

TextureContainer decodeContainer0D(const QByteArray &bytes) {
    requireRange(bytes, 0, 4);
    if (quint8(bytes[0]) != 0x0d) fail("Expected texture container type 0x0D.");
    const quint16 count = read16(bytes, 2);
    if (count > 4096 || qsizetype(count) > (bytes.size() - 4) / 12)
        fail("Texture block count exceeds input bounds.");
    TextureContainer result;
    qsizetype cursor = 4, totalPixels = 0;
    result.blocks.reserve(count);
    for (quint16 index = 0; index < count; ++index) {
        requireRange(bytes, cursor, 12);
        const quint32 lengthFlags = read32(bytes, cursor);
        const qsizetype length = lengthFlags & 0x00ffffff;
        const qsizetype alignedLength = (length + 3) & ~qsizetype(3);
        requireRange(bytes, cursor + 12, alignedLength);
        TextureBlock block;
        block.flags = quint8(lengthFlags >> 24);
        block.x = read16(bytes, cursor + 4);
        block.y = read16(bytes, cursor + 6);
        block.widthWords = read16(bytes, cursor + 8);
        block.height = read16(bytes, cursor + 10);
        const qsizetype decodedSize = pixelSize(block);
        totalPixels += decodedSize;
        if (totalPixels > 64 * 1024 * 1024) fail("Decoded texture container exceeds 64 MiB.");
        block.pixels.resize(decodedSize);
        qsizetype input = cursor + 12;
        const qsizetype end = input + length;
        auto readByte = [&]() -> quint8 {
            if (input >= end) fail("Texture payload ends before all pixels are decoded.");
            return quint8(bytes[input++]);
        };
        if ((block.flags & 12) == 4) {
            if (decodedSize > length) fail("Raw texture payload is shorter than its rectangle.");
            block.pixels = bytes.mid(input, decodedSize);
        } else {
            // The game carries the run across row/column boundaries and
            // resets it only between blocks. Control 0 repeats one byte.
            int run = 0;
            bool repeat = false;
            quint8 value = 0;
            auto nextByte = [&]() -> char {
                if (run <= 0) {
                    const quint8 control = readByte();
                    repeat = control < 128;
                    run = repeat ? int(control) : 256 - int(control);
                    if (repeat) value = readByte();
                }
                const quint8 decoded = repeat ? value : readByte();
                --run;
                return char(decoded);
            };
            if (block.flags & 8) {
                // The column stream stores all low bytes, then all high
                // bytes of that word column, before moving to the next x.
                const qsizetype stride = qsizetype(block.widthWords) * 2;
                for (int x = 0; x < block.widthWords; ++x)
                    for (int byte = 0; byte < 2; ++byte)
                        for (int y = 0; y < block.height; ++y)
                            block.pixels[y * stride + x * 2 + byte] = nextByte();
            } else {
                for (qsizetype i = 0; i < decodedSize; ++i) block.pixels[i] = nextByte();
            }
        }
        result.blocks.append(std::move(block));
        cursor += 12 + alignedLength;
    }
    result.bytesConsumed = cursor;
    return result;
}

QImage previewBgr555(const TextureBlock &block) {
    if (block.pixels.size() != pixelSize(block)) fail("VRAM block has an inconsistent pixel buffer.");
    QImage image(block.widthWords, block.height, QImage::Format_RGB32);
    if (image.isNull()) fail("Unable to allocate texture preview.");
    for (int y = 0; y < block.height; ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < block.widthWords; ++x) {
            const auto word = qFromLittleEndian<quint16>(block.pixels.constData() + (qsizetype(y) * block.widthWords + x) * 2);
            const int r = word & 31, g = (word >> 5) & 31, b = (word >> 10) & 31;
            row[x] = qRgb((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
        }
    }
    return image;
}

QImage previewIndexed(const TextureBlock &block, const TextureBlock &palette, int bitsPerPixel) {
    if (bitsPerPixel != 4 && bitsPerPixel != 8) fail("Indexed textures require 4 or 8 bits per pixel.");
    if (block.pixels.size() != pixelSize(block) || palette.pixels.size() != pixelSize(palette))
        fail("Texture or palette has an inconsistent pixel buffer.");
    const int colors = 1 << bitsPerPixel;
    if (!palette.isPalette() || palette.widthWords < colors) fail("Selected palette does not contain enough colors in its first row.");
    const int width = block.widthWords * 16 / bitsPerPixel;
    QImage image(width, block.height, QImage::Format_ARGB32);
    if (image.isNull()) fail("Unable to allocate indexed texture preview.");
    for (int y = 0; y < block.height; ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const auto value = quint8(block.pixels[y * block.widthWords * 2 + x * bitsPerPixel / 8]);
            const int index = bitsPerPixel == 4 ? (value >> ((x & 1) * 4)) & 15 : value;
            const auto word = qFromLittleEndian<quint16>(palette.pixels.constData() + index * 2);
            const int r = word & 31, g = (word >> 5) & 31, b = (word >> 10) & 31;
            row[x] = qRgba((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), word == 0 ? 0 : 255);
        }
    }
    return image;
}

}
