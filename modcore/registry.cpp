#include "registry.h"

#include <algorithm>

#include "util.h"

namespace nm {

// ------------------------------------------------------------------ schema tables
namespace {
enum class T { Str, Loc, Int, Bool, Ref, RefList, Asset, Enum, Obj, Arr };
struct F {
    const char* name; T type; bool required = false;
    int64_t lo = 0, hi = 0;             // Int range
    const char* refKind = nullptr;      // Ref/RefList
    const char* choices = nullptr;      // Enum / Arr-of-enum: comma separated
};
struct KindSpec { const char* kind; bool numeric; std::vector<F> fields; };

const std::vector<KindSpec>& specs() {
    static const std::vector<KindSpec> s = {
        {"item", true, {
            {"name", T::Loc, true}, {"description", T::Loc},
            {"category", T::Enum, false, 0, 0, nullptr, "crop,tool,seed,consumable,object"},
            {"buy_price", T::Int, false, -32768, 32767}, {"sell_price", T::Int, false, -32768, 32767},
            {"stamina", T::Int, false, -128, 127}, {"fatigue", T::Int, false, -128, 127},
            {"sprite", T::Asset}, {"inherit", T::Ref, false, 0, 0, "item"}, {"crop", T::Ref, false, 0, 0, "crop"},
            {"flags", T::Arr, false, 0, 0, nullptr, "pickup,tool,seed"}, {"hooks", T::Obj}, {"shippable", T::Bool}}},
        {"npc", true, {
            {"name", T::Loc, true}, {"sprite", T::Asset}, {"home_area", T::Ref, false, 0, 0, "area"},
            {"schedule", T::Ref, false, 0, 0, "schedule"}, {"dialog", T::Ref, false, 0, 0, "dialog"}, {"hooks", T::Obj}}},
        {"area", true, {
            {"name", T::Loc}, {"map", T::Asset, true}, {"collision", T::Asset}, {"music", T::Asset},
            {"entrances", T::Arr}, {"exits", T::Arr}, {"npcs", T::RefList, false, 0, 0, "npc"}, {"hooks", T::Obj}}},
        {"shop", true, {{"name", T::Loc}, {"owner", T::Ref, false, 0, 0, "npc"}, {"stock", T::Arr, true}}},
        {"crop", true, {
            {"name", T::Loc}, {"seed_item", T::Ref, true, 0, 0, "item"}, {"harvest_item", T::Ref, true, 0, 0, "item"},
            {"growth_days", T::Int, false, 1, 99}, {"stages", T::Int, false, 1, 8}, {"hooks", T::Obj}}},
        {"building", true, {{"name", T::Loc, true}, {"area", T::Ref, false, 0, 0, "area"}, {"sprite", T::Asset}, {"cost", T::Int, false, 0, 99999999}}},
        {"event", true, {{"name", T::Loc}, {"trigger", T::Obj}, {"actions", T::Arr}, {"hooks", T::Obj}}},
        {"recipe", true, {{"name", T::Loc}, {"ingredients", T::RefList, true, 0, 0, "item"}, {"result", T::Ref, true, 0, 0, "item"}}},
        {"dialog", false, {{"lines", T::Arr, true}}},
        {"schedule", false, {{"entries", T::Arr, true}}},
        {"sprite", false, {{"image", T::Asset, true}, {"frames", T::Int, false, 1, 256}, {"frame_w", T::Int, false, 1, 4096},
                           {"frame_h", T::Int, false, 1, 4096}, {"anim", T::Obj}}},
        {"asset_override", false, {{"target", T::Str, true}, {"file", T::Asset, true}, {"scale", T::Int, false, 1, 16}, {"pivot", T::Arr}}},
        {"language", false, {{"locale", T::Str, true}, {"name", T::Str}, {"strings", T::Obj, true}, {"font", T::Asset}}},
        {"rules", false, {{"values", T::Obj, true}}},
        {"ui_theme", false, {{"name", T::Loc}, {"colors", T::Obj}, {"font", T::Asset}}},
    };
    return s;
}
const KindSpec* specFor(const std::string& k) {
    for (const auto& s : specs()) if (k == s.kind) return &s;
    return nullptr;
}
bool inList(const std::string& v, const char* list) {
    std::string l = std::string(",") + list + ",";
    return l.find("," + v + ",") != std::string::npos;
}
}  // namespace

bool Registry::isKind(const std::string& k) { return specFor(k) != nullptr || k == "base"; }
bool Registry::isNumericKind(const std::string& k) { auto s = specFor(k); return s && s->numeric; }

const std::map<std::string, std::string>& Registry::hookFields(const std::string& kind) {
    static const std::map<std::string, std::map<std::string, std::string>> t = {
        {"item", {{"use", "item.use"}, {"on_buy", "item.buy"}, {"on_sell", "item.sell"}}},
        {"area", {{"on_enter", "area.enter"}, {"on_exit", "area.exit"}}},
        {"npc", {{"on_talk", "npc.talk"}, {"on_gift", "npc.gift"}}},
        {"crop", {{"on_harvest", "crop.harvest"}}},
        {"event", {{"on_run", "event.run"}}},
    };
    static const std::map<std::string, std::string> empty;
    auto it = t.find(kind);
    return it == t.end() ? empty : it->second;
}

std::string Registry::localized(const Json& v, const std::string& locale) {
    if (v.isString()) return v.asString();
    if (v.isObject()) {
        if (const Json* l = v.find(locale)) if (l->isString()) return l->asString();
        if (const Json* e = v.find("en")) if (e->isString()) return e->asString();
        for (const auto& kv : v.members()) if (kv.second.isString()) return kv.second.asString();
    }
    return "";
}

// ------------------------------------------------------------------ IdTable
Json IdTable::toJson() const {
    Json j = Json::object();
    Json ids_ = Json::object();
    for (const auto& k : ids) {
        Json m = Json::object();
        for (const auto& e : k.second) m[e.first] = static_cast<int64_t>(e.second);
        ids_[k.first] = m;
    }
    j["ids"] = ids_;
    Json nx = Json::object();
    for (const auto& n : next) nx[n.first] = static_cast<int64_t>(n.second);
    j["next"] = nx;
    return j;
}

bool IdTable::fromJson(const Json& j, IdTable& out, std::string& err) {
    IdTable t;
    if (!j.isObject() || !j["ids"].isObject() || !j["next"].isObject()) { err = "idtable: struktur tidak valid"; return false; }
    for (const auto& k : j["ids"].members()) {
        if (!Registry::isNumericKind(k.first) || !k.second.isObject()) { err = "idtable: jenis tidak valid " + k.first; return false; }
        std::set<int32_t> seen;
        for (const auto& e : k.second.members()) {
            std::string m, l;
            if (!splitId(e.first, m, l) || !e.second.isInt() || e.second.asInt() < 0 || e.second.asInt() > 0xFFFF) {
                err = "idtable: entri tidak valid " + e.first; return false;
            }
            if (!seen.insert(static_cast<int32_t>(e.second.asInt())).second) { err = "idtable: handle ganda untuk " + e.first; return false; }
            t.ids[k.first][e.first] = static_cast<int32_t>(e.second.asInt());
        }
    }
    for (const auto& n : j["next"].members()) {
        if (!n.second.isInt() || n.second.asInt() < 0 || n.second.asInt() > 0x10000) { err = "idtable: next tidak valid"; return false; }
        t.next[n.first] = static_cast<int32_t>(n.second.asInt());
    }
    out = t;
    return true;
}

// ------------------------------------------------------------------ Registry
bool Registry::loadBase(const Json& catalog, std::string& err) {
    if (!catalog["kinds"].isObject()) { err = "katalog dasar tidak valid"; return false; }
    for (const auto& k : catalog["kinds"].members()) {
        if (!isNumericKind(k.first)) { err = "jenis katalog dasar tidak dikenal: " + k.first; return false; }
        KindRange r;
        r.retailCount = static_cast<int32_t>(k.second["retail_count"].asInt());
        r.modFirst = static_cast<int32_t>(k.second["mod_first"].asInt());
        r.modLast = static_cast<int32_t>(k.second["mod_last"].asInt());
        ranges_[k.first] = r;
        for (const auto& n : k.second["names"].members()) {
            int32_t h = static_cast<int32_t>(std::stoi(n.first));
            Record rec;
            rec.id = "base:" + n.second.asString();
            rec.kind = k.first;
            rec.owner = "base";
            rec.handle = h;
            rec.data = Json::object();
            if (records_.count(rec.id)) continue;  // aliases: first wins
            byHandle_[k.first][h] = rec.id;
            order_[k.first].push_back(rec.id);
            records_[rec.id] = rec;
        }
    }
    return true;
}

int32_t Registry::baseCount(const std::string& kind) const {
    auto it = ranges_.find(kind);
    return it == ranges_.end() ? 0 : it->second.retailCount;
}

bool Registry::add(const std::string& owner, const std::string& kind, const std::string& local, const Json& data, std::string& err) {
    if (!specFor(kind)) { err = "jenis konten tidak dikenal: " + kind; return false; }
    if (!validLocalId(local)) { err = "id lokal tidak valid: '" + local + "'"; return false; }
    if (!data.isObject()) { err = owner + ":" + local + " harus objek"; return false; }
    std::string id = owner + ":" + local;
    if (records_.count(id)) { err = "id konten ganda: " + id; return false; }
    Record r{id, kind, owner, data, -1};
    records_[id] = r;
    order_[kind].push_back(id);
    return true;
}

bool Registry::patch(const std::string& owner, const std::string& target, const Json& fields, std::string& err) {
    auto it = records_.find(target);
    if (it == records_.end()) { err = "target patch tidak ada: " + target; return false; }
    if (!fields.isObject() || fields.size() == 0) { err = "patch untuk " + target + " harus objek tidak kosong"; return false; }
    if (it->second.owner == owner) { err = "mod tidak perlu mem-patch rekamannya sendiri: " + target; return false; }
    const KindSpec* sp = specFor(it->second.kind);
    for (const auto& kv : fields.members()) {
        bool ok = false;
        for (const auto& f : sp->fields) if (kv.first == f.name) ok = true;
        if (!ok) { err = "patch " + target + ": field tidak dikenal '" + kv.first + "'"; return false; }
    }
    for (const auto& kv : fields.members()) {
        std::string key = target + "#" + kv.first;
        auto prev = fieldOwner_.find(key);
        patches_.push_back({target, kv.first, prev == fieldOwner_.end() ? it->second.owner : prev->second, owner});
        fieldOwner_[key] = owner;
        it->second.data[kv.first] = kv.second;
    }
    return true;
}

namespace {
std::string needsRefAllowed(const std::string& owner, const std::string& ref, const std::set<std::string>& allowed) {
    std::string m, l;
    if (!splitId(ref, m, l)) return "referensi bukan id berformat mod:id: '" + ref + "'";
    if (m == "base" || m == owner || allowed.count(m)) return "";
    return "merujuk mod '" + m + "' tanpa mendeklarasikannya di depends/optional";
}
}  // namespace

std::map<std::string, std::vector<std::string>> Registry::validate(
    const std::map<std::string, std::set<std::string>>& allowedRefs, std::vector<std::string>* warnings) const {
    std::map<std::string, std::vector<std::string>> errs;
    assetRefs_.clear();
    static const std::set<std::string> none;
    for (const auto& kv : records_) {
        const Record& r = kv.second;
        if (r.owner == "base") continue;
        const KindSpec* sp = specFor(r.kind);
        auto ai = allowedRefs.find(r.owner);
        const std::set<std::string>& allowed = ai == allowedRefs.end() ? none : ai->second;
        auto E = [&](const std::string& m) { errs[r.owner].push_back(r.id + ": " + m); };
        auto checkRef = [&](const std::string& field, const Json& v, const char* kind) {
            if (!v.isString()) { E("'" + field + "' harus string id"); return; }
            std::string why = needsRefAllowed(r.owner, v.asString(), allowed);
            if (!why.empty()) { E(field + ": " + why); return; }
            const Record* t = find(v.asString());
            if (!t) { E(field + ": tujuan tidak ada: " + v.asString()); return; }
            if (kind && t->kind != kind) E(field + ": " + v.asString() + " bertipe " + t->kind + ", butuh " + kind);
        };
        auto checkAsset = [&](const std::string& field, const Json& v) {
            if (!v.isString()) { E("'" + field + "' harus string id aset"); return; }
            size_t c = v.asString().find(':');
            if (c == std::string::npos) { E("'" + field + "' harus berbentuk mod:path"); return; }
            std::string m2 = v.asString().substr(0, c);
            std::string why = needsRefAllowed(r.owner, m2 + ":x", allowed);
            if (!why.empty()) { E(field + ": " + why); return; }
            assetRefs_.push_back({r.owner, r.id, field, v.asString()});
        };
        for (const auto& f : sp->fields) {
            const Json* v = r.data.find(f.name);
            if (!v) { if (f.required) E(std::string("field wajib '") + f.name + "' hilang"); continue; }
            switch (f.type) {
                case T::Str: if (!v->isString() || v->asString().empty()) E(std::string("'") + f.name + "' harus string"); break;
                case T::Loc: {
                    bool ok = v->isString() && !v->asString().empty();
                    if (v->isObject() && v->size() > 0) { ok = true; for (const auto& e : v->members()) if (!e.second.isString()) ok = false; }
                    if (!ok) E(std::string("'") + f.name + "' harus string atau {locale: string}");
                    break;
                }
                case T::Int:
                    if (!v->isInt() || v->asInt() < f.lo || v->asInt() > f.hi)
                        E(std::string("'") + f.name + "' harus bilangan bulat " + std::to_string(f.lo) + ".." + std::to_string(f.hi) +
                          " (batas tipe data game)");
                    break;
                case T::Bool: if (!v->isBool()) E(std::string("'") + f.name + "' harus boolean"); break;
                case T::Ref: checkRef(f.name, *v, f.refKind); break;
                case T::RefList:
                    if (!v->isArray() || v->size() == 0) E(std::string("'") + f.name + "' harus array id tidak kosong");
                    else for (const auto& e : v->items()) checkRef(f.name, e, f.refKind);
                    break;
                case T::Asset: checkAsset(f.name, *v); break;
                case T::Enum: if (!v->isString() || !inList(v->asString(), f.choices)) E(std::string("'") + f.name + "' harus salah satu dari: " + f.choices); break;
                case T::Obj: if (!v->isObject()) E(std::string("'") + f.name + "' harus objek"); break;
                case T::Arr:
                    if (!v->isArray()) { E(std::string("'") + f.name + "' harus array"); break; }
                    if (f.choices) for (const auto& e : v->items()) if (!e.isString() || !inList(e.asString(), f.choices)) E(std::string("'") + f.name + "' berisi nilai tidak valid");
                    break;
            }
        }
        if (warnings)
            for (const auto& m : r.data.members()) {
                bool known = false;
                for (const auto& f : sp->fields) if (m.first == f.name) known = true;
                if (!known) warnings->push_back(r.id + ": field tidak dikenal '" + m.first + "' (diabaikan)");
            }
        // hooks: only known fields, handler ids must be mod:name
        if (const Json* h = r.data.find("hooks")) if (h->isObject()) {
            const auto& hf = hookFields(r.kind);
            for (const auto& e : h->members()) {
                if (!hf.count(e.first)) E("hook tidak dikenal untuk " + r.kind + ": " + e.first);
                else { std::string m, l; if (!e.second.isString() || !splitId(e.second.asString(), m, l)) E("handler hook '" + e.first + "' harus berbentuk mod:nama"); }
            }
        }
        // nested structures
        if (r.kind == "area") {
            std::set<std::string> ent;
            for (const auto& e : r.data["entrances"].items()) {
                if (!validLocalId(e["id"].asString()) || !e["x"].isNumber() || !e["y"].isNumber()) E("entrance butuh id,x,y");
                else if (!ent.insert(e["id"].asString()).second) E("entrance ganda: " + e["id"].asString());
            }
            for (const auto& e : r.data["exits"].items()) {
                if (e.has("to_section")) {
                    // exit into a retail area, addressed by its section number (no stable name exists yet)
                    auto rg = ranges_.find("area");
                    if (!e["to_section"].isInt() || e["to_section"].asInt() < 0 || (rg != ranges_.end() && e["to_section"].asInt() >= rg->second.retailCount))
                        E("exits.to_section di luar rentang area retail");
                } else checkRef("exits.to", e["to"], "area");
                if (e["to"].isString()) if (const Record* t = find(e["to"].asString())) {
                    if (t->owner != "base") {
                        bool found = false;
                        for (const auto& te : t->data["entrances"].items()) if (te["id"].asString() == e["entrance"].asString()) found = true;
                        if (!found) E("exit menuju entrance '" + e["entrance"].asString() + "' yang tidak ada di " + t->id);
                    }
                }
                if (e["rect"].size() != 4) E("exit butuh rect [x,y,w,h]");
            }
        }
        if (r.kind == "shop")
            for (const auto& e : r.data["stock"].items()) {
                checkRef("stock.item", e["item"], "item");
                if (e.has("price") && (!e["price"].isInt() || e["price"].asInt() < 0 || e["price"].asInt() > 32767)) E("stock.price di luar 0..32767");
            }
        if (r.kind == "asset_override") {
            const std::string& t = r.data["target"].asString();
            if (t.compare(0, 9, "base:tex/") != 0 && t.compare(0, 11, "base:sound/") != 0 && t.compare(0, 10, "base:font/") != 0)
                E("target override harus berawalan base:tex/, base:sound/ atau base:font/");
        }
    }
    return errs;
}

bool Registry::allocateHandles(const std::vector<std::string>& loadOrder, IdTable& table, std::string& err) {
    // Reserve what the save already knows (even for mods not loaded now).
    for (auto& k : table.ids) {
        auto rg = ranges_.find(k.first);
        if (rg == ranges_.end()) continue;
        for (const auto& e : k.second) {
            if (e.second < rg->second.modFirst || e.second > rg->second.modLast) { err = "handle " + e.first + " di luar rentang " + k.first; return false; }
            auto rec = records_.find(e.first);
            if (rec != records_.end() && rec->second.kind == k.first) {
                rec->second.handle = e.second;
                byHandle_[k.first][e.second] = e.first;
            }
        }
    }
    for (const auto& modId : loadOrder)
        for (const auto& kv : order_) {
            if (!isNumericKind(kv.first)) continue;
            auto rg = ranges_.find(kv.first);
            if (rg == ranges_.end()) continue;
            for (const auto& id : kv.second) {
                Record& r = records_[id];
                if (r.owner != modId || r.handle >= 0) continue;
                int32_t& nx = table.next[kv.first];
                if (nx < rg->second.modFirst) nx = rg->second.modFirst;
                // skip handles reserved by missing mods
                std::set<int32_t> used;
                for (const auto& e : table.ids[kv.first]) used.insert(e.second);
                while (used.count(nx)) ++nx;
                if (nx > rg->second.modLast) { err = "rentang handle " + kv.first + " habis"; return false; }
                r.handle = nx;
                table.ids[kv.first][id] = nx;
                byHandle_[kv.first][nx] = id;
                ++nx;
            }
        }
    return true;
}

const Record* Registry::find(const std::string& id) const {
    auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}
const Record* Registry::byHandle(const std::string& kind, int32_t handle) const {
    auto k = byHandle_.find(kind);
    if (k == byHandle_.end()) return nullptr;
    auto h = k->second.find(handle);
    if (h == k->second.end()) return nullptr;
    const Record* r = find(h->second);
    return (r && r->handle == handle) ? r : nullptr;
}
std::vector<const Record*> Registry::all(const std::string& kind) const {
    std::vector<const Record*> v;
    auto o = order_.find(kind);
    if (o == order_.end()) return v;
    for (const auto& id : o->second) v.push_back(&records_.at(id));
    return v;
}

}  // namespace nm
