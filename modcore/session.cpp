#include "session.h"

#include <algorithm>

namespace nm {

std::string Report::text() const {
    std::string s;
    for (const auto& m : messages) {
        const char* lv = m.level == Message::Error ? "ERROR" : (m.level == Message::Warning ? "PERINGATAN" : "info");
        s += std::string("[") + lv + "] fase " + m.phase + (m.mod.empty() ? "" : " (" + m.mod + ")") + ": " + m.text + "\n";
    }
    return s;
}

Session::Session(HostInfo host, const Json& baseCatalog) : info_(std::move(host)), base_(baseCatalog) {}
Session::~Session() { plugins_->unloadAll(bus_); plugins_.reset(); }

void Session::addPack(std::shared_ptr<PackSource> pack) {
    ManifestResult r = loadManifest(*pack);
    if (!r.ok) {
        for (const auto& e : r.errors) bootErrors_.push_back(pack->label() + ": " + e);
        return;
    }
    cands_.push_back({r.manifest, pack});
}

std::vector<Manifest> Session::candidates() const {
    std::vector<Manifest> v;
    for (const auto& c : cands_) v.push_back(c.m);
    // user order first, the rest alphabetical (deterministic)
    std::stable_sort(v.begin(), v.end(), [&](const Manifest& a, const Manifest& b) {
        auto pa = std::find(userOrder_.begin(), userOrder_.end(), a.id);
        auto pb = std::find(userOrder_.begin(), userOrder_.end(), b.id);
        bool ia = pa != userOrder_.end(), ib = pb != userOrder_.end();
        if (ia && ib) return pa < pb;
        if (ia != ib) return ia;
        return a.id < b.id;
    });
    return v;
}

const Manifest* Session::manifest(const std::string& id) const {
    for (const auto& c : cands_) if (c.m.id == id) return &c.m;
    return nullptr;
}

Report Session::preflight() const {
    Report rep;
    for (const auto& e : bootErrors_) rep.messages.push_back({Message::Error, "1-manifest", "", e});
    ResolveInput in;
    in.mods = candidates();
    in.gameId = info_.gameId;
    in.hostApi = info_.api;
    ResolveResult rr = resolveMods(in);
    rep.loaded = rr.order;
    rep.rejected = rr.rejected;
    for (const auto& kv : rr.rejected) for (const auto& why : kv.second) rep.messages.push_back({Message::Error, "1-resolve", kv.first, why});
    for (const auto& w : rr.warnings) rep.messages.push_back({Message::Warning, "1-resolve", "", w});
    return rep;
}

bool Session::build(const std::set<std::string>& excluded, SaveExtension* save, Report& rep, std::set<std::string>& failed) {
    // fresh state every attempt
    plugins_->unloadAll(bus_);
    plugins_ = std::make_unique<PluginHost>();
    reg_ = Registry();
    vfs_ = Vfs();
    bus_ = HookBus();
    actions_ = ActionEngine();
    rep = Report();
    failed.clear();
    for (const auto& e : bootErrors_) rep.messages.push_back({Message::Error, "1-manifest", "", e});

    auto err = [&](const std::string& phase, const std::string& mod, const std::string& text) {
        rep.messages.push_back({Message::Error, phase, mod, text});
        if (!mod.empty()) failed.insert(mod);
    };
    auto warn = [&](const std::string& phase, const std::string& mod, const std::string& text) {
        rep.messages.push_back({Message::Warning, phase, mod, text});
    };

    // ---- phase 1
    ResolveInput in;
    in.mods = candidates();
    in.gameId = info_.gameId;
    in.hostApi = info_.api;
    in.excluded = excluded;
    ResolveResult rr = resolveMods(in);
    rep.rejected = rr.rejected;
    for (const auto& kv : rr.rejected)
        for (const auto& why : kv.second)
            if (!excluded.count(kv.first)) rep.messages.push_back({Message::Error, "1-resolve", kv.first, why});
    for (const auto& w : rr.warnings) warn("1-resolve", "", w);
    const std::vector<std::string>& order = rr.order;

    std::map<std::string, const Candidate*> byId;
    for (const auto& c : cands_) byId[c.m.id] = &c;

    std::string e;
    if (!reg_.loadBase(base_, e)) { err("2-register", "", "katalog dasar: " + e); return false; }

    // ---- phase 2: register content of every mod before anything is validated
    std::map<std::string, std::set<std::string>> allowed;
    for (const auto& id : order) {
        const Manifest& m = byId[id]->m;
        for (const auto& d : m.depends) allowed[id].insert(d.id);
        for (const auto& a : m.loadAfter) allowed[id].insert(a);
        std::vector<Json> docs;
        docs.push_back(m.inlineContent);
        for (const auto& f : m.contentFiles) {
            std::string text;
            if (!byId[id]->pack->read(f, text)) { err("2-register", id, "file konten tidak terbaca: " + f); continue; }
            try { docs.push_back(Json::parse(text)); }
            catch (const JsonError& ex) { err("2-register", id, f + ": " + ex.what()); }
        }
        for (const auto& doc : docs) {
            if (!doc.isObject()) { err("2-register", id, "file konten harus objek"); continue; }
            for (const auto& kind : doc.members()) {
                if (kind.first == "$patch") continue;
                if (!kind.second.isObject()) { err("2-register", id, "'" + kind.first + "' harus objek {id-lokal: data}"); continue; }
                for (const auto& rec : kind.second.members())
                    if (!reg_.add(id, kind.first, rec.first, rec.second, e)) err("2-register", id, e);
            }
        }
    }
    // patches only after all records exist (a patch may target a later mod's record)
    for (const auto& id : order) {
        const Manifest& m = byId[id]->m;
        std::vector<Json> docs{m.inlineContent};
        for (const auto& f : m.contentFiles) {
            std::string text;
            if (byId[id]->pack->read(f, text)) { try { docs.push_back(Json::parse(text)); } catch (...) {} }
        }
        for (const auto& doc : docs)
            for (const auto& p : doc["$patch"].members()) {
                std::string tm, tl;
                if (splitId(p.first, tm, tl) && tm != "base" && tm != id && !allowed[id].count(tm)) {
                    err("2-register", id, "patch ke mod '" + tm + "' tanpa deklarasi dependensi"); continue;
                }
                if (!reg_.patch(id, p.first, p.second, e)) err("2-register", id, e);
            }
    }
    for (const auto& p : reg_.patches())
        if (p.previousOwner != "base" && p.previousOwner != p.owner)
            warn("2-register", p.owner, "menimpa field '" + p.field + "' pada " + p.target + " yang sebelumnya diubah oleh " + p.previousOwner);

    // ---- phase 3: validate
    std::vector<std::string> warns;
    for (const auto& kv : reg_.validate(allowed, &warns)) for (const auto& m : kv.second) err("3-validate", kv.first, m);
    for (const auto& w : warns) warn("3-validate", "", w);

    // ---- phase 4: handles
    SaveExtension* sv = save ? save : &localSave_;
    IdTable table = sv->ids;
    // forget handles of records that will not exist and belong to nobody loaded: keep them (tombstones)
    if (!failed.empty()) { /* handled by the restart */ }
    if (!reg_.allocateHandles(order, table, e)) err("4-handles", "", e);
    lastIds_ = table;

    // ---- phase 5: assets
    for (const auto& id : order) {
        std::vector<std::string> errs;
        vfs_.addPack(id, byId[id]->pack, byId[id]->m.assetRoot, errs);
        for (const auto& x : errs) err("5-assets", id, x);
    }
    for (const auto& r : reg_.assetRefs()) {
        std::string m = r.ref.substr(0, r.ref.find(':'));
        if (m == "base") continue;
        if (!vfs_.exists(r.ref)) err("5-assets", r.owner, r.record + "." + r.field + ": aset tidak ada: " + r.ref);
    }
    for (const Record* r : reg_.all("asset_override")) {
        OverrideInfo o;
        o.target = r->data["target"].asString();
        o.asset = r->data["file"].asString();
        o.owner = r->owner;
        o.scale = static_cast<int>(r->data["scale"].asInt(1));
        if (r->data["pivot"].size() == 2) { o.pivotX = static_cast<int>(r->data["pivot"][0].asInt()); o.pivotY = static_cast<int>(r->data["pivot"][1].asInt()); }
        std::string replaced;
        vfs_.addOverride(o, &replaced);
        if (!replaced.empty() && replaced != o.owner) warn("5-assets", o.owner, "menimpa override " + o.target + " milik " + replaced + " (urutan muat: yang terakhir menang)");
    }

    // ---- phase 6: scripts + hooks
    std::map<std::string, std::set<std::string>> handlerOwners;
    for (const auto& id : order) {
        const Manifest& m = byId[id]->m;
        for (const auto& f : m.scriptFiles) {
            std::string text;
            if (!byId[id]->pack->read(f, text)) { err("6-hooks", id, "script tidak terbaca: " + f); continue; }
            try {
                for (const auto& x : actions_.load(id, Json::parse(text), m.permissions)) err("6-hooks", id, f + ": " + x);
            } catch (const JsonError& ex) { err("6-hooks", id, f + ": " + ex.what()); }
        }
    }
    for (const auto& x : actions_.unresolvedCalls()) err("6-hooks", x.substr(0, x.find(':')), x);

    auto bindHandler = [&](const std::string& owner, const std::string& hook, const std::string& subject, int prio, const std::string& handler) {
        if (!HookBus::knownHooks().count(hook)) { err("6-hooks", owner, "hook tidak dikenal: " + hook); return; }
        std::string h = handler;
        if (h.find(':') == std::string::npos) h = owner + ":" + h;
        if (!actions_.has(h)) { err("6-hooks", owner, "handler tidak ada: " + h); return; }
        bus_.bind(owner, hook, subject, prio, [this, h, owner](HookCtx& ctx) {
            std::string ex;
            FlagStore* fs = save_ ? static_cast<FlagStore*>(save_) : static_cast<FlagStore*>(&localSave_);
            if (!actions_.run(h, ctx, *host_, fs, &ex)) throw std::runtime_error(ex);
        });
    };
    for (const auto& id : order) {
        for (const auto& hb : byId[id]->m.hooks) bindHandler(id, hb.on, hb.subject, hb.priority, hb.handler);
    }
    for (const auto& kv : std::vector<std::string>{"item", "npc", "area", "crop", "event"})
        for (const Record* r : reg_.all(kv)) {
            if (r->owner == "base") continue;
            for (const auto& hf : Registry::hookFields(kv)) {
                const Json* h = r->data["hooks"].find(hf.first);
                if (h && h->isString()) bindHandler(r->owner, hf.second, r->id, 0, h->asString());
            }
        }
    actions_.emitFn = [this](HookCtx& c) { bus_.fire(c); };

    // ---- phase 7: native plugins
    for (const auto& id : order) {
        const Candidate& c = *byId[id];
        for (const auto& p : c.m.plugins) {
            std::string bin;
            if (!c.pack->read(p.file, bin)) { err("7-plugins", id, "plugin tidak terbaca: " + p.file); continue; }
            std::string actual = sha256Hex(bin);
            bool approved = approvals_.approved(id, c.m.versionText, actual);
            PluginCheck chk = checkPlugin(p, actual, info_.exeSha256, info_.api, approved);
            if (!chk.ok) { err("7-plugins", id, "plugin ditolak (" + p.file + "): " + chk.reason); continue; }
            std::string pe;
            if (!plugins_->load(id, p.sha256, bin, bus_, *host_, info_.api, info_.gameId, pe)) err("7-plugins", id, "plugin gagal dimuat: " + pe);
        }
    }

    for (const auto& id : order) if (!failed.count(id)) rep.loaded.push_back(id);
    return failed.empty();
}

bool Session::load(SaveExtension* save) {
    save_ = save;
    std::set<std::string> excluded;
    std::set<std::string> failed;
    Report rep;
    std::vector<Message> history;   // errors of earlier attempts must survive the rebuild
    auto remember = [&](const Report& r) {
        for (const auto& m : r.messages) {
            bool dup = false;
            for (const auto& h : history) if (h.level == m.level && h.phase == m.phase && h.mod == m.mod && h.text == m.text) dup = true;
            if (!dup) history.push_back(m);
        }
    };
    for (int attempt = 0; attempt < 32; ++attempt) {
        bool ok = build(excluded, save, rep, failed);
        remember(rep);
        if (ok) break;
        size_t before = excluded.size();
        excluded.insert(failed.begin(), failed.end());
        if (excluded.size() == before) break;  // errors not attributable to a mod (e.g. base catalog)
    }
    for (const auto& kv : rep.rejected) excluded.insert(kv.first);
    for (const auto& id : excluded)
        if (!rep.rejected.count(id)) rep.rejected[id].push_back("gagal pada fase pemuatan");
    rep.messages = history;
    report_ = rep;
    bool clean = failed.empty() && excluded.empty() && !report_.hasErrors();
    // publish the allocation result into the save and record which mods were active
    if (save && failed.empty()) {
        save->ids = lastIds_;
        std::vector<SaveModEntry> now = loadedMods();
        // mods missing now stay recorded so their data and handles survive
        for (const auto& old : save->mods) {
            bool present = false;
            for (const auto& n : now) if (n.id == old.id) present = true;
            if (!present) now.push_back(old);
        }
        save->mods = now;
    }
    return clean;
}

std::vector<SaveModEntry> Session::loadedMods() const {
    std::vector<SaveModEntry> v;
    for (const auto& id : report_.loaded) {
        const Manifest* m = manifest(id);
        if (m) v.push_back({m->id, m->versionText, m->saveVersion});
    }
    return v;
}

HookReport Session::fire(HookCtx& ctx) {
    if (!ctx.args.has("locale")) ctx.args["locale"] = info_.locale;
    return bus_.fire(ctx);
}

std::string Session::orphanDescription(const std::string& kind, int32_t handle) const {
    for (const auto& k : lastIds_.ids) {
        if (k.first != kind) continue;
        for (const auto& e : k.second)
            if (e.second == handle) return e.first;
    }
    return "";
}

}  // namespace nm
