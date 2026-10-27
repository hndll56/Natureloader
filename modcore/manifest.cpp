#include "manifest.h"

namespace nm {

const std::set<std::string>& knownPermissions() {
    static const std::set<std::string> p = {"inventory", "player", "world", "ui", "save"};
    return p;
}

namespace {
struct Ctx {
    std::vector<std::string>& errs;
    void err(const std::string& m) { errs.push_back(m); }
};

bool reqString(const Json& o, const char* k, std::string& out, Ctx& c, size_t maxLen) {
    const Json* v = o.find(k);
    if (!v || !v->isString() || v->asString().empty()) { c.err(std::string("field wajib '") + k + "' harus string tidak kosong"); return false; }
    if (v->asString().size() > maxLen) { c.err(std::string("field '") + k + "' terlalu panjang"); return false; }
    out = v->asString();
    return true;
}

void stringList(const Json& o, const char* k, std::vector<std::string>& out, Ctx& c) {
    const Json* v = o.find(k);
    if (!v) return;
    if (!v->isArray()) { c.err(std::string("'") + k + "' harus array string"); return; }
    for (const auto& e : v->items()) {
        if (!e.isString()) { c.err(std::string("'") + k + "' berisi nilai bukan string"); continue; }
        out.push_back(e.asString());
    }
}

void idList(const Json& o, const char* k, std::vector<std::string>& out, Ctx& c) {
    std::vector<std::string> tmp;
    stringList(o, k, tmp, c);
    for (auto& s : tmp) {
        if (!validLocalId(s)) c.err(std::string("id mod tidak valid di '") + k + "': " + s);
        else out.push_back(s);
    }
}

void depMap(const Json& o, const char* k, bool optional, Manifest& m, Ctx& c) {
    const Json* v = o.find(k);
    if (!v) return;
    if (!v->isObject()) { c.err(std::string("'") + k + "' harus objek {id: rentang}"); return; }
    for (const auto& kv : v->members()) {
        VersionRange r;
        if (!validLocalId(kv.first)) { c.err("id dependensi tidak valid: " + kv.first); continue; }
        if (!kv.second.isString() || !VersionRange::parse(kv.second.asString(), r)) {
            c.err("rentang versi tidak valid untuk " + kv.first); continue;
        }
        m.depends.push_back({kv.first, r, optional});
    }
}

void checkKeys(const Json& o, const std::set<std::string>& allowed, Ctx& c) {
    for (const auto& kv : o.members())
        if (!allowed.count(kv.first)) c.err("field manifest tidak dikenal: " + kv.first);
}

ManifestResult fromV1(const Json& root, std::vector<std::string> errs) {
    // schema 1: {schema,id,name,version,game,texts?,content?{ "ns:local":{type,name,data} }}
    Ctx c{errs};
    Manifest m;
    m.schema = 1;
    checkKeys(root, {"schema", "id", "name", "version", "game", "texts", "content"}, c);
    reqString(root, "id", m.id, c, 64);
    reqString(root, "name", m.name, c, 120);
    reqString(root, "version", m.versionText, c, 40);
    reqString(root, "game", m.game, c, 32);
    if (!m.id.empty() && !validLocalId(m.id)) c.err("id mod tidak valid");
    if (!Version::parse(m.versionText, m.version)) { m.version = Version{0, 0, 0}; }
    m.api = VersionRange::any();
    if (root.has("texts")) m.texts = root["texts"];
    if (root["content"].isObject()) {
        for (const auto& kv : root["content"].members()) {
            std::string ns, local;
            if (!splitId(kv.first, ns, local) || ns != m.id) { c.err("kunci konten v1 tidak valid: " + kv.first); continue; }
            const Json& rec = kv.second;
            std::string type = rec["type"].asString();
            Json out = Json::object();
            out["name"] = rec["name"];
            for (const auto& d : rec["data"].members()) out[d.first] = d.second;
            m.inlineContent[type][local] = out;
        }
    }
    ManifestResult r;
    r.ok = errs.empty();
    r.manifest = m;
    r.errors = errs;
    return r;
}
}  // namespace

ManifestResult parseManifest(const Json& root) {
    ManifestResult res;
    std::vector<std::string>& errs = res.errors;
    Ctx c{errs};
    if (!root.isObject()) { errs.push_back("mod.json harus objek JSON"); return res; }
    if (!root["schema"].isInt()) { errs.push_back("schema harus bilangan bulat 1 atau 2"); return res; }
    int64_t schema = root["schema"].asInt(0);
    if (schema == 1) {
        ManifestResult r = fromV1(root, {});
        return r;
    }
    if (schema != 2) { errs.push_back("schema harus 1 atau 2"); return res; }

    Manifest m;
    checkKeys(root, {"schema", "id", "name", "version", "authors", "description", "game", "api", "depends", "optional",
                     "load_after", "load_before", "conflicts", "content", "scripts", "hooks", "assets", "permissions",
                     "plugins", "texts", "save_version"}, c);
    reqString(root, "id", m.id, c, 64);
    reqString(root, "name", m.name, c, 120);
    reqString(root, "version", m.versionText, c, 40);
    reqString(root, "game", m.game, c, 32);
    if (!m.id.empty()) {
        if (!validLocalId(m.id)) c.err("id mod tidak valid (huruf kecil/angka/-/_, mulai huruf, maks 64)");
        if (reservedWindowsName(m.id)) c.err("id mod memakai nama perangkat Windows");
        if (m.id == "base") c.err("id 'base' dicadangkan untuk game asli");
    }
    if (!m.versionText.empty() && !Version::parse(m.versionText, m.version)) c.err("versi mod harus MAJOR.MINOR.PATCH");
    if (root.has("description")) m.description = root["description"].asString();
    stringList(root, "authors", m.authors, c);

    if (!root.has("api") || !root["api"].isString() || !VersionRange::parse(root["api"].asString(), m.api))
        c.err("field 'api' wajib berisi rentang versi, mis. \">=1.0 <2\"");
    depMap(root, "depends", false, m, c);
    depMap(root, "optional", true, m, c);
    idList(root, "load_after", m.loadAfter, c);
    idList(root, "load_before", m.loadBefore, c);
    if (root["conflicts"].isObject()) {
        for (const auto& kv : root["conflicts"].members()) {
            VersionRange r;
            if (!validLocalId(kv.first) || !kv.second.isString() || !VersionRange::parse(kv.second.asString(), r))
                c.err("entri conflicts tidak valid: " + kv.first);
            else m.conflicts.push_back({kv.first, r});
        }
    } else if (root.has("conflicts")) c.err("'conflicts' harus objek");

    const Json& content = root["content"];
    if (content.isArray()) {
        for (const auto& e : content.items()) {
            if (!e.isString() || !safePackPath(e.asString())) c.err("path file content tidak aman/valid");
            else m.contentFiles.push_back(e.asString());
        }
    } else if (content.isObject()) m.inlineContent = content;
    else if (root.has("content")) c.err("'content' harus array path atau objek");

    const Json& scripts = root["scripts"];
    if (scripts.isArray()) {
        for (const auto& e : scripts.items()) {
            if (!e.isString() || !safePackPath(e.asString())) c.err("path script tidak aman/valid");
            else m.scriptFiles.push_back(e.asString());
        }
    } else if (root.has("scripts")) c.err("'scripts' harus array path");

    if (root["hooks"].isArray()) {
        for (const auto& h : root["hooks"].items()) {
            HookBinding b;
            b.on = h["on"].asString();
            b.handler = h["handler"].asString();
            b.subject = h["subject"].asString();
            b.priority = static_cast<int>(h["priority"].asInt(0));
            if (b.on.empty() || b.handler.empty()) c.err("hook global butuh 'on' dan 'handler'");
            else m.hooks.push_back(b);
        }
    } else if (root.has("hooks")) c.err("'hooks' harus array");

    if (root.has("assets")) {
        const Json& a = root["assets"];
        std::string dir = a["dir"].asString();
        if (!a.isObject() || !safePackPath(dir)) c.err("assets.dir tidak valid");
        else m.assetRoot = dir;
    }
    if (root["permissions"].isArray()) {
        for (const auto& p : root["permissions"].items()) {
            if (!p.isString() || !knownPermissions().count(p.asString())) c.err("permission tidak dikenal: " + p.asString());
            else m.permissions.insert(p.asString());
        }
    } else if (root.has("permissions")) c.err("'permissions' harus array");

    if (root["plugins"].isArray()) {
        for (const auto& p : root["plugins"].items()) {
            PluginDecl d;
            d.file = p["file"].asString();
            d.sha256 = p["sha256"].asString();
            if (!safePackPath(d.file) || d.sha256.size() != 64) { c.err("plugin butuh 'file' aman dan 'sha256' 64 hex"); continue; }
            for (const auto& h : p["exe_hashes"].items()) d.exeHashes.push_back(h.asString());
            if (d.exeHashes.empty()) { c.err("plugin wajib mendaftarkan 'exe_hashes' game yang cocok"); continue; }
            if (!VersionRange::parse(p["api"].isString() ? p["api"].asString() : std::string("*"), d.api)) { c.err("plugin.api tidak valid"); continue; }
            m.plugins.push_back(d);
        }
    } else if (root.has("plugins")) c.err("'plugins' harus array");

    if (root["texts"].isObject()) m.texts = root["texts"];
    m.saveVersion = static_cast<int>(root["save_version"].asInt(1));
    if (m.saveVersion < 1) c.err("save_version minimal 1");

    res.manifest = m;
    res.ok = errs.empty();
    return res;
}

ManifestResult loadManifest(const PackSource& pack) {
    std::string text;
    if (!pack.read("mod.json", text)) {
        ManifestResult r;
        r.errors.push_back("mod.json tidak ditemukan di " + pack.label());
        return r;
    }
    try {
        return parseManifest(Json::parse(text));
    } catch (const JsonError& e) {
        ManifestResult r;
        r.errors.push_back(e.what());
        return r;
    }
}

}  // namespace nm
