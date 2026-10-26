#pragma once
#include "core.h"
namespace nature {
// Convert active text mods into guarded RAM patches for the local native runtime.
// Reject unsupported content, glyphs and capacity overflow instead of silently ignoring them.
QString exportNativeTextMods(const Library &library, const QString &exePath, const QString &outputRoot);
}
