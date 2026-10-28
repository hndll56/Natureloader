// Native item model: the modcore-side slice of the hybrid runtime.
//
// WHAT THIS IS: a headless, deterministic model of "a mod item exists, can be bought or
// acquired, sits in an inventory, can be selected and used, changes player state, and
// survives save/reload/disable/re-enable". It is built on the existing Session, Registry,
// IdTable, hook bus and save extension. It adds no second registry.
//
// WHAT THIS IS NOT: nothing here is connected to the PS1-compatible BTN runtime. There is no
// renderer, no on-screen inventory, no farm, and no retail save bridge. The only "player" is
// a PlayerPort, which tests implement with an in-memory ModelPlayer. Vanilla items are
// resolvable (ids and handles) but are refused by the inventory (ItemCode::VanillaNotBridged)
// because the retail inventory lives in PS1 guest memory and no bridge exists yet.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hooks.h"
#include "json.h"
#include "native_save.h"
#include "session.h"
#include "vanilla_ids.h"

namespace nm {

// Opaque item handle. It can only be obtained from NativeItems::resolve, it carries no public
// number, and it never holds a PS1 address. Its value is the registry handle from the persisted
// IdTable, so the same item id yields an equal handle across reloads, saves and mod re-enable.
// A handle whose mod is not loaded no longer resolves: NativeItems::info returns false.
class ItemHandle {
public:
    ItemHandle() = default;
    bool valid() const { return v_ >= 0; }
    friend bool operator==(ItemHandle a, ItemHandle b) { return a.v_ == b.v_; }
    friend bool operator!=(ItemHandle a, ItemHandle b) { return a.v_ != b.v_; }
    friend bool operator<(ItemHandle a, ItemHandle b) { return a.v_ < b.v_; }

private:
    explicit ItemHandle(int32_t v) : v_(v) {}
    int32_t v_ = -1;
    friend class NativeItems;
};

struct ItemInfo {
    ItemHandle handle;
    std::string id;          // canonical "mod:local" ("base:turnip" for retail)
    std::string owner;       // mod id or "base"
    std::string name, description, category, sprite;
    int buyPrice = 0, sellPrice = 0, stamina = 0, fatigue = 0;
    std::vector<std::string> flags;
    bool vanilla = false;    // retail item (owner "base")
    bool hasUseHook = false; // record declares hooks.use
};

// Read-only view over a loaded Session. Cheap to copy; re-reads the registry on every call so it
// stays valid across Session::load, but it must not outlive the Session.
class NativeItems {
public:
    explicit NativeItems(const Session& s) : s_(&s) {}
    // Accepts canonical ids and "vanilla:" aliases (see vanilla_ids.h).
    ItemHandle resolve(const std::string& anyId, std::string* err = nullptr) const;
    bool info(ItemHandle h, ItemInfo& out) const;
    std::string idOf(ItemHandle h) const;           // "" when unavailable
    std::vector<ItemHandle> modItems() const;       // registration order, retail excluded
    // Raw bytes of the item's sprite asset from the mod pack. Not decoded, not rendered.
    bool loadSprite(ItemHandle h, std::string& bytes) const;

private:
    const Session* s_;
};

// The gameplay side the item runtime acts on. The real bridge to BTN does not exist.
class PlayerPort {
public:
    virtual ~PlayerPort() = default;
    virtual int stamina() const = 0;
    virtual int fatigue() const = 0;
    virtual int money() const = 0;
    virtual int day() const = 0;
    virtual void addStamina(int amount) = 0;
    virtual void addFatigue(int amount) = 0;
    virtual void addMoney(int amount) = 0;
    virtual bool spend(int amount) = 0;  // false and no change when money < amount
};

// In-memory stand-in. Ranges are model constants, not retail values.
class ModelPlayer : public PlayerPort {
public:
    int stamina_ = 50, maxStamina_ = 100, fatigue_ = 0, maxFatigue_ = 100, money_ = 0, day_ = 1;
    int stamina() const override { return stamina_; }
    int fatigue() const override { return fatigue_; }
    int money() const override { return money_; }
    int day() const override { return day_; }
    void addStamina(int a) override { stamina_ = clamp(static_cast<int64_t>(stamina_) + a, maxStamina_); }
    void addFatigue(int a) override { fatigue_ = clamp(static_cast<int64_t>(fatigue_) + a, maxFatigue_); }
    void addMoney(int a) override {
        const int64_t v = static_cast<int64_t>(money_) + a;
        money_ = v < 0 ? 0 : (v > 99999999 ? 99999999 : static_cast<int>(v));
    }
    bool spend(int a) override { if (a < 0 || money_ < a) return false; money_ -= a; return true; }

private:
    static int clamp(int64_t v, int hi) { return v < 0 ? 0 : (v > hi ? hi : static_cast<int>(v)); }
};

class NativeInventory {
public:
    static constexpr int kMaxStack = 99;      // model constant
    static constexpr int kDefaultSlots = 24;  // model constant
    struct Slot { std::string id; int count = 0; Json state = Json::object(); uint64_t seq = 0; };

