// Save extension: everything mods need persisted, stored beside (never inside) the
// retail save so the base save stays valid with or without mods.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hooks.h"
#include "json.h"
#include "registry.h"

namespace nm {

struct SaveModEntry { std::string id, version; int saveVersion = 1; };

struct SaveExtension : public FlagStore {
    int format = 1;
    std::string baseSaveHash;                 // binds the sidecar to one retail save
    std::vector<SaveModEntry> mods;
    IdTable ids;
    std::map<std::string, Json> modData;      // per-mod private data (kept for missing mods)
    std::map<std::string, Json> flags;        // "mod:flag" -> value

    Json getFlag(const std::string& key) const override {
        auto it = flags.find(key);
        return it == flags.end() ? Json() : it->second;
    }
    void setFlag(const std::string& key, const Json& v) override { flags[key] = v; }

    Json toJson() const;
    static bool fromJson(const Json& j, SaveExtension& out, std::string& err);
};

struct SaveCompat {
    struct Missing { std::string id, version; std::vector<std::string> content; };
    struct Changed { std::string id, from, to; bool newerSave = false; bool saveVersionChanged = false; };
    std::vector<Missing> missing;
    std::vector<Changed> changed;
    std::vector<std::string> added;
    bool clean() const { return missing.empty() && changed.empty(); }
    std::string describe() const;
};

SaveCompat checkSave(const SaveExtension& save, const std::vector<SaveModEntry>& loaded);

}  // namespace nm
