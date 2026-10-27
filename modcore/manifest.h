// Mod manifest (schema 2) with an adapter for the legacy launcher schema 1.
#pragma once
#include <set>
#include <string>
#include <vector>

#include "json.h"
#include "util.h"
#include "version.h"

namespace nm {

struct Dependency { std::string id; VersionRange range; bool optional = false; };
struct PluginDecl {
    std::string file;                    // path inside the pack
    std::string sha256;                  // required: hash of the binary
    std::vector<std::string> exeHashes;  // game executables this build was made for
    VersionRange api;
};
struct HookBinding { std::string on, handler, subject; int priority = 0; };

struct Manifest {
    int schema = 2;
    std::string id, name, versionText, description, game;
    Version version;
    std::vector<std::string> authors;
    VersionRange api;                        // modding API range the mod targets
    std::vector<Dependency> depends;
    std::vector<std::string> loadAfter, loadBefore;
    std::vector<std::pair<std::string, VersionRange>> conflicts;
    std::vector<std::string> contentFiles;   // pack paths of content JSON
    Json inlineContent = Json::object();     // {kind:{local:{...}}}
    std::vector<std::string> scriptFiles;
    std::vector<HookBinding> hooks;
    std::string assetRoot = "assets";
    std::set<std::string> permissions;       // inventory, player, world, ui, save
    std::vector<PluginDecl> plugins;
    Json texts = Json::object();             // legacy: base text-bank overrides
    int saveVersion = 1;
};

struct ManifestResult { bool ok = false; Manifest manifest; std::vector<std::string> errors; };

ManifestResult parseManifest(const Json& root);
// Read "mod.json" from a pack and parse it.
ManifestResult loadManifest(const PackSource& pack);
const std::set<std::string>& knownPermissions();

}  // namespace nm
