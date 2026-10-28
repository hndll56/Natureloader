#include "native_items.h"

#include <algorithm>
#include <limits>

#include "util.h"

namespace nm {

// ------------------------------------------------------------------ NativeItems
ItemHandle NativeItems::resolve(const std::string& anyId, std::string* err) const {
    auto fail = [&](const std::string& m) { if (err) *err = m; return ItemHandle(); };
    std::string canonical, kind, why;
    if (!canonicalContentId(anyId, canonical, &kind, why)) return fail(why);
    const std::string aliasPrefix = std::string(kVanillaAliasPrefix) + ":";
    if (anyId.compare(0, aliasPrefix.size(), aliasPrefix) == 0) {
        // A real mod named "vanilla" would make this prefix ambiguous: refuse aliases instead of guessing.
        const auto& loaded = s_->report().loaded;
        if (std::find(loaded.begin(), loaded.end(), std::string(kVanillaAliasPrefix)) != loaded.end())
            return fail("alias vanilla dinonaktifkan: ada mod bernama 'vanilla'");
        if (kind != "item") return fail("'" + anyId + "' bukan item (jenis " + kind + ")");
    }
    const Record* r = s_->registry().find(canonical);
    if (!r) return fail("item tidak ada: " + canonical);
    if (r->kind != "item") return fail(canonical + " bertipe " + r->kind + ", bukan item");
    if (r->handle < 0) return fail("item belum memiliki handle: " + canonical);
    return ItemHandle(r->handle);
}

bool NativeItems::info(ItemHandle h, ItemInfo& out) const {
    if (!h.valid()) return false;
    const Record* r = s_->registry().byHandle("item", h.v_);
    if (!r) return false;
    ItemInfo i;
    i.handle = h;
    i.id = r->id;
    i.owner = r->owner;
    i.vanilla = r->owner == "base";
    i.name = Registry::localized(r->data["name"], s_->host().locale);
    if (i.name.empty()) i.name = r->id;
    i.description = Registry::localized(r->data["description"], s_->host().locale);
    i.category = r->data["category"].asString();
    i.sprite = r->data["sprite"].asString();
    i.buyPrice = static_cast<int>(r->data["buy_price"].asInt());
    i.sellPrice = static_cast<int>(r->data["sell_price"].asInt());
    i.stamina = static_cast<int>(r->data["stamina"].asInt());
    i.fatigue = static_cast<int>(r->data["fatigue"].asInt());
    for (const auto& f : r->data["flags"].items()) if (f.isString()) i.flags.push_back(f.asString());
    i.hasUseHook = r->data["hooks"].isObject() && r->data["hooks"]["use"].isString();
    out = i;
    return true;
}

std::string NativeItems::idOf(ItemHandle h) const {
    ItemInfo i;
    return info(h, i) ? i.id : std::string();
}

std::vector<ItemHandle> NativeItems::modItems() const {
    std::vector<ItemHandle> v;
    for (const Record* r : s_->registry().all("item"))
        if (r->owner != "base" && r->handle >= 0) v.push_back(ItemHandle(r->handle));
    return v;
}

bool NativeItems::loadSprite(ItemHandle h, std::string& bytes) const {
    ItemInfo i;
    if (!info(h, i) || i.sprite.empty()) return false;
    return s_->vfs().read(i.sprite, bytes);
}

// ------------------------------------------------------------------ NativeInventory
const NativeInventory::Slot* NativeInventory::find(const std::string& id) const {
    for (const auto& s : slots_) if (s.id == id) return &s;
    return nullptr;
}

int NativeInventory::count(const std::string& id) const {
    const Slot* s = find(id);
    return s ? s->count : 0;
}

void NativeInventory::add(const std::string& id, int count) {
    for (auto& s : slots_) if (s.id == id) { s.count += count; return; }
    Slot s;
    s.id = id; s.count = count; s.seq = nextSeq_++;
    slots_.push_back(std::move(s));
}

void NativeInventory::remove(const std::string& id, int count) {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->id != id) continue;
        it->count -= count;
        if (it->count <= 0) slots_.erase(it);
        return;
    }
}

bool NativeInventory::setState(const std::string& id, const Json& state) {
    for (auto& s : slots_) if (s.id == id) { s.state = state; return true; }
    return false;
}

void NativeInventory::restore(std::vector<Slot> slots, int reserved) {
    std::stable_sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
        return a.seq != b.seq ? a.seq < b.seq : a.id < b.id;
    });
    nextSeq_ = 1;
    for (const auto& s : slots) nextSeq_ = std::max(nextSeq_, s.seq + 1);
    slots_ = std::move(slots);
    reserved_ = reserved;
}

