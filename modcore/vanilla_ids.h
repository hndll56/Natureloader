// Vanilla (retail) content ids as seen by the native mod layer.
//
// Repository convention (unchanged): retail content lives in the registry under
// the reserved mod id "base", written "base:<local>" with exactly one ':' and a
// local id matching [a-z][a-z0-9_-]{0,63}. Example: "base:turnip" (item 65).
// That string is the CANONICAL id. Registry, save files, manifests and the C ABI
// keep using it; nothing in this header changes how they parse ids.
//
// The architecture notes for the hybrid runtime also use a typed "vanilla:" form
// (vanilla:item.turnip, vanilla:npc:elli, vanilla:map:farm). Those spellings are
// accepted ONLY at the native-layer entry points as ALIASES and are rewritten to
// the canonical id before any lookup. They are never stored and never reach the
// registry, so the two spellings cannot drift apart.
//
//   alias                      kind (registry)   canonical
//   vanilla:item.turnip        item              base:turnip
//   vanilla:item:turnip        item              base:turnip
//   vanilla:npc:elli           npc               base:elli
//   vanilla:map:farm           area              base:farm      ("map" is the registry kind "area")
//
// The adapter only normalises text. Whether a canonical id exists is a registry
// question (today the base catalog names items only; npc/area names are empty).
#pragma once
#include <string>

namespace nm {

inline constexpr const char* kVanillaAliasPrefix = "vanilla";

struct VanillaId {
    std::string kind;       // registry kind: item, npc, area, ...
    std::string canonical;  // "base:<local>"
};

// Parse a "vanilla:<kind>.<local>" / "vanilla:<kind>:<local>" alias.
bool parseVanillaAlias(const std::string& text, VanillaId& out, std::string& err);

// Normalise any content id used by the native layer:
//   - "vanilla:..." aliases -> canonical "base:<local>" (kind filled in)
//   - "<mod>:<local>"       -> unchanged (kind left empty; the registry knows it)
// Returns false with a message for anything else.
bool canonicalContentId(const std::string& text, std::string& canonical, std::string* kind, std::string& err);

// Preferred alias spelling of a canonical retail id ("" if baseId is not "base:<local>").
std::string vanillaAliasOf(const std::string& kind, const std::string& baseId);

}  // namespace nm
