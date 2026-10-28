// Tests for the modcore native item model (vanilla ids, handles, inventory, save, orphans).
// These are unit/integration tests of modcore only. They do NOT run the BTN native runtime and
// prove nothing about on-screen items, the farm, or retail saves.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <type_traits>

#include "capi.h"
#include "modcore_catalog.h"
#include "native_items.h"

using namespace nm;
namespace fs = std::filesystem;

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
static void test(const char* name, std::function<void()> f) { ++g_run; std::printf("[test] %s\n", name); f(); }

static std::string g_examples = "examples/native-items";

// ---- fixtures -------------------------------------------------------------------------------
static std::shared_ptr<PackSource> expandedPack() { return std::make_shared<DirPackSource>(g_examples + "/expanded"); }

static std::string manifestJson(const std::string& id) {
    return "{\"schema\":2,\"id\":\"" + id + "\",\"name\":\"" + id + "\",\"version\":\"1.0.0\",\"game\":\"SLUS_011.15\","
           "\"api\":\">=1.0 <2\",\"permissions\":[\"inventory\",\"player\",\"ui\",\"save\"],"
           "\"content\":[\"content/items.json\"],\"scripts\":[\"scripts/s.json\"]}";
}

static std::shared_ptr<PackSource> extrasPack() {
    auto p = std::make_shared<MemPackSource>("extras");
    p->put("mod.json", manifestJson("extras"));
    p->put("content/items.json", R"({"item":{
      "apple":{"name":"Apple","buy_price":10,"sell_price":5,"stamina":10},
      "rock":{"name":"Rock"},
      "bundle":{"name":"Bundle","hooks":{"use":"extras:open-bundle"}},
      "gem":{"name":"Gem","sell_price":50},
      "cancelfruit":{"name":"Cancel Fruit","stamina":20,"hooks":{"use":"extras:take-over"}},
      "loopfruit":{"name":"Loop Fruit","stamina":5,"hooks":{"use":"extras:runaway"}},
      "locked":{"name":"Locked","buy_price":50,"hooks":{"on_buy":"extras:deny"}}}})");
    p->put("scripts/s.json", R"({"handlers":{
      "open-bundle":[{"do":"give_item","item":"gem","count":2},{"do":"add_money","amount":5}],
      "take-over":[{"do":"show_text","text":"taken over"},{"do":"cancel"}],
      "runaway":[{"do":"repeat","times":1000,"do_list":[{"do":"repeat","times":1000,"do_list":[{"do":"set_var","name":"x","value":1}]}]}],
      "deny":[{"do":"cancel"}]}})");
    return p;
}

static std::shared_ptr<PackSource> namedVanillaPack() {
    auto p = std::make_shared<MemPackSource>("vanilla-mod");
    p->put("mod.json", manifestJson("vanilla"));
    p->put("content/items.json", R"({"item":{"x":{"name":"X"}}})");
    p->put("scripts/s.json", R"({"handlers":{}})");
    return p;
}

struct Game {
    Session s;
    SaveExtension core;
    ModelPlayer player;
    std::unique_ptr<NativeItemRuntime> rt;
    bool clean = false;
    Game(const std::vector<std::shared_ptr<PackSource>>& packs, const SaveExtension& saved = SaveExtension(), int slots = 24,
         const std::vector<std::string>& order = {})
        : s(HostInfo{}, Json::parse(natureBaseCatalog)), core(saved) {
        for (const auto& p : packs) s.addPack(p);
        s.setUserOrder(order);
        clean = s.load(&core);
        rt = std::make_unique<NativeItemRuntime>(s, player, slots);
    }
};

struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& tag) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("nature-native-items-" + tag + "-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code e; fs::remove_all(path, e); }
    std::string str() const { return path.string(); }
};

static std::string slurp(const fs::path& p) { std::string s; readWholeFile(p.string(), s); return s; }
static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
static bool anyNote(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& n : v) if (has(n, needle)) return true;
    return false;
}
static const char* GOLD = "expanded:golden_turnip";

static SaveExtension loadCore(const NativeSaveStore& st) {
    SaveExtension c; bool existed = false; std::string err;
    CHECK(st.loadCore(c, &existed, err));
    CHECK(existed);
    return c;
}