const char* itemCodeName(ItemCode c) {
    switch (c) {
        case ItemCode::Ok: return "ok";
        case ItemCode::Cancelled: return "cancelled";
        case ItemCode::UnknownItem: return "unknown_item";
        case ItemCode::Unavailable: return "unavailable";
        case ItemCode::VanillaNotBridged: return "vanilla_not_bridged";
        case ItemCode::InvalidCount: return "invalid_count";
        case ItemCode::InventoryFull: return "inventory_full";
        case ItemCode::StackFull: return "stack_full";
        case ItemCode::NotForSale: return "not_for_sale";
        case ItemCode::NotEnoughMoney: return "not_enough_money";
        case ItemCode::NotEnoughItems: return "not_enough_items";
        case ItemCode::NotInInventory: return "not_in_inventory";
        case ItemCode::NotUsable: return "not_usable";
        case ItemCode::BadSlot: return "bad_slot";
        case ItemCode::NothingSelected: return "nothing_selected";
        case ItemCode::BadState: return "bad_state";
    }
    return "?";
}

// ------------------------------------------------------------------ host bridge
bool NativeItemRuntime::Bridge::giveItem(const std::string& id, int count) { return rt.acquire(id, count).ok(); }
bool NativeItemRuntime::Bridge::removeItem(const std::string& id, int count) { return rt.remove(id, count).ok(); }
int NativeItemRuntime::Bridge::countItem(const std::string& id) {
    std::string canonical, why;
    return canonicalContentId(id, canonical, nullptr, why) ? rt.inv_.count(canonical) : 0;
}
void NativeItemRuntime::Bridge::showText(const std::string& text) { rt.messages_.push_back(text); }
void NativeItemRuntime::Bridge::addStamina(int amount) { rt.player_.addStamina(amount); }
void NativeItemRuntime::Bridge::addMoney(int amount) { rt.player_.addMoney(amount); }
int NativeItemRuntime::Bridge::money() { return rt.player_.money(); }
int NativeItemRuntime::Bridge::day() { return rt.player_.day(); }
uint32_t NativeItemRuntime::Bridge::random(uint32_t n) {
    rng = rng * 6364136223846793005ull + 1442695040888963407ull;  // deterministic: same seed, same sequence
    return n == 0 ? 0 : static_cast<uint32_t>(rng >> 33) % n;
}
void NativeItemRuntime::Bridge::log(int level, const std::string& msg) {
    rt.messages_.push_back("[log " + std::to_string(level) + "] " + msg);
}

// ------------------------------------------------------------------ runtime
NativeItemRuntime::NativeItemRuntime(Session& session, PlayerPort& player, int slots)
    : session_(session), player_(player), items_(session), inv_(slots), bridge_(*this) {
    session_.setHostApi(&bridge_);
}

NativeItemRuntime::~NativeItemRuntime() { session_.setHostApi(nullptr); }

void NativeItemRuntime::dropStaleSelection() {
    if (!selectedId_.empty() && !inv_.find(selectedId_)) selectedId_.clear();
}

ItemOutcome NativeItemRuntime::checkAdd(const std::string& anyId, int count, ItemInfo& info) const {
    ItemOutcome o;
    if (count < 1 || count > NativeInventory::kMaxStack) {
        o.code = ItemCode::InvalidCount;
        o.message = "jumlah harus 1.." + std::to_string(NativeInventory::kMaxStack);
        return o;
    }
    std::string err;
    const ItemHandle h = items_.resolve(anyId, &err);
    if (!h.valid()) { o.code = ItemCode::UnknownItem; o.message = err; return o; }
    if (!items_.info(h, info)) { o.code = ItemCode::Unavailable; o.message = "item tidak tersedia: " + anyId; return o; }
    if (info.vanilla) {
        o.code = ItemCode::VanillaNotBridged;
        o.message = info.id + " adalah item retail; inventory retail ada di memori PS1 dan belum dijembatani";
        return o;
    }
    if (const auto* s = inv_.find(info.id)) {
        if (s->count + count > NativeInventory::kMaxStack) {
            o.code = ItemCode::StackFull;
            o.message = "tumpukan " + info.id + " melebihi " + std::to_string(NativeInventory::kMaxStack);
        }
    } else if (!inv_.hasRoomForNewStack()) {
        o.code = ItemCode::InventoryFull;
        o.message = "inventory penuh (" + std::to_string(inv_.usedSlots()) + "/" + std::to_string(inv_.capacity()) + ")";
    }
    return o;
}

