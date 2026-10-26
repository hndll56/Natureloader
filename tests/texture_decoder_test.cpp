#include "texture_decoder.h"
#include <QCoreApplication>
#include <QtEndian>
#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception &) { rejected = true; }
    check(rejected, "Expected malformed texture rejection.");
}
QByteArray container(quint8 flags, quint16 width, quint16 height, const QByteArray &data) {
    QByteArray bytes(16, 0);
    bytes[0] = 0x0d;
    qToLittleEndian<quint16>(1, bytes.data() + 2);
    qToLittleEndian<quint32>(quint32(data.size()) | (quint32(flags) << 24), bytes.data() + 4);
    qToLittleEndian<quint16>(12, bytes.data() + 8);
    qToLittleEndian<quint16>(34, bytes.data() + 10);
    qToLittleEndian(width, bytes.data() + 12);
    qToLittleEndian(height, bytes.data() + 14);
    bytes += data;
    while (bytes.size() % 4) bytes += char(0);
    return bytes;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto raw = container(4, 3, 1, QByteArray::fromHex("1f00e003007c"));
        const auto decoded = nature::decodeContainer0D(raw + "unrelated trailing archive data");
        check(decoded.bytesConsumed == raw.size(), "Aligned container length");
        check(decoded.blocks.size() == 1, "Block count");
        const auto &block = decoded.blocks[0];
        check(block.x == 12 && block.y == 34 && block.widthWords == 3 && block.height == 1, "Rectangle metadata");
        check(!block.isPalette(), "Direct block flag");
        const auto image = nature::previewBgr555(block);
        check(image.pixel(0,0) == qRgb(255,0,0) && image.pixel(1,0) == qRgb(0,255,0) && image.pixel(2,0) == qRgb(0,0,255), "BGR555 channel order");
        const auto rowRle = nature::decodeContainer0D(container(0, 2, 2, QByteArray::fromHex("0344fb0102030405")));
        check(rowRle.blocks[0].pixels == QByteArray::fromHex("4444440102030405"), "Runs cross row boundaries");
        const auto zeroRun = nature::decodeContainer0D(container(0, 1, 1, QByteArray::fromHex("00770188")));
        check(zeroRun.blocks[0].pixels == QByteArray::fromHex("7788"), "Control zero emits one repeated byte");
        const auto columnRle = nature::decodeContainer0D(container(8, 2, 2, QByteArray::fromHex("f80102030405060708")));
        check(columnRle.blocks[0].pixels == QByteArray::fromHex("0103050702040608"), "Column RLE byte ordering");
        const auto palette = nature::decodeContainer0D(container(6, 1, 1, QByteArray::fromHex("ffff")));
        check(palette.blocks[0].isPalette() && palette.blocks[0].pixels == QByteArray::fromHex("ffff"), "Palette marker and STP bit retained");
        const auto clut = nature::decodeContainer0D(container(6, 16, 1, QByteArray::fromHex("00001f00e003007c") + QByteArray(24, 0)));
        const auto packed = nature::decodeContainer0D(container(4, 1, 1, QByteArray::fromHex("1032")));
        const auto sprite = nature::previewIndexed(packed.blocks[0], clut.blocks[0], 4);
        check(sprite.width() == 4 && qAlpha(sprite.pixel(0,0)) == 0 && sprite.pixel(1,0) == qRgba(255,0,0,255)
            && sprite.pixel(2,0) == qRgba(0,255,0,255) && sprite.pixel(3,0) == qRgba(0,0,255,255), "4bpp palette and nibble ordering");
        rejects([&] { nature::previewIndexed(packed.blocks[0], clut.blocks[0], 8); });
        rejects([&] { nature::previewIndexed(packed.blocks[0], clut.blocks[0], 16); });
        const auto longLiteral = nature::decodeContainer0D(container(0, 64, 1, QByteArray(1, char(0x80)) + QByteArray(128, char(0x5a))));
        check(longLiteral.blocks[0].pixels == QByteArray(128, char(0x5a)), "128-byte literal run");
        QByteArray multiple = raw;
        qToLittleEndian<quint16>(2, multiple.data() + 2);
        multiple += container(4, 1, 1, QByteArray::fromHex("0080")).mid(4);
        const auto two = nature::decodeContainer0D(multiple);
        check(two.blocks.size() == 2 && two.blocks[1].pixels == QByteArray::fromHex("0080"), "Aligned blocks decode independently");
        for (qsizetype n = 0; n < raw.size(); ++n) rejects([&] { nature::decodeContainer0D(raw.first(n)); });
        rejects([&] { auto b = raw; b[0] = 12; nature::decodeContainer0D(b); });
        rejects([&] { auto b = raw; qToLittleEndian<quint16>(4097, b.data()+2); nature::decodeContainer0D(b); });
        rejects([&] { nature::decodeContainer0D(container(4, 0, 1, {})); });
        rejects([&] { nature::decodeContainer0D(container(4, 1025, 1, {})); });
        rejects([&] { nature::decodeContainer0D(container(4, 1, 513, {})); });
        rejects([&] { nature::decodeContainer0D(container(4, 3, 1, "abc")); });
        rejects([&] { nature::decodeContainer0D(container(0, 2, 1, QByteArray::fromHex("fc0102"))); });
        rejects([&] { nature::decodeContainer0D(container(8, 2, 2, QByteArray::fromHex("01"))); });
        rejects([&] { auto b = raw; qToLittleEndian<quint32>(0x04ffffff, b.data()+4); nature::decodeContainer0D(b); });
        rejects([&] { auto b = block; b.pixels.chop(1); nature::previewBgr555(b); });
        const auto empty = nature::decodeContainer0D(QByteArray::fromHex("0d000000"));
        check(empty.blocks.isEmpty() && empty.bytesConsumed == 4, "Empty container");
        std::puts("PASS: 0D raw, row/column RLE, block alignment, palette metadata, BGR555 preview and malformed-input bounds.");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
