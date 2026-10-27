#include "capi.h"

#include <cstdlib>
#include <cstring>

#include "session.h"

using namespace nm;

namespace {
class CHost : public HostApi {
public:
    NmHost h{};
    bool giveItem(const std::string& i, int n) override { return h.give_item && h.give_item(h.user, i.c_str(), n) == 0; }
    bool removeItem(const std::string& i, int n) override { return h.remove_item && h.remove_item(h.user, i.c_str(), n) == 0; }
    int countItem(const std::string& i) override { return h.count_item ? h.count_item(h.user, i.c_str()) : 0; }
    void showText(const std::string& t) override { if (h.show_text) h.show_text(h.user, t.c_str()); }
    void addStamina(int a) override { if (h.add_stamina) h.add_stamina(h.user, a); }
    void addMoney(int a) override { if (h.add_money) h.add_money(h.user, a); }
    int money() override { return h.money ? h.money(h.user) : 0; }
    bool warp(const std::string& a, const std::string& e) override { return h.warp && h.warp(h.user, a.c_str(), e.c_str()) == 0; }
    int day() override { return h.day ? h.day(h.user) : 0; }
    uint32_t random(uint32_t n) override { return h.random ? h.random(h.user, n) : 0; }
    void log(int l, const std::string& m) override { if (h.log) h.log(h.user, l, m.c_str()); }
};
char* dupStr(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}
}  // namespace

struct NmSession {
    CHost host;
    SaveExtension save;
    ApprovalStore approvals;
    // Destroy the session/plugins before the host and save state they may use on shutdown.
    std::unique_ptr<Session> s;
    std::string report, scratch;
    std::vector<std::string> modDirs;
    bool loaded = false;
};