ItemOutcome NativeItemRuntime::acquire(const std::string& id, int count) {
    ItemInfo info;
    ItemOutcome o = checkAdd(id, count, info);
    if (!o.ok()) return o;
    inv_.add(info.id, count);
    return o;
}

ItemOutcome NativeItemRuntime::remove(const std::string& id, int count) {
    ItemOutcome o;
    if (count < 1 || count > NativeInventory::kMaxStack) { o.code = ItemCode::InvalidCount; o.message = "jumlah tidak valid"; return o; }
    std::string canonical, why;
    if (!canonicalContentId(id, canonical, nullptr, why)) { o.code = ItemCode::UnknownItem; o.message = why; return o; }
    if (!inv_.find(canonical)) { o.code = ItemCode::NotInInventory; o.message = canonical + " tidak ada di inventory"; return o; }
    if (inv_.count(canonical) < count) { o.code = ItemCode::NotEnoughItems; o.message = "jumlah " + canonical + " tidak cukup"; return o; }
    inv_.remove(canonical, count);
    dropStaleSelection();
    return o;
}

ItemOutcome NativeItemRuntime::buy(const std::string& id, int count) {
    ItemInfo info;
    ItemOutcome o = checkAdd(id, count, info);
    if (!o.ok()) return o;
    if (info.buyPrice <= 0) { o.code = ItemCode::NotForSale; o.message = info.id + " tidak dijual (buy_price <= 0)"; return o; }
    const int64_t total = static_cast<int64_t>(info.buyPrice) * count;  // <= 32767 * 99, fits int
    if (player_.money() < total) { o.code = ItemCode::NotEnoughMoney; o.message = "uang tidak cukup"; return o; }

    HookCtx ctx;
    ctx.hook = "item.buy";
    ctx.subject = info.id;
    ctx.args["count"] = count;
    ctx.args["price"] = info.buyPrice;
    ctx.args["total"] = static_cast<int64_t>(total);
    const HookReport rep = session_.fire(ctx);
    o.hookFaults = rep.faults;
    if (rep.cancelled) { o.code = ItemCode::Cancelled; o.message = "pembelian dibatalkan oleh mod"; return o; }

    // A handler may have changed money or inventory: validate again before charging.
    ItemInfo again;
    ItemOutcome re = checkAdd(info.id, count, again);
    if (!re.ok()) { re.hookFaults = rep.faults; return re; }
    if (!player_.spend(static_cast<int>(total))) { o.code = ItemCode::NotEnoughMoney; o.message = "uang tidak cukup"; return o; }
    inv_.add(info.id, count);
    o.moneySpent = static_cast<int>(total);
    return o;
}

ItemOutcome NativeItemRuntime::select(int slot) {
    ItemOutcome o;
    const auto& s = inv_.slots();
    if (slot < 0 || slot >= static_cast<int>(s.size())) { o.code = ItemCode::BadSlot; o.message = "slot di luar rentang"; return o; }
    selectedId_ = s[static_cast<size_t>(slot)].id;
    return o;
}

int NativeItemRuntime::selectedSlot() const {
    if (selectedId_.empty()) return -1;
    const auto& s = inv_.slots();
    for (size_t i = 0; i < s.size(); ++i) if (s[i].id == selectedId_) return static_cast<int>(i);
    return -1;
}

ItemOutcome NativeItemRuntime::useSelected() {
    const int slot = selectedSlot();
    if (slot < 0) { ItemOutcome o; o.code = ItemCode::NothingSelected; o.message = "tidak ada item terpilih"; return o; }
    return use(slot);
}

