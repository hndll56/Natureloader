// Layered asset view: mod assets by logical id, plus overrides of base assets
// (sprites -> HD, fonts, sounds) resolved at the asset-loading boundary.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json.h"
#include "util.h"

namespace nm {

struct AssetInfo {
    std::string logical;   // "kebunku:sprites/lobak.png"
    std::string owner;
    std::string path;      // inside the pack
    uint64_t size = 0;
    int width = 0, height = 0;  // PNG only
};
struct OverrideInfo {
    std::string target;    // "base:tex/11/3"
    std::string asset;     // logical id of replacement
    std::string owner;
    int scale = 1;         // replacement is scale x the original pixel size
    int pivotX = 0, pivotY = 0;
};

bool pngDimensions(const std::string& bytes, int& w, int& h);

class Vfs {
public:
    // Index every file under assetRoot of the pack as "<mod>:<relative path>".
    void addPack(const std::string& mod, std::shared_ptr<PackSource> pack, const std::string& assetRoot, std::vector<std::string>& errors);
    bool exists(const std::string& logical) const { return assets_.count(logical) != 0; }
    const AssetInfo* info(const std::string& logical) const;
    bool read(const std::string& logical, std::string& out) const;

    // Later mods win; the replaced owner is reported through `replaced`.
    void addOverride(const OverrideInfo& o, std::string* replaced);
    const OverrideInfo* overrideFor(const std::string& target) const;
    size_t assetCount() const { return assets_.size(); }
    std::vector<OverrideInfo> overrides() const;

private:
    std::map<std::string, AssetInfo> assets_;
    std::map<std::string, std::shared_ptr<PackSource>> packs_;  // owner -> pack
    std::map<std::string, OverrideInfo> overrides_;
};

}  // namespace nm
