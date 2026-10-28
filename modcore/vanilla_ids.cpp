#include "vanilla_ids.h"

#include "registry.h"
#include "util.h"

namespace nm {

bool parseVanillaAlias(const std::string& text, VanillaId& out, std::string& err) {
    const std::string prefix = std::string(kVanillaAliasPrefix) + ":";
    if (text.compare(0, prefix.size(), prefix) != 0) { err = "bukan alias vanilla: '" + text + "'"; return false; }
    const std::string rest = text.substr(prefix.size());
    const size_t sep = rest.find_first_of(".:");
    if (sep == std::string::npos) { err = "alias vanilla harus berbentuk vanilla:<jenis>.<nama>: '" + text + "'"; return false; }
    std::string kind = rest.substr(0, sep);
    const std::string local = rest.substr(sep + 1);
    if (kind == "map") kind = "area";  // the registry calls maps "area"
    if (!Registry::isNumericKind(kind)) { err = "jenis alias vanilla tidak dikenal: '" + rest.substr(0, sep) + "'"; return false; }
    if (!validLocalId(local)) { err = "nama alias vanilla tidak valid: '" + local + "'"; return false; }
    out.kind = kind;
    out.canonical = "base:" + local;
    return true;
}

bool canonicalContentId(const std::string& text, std::string& canonical, std::string* kind, std::string& err) {
    if (text.compare(0, std::string(kVanillaAliasPrefix).size() + 1, std::string(kVanillaAliasPrefix) + ":") == 0) {
        VanillaId v;
        if (!parseVanillaAlias(text, v, err)) return false;
        canonical = v.canonical;
        if (kind) *kind = v.kind;
        return true;
    }
    std::string mod, local;
    if (!splitId(text, mod, local)) { err = "id konten harus berbentuk mod:id: '" + text + "'"; return false; }
    canonical = text;
    if (kind) kind->clear();
    return true;
}

std::string vanillaAliasOf(const std::string& kind, const std::string& baseId) {
    std::string mod, local;
    if (!splitId(baseId, mod, local) || mod != "base" || !Registry::isNumericKind(kind)) return "";
    return std::string(kVanillaAliasPrefix) + ":" + (kind == "area" ? "map" : kind) + "." + local;
}

}  // namespace nm
