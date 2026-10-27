#include "vfs.h"

namespace nm {

bool pngDimensions(const std::string& b, int& w, int& h) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 24) return false;
    for (int i = 0; i < 8; ++i) if (static_cast<unsigned char>(b[i]) != sig[i]) return false;
    if (b.compare(12, 4, "IHDR") != 0) return false;
    auto be = [&](size_t o) {
        return (uint32_t(uint8_t(b[o])) << 24) | (uint32_t(uint8_t(b[o + 1])) << 16) | (uint32_t(uint8_t(b[o + 2])) << 8) | uint32_t(uint8_t(b[o + 3]));
    };
    uint32_t ww = be(16), hh = be(20);
    if (ww == 0 || hh == 0 || ww > 16384 || hh > 16384) return false;
    w = static_cast<int>(ww);
    h = static_cast<int>(hh);
    return true;
}

void Vfs::addPack(const std::string& mod, std::shared_ptr<PackSource> pack, const std::string& root, std::vector<std::string>& errors) {
    packs_[mod] = pack;
    std::string prefix = root + "/";
    for (const auto& path : pack->list(prefix)) {
        std::string rel = path.substr(prefix.size());
        AssetInfo a;
        a.logical = mod + ":" + rel;
        a.owner = mod;
        a.path = path;
        std::string data;
        if (!pack->read(path, data)) { errors.push_back("aset tidak terbaca: " + path); continue; }
        a.size = data.size();
        if (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".png") == 0) {
            if (!pngDimensions(data, a.width, a.height)) { errors.push_back("PNG tidak valid: " + path); continue; }
        }
        assets_[a.logical] = a;
    }
}

const AssetInfo* Vfs::info(const std::string& l) const {
    auto it = assets_.find(l);
    return it == assets_.end() ? nullptr : &it->second;
}

bool Vfs::read(const std::string& l, std::string& out) const {
    const AssetInfo* a = info(l);
    if (!a) return false;
    return packs_.at(a->owner)->read(a->path, out);
}

void Vfs::addOverride(const OverrideInfo& o, std::string* replaced) {
    auto it = overrides_.find(o.target);
    if (it != overrides_.end() && replaced) *replaced = it->second.owner;
    overrides_[o.target] = o;
}

const OverrideInfo* Vfs::overrideFor(const std::string& t) const {
    auto it = overrides_.find(t);
    return it == overrides_.end() ? nullptr : &it->second;
}

std::vector<OverrideInfo> Vfs::overrides() const {
    std::vector<OverrideInfo> v;
    for (const auto& kv : overrides_) v.push_back(kv.second);
    return v;
}

}  // namespace nm