int main(int argc, char** argv) {
    if (argc > 1) g_examples = argv[1];

    test("ids: vanilla aliases normalise to the repository's base: ids", [] {
        VanillaId v; std::string err;
        CHECK(parseVanillaAlias("vanilla:item.turnip", v, err) && v.kind == "item" && v.canonical == "base:turnip");
        CHECK(parseVanillaAlias("vanilla:item:turnip", v, err) && v.canonical == "base:turnip");
        CHECK(parseVanillaAlias("vanilla:npc:elli", v, err) && v.kind == "npc" && v.canonical == "base:elli");
        CHECK(parseVanillaAlias("vanilla:map:farm", v, err) && v.kind == "area" && v.canonical == "base:farm");
        CHECK(parseVanillaAlias("vanilla:map.farm", v, err) && v.kind == "area");
        for (const char* bad : {"vanilla:", "vanilla:item", "vanilla:item.", "vanilla:item.Turnip", "vanilla:item.a.b", "vanilla:item.a:b",
                                "vanilla:dialog.x", "vanilla:thing.x", "vanilla:.x", "base:turnip", "vanilla"})
            CHECK(!parseVanillaAlias(bad, v, err));
        std::string c, kind;
        CHECK(canonicalContentId("expanded:golden_turnip", c, &kind, err) && c == "expanded:golden_turnip" && kind.empty());
        CHECK(canonicalContentId("base:turnip", c, &kind, err) && c == "base:turnip");
        CHECK(canonicalContentId("vanilla:item.turnip", c, &kind, err) && c == "base:turnip" && kind == "item");
        for (const char* bad : {"", "a:b:c", "Bad:id", "nocolon", ":x", "x:"}) CHECK(!canonicalContentId(bad, c, &kind, err));
        CHECK(vanillaAliasOf("item", "base:turnip") == "vanilla:item.turnip");
        CHECK(vanillaAliasOf("area", "base:farm") == "vanilla:map.farm");
        CHECK(vanillaAliasOf("item", "expanded:golden_turnip").empty());
        CHECK(vanillaAliasOf("dialog", "base:x").empty());
        // alias -> canonical -> alias round trip
        CHECK(parseVanillaAlias(vanillaAliasOf("npc", "base:elli"), v, err) && v.canonical == "base:elli");
    });

    test("ids: aliases are compatible with the existing registry and C ABI (convention unchanged)", [] {
        Game g({});
        const NativeItems& it = g.rt->items();
        std::string err;
        const ItemHandle a = it.resolve("base:turnip", &err), b = it.resolve("vanilla:item.turnip"), c = it.resolve("vanilla:item:turnip");
        CHECK(a.valid() && a == b && b == c);
        CHECK(it.idOf(a) == "base:turnip");
        CHECK(g.s.registry().find("base:turnip") && g.s.registry().find("base:turnip")->handle == 65);
        CHECK(it.resolve("base:mystrile-hoe").valid());
        // Wrong kind / missing: explicit errors, no guessing.
        CHECK(!it.resolve("vanilla:npc:elli", &err).valid() && has(err, "bukan item"));
        CHECK(!it.resolve("vanilla:item.no-such-thing", &err).valid() && has(err, "item tidak ada"));
        CHECK(!it.resolve("vanilla:item", &err).valid());
        // The adapter normalises text only. The base catalog has no npc/area names yet.
        std::string canonical, kind;
        CHECK(canonicalContentId("vanilla:npc:elli", canonical, &kind, err) && canonical == "base:elli" && kind == "npc");
        CHECK(g.s.registry().find("base:elli") == nullptr);
        // The C ABI still speaks only canonical ids: the global format was not changed.
        NmSession* c_api = nm_create(nullptr, nullptr, natureBaseCatalog, "en");
        CHECK(c_api != nullptr);
        if (c_api) {
            CHECK(nm_load(c_api, nullptr, nullptr) == 0);
            CHECK(nm_handle(c_api, "item", "base:turnip") == 65);
            CHECK(nm_handle(c_api, "item", "vanilla:item.turnip") == -1);
            nm_destroy(c_api);
        }
    });

    test("manifest: expanded:golden_turnip registers with fields, handle and sprite asset", [] {
        Game g({expandedPack()});
        CHECK(g.clean);
        const NativeItems& it = g.rt->items();
        CHECK(it.modItems().size() == 1);
        std::string err;
        const ItemHandle h = it.resolve(GOLD, &err);
        CHECK(h.valid());
        ItemInfo i;
        CHECK(it.info(h, i));
        CHECK(i.id == GOLD && i.owner == "expanded" && !i.vanilla && i.name == "Golden Turnip" && i.category == "consumable");
        CHECK(i.buyPrice == 300 && i.sellPrice == 150 && i.stamina == 40 && i.fatigue == -15 && i.hasUseHook);
        CHECK(i.sprite == "expanded:sprites/golden-turnip.png" && !i.flags.empty() && i.flags[0] == "pickup");
        CHECK(g.s.registry().find(GOLD)->handle >= 512);  // modFirst of the item range
        CHECK(it.idOf(h) == GOLD);
        std::string png;
        CHECK(it.loadSprite(h, png) && png.size() > 8 && png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0);
        const AssetInfo* a = g.s.vfs().info("expanded:sprites/golden-turnip.png");
        CHECK(a && a->width == 16 && a->height == 16);
        // Aliases never reach mod content: "vanilla:" always means "base:".
        CHECK(!it.resolve("vanilla:item.golden_turnip", &err).valid() && has(err, "base:golden_turnip"));
    });

    test("handle: opaque, invalid by default, stable across sessions", [] {
        static_assert(!std::is_constructible<ItemHandle, int>::value, "handles must not be constructible from numbers");
        static_assert(!std::is_convertible<ItemHandle, int>::value, "handles must not convert to numbers");
        ItemHandle none;
        CHECK(!none.valid());
        Game g({expandedPack()});
        ItemInfo i;
        CHECK(!g.rt->items().info(none, i));
        CHECK(g.rt->items().idOf(none).empty());
        const ItemHandle h1 = g.rt->items().resolve(GOLD);
        Game g2({extrasPack(), expandedPack()}, g.core, 24, {"extras", "expanded"});  // different load order, same IdTable
        CHECK(g2.rt->items().resolve(GOLD) == h1);
        Game fresh({extrasPack(), expandedPack()}, SaveExtension(), 24, {"extras", "expanded"});  // no IdTable: only the table guarantees stability
        CHECK(fresh.rt->items().resolve(GOLD).valid());
    });

    test("acquire: stacking, limits and refusals", [] {
        Game g({extrasPack(), expandedPack()});
        NativeItemRuntime& rt = *g.rt;
        CHECK(rt.acquire(GOLD, 2).ok() && rt.inventory().count(GOLD) == 2);
        CHECK(rt.acquire(GOLD, 98).code == ItemCode::StackFull && rt.inventory().count(GOLD) == 2);  // no partial add
        CHECK(rt.acquire(GOLD, 97).ok() && rt.inventory().count(GOLD) == 99);
        CHECK(rt.acquire(GOLD, 1).code == ItemCode::StackFull);
        for (int bad : {0, -1, 100}) CHECK(rt.acquire(GOLD, bad).code == ItemCode::InvalidCount);
        CHECK(rt.acquire("expanded:nope", 1).code == ItemCode::UnknownItem);
        CHECK(rt.acquire("not an id", 1).code == ItemCode::UnknownItem);
        const int before = rt.inventory().liveSlots();
        CHECK(rt.acquire("base:turnip", 1).code == ItemCode::VanillaNotBridged);
        CHECK(rt.acquire("vanilla:item.turnip", 1).code == ItemCode::VanillaNotBridged);
        CHECK(rt.inventory().liveSlots() == before);
        CHECK(rt.remove(GOLD, 100).code == ItemCode::InvalidCount);
        CHECK(rt.remove("extras:apple", 1).code == ItemCode::NotInInventory);
        CHECK(rt.remove(GOLD, 99).ok() && rt.inventory().find(GOLD) == nullptr);
        // capacity
        Game small({extrasPack(), expandedPack()}, SaveExtension(), 2);
        CHECK(small.rt->acquire(GOLD, 1).ok() && small.rt->acquire("extras:apple", 1).ok());
        const auto full = small.rt->acquire("extras:rock", 1);
        CHECK(full.code == ItemCode::InventoryFull && small.rt->inventory().usedSlots() == 2);
        CHECK(small.rt->acquire(GOLD, 1).ok());  // existing stack still grows when full
    });

    test("select/use: visible effect on model state, consumption, hook and flag", [] {
        Game g({extrasPack(), expandedPack()});
        NativeItemRuntime& rt = *g.rt;
        g.player.stamina_ = 50; g.player.fatigue_ = 30;
        CHECK(rt.acquire(GOLD, 3).ok());
        CHECK(rt.selectedSlot() == -1 && rt.useSelected().code == ItemCode::NothingSelected);
        CHECK(rt.select(0).ok() && rt.selectedSlot() == 0);
        CHECK(rt.select(5).code == ItemCode::BadSlot && rt.selectedSlot() == 0);
        ItemOutcome o = rt.useSelected();
        CHECK(o.ok() && o.consumed && o.staminaDelta == 40 && o.fatigueDelta == -15 && o.hookFaults == 0);
        CHECK(g.player.stamina_ == 90 && g.player.fatigue_ == 15 && rt.inventory().count(GOLD) == 2);
        CHECK(g.core.flags["expanded:golden-turnips-eaten"].asInt() == 1);
        CHECK(!rt.messages().empty() && rt.messages().back() == "Golden Turnip: so refreshing!");
        o = rt.useSelected();
        CHECK(o.ok() && o.staminaDelta == 10 && o.fatigueDelta == -15 && g.player.stamina_ == 100 && g.player.fatigue_ == 0);  // clamped
        o = rt.useSelected();
        CHECK(o.ok() && o.staminaDelta == 0 && o.fatigueDelta == 0 && rt.inventory().count(GOLD) == 0);
        CHECK(rt.selectedSlot() == -1);  // selection dropped with the last unit
        CHECK(rt.useSelected().code == ItemCode::NothingSelected);
        CHECK(g.core.flags["expanded:golden-turnips-eaten"].asInt() == 3);
        CHECK(rt.use(0).code == ItemCode::BadSlot);
        // item without any use effect
        CHECK(rt.acquire("extras:rock", 1).ok());
        CHECK(rt.use(0).code == ItemCode::NotUsable && rt.inventory().count("extras:rock") == 1);
    });

    test("use: a cancelling handler takes over; a faulting handler is isolated", [] {
        Game g({extrasPack(), expandedPack()});
        NativeItemRuntime& rt = *g.rt;
        CHECK(rt.acquire("extras:cancelfruit", 1).ok());
        const int stamina = g.player.stamina_;
        ItemOutcome o = rt.use(0);
        CHECK(o.code == ItemCode::Cancelled && !o.consumed && g.player.stamina_ == stamina);
        CHECK(rt.inventory().count("extras:cancelfruit") == 1 && rt.messages().back() == "taken over");
        // runaway script: budget exhaustion is a fault, the default behaviour still applies
        CHECK(rt.acquire("extras:loopfruit", 6).ok());
        const int slot = rt.inventory().slots().size() - 1;
        int faults = 0;
        for (int i = 0; i < 6; ++i) {
            o = rt.use(slot);
            CHECK(o.ok() && o.consumed && o.staminaDelta == 5);
            faults += o.hookFaults;
        }
        CHECK(faults == 5);                        // the fifth fault mutes the mod's hooks (circuit breaker)
        CHECK(g.s.hooks().muted("extras"));
        CHECK(rt.inventory().find("extras:loopfruit") == nullptr);
        // other mods keep working
        CHECK(rt.acquire(GOLD, 1).ok() && rt.use(static_cast<int>(rt.inventory().slots().size()) - 1).ok());
    });

    test("buy: charges, validates first, never half-applies", [] {
        Game g({extrasPack(), expandedPack()});
        NativeItemRuntime& rt = *g.rt;
        g.player.money_ = 1000;
        ItemOutcome o = rt.buy(GOLD, 2);
        CHECK(o.ok() && o.moneySpent == 600 && g.player.money_ == 400 && rt.inventory().count(GOLD) == 2);
        CHECK(rt.buy(GOLD, 2).code == ItemCode::NotEnoughMoney && g.player.money_ == 400 && rt.inventory().count(GOLD) == 2);
        CHECK(rt.buy(GOLD, 1).ok() && g.player.money_ == 100);
        CHECK(rt.buy(GOLD, 1).code == ItemCode::NotEnoughMoney);
        CHECK(rt.buy("extras:rock", 1).code == ItemCode::NotForSale);
        CHECK(rt.buy("base:turnip", 1).code == ItemCode::VanillaNotBridged && g.player.money_ == 100);
        CHECK(rt.buy(GOLD, 0).code == ItemCode::InvalidCount);
        // affordability is checked before the hook runs: a poor player gets NotEnoughMoney, not a mod's cancel
        g.player.money_ = 0;
        CHECK(rt.buy("extras:locked", 1).code == ItemCode::NotEnoughMoney);
        // cancelled by a mod: nothing charged, nothing added
        g.player.money_ = 1000;
        CHECK(rt.buy("extras:locked", 1).code == ItemCode::Cancelled && g.player.money_ == 1000 && rt.inventory().count("extras:locked") == 0);
        // full inventory / full stack: refused before charging
        Game small({extrasPack(), expandedPack()}, SaveExtension(), 1);
        small.player.money_ = 1000;
        CHECK(small.rt->acquire(GOLD, 1).ok());
        CHECK(small.rt->buy("extras:apple", 1).code == ItemCode::InventoryFull && small.player.money_ == 1000);
        CHECK(small.rt->acquire(GOLD, 98).ok());
        CHECK(small.rt->buy(GOLD, 1).code == ItemCode::StackFull && small.player.money_ == 1000);
    });

    test("host bridge: mod scripts act on the model inventory and player", [] {
        Game g({extrasPack(), expandedPack()});
        NativeItemRuntime& rt = *g.rt;
        CHECK(rt.host().giveItem(GOLD, 2) && rt.host().countItem(GOLD) == 2 && rt.host().countItem("nope") == 0);
        CHECK(!rt.host().removeItem(GOLD, 3) && rt.host().removeItem(GOLD, 2) && rt.host().countItem(GOLD) == 0);
        CHECK(!rt.host().giveItem("base:turnip", 1));
        rt.host().addMoney(25);
        CHECK(rt.host().money() == 25 && rt.host().day() == 1);
        // item script: give_item (own mod, unqualified id) + add_money, default consumption of the bundle
        g.player.money_ = 0;
        CHECK(rt.acquire("extras:bundle", 1).ok());
        ItemOutcome o = rt.use(0);
        CHECK(o.ok() && o.consumed && rt.inventory().count("extras:gem") == 2 && g.player.money_ == 5 && rt.inventory().count("extras:bundle") == 0);
        // determinism of the host random source
        Game g2({extrasPack()});
        const uint32_t a = g.rt->host().random(1000), b = g.rt->host().random(1000);
        const uint32_t c = g2.rt->host().random(1000), d = g2.rt->host().random(1000);
        CHECK(a == c && b == d && g.rt->host().random(0) == 0);
    });

    test("save: round trip through natureloader.json and mods/<mod>.json", [] {
        TempDir dir("roundtrip");
        NativeSaveStore store(dir.str());
        CHECK(writeWholeFile((dir.path / "vanilla.mcr").string(), "VANILLA-SAVE-BYTES"));
        const std::string vanillaHash = sha256Hex(slurp(dir.path / "vanilla.mcr"));
        {
            Game a({extrasPack(), expandedPack()});
            CHECK(a.rt->acquire("extras:apple", 5).ok() && a.rt->acquire(GOLD, 3).ok());  // apple first: order must survive
            Json st = Json::object(); st["quality"] = 2; st["note"] = "shiny";
            CHECK(a.rt->setState(GOLD, st).ok());
            CHECK(a.rt->setState(GOLD, Json("not an object")).code == ItemCode::BadState);
            CHECK(a.rt->setState("extras:gem", st).code == ItemCode::NotInInventory);
            CHECK(a.rt->select(1).ok() && a.rt->use(1).ok());
            std::string err;
            CHECK(a.rt->saveState(store, a.core, err));
            CHECK(fs::exists(dir.path / "natureloader.json") && fs::exists(dir.path / "mods" / "expanded.json") && fs::exists(dir.path / "mods" / "extras.json"));
            CHECK(!fs::exists(dir.path / "mods" / "expanded.json.tmp") && !fs::exists(dir.path / "natureloader.json.tmp"));
            const Json doc = Json::parse(slurp(dir.path / "mods" / "expanded.json"));
            CHECK(doc["format"].asInt() == 1 && doc["mod"].asString() == "expanded" && doc["version"].asString() == "1.0.0" && doc["save_version"].asInt() == 1);
            CHECK(doc["items"][GOLD]["count"].asInt() == 2 && doc["items"][GOLD]["state"]["quality"].asInt() == 2);
            CHECK(sha256Hex(slurp(dir.path / "vanilla.mcr")) == vanillaHash);  // retail save never touched
        }
        const std::string expandedBytes = slurp(dir.path / "mods" / "expanded.json");
        {
            const SaveExtension core = loadCore(store);
            Game b({extrasPack(), expandedPack()}, core);
            std::vector<std::string> notes;
            CHECK(b.rt->loadState(store, notes) && notes.empty());
            const auto& slots = b.rt->inventory().slots();
            CHECK(slots.size() == 2 && slots[0].id == "extras:apple" && slots[1].id == GOLD);  // acquisition order, not alphabetical
            CHECK(b.rt->inventory().count("extras:apple") == 5 && b.rt->inventory().count(GOLD) == 2);
            CHECK(slots[1].state["quality"].asInt() == 2 && slots[1].state["note"].asString() == "shiny");
            CHECK(b.core.flags["expanded:golden-turnips-eaten"].asInt() == 1);
            CHECK(b.rt->selectedSlot() == -1);  // selection is transient
            CHECK(checkSave(b.core, b.s.loadedMods()).clean());
            SaveExtension reparsed; std::string err;
            CHECK(SaveExtension::fromJson(Json::parse(slurp(dir.path / "natureloader.json")), reparsed, err));
            // saving without changes must not rewrite (or back up) the state files
            CHECK(b.rt->saveState(store, b.core, err));
            CHECK(slurp(dir.path / "mods" / "expanded.json") == expandedBytes && !fs::exists(dir.path / "mods" / "expanded.json.bak"));
            // a real change rewrites atomically and keeps the previous version as .bak
            CHECK(b.rt->use(1).ok() && b.rt->saveState(store, b.core, err));
            CHECK(slurp(dir.path / "mods" / "expanded.json.bak") == expandedBytes);
            CHECK(Json::parse(slurp(dir.path / "mods" / "expanded.json"))["items"][GOLD]["count"].asInt() == 1);
            CHECK(sha256Hex(slurp(dir.path / "vanilla.mcr")) == vanillaHash);
        }
    });

    test("disable then re-enable: orphan data survives, vanilla save untouched, handle stable", [] {
        TempDir dir("orphan");
        NativeSaveStore store(dir.str());
        CHECK(writeWholeFile((dir.path / "vanilla.mcr").string(), "VANILLA-SAVE-BYTES"));
        const std::string vanillaHash = sha256Hex(slurp(dir.path / "vanilla.mcr"));
        std::string err;
        ItemHandle phase1Handle;
        int idTableHandle = -1;
        // phase 1: mod enabled
        {
            Game g({extrasPack(), expandedPack()}, SaveExtension(), 2);
            CHECK(g.rt->acquire(GOLD, 4).ok());
            Json st = Json::object(); st["q"] = 7;
            CHECK(g.rt->setState(GOLD, st).ok());
            phase1Handle = g.rt->items().resolve(GOLD);
            CHECK(g.rt->saveState(store, g.core, err));
            idTableHandle = static_cast<int>(g.core.ids.ids.at("item").at(GOLD));
        }
        const std::string expandedBytes = slurp(dir.path / "mods" / "expanded.json");
        // phase 2: expanded disabled (pack not offered)
        {
            const SaveExtension core = loadCore(store);
            Game g({extrasPack()}, core, 2);
            std::vector<std::string> notes;
            CHECK(g.rt->loadState(store, notes));
            CHECK(anyNote(notes, "expanded") && anyNote(notes, "tidak aktif"));
            CHECK(g.rt->inventory().liveSlots() == 0 && g.rt->inventory().reservedSlots() == 1);
            ItemInfo stale;
            CHECK(!g.rt->items().info(phase1Handle, stale));              // old handle does not dangle
            CHECK(!g.rt->items().resolve(GOLD).valid());
            CHECK(g.rt->acquire(GOLD, 1).code == ItemCode::UnknownItem);
            const SaveCompat compat = checkSave(g.core, g.s.loadedMods());
            CHECK(compat.missing.size() == 1 && compat.missing[0].id == "expanded" && has(compat.describe(), "Data tetap disimpan"));
            CHECK(!compat.missing[0].content.empty() && compat.missing[0].content[0] == std::string("item ") + GOLD);
            CHECK(g.rt->acquire("extras:apple", 2).ok());
            CHECK(g.rt->acquire("extras:rock", 1).code == ItemCode::InventoryFull);  // the orphan stack still holds its slot
            CHECK(g.rt->saveState(store, g.core, err));
            CHECK(slurp(dir.path / "mods" / "expanded.json") == expandedBytes);       // byte-identical, not even rewritten
            CHECK(!fs::exists(dir.path / "mods" / "expanded.json.bak"));
            CHECK(sha256Hex(slurp(dir.path / "vanilla.mcr")) == vanillaHash);
            const SaveExtension after = loadCore(store);
            CHECK(after.ids.ids.at("item").at(GOLD) == idTableHandle);               // handle reservation kept
        }
        // phase 3: re-enabled, different load order
        {
            const SaveExtension core = loadCore(store);
            Game g({extrasPack(), expandedPack()}, core, 2, {"extras", "expanded"});
            std::vector<std::string> notes;
            CHECK(g.rt->loadState(store, notes) && notes.empty());
            CHECK(g.rt->inventory().reservedSlots() == 0 && g.rt->inventory().liveSlots() == 2 && g.rt->inventory().usedSlots() <= g.rt->inventory().capacity());
            CHECK(g.rt->inventory().count(GOLD) == 4 && g.rt->inventory().count("extras:apple") == 2);
            CHECK(g.rt->inventory().find(GOLD)->state["q"].asInt() == 7);
            CHECK(g.rt->items().resolve(GOLD) == phase1Handle);
            CHECK(g.s.registry().find(GOLD)->handle == idTableHandle);
            CHECK(g.rt->select(g.rt->inventory().slots()[0].id == GOLD ? 0 : 1).ok());
            CHECK(g.rt->useSelected().ok());
            CHECK(g.rt->saveState(store, g.core, err));
            CHECK(Json::parse(slurp(dir.path / "mods" / "expanded.json"))["items"][GOLD]["count"].asInt() == 3);
            CHECK(sha256Hex(slurp(dir.path / "vanilla.mcr")) == vanillaHash);
        }
    });

    test("damaged state file is never overwritten silently", [] {
        TempDir dir("damaged");
        NativeSaveStore store(dir.str());
        fs::create_directories(dir.path / "mods");
        const std::string junk = "{not json";
        CHECK(writeWholeFile((dir.path / "mods" / "expanded.json").string(), junk));
        Game g({expandedPack()});
        std::vector<std::string> notes; std::string err;
        CHECK(g.rt->loadState(store, notes) && anyNote(notes, "rusak") && g.rt->inventory().slots().empty());
        CHECK(g.rt->saveState(store, g.core, err));
        CHECK(slurp(dir.path / "mods" / "expanded.json") == junk);                   // nothing new to store: untouched
        CHECK(g.rt->acquire(GOLD, 1).ok() && g.rt->saveState(store, g.core, err));
        CHECK(slurp(dir.path / "mods" / "expanded.json.damaged") == junk);           // original kept
        CHECK(Json::parse(slurp(dir.path / "mods" / "expanded.json"))["items"][GOLD]["count"].asInt() == 1);
    });

    test("state written by a newer save_version is read-only", [] {
        TempDir dir("newer");
        NativeSaveStore store(dir.str());
        Json doc = Json::object(), items = Json::object(), e = Json::object();
        e["count"] = 3; e["seq"] = 1; e["state"] = Json::object();
        items[GOLD] = e;
        doc["format"] = 1; doc["mod"] = "expanded"; doc["version"] = "2.0.0"; doc["save_version"] = 2; doc["items"] = items;
        fs::create_directories(dir.path / "mods");
        CHECK(writeWholeFile((dir.path / "mods" / "expanded.json").string(), doc.dump(2)));
        const std::string bytes = slurp(dir.path / "mods" / "expanded.json");
        Game g({expandedPack()});
        std::vector<std::string> notes; std::string err;
        CHECK(g.rt->loadState(store, notes) && anyNote(notes, "lebih baru"));
        CHECK(g.rt->inventory().liveSlots() == 0 && g.rt->inventory().reservedSlots() == 1);
        CHECK(g.rt->acquire(GOLD, 1).ok());
        CHECK(!g.rt->saveState(store, g.core, err) && has(err, "save_version"));
        CHECK(slurp(dir.path / "mods" / "expanded.json") == bytes);
    });

    test("unknown, invalid and unrelated entries are preserved verbatim", [] {
        TempDir dir("preserve");
        NativeSaveStore store(dir.str());
        Json removed = Json::object(), st = Json::object();
        st["a"] = 1; removed["count"] = 5; removed["state"] = st; removed["seq"] = 9;
        Json badCount = Json::object(); badCount["count"] = 0;
        Json foreign = Json::object(); foreign["count"] = 1;
        Json good = Json::object(); good["count"] = 2; good["seq"] = 1;
        Json items = Json::object();
        items[GOLD] = good; items["expanded:removed_item"] = removed; items["expanded:badcount"] = badCount; items["other:thing"] = foreign;
        Json future = Json::object(); future["x"] = 1;
        Json doc = Json::object();
        doc["format"] = 1; doc["mod"] = "expanded"; doc["version"] = "1.0.0"; doc["save_version"] = 1; doc["items"] = items; doc["future"] = future;
        fs::create_directories(dir.path / "mods");
        CHECK(writeWholeFile((dir.path / "mods" / "expanded.json").string(), doc.dump(2)));
        Game g({expandedPack()});
        std::vector<std::string> notes; std::string err;
        CHECK(g.rt->loadState(store, notes));
        CHECK(g.rt->inventory().count(GOLD) == 2 && g.rt->inventory().reservedSlots() == 3 && notes.size() == 3);
        CHECK(g.rt->select(0).ok() && g.rt->use(0).ok() && g.rt->saveState(store, g.core, err));
        const Json out = Json::parse(slurp(dir.path / "mods" / "expanded.json"));
        CHECK(out["items"][GOLD]["count"].asInt() == 1);
        CHECK(out["items"]["expanded:removed_item"] == removed && out["items"]["expanded:badcount"] == badCount && out["items"]["other:thing"] == foreign);
        CHECK(out["future"] == future && out["items"].size() == 4);
    });

    test("a mod named 'vanilla' disables the alias instead of guessing", [] {
        Game g({namedVanillaPack()});
        std::string err;
        CHECK(g.s.report().loaded.size() == 1);
        CHECK(!g.rt->items().resolve("vanilla:item.turnip", &err).valid() && has(err, "'vanilla'"));
        CHECK(g.rt->items().resolve("base:turnip").valid() && g.rt->items().resolve("vanilla:x", &err).valid() == false);
        // Known limitation, by design: the mod's own ids ("vanilla:x") are unreachable through the native
        // layer while the alias is ambiguous. Refusing is safer than guessing; reserving the id is future work.
        CHECK(g.rt->acquire("vanilla:x", 1).code == ItemCode::UnknownItem);
    });

    test("atomic writer: backup of the previous file, no temp leftovers, failure reported", [] {
        TempDir dir("atomic");
        const std::string p = (dir.path / "deep" / "nested" / "f.json").string();
        std::string err;
        CHECK(NativeSaveStore::writeFileAtomic(p, "one", err));
        CHECK(NativeSaveStore::writeFileAtomic(p, "two", err));
        CHECK(slurp(p) == "two" && slurp(p + ".bak") == "one" && !fs::exists(p + ".tmp"));
        CHECK(writeWholeFile((dir.path / "afile").string(), "x"));
        CHECK(!NativeSaveStore::writeFileAtomic((dir.path / "afile" / "child.json").string(), "y", err) && !err.empty());
        NativeSaveStore store(dir.str());
        SaveExtension c; bool existed = true;
        CHECK(store.loadCore(c, &existed, err) && !existed);                 // missing core file = fresh save
        CHECK(writeWholeFile(store.corePath(), "{\"format\":9}"));
        CHECK(!store.loadCore(c, &existed, err) && has(err, "natureloader.json"));
    });

    test("save directory failure preserves live inventory and core state", [] {
        TempDir dir("scan-failure");
        NativeSaveStore store(dir.str());
        Game g({expandedPack()});
        CHECK(g.rt->acquire(GOLD, 2).ok() && g.rt->select(0).ok());
        CHECK(writeWholeFile(store.corePath(), "preserve core"));
        CHECK(writeWholeFile((dir.path / "mods").string(), "not a directory"));
        std::vector<std::string> notes;
        std::string err;
        CHECK(!g.rt->loadState(store, notes) && !notes.empty());
        CHECK(g.rt->inventory().count(GOLD) == 2 && g.rt->selectedSlot() == 0);
        CHECK(!g.rt->saveState(store, g.core, err) && !err.empty());
        CHECK(slurp(store.corePath()) == "preserve core");
        CHECK(!fs::exists(store.corePath() + ".bak"));
        std::string moved;
        CHECK(!store.quarantineMod("../outside", moved, err));
    });

    std::printf("\n%d tests, %d failed checks\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