    explicit NativeInventory(int capacity = kDefaultSlots) : capacity_(capacity < 1 ? 1 : capacity) {}
    int capacity() const { return capacity_; }
    int liveSlots() const { return static_cast<int>(slots_.size()); }
    // Stacks that exist in saved state but cannot be shown (missing mod, unknown id, damaged
    // entry). They keep their slot so re-enabling a mod can never overflow the inventory.
    int reservedSlots() const { return reserved_; }
    int usedSlots() const { return liveSlots() + reserved_; }
    bool hasRoomForNewStack() const { return usedSlots() < capacity_; }
    const std::vector<Slot>& slots() const { return slots_; }  // acquisition order (seq, then id)
    const Slot* find(const std::string& id) const;
    int count(const std::string& id) const;

    // Low level; NativeItemRuntime validates before calling these.
    void add(const std::string& id, int count);
    void remove(const std::string& id, int count);
    bool setState(const std::string& id, const Json& state);
    void restore(std::vector<Slot> slots, int reserved);
    void clear() { slots_.clear(); reserved_ = 0; nextSeq_ = 1; }

private:
    int capacity_;
    int reserved_ = 0;
    uint64_t nextSeq_ = 1;
    std::vector<Slot> slots_;
};

enum class ItemCode {
    Ok, Cancelled, UnknownItem, Unavailable, VanillaNotBridged, InvalidCount, InventoryFull, StackFull,
    NotForSale, NotEnoughMoney, NotEnoughItems, NotInInventory, NotUsable, BadSlot, NothingSelected, BadState
};
const char* itemCodeName(ItemCode c);

struct ItemOutcome {
    ItemCode code = ItemCode::Ok;
    std::string message;
    bool consumed = false;       // one unit removed by the default behaviour
    int staminaDelta = 0, fatigueDelta = 0, moneySpent = 0;
    int hookFaults = 0;          // faulting mod handlers (isolated, they never abort the operation)
    bool ok() const { return code == ItemCode::Ok; }
};

class NativeItemRuntime {
public:
    NativeItemRuntime(Session& session, PlayerPort& player, int slots = NativeInventory::kDefaultSlots);
    ~NativeItemRuntime();
    NativeItemRuntime(const NativeItemRuntime&) = delete;
    NativeItemRuntime& operator=(const NativeItemRuntime&) = delete;

    const NativeItems& items() const { return items_; }
    const NativeInventory& inventory() const { return inv_; }
    // HostApi installed on the session so mod scripts (give_item, add_stamina, show_text, ...)
    // act on this model. Installed by the constructor, removed by the destructor.
    HostApi& host() { return bridge_; }
    // show_text / log output produced by scripts (the model's stand-in for on-screen text).
    const std::vector<std::string>& messages() const { return messages_; }

    // Registers nothing: items come from the Session registry. These act on the inventory.
    ItemOutcome acquire(const std::string& id, int count);   // free pickup, no hooks
    ItemOutcome remove(const std::string& id, int count);    // all-or-nothing
    ItemOutcome buy(const std::string& id, int count);       // item.buy hook (cancelable), then charge
    ItemOutcome select(int slot);
    int selectedSlot() const;                                // -1 when nothing is selected
    ItemOutcome use(int slot);
    ItemOutcome useSelected();
    ItemOutcome setState(const std::string& id, const Json& state);  // object, <= 4096 bytes

    // Native state. Call after Session::load. Selection is transient and not saved.
    bool loadState(const NativeSaveStore& store, std::vector<std::string>& notes);
    // Writes natureloader.json (`core`) then mods/<owner>.json for every loaded owner whose state
    // changed. Files of mods that are not loaded are never touched.
    bool saveState(const NativeSaveStore& store, const SaveExtension& core, std::string& err);

private:
    struct Bridge : HostApi {
        explicit Bridge(NativeItemRuntime& r) : rt(r) {}
        bool giveItem(const std::string& itemId, int count) override;
        bool removeItem(const std::string& itemId, int count) override;
        int countItem(const std::string& itemId) override;
        void showText(const std::string& text) override;
        void addStamina(int amount) override;
        void addMoney(int amount) override;
        int money() override;
        int day() override;
        uint32_t random(uint32_t n) override;
        void log(int level, const std::string& msg) override;
        NativeItemRuntime& rt;
        uint64_t rng = 0x9E3779B97F4A7C15ull;
    };

    ItemOutcome checkAdd(const std::string& anyId, int count, ItemInfo& info) const;
    void dropStaleSelection();

    Session& session_;
    PlayerPort& player_;
    NativeItems items_;
    NativeInventory inv_;
    Bridge bridge_;
    std::string selectedId_;
    std::vector<std::string> messages_;
    // Saved entries of loaded mods that could not become live stacks; written back unchanged.
    std::map<std::string, std::map<std::string, Json>> preserved_;
};

}  // namespace nm