// Semantics (documented in docs/NATIVE_ITEM_RUNTIME.md): the item.use hook runs first. A handler
// that cancels takes over completely (no default effect, nothing consumed). Otherwise the default
// behaviour applies the item's stamina/fatigue fields and consumes one unit. Handler faults are
// isolated and reported but never block the default behaviour.
ItemOutcome NativeItemRuntime::use(int slot) {
    ItemOutcome o;
    const auto& slots = inv_.slots();
    if (slot < 0 || slot >= static_cast<int>(slots.size())) { o.code = ItemCode::BadSlot; o.message = "slot di luar rentang"; return o; }
    const std::string id = slots[static_cast<size_t>(slot)].id;
    const int countBefore = slots[static_cast<size_t>(slot)].count;
    ItemInfo info;
    const ItemHandle h = items_.resolve(id);
    if (!h.valid() || !items_.info(h, info)) { o.code = ItemCode::Unavailable; o.message = id + " tidak tersedia (mod tidak aktif?)"; return o; }
    if (info.vanilla) { o.code = ItemCode::VanillaNotBridged; o.message = "item retail belum dijembatani"; return o; }
    if (!info.hasUseHook && info.stamina == 0 && info.fatigue == 0) { o.code = ItemCode::NotUsable; o.message = id + " tidak punya efek pakai"; return o; }

    const int stamina0 = player_.stamina(), fatigue0 = player_.fatigue();
    HookCtx ctx;
    ctx.hook = "item.use";
    ctx.subject = id;
    ctx.args["item"] = id;
    ctx.args["slot"] = slot;
    ctx.args["count"] = countBefore;
    const HookReport rep = session_.fire(ctx);
    o.hookFaults = rep.faults;
    if (rep.cancelled) {
        o.code = ItemCode::Cancelled;
        o.message = "dibatalkan oleh mod; efek bawaan dilewati";
    } else {
        player_.addStamina(info.stamina);
        player_.addFatigue(info.fatigue);
        if (inv_.count(id) > 0) { inv_.remove(id, 1); o.consumed = true; }
    }
    o.staminaDelta = player_.stamina() - stamina0;
    o.fatigueDelta = player_.fatigue() - fatigue0;
    dropStaleSelection();
    return o;
}

ItemOutcome NativeItemRuntime::setState(const std::string& id, const Json& state) {
    ItemOutcome o;
    std::string canonical, why;
    if (!canonicalContentId(id, canonical, nullptr, why)) { o.code = ItemCode::UnknownItem; o.message = why; return o; }
    if (!state.isObject() || state.dump().size() > 4096) { o.code = ItemCode::BadState; o.message = "state harus objek <= 4096 byte"; return o; }
    if (!inv_.setState(canonical, state)) { o.code = ItemCode::NotInInventory; o.message = canonical + " tidak ada di inventory"; return o; }
    return o;
}

// ------------------------------------------------------------------ native state
namespace {
enum class DocClass { Usable, Damaged, BadShape, Newer, Inactive };

DocClass classify(const ModStateFile& f, bool modLoaded, const Manifest* m) {
    if (f.damaged) return DocClass::Damaged;
    const Json& d = f.doc;
    if (!d.isObject() || !d["format"].isInt() || d["format"].asInt() != 1 || !d["mod"].isString() ||
        d["mod"].asString() != f.mod || !d["items"].isObject())
        return DocClass::BadShape;
    if (!modLoaded || !m) return DocClass::Inactive;
    if (d["save_version"].isInt() && d["save_version"].asInt() > m->saveVersion) return DocClass::Newer;
    return DocClass::Usable;
}
}  // namespace

bool NativeItemRuntime::loadState(const NativeSaveStore& store, std::vector<std::string>& notes) {
    std::vector<ModStateFile> files;
    if (!store.scanMods(files, notes)) return false;
    inv_.clear();
    preserved_.clear();
    selectedId_.clear();
    const auto& loadedList = session_.report().loaded;
    const std::set<std::string> loaded(loadedList.begin(), loadedList.end());

    std::vector<NativeInventory::Slot> live;
    int reserved = 0;
    for (const auto& f : files) {
        const bool isLoaded = loaded.count(f.mod) != 0;
        const Manifest* m = isLoaded ? session_.manifest(f.mod) : nullptr;
        const int entries = f.doc["items"].isObject() ? static_cast<int>(f.doc["items"].size()) : 0;
        switch (classify(f, isLoaded, m)) {
            case DocClass::Damaged:
                notes.push_back("mods/" + f.mod + ".json rusak (" + f.problem + "); tidak dimuat dan tidak diubah");
                continue;
            case DocClass::BadShape:
                notes.push_back("mods/" + f.mod + ".json bukan format state yang dikenal; tidak dimuat dan tidak diubah");
                continue;
            case DocClass::Inactive:
                reserved += entries;
                notes.push_back("mod '" + f.mod + "' tidak aktif; " + std::to_string(entries) + " entri item dipertahankan");
                continue;
            case DocClass::Newer:
                reserved += entries;
                notes.push_back("mods/" + f.mod + ".json dibuat dengan save_version lebih baru; dibaca-saja, tidak diubah");
                continue;
            case DocClass::Usable: break;
        }
        if (f.doc["save_version"].isInt() && f.doc["save_version"].asInt() < m->saveVersion)
            notes.push_back("mods/" + f.mod + ".json memakai save_version lebih lama; dibaca apa adanya (belum ada migrasi)");
        for (const auto& kv : f.doc["items"].members()) {
            const std::string& id = kv.first;
            const Json& e = kv.second;
            std::string mod, local;
            bool good = splitId(id, mod, local) && mod == f.mod && e.isObject() && e["count"].isInt() &&
                        e["count"].asInt() >= 1 && e["count"].asInt() <= NativeInventory::kMaxStack &&
                        (!e.has("state") || e["state"].isObject()) &&
                        (!e.has("seq") || (e["seq"].isInt() && e["seq"].asInt() >= 0));
            if (good) {
                const ItemHandle h = items_.resolve(id);
                ItemInfo info;
                good = h.valid() && items_.info(h, info) && !info.vanilla;
            }
            if (!good) {
                preserved_[f.mod][id] = e;
                ++reserved;
                notes.push_back("entri " + id + " dipertahankan tanpa dimuat (id tidak dikenal atau data tidak valid)");
                continue;
            }
            NativeInventory::Slot s;
            s.id = id;
            s.count = static_cast<int>(e["count"].asInt());
            s.state = e.has("state") ? e["state"] : Json::object();
            s.seq = static_cast<uint64_t>(e["seq"].asInt(0));
            live.push_back(std::move(s));
        }
    }
    inv_.restore(std::move(live), reserved);
    return true;
}

