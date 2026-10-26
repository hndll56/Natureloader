#pragma once
#include <QByteArray>
#include <QImage>
#include <QList>

namespace nature {

// Unrelocated LoadImage rectangle. Pixels remain little-endian VRAM words;
// they may be packed indices, palette colors, or direct-color texels.
struct TextureBlock {
    quint16 x = 0, y = 0, widthWords = 0, height = 0;
    quint8 flags = 0;
    QByteArray pixels;
    bool isPalette() const { return (flags & 3) == 2; }
};

struct TextureContainer {
    QList<TextureBlock> blocks;
    qsizetype bytesConsumed = 0;
};

// Portable decoding of the supplied decomp's DecodeContainer0D. Includes
// raw rows and byte RLE in row/column order. No MIPS or GPU calls are used.
// Throws std::runtime_error for truncated or oversized inputs.
TextureContainer decodeContainer0D(const QByteArray &bytes);

// Opaque diagnostic preview of 16-bit VRAM words as PS1 BGR555 colors.
// Packed-index textures require a separately identified CLUT and bit depth;
// this function does not infer those or claim to render their actual sprites.
QImage previewBgr555(const TextureBlock &block);

// Explicit CLUT selection: callers choose the palette and the packed bit depth.
// Zero palette words are transparent; hardware semitransparency is not simulated.
QImage previewIndexed(const TextureBlock &block, const TextureBlock &palette, int bitsPerPixel);

}
