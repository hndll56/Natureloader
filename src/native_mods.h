#pragma once
#include "core.h"
namespace nature {
// Encode one retail text slot; shared by native preflight and the exporter.
QByteArray encodeNativeText(const QString &text, int capacity, const QString &key);
// Convert supported text and retail item-field mods into guarded native RAM patches.
// Reject unsupported content, glyphs and capacity overflow instead of silently ignoring them.
QString exportNativeMods(const Library &library, const QString &exePath, const QString &outputRoot);
// Compatibility entry point retained for existing callers.
inline QString exportNativeTextMods(const Library &library, const QString &exePath, const QString &outputRoot) {
    return exportNativeMods(library, exePath, outputRoot);
}
}