extern "C" {

NmSession* nm_create(const char* game_id, const char* exe_sha256, const char* catalog, const char* locale) {
    try {
        auto n = std::make_unique<NmSession>();
        HostInfo hi;
        if (game_id) hi.gameId = game_id;
        if (exe_sha256) hi.exeSha256 = exe_sha256;
        if (locale) hi.locale = locale;
        n->s = std::make_unique<Session>(hi, Json::parse(catalog ? catalog : "{}"));
        n->s->setHostApi(&n->host);
        return n.release();
    } catch (...) { return nullptr; }
}

void nm_destroy(NmSession* n) { delete n; }

void nm_set_host(NmSession* n, const NmHost* h) { if (n && h) n->host.h = *h; }

int nm_add_mod_dir(NmSession* n, const char* dir) {
    if (!n || !dir) return -1;
    auto before = n->s->preflight().messages.size();
    n->s->addPack(std::make_shared<DirPackSource>(dir));
    return n->s->preflight().messages.size() > before ? 1 : 0;
}

void nm_approve_plugin(NmSession* n, const char* mod, const char* ver, const char* sha) {
    if (!n || !mod || !ver || !sha) return;
    n->approvals.approve(mod, ver, sha);
    n->s->setApprovals(n->approvals);
}

int nm_load(NmSession* n, const char* save_json, char** save_out) {
    if (save_out) *save_out = nullptr;
    if (!n) return -1;
    n->loaded = false;
    try {
        std::string extra;
        SaveExtension in;
        bool haveSave = false;
        if (save_json && *save_json) {
            std::string err;
            try {
                if (SaveExtension::fromJson(Json::parse(save_json), in, err)) haveSave = true;
                else extra += "[ERROR] save mod ditolak (data asli dipertahankan oleh pemanggil): " + err + "\n";
            } catch (const JsonError& e) { extra += std::string("[ERROR] save mod rusak: ") + e.what() + "\n"; }
        }
        n->save = haveSave ? in : SaveExtension();
        SaveExtension prev = n->save;
        const bool clean = n->s->load(&n->save);
        if (haveSave) {
            SaveCompat c = checkSave(prev, n->s->loadedMods());
            extra += c.describe();
        }
        n->report = n->s->report().text() + extra;
        const int rejected = static_cast<int>(n->s->report().rejected.size());
        // Global failures (catalog, unreadable manifest) have no rejected mod ID.
        bool globalFailure = false;
        for (const auto& message : n->s->report().messages)
            if (message.level == Message::Error && message.mod.empty()) globalFailure = true;
        if (globalFailure || (!clean && rejected == 0)) return -1;
        if (save_out) {
            *save_out = dupStr(n->save.toJson().dump());
            if (!*save_out) { n->report += "[ERROR] alokasi save mod gagal\n"; return -1; }
        }
        n->loaded = true;
        return rejected;
    } catch (const std::exception& e) {
        n->report = std::string("[ERROR] sesi mod gagal dimuat: ") + e.what() + "\n";
    } catch (...) {
        n->report = "[ERROR] sesi mod gagal dimuat\n";
    }
    return -1;
}

int nm_save(NmSession* n, char** out) {
    if (!n || !out || !n->loaded) return -1;
    HookCtx ctx;
    ctx.hook = "save.before";
    n->s->fire(ctx);
    *out = dupStr(n->save.toJson().dump());
    return *out ? 0 : -1;
}

const char* nm_faults(NmSession* n) {
    if (!n) return "";
    n->scratch.clear();
    for (const auto& l : n->s->hooks().log()) n->scratch += l + "\n";
    return n->scratch.c_str();
}

const char* nm_report(NmSession* n) { return n ? n->report.c_str() : ""; }
void nm_free(void* p) { std::free(p); }

int32_t nm_handle(NmSession* n, const char* kind, const char* id) {
    if (!n || !kind || !id) return -1;
    const Record* r = n->s->registry().find(id);
    return (r && r->kind == kind) ? r->handle : -1;
}

const char* nm_id(NmSession* n, const char* kind, int32_t handle) {
    if (!n || !kind) return nullptr;
    const Record* r = n->s->registry().byHandle(kind, handle);
    return r ? r->id.c_str() : nullptr;
}

int32_t nm_kind_count(NmSession* n, const char* kind) {
    if (!n || !kind) return 0;
    int32_t hi = n->s->registry().baseCount(kind);
    for (const Record* r : n->s->registry().all(kind)) if (r->handle + 1 > hi) hi = r->handle + 1;
    return hi;
}

int nm_item_row(NmSession* n, int32_t handle, NmItemRow* out) {
    if (!n || !out) return -1;
    const Record* r = n->s->registry().byHandle("item", handle);
    if (!r) return -1;
    std::memset(out, 0, sizeof *out);
    out->handle = handle;
    out->inherit_handle = -1;
    if (r->owner == "base") return 0;
    const Json& d = r->data;
    if (const Json* inh = d.find("inherit")) {
        const Record* p = n->s->registry().find(inh->asString());
        if (p) out->inherit_handle = p->handle;
    }
    out->buy_price = static_cast<int16_t>(d["buy_price"].asInt(0));
    out->sell_price = static_cast<int16_t>(d["sell_price"].asInt(0));
    out->stamina = static_cast<int8_t>(d["stamina"].asInt(0));
    out->fatigue = static_cast<int8_t>(d["fatigue"].asInt(0));
    static const char* cats[] = {"crop", "tool", "seed", "consumable", "object"};
    for (uint8_t i = 0; i < 5; ++i) if (d["category"].asString() == cats[i]) out->category = i;
    for (const auto& f : d["flags"].items()) {
        if (f.asString() == "pickup") out->flags |= 0x8000;
        if (f.asString() == "tool") out->flags |= 0x4000;
        if (f.asString() == "seed") out->flags |= 0x40;
    }
    std::strncpy(out->sprite_asset, d["sprite"].asString().c_str(), sizeof out->sprite_asset - 1);
    return 0;
}

const char* nm_name(NmSession* n, const char* id) {
    if (!n || !id) return "";
    const Record* r = n->s->registry().find(id);
    if (!r) return "";
    n->scratch = Registry::localized(r->data["name"], n->s->host().locale);
    return n->scratch.c_str();
}

const char* nm_record_json(NmSession* n, const char* id) {
    if (!n || !id) return "";
    const Record* r = n->s->registry().find(id);
    n->scratch = r ? r->data.dump() : "";
    return n->scratch.c_str();
}

int32_t nm_record_count(NmSession* n, const char* kind) {
    return (n && kind) ? static_cast<int32_t>(n->s->registry().all(kind).size()) : 0;
}

const char* nm_record_id(NmSession* n, const char* kind, int32_t index) {
    if (!n || !kind || index < 0) return nullptr;
    auto v = n->s->registry().all(kind);
    return static_cast<size_t>(index) < v.size() ? v[static_cast<size_t>(index)]->id.c_str() : nullptr;
}

const char* nm_orphan(NmSession* n, const char* kind, int32_t handle) {
    if (!n || !kind) return "";
    n->scratch = n->s->orphanDescription(kind, handle);
    return n->scratch.c_str();
}

int nm_asset_override(NmSession* n, const char* base_id, char* out, size_t cap, int* scale, int* px, int* py) {
    if (!n || !base_id) return -1;
    const OverrideInfo* o = n->s->vfs().overrideFor(base_id);
    if (!o) return -1;
    if (out && cap) { std::strncpy(out, o->asset.c_str(), cap - 1); out[cap - 1] = 0; }
    if (scale) *scale = o->scale;
    if (px) *px = o->pivotX;
    if (py) *py = o->pivotY;
    return 0;
}

int64_t nm_asset_read(NmSession* n, const char* logical, void* buf, size_t cap) {
    if (!n || !logical) return -1;
    std::string data;
    if (!n->s->vfs().read(logical, data)) return -1;
    if (buf && cap >= data.size()) std::memcpy(buf, data.data(), data.size());
    return static_cast<int64_t>(data.size());
}

int nm_fire(NmSession* n, const char* hook, const char* subject, const char* args_json, char* result, size_t cap) {
    if (!n || !hook) return 0;
    HookCtx ctx;
    ctx.hook = hook;
    ctx.subject = subject ? subject : "";
    if (args_json && *args_json) {
        try { Json a = Json::parse(args_json); if (a.isObject()) ctx.args = a; } catch (const JsonError&) {}
    }
    HookReport rep = n->s->fire(ctx);
    if (result && cap) {
        std::string r = ctx.result.dump();
        if (r.size() + 1 <= cap) std::memcpy(result, r.c_str(), r.size() + 1);
        else result[0] = 0;
    }
    return rep.cancelled ? 1 : 0;
}

}  // extern "C"