bool NativeItemRuntime::saveState(const NativeSaveStore& store, const SaveExtension& core, std::string& err) {
    std::vector<ModStateFile> files;
    std::vector<std::string> warnings;
    if (!store.scanMods(files, warnings)) { err = "Cannot read native mod save directory"; return false; }
    HookCtx before;
    before.hook = "save.before";
    session_.fire(before);

    std::string e;
    if (!store.saveCore(core, e)) { err = e; return false; }

    std::map<std::string, const ModStateFile*> onDisk;
    for (const auto& f : files) onDisk[f.mod] = &f;

    const auto& loadedList = session_.report().loaded;
    const std::set<std::string> loaded(loadedList.begin(), loadedList.end());
    std::map<std::string, std::vector<const NativeInventory::Slot*>> byOwner;
    for (const auto& s : inv_.slots()) byOwner[s.id.substr(0, s.id.find(':'))].push_back(&s);

    std::set<std::string> owners;
    for (const auto& kv : byOwner) owners.insert(kv.first);
    for (const auto& f : files) if (loaded.count(f.mod)) owners.insert(f.mod);

    bool allOk = true;
    std::string problems;
    auto problem = [&](const std::string& m) { allOk = false; problems += (problems.empty() ? "" : "; ") + m; };

    for (const std::string& owner : owners) {
        if (!loaded.count(owner)) continue;  // never write for a mod that is not loaded
        const Manifest* m = session_.manifest(owner);
        if (!m) continue;
        const auto fit = onDisk.find(owner);
        const ModStateFile* file = fit == onDisk.end() ? nullptr : fit->second;
        const auto stacks = byOwner.find(owner);
        const bool hasStacks = stacks != byOwner.end() && !stacks->second.empty();
        const DocClass cls = file ? classify(*file, true, m) : DocClass::Usable;
        bool fresh = false;
        if (cls == DocClass::Newer) {
            if (hasStacks) problem("mods/" + owner + ".json dibuat dengan save_version lebih baru; tidak ditimpa");
            continue;
        }
        if (cls == DocClass::Damaged || cls == DocClass::BadShape) {
            if (!hasStacks) continue;  // nothing new to store: leave the unreadable file exactly as it is
            std::string moved;
            if (!store.quarantineMod(owner, moved, e)) { problem(e); continue; }
            fresh = true;
        }
        Json doc = (file && !fresh && cls == DocClass::Usable) ? file->doc : Json::object();
        doc["format"] = 1;
        doc["mod"] = owner;
        doc["version"] = m->versionText;
        doc["save_version"] = m->saveVersion;
        Json items = Json::object();
        const auto pit = preserved_.find(owner);
        if (pit != preserved_.end()) for (const auto& kv : pit->second) items[kv.first] = kv.second;
        if (hasStacks)
            for (const auto* s : stacks->second) {
                Json entry = Json::object();
                entry["count"] = s->count;
                entry["seq"] = static_cast<int64_t>(s->seq);
                entry["state"] = s->state;
                items[s->id] = entry;
            }
        doc["items"] = items;
        if (file && !fresh && cls == DocClass::Usable && file->doc == doc) continue;  // unchanged: do not touch
        if (!file && items.size() == 0) continue;
        if (!store.writeMod(owner, doc, e)) { problem(e); continue; }
    }

    if (!allOk) { err = problems; return false; }
    HookCtx after;
    after.hook = "save.after";
    session_.fire(after);
    return true;
}

}  // namespace nm
