#include "save.h"

#include "util.h"
#include "version.h"

namespace nm {

Json SaveExtension::toJson() const {
    Json j = Json::object();
    j["format"] = format;
    j["base_save_hash"] = baseSaveHash;
    Json m = Json::array();
    for (const auto& e : mods) {
        Json o = Json::object();
        o["id"] = e.id; o["version"] = e.version; o["save_version"] = e.saveVersion;
        m.push(o);
    }
    j["mods"] = m;
    j["idtable"] = ids.toJson();
    Json d = Json::object();
    for (const auto& kv : modData) d[kv.first] = kv.second;
    j["mod_data"] = d;
    Json f = Json::object();
    for (const auto& kv : flags) f[kv.first] = kv.second;
    j["flags"] = f;
    return j;
}

bool SaveExtension::fromJson(const Json& j, SaveExtension& out, std::string& err) {
    SaveExtension s;
    if (!j.isObject() || j["format"].asInt() != 1) { err = "format save mod tidak didukung"; return false; }
    s.baseSaveHash = j["base_save_hash"].asString();
    if (!j["mods"].isArray()) { err = "save: 'mods' bukan array"; return false; }
    std::set<std::string> seen;
    for (const auto& e : j["mods"].items()) {
        if (!e.isObject() || !validLocalId(e["id"].asString()) || !e["version"].isString() || !seen.insert(e["id"].asString()).second) {
            err = "save: entri mod tidak valid"; return false;
        }
        s.mods.push_back({e["id"].asString(), e["version"].asString(), static_cast<int>(e["save_version"].asInt(1))});
    }
    if (!IdTable::fromJson(j["idtable"], s.ids, err)) return false;
    if (!j["mod_data"].isObject() || !j["flags"].isObject()) { err = "save: mod_data/flags tidak valid"; return false; }
    for (const auto& kv : j["mod_data"].members()) {
        if (!validLocalId(kv.first) || !kv.second.isObject()) { err = "save: mod_data tidak valid untuk " + kv.first; return false; }
        s.modData[kv.first] = kv.second;
    }
    for (const auto& kv : j["flags"].members()) {
        size_t c = kv.first.find(':');
        if (c == std::string::npos || !validLocalId(kv.first.substr(0, c))) { err = "save: kunci flag tidak valid: " + kv.first; return false; }
        s.flags[kv.first] = kv.second;
    }
    out = s;
    return true;
}

SaveCompat checkSave(const SaveExtension& save, const std::vector<SaveModEntry>& loaded) {
    SaveCompat r;
    std::map<std::string, const SaveModEntry*> now;
    for (const auto& m : loaded) now[m.id] = &m;
    std::set<std::string> saved;
    for (const auto& m : save.mods) {
        saved.insert(m.id);
        auto it = now.find(m.id);
        if (it == now.end()) {
            SaveCompat::Missing ms{m.id, m.version, {}};
            for (const auto& k : save.ids.ids)
                for (const auto& e : k.second)
                    if (e.first.compare(0, m.id.size() + 1, m.id + ":") == 0) ms.content.push_back(k.first + " " + e.first);
            r.missing.push_back(ms);
        } else if (it->second->version != m.version || it->second->saveVersion != m.saveVersion) {
            SaveCompat::Changed c{m.id, m.version, it->second->version, false, it->second->saveVersion != m.saveVersion};
            Version a, b;
            if (Version::parse(m.version, a) && Version::parse(it->second->version, b)) c.newerSave = b < a;
            r.changed.push_back(c);
        }
    }
    for (const auto& m : loaded) if (!saved.count(m.id)) r.added.push_back(m.id);
    return r;
}

std::string SaveCompat::describe() const {
    std::string s;
    for (const auto& m : missing) {
        s += "Mod hilang: " + m.id + " (" + m.version + "). ";
        if (m.content.empty()) s += "Tidak ada konten bernomor yang tercatat. ";
        else {
            s += "Konten tidak tersedia: ";
            for (size_t i = 0; i < m.content.size(); ++i) s += (i ? ", " : "") + m.content[i];
            s += ". ";
        }
        s += "Data tetap disimpan dan pulih saat mod dipasang lagi.\n";
    }
    for (const auto& c : changed) {
        s += "Mod berubah: " + c.id + " " + c.from + " -> " + c.to;
        if (c.newerSave) s += " (save dibuat dengan versi LEBIH BARU, hati-hati)";
        s += "\n";
    }
    for (const auto& a : added) s += "Mod baru sejak save: " + a + "\n";
    return s;
}

}  // namespace nm
