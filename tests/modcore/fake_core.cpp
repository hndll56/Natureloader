// End-to-end proof of the architecture using ONLY the C ABI (capi.h), the way the ported
// game core will use it. The "game" here is a stand-in: an item table, inventory, shop,
// renderer queries, area change and a day tick. It is NOT the real BTN core - it shows
// that registry + API + hooks work together:
//   mod item -> shop offers it -> inventory stores the numeric handle -> renderer finds its
//   sprite -> use calls the mod hook -> save keeps ids -> mod removed/restored safely.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "capi.h"
#include "json.h"
#include "util.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

struct Game {
    std::map<std::string, int> inventory;   // stable id -> count (keyed by id for readability)
    std::vector<int32_t> slots;             // what the retail core really stores: numeric ids
    int stamina = 50, gold = 1000, day = 1, areaSection = 1;
    std::vector<std::string> textbox;
    std::string currentArea;
};

static NmHost makeHost(Game* g) {
    NmHost h{};
    h.user = g;
    h.give_item = [](void* u, const char* id, int n) { static_cast<Game*>(u)->inventory[id] += n; return 0; };
    h.remove_item = [](void* u, const char* id, int n) { auto* g = static_cast<Game*>(u); if (g->inventory[id] < n) return -1; g->inventory[id] -= n; return 0; };
    h.count_item = [](void* u, const char* id) { return static_cast<Game*>(u)->inventory[id]; };
    h.show_text = [](void* u, const char* t) { static_cast<Game*>(u)->textbox.push_back(t); };
    h.add_stamina = [](void* u, int a) { static_cast<Game*>(u)->stamina += a; };
    h.add_money = [](void* u, int a) { static_cast<Game*>(u)->gold += a; };
    h.money = [](void* u) { return static_cast<Game*>(u)->gold; };
    h.day = [](void* u) { return static_cast<Game*>(u)->day; };
    h.warp = [](void* u, const char* a, const char*) { static_cast<Game*>(u)->currentArea = a; return 0; };
    h.random = [](void*, uint32_t) { return 0u; };
    return h;
}

struct Ctx { std::string catalog; std::vector<std::string> dirs; };

static NmSession* start(const Ctx& c, Game& g, const std::string& saveIn, std::string* saveOut, int* rejected = nullptr) {
    NmSession* s = nm_create("SLUS_011.15", "", c.catalog.c_str(), "id");
    NmHost h = makeHost(&g);
    nm_set_host(s, &h);
    for (auto& d : c.dirs) nm_add_mod_dir(s, d.c_str());
    char* out = nullptr;
    int rej = nm_load(s, saveIn.empty() ? nullptr : saveIn.c_str(), &out);
    if (rejected) *rejected = rej;
    if (saveOut) *saveOut = out ? out : "";
    nm_free(out);
    return s;
}

static std::string use(NmSession* s, const std::string& id, bool* cancelled = nullptr) {
    char res[256] = {0};
    int c = nm_fire(s, "item.use", id.c_str(), "{}", res, sizeof res);
    if (cancelled) *cancelled = c != 0;
    return res;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::string root = argc > 1 ? argv[1] : "examples/v2";
    Ctx full;
    nm::readWholeFile("modcore/data/base_catalog.json", full.catalog);
    for (auto& e : fs::directory_iterator(root)) if (e.is_directory() && fs::exists(e.path() / "mod.json")) full.dirs.push_back(e.path().string());
    std::sort(full.dirs.begin(), full.dirs.end());
    Ctx noKebun = full;
    noKebun.dirs.erase(std::remove_if(noKebun.dirs.begin(), noKebun.dirs.end(), [](const std::string& d) { return fs::path(d).filename() == "kebunku"; }), noKebun.dirs.end());

    // ---------------------------------------------------------------- session 1: new game with mods
    Game g;
    std::string save1;
    NmSession* s = start(full, g, "", &save1);
    std::printf("[1] muat mod\n%s", nm_report(s));

    // item registry -> numeric handles the retail tables can index
    int32_t lobak = nm_handle(s, "item", "kebunku:lobak-biru");
    int32_t benih = nm_handle(s, "item", "kebunku:benih-lobak-biru");
    CHECK(lobak >= 512 && benih >= 512 && lobak != benih);
    CHECK(nm_handle(s, "item", "base:turnip") == 65);                       // retail ids unchanged
    CHECK(std::string(nm_id(s, "item", lobak)) == "kebunku:lobak-biru");
    CHECK(std::string(nm_name(s, "kebunku:lobak-biru")) == "Lobak Biru");   // locale "id"

    // the core grows its ItemInfo table (game_state.itemInfo is a pointer in the decomp)
    int32_t tableSize = nm_kind_count(s, "item");
    CHECK(tableSize == lobak + 1 || tableSize == benih + 1 || tableSize > 512);
    struct Row { int16_t buy = 0, sell = 0; int8_t st = 0; std::string sprite; int32_t inherit = -1; };
    std::vector<Row> itemInfo(static_cast<size_t>(tableSize));
    for (int32_t h = 0; h < tableSize; ++h) { NmItemRow r; if (nm_item_row(s, h, &r) == 0 && h >= 512) itemInfo[h] = {r.buy_price, r.sell_price, r.stamina, r.sprite_asset, r.inherit_handle}; }
    CHECK(itemInfo[lobak].sell == 120 && itemInfo[lobak].st == 30);
    CHECK(itemInfo[benih].inherit == 34);                                   // copies retail turnip-seeds row first

    // shop: stock comes from the registry, purchase stores the handle in an inventory slot
    CHECK(nm_record_count(s, "shop") == 1);
    nm::Json shop = nm::Json::parse(nm_record_json(s, nm_record_id(s, "shop", 0)));
    int bought = 0;
    for (auto& st : shop["stock"].items()) {
        int32_t h = nm_handle(s, "item", st["item"].asString().c_str());
        CHECK(h >= 0);
        int price = st.has("price") ? int(st["price"].asInt()) : itemInfo[h].buy;
        if (h == benih) { g.gold -= price; g.slots.push_back(h); ++bought; }
    }
    CHECK(bought == 1 && g.gold == 800 && g.slots[0] == benih);

    // renderer: sprite of a mod item is fetched through the asset API and is a real PNG
    char png[4096];
    int64_t n = nm_asset_read(s, itemInfo[lobak].sprite.c_str(), png, sizeof png);
    CHECK(n > 8 && std::memcmp(png, "\x89PNG", 4) == 0);
    // HD mod: renderer asks for an override of a retail texture before loading the original
    char logical[128]; int scale = 0, px = 0, py = 0;
    CHECK(nm_asset_override(s, "base:tex/11/3", logical, sizeof logical, &scale, &px, &py) == 0);
    CHECK(std::string(logical) == "hd-pack:tex/player-hd.png" && scale == 4 && py == 180);
    CHECK(nm_asset_override(s, "base:tex/11/4", logical, sizeof logical, &scale, &px, &py) != 0);  // untouched -> original

    // area: enter hook gives the first-visit gift once
    nm_fire(s, "area.enter", "kebunku:kebun-rahasia", "{}", nullptr, 0);
    CHECK(g.inventory["kebunku:lobak-biru"] == 1);
    nm_fire(s, "area.enter", "kebunku:kebun-rahasia", "{}", nullptr, 0);
    CHECK(g.inventory["kebunku:lobak-biru"] == 1);                          // flag persisted -> no second gift
    CHECK(!g.textbox.empty() && g.textbox[0] == "Ada lobak di dekat gerbang.");
    nm::Json area = nm::Json::parse(nm_record_json(s, "kebunku:kebun-rahasia"));
    CHECK(area["exits"].items()[0]["to_section"].asInt() == 1);

    // item use hook (3 uses -> seeds), numeric handle -> id -> hook
    for (int i = 0; i < 3; ++i) use(s, nm_id(s, "item", lobak));
    CHECK(g.stamina == 50 + 3 * 30);
    CHECK(g.inventory["kebunku:benih-lobak-biru"] == 2);

    // npc talk hook cancels the default dialog
    CHECK(nm_fire(s, "npc.talk", "kebunku:pak-biru", "{}", nullptr, 0) == 1);
    CHECK(nm_fire(s, "npc.talk", "base:nobody", "{}", nullptr, 0) == 0);

    // day tick: global hook with script-to-script call; odd days pay 10
    int gold0 = g.gold; g.day = 3; nm_fire(s, "day.start", "", "{}", nullptr, 0); CHECK(g.gold == gold0 + 10);
    g.day = 4; nm_fire(s, "day.start", "", "{}", nullptr, 0); CHECK(g.gold == gold0 + 10);

    // cross-mod reference works (kebunku-plus recipe uses kebunku item)
    nm::Json rec = nm::Json::parse(nm_record_json(s, "kebunku-plus:sup-lobak"));
    CHECK(rec["ingredients"].items()[0].asString() == "kebunku:lobak-biru");

    CHECK(std::string(nm_faults(s)).empty());                               // no handler faulted during play

    // ---------------------------------------------------------------- save: ids + flags
    char* out = nullptr;
    CHECK(nm_save(s, &out) == 0);
    std::string save2 = out; nm_free(out);
    nm_destroy(s);
    nm::Json sj = nm::Json::parse(save2);
    CHECK(sj["idtable"]["ids"]["item"]["kebunku:lobak-biru"].asInt() == lobak);
    CHECK(sj["mods"].size() == 4);

    // ---------------------------------------------------------------- session 3: kebunku removed
    Game g3 = g;
    std::string save3;
    int rejected = 0;
    s = start(noKebun, g3, save2, &save3, &rejected);
    std::printf("[3] tanpa kebunku\n%s", nm_report(s));
    CHECK(nm_handle(s, "item", "kebunku:lobak-biru") == -1);
    CHECK(nm_id(s, "item", lobak) == nullptr);                              // handle unresolved ...
    CHECK(std::string(nm_orphan(s, "item", lobak)) == "kebunku:lobak-biru"); // ... but the core can explain what it was
    CHECK(std::string(nm_report(s)).find("Mod hilang: kebunku") != std::string::npos);
    CHECK(std::string(nm_report(s)).find("item kebunku:lobak-biru") != std::string::npos);
    CHECK(rejected == 1);                                                   // kebunku-plus lost its dependency, rejected cleanly
    // a stale handle in an inventory slot must not crash: core shows placeholder, keeps the number
    int32_t slot = g.slots[0];
    NmItemRow orphanRow;
    CHECK(nm_item_row(s, slot, &orphanRow) != 0);                           // no row: core must use its placeholder
    CHECK(std::string(nm_orphan(s, "item", slot)) == "kebunku:benih-lobak-biru");
    CHECK(nm_save(s, &out) == 0);
    save3 = out; nm_free(out);
    nm_destroy(s);

    // data survived: ids/flags still in the save written without the mod
    nm::Json s3j = nm::Json::parse(save3);
    CHECK(s3j["idtable"]["ids"]["item"]["kebunku:lobak-biru"].asInt() == lobak);
    CHECK(s3j["flags"]["kebunku:kunjungan-pertama"].asBool());

    // ---------------------------------------------------------------- session 4: mod restored
    Game g4;
    std::string save4;
    s = start(full, g4, save3, &save4);
    CHECK(nm_handle(s, "item", "kebunku:lobak-biru") == lobak);             // same number as before
    CHECK(std::string(nm_id(s, "item", slot)) == "kebunku:benih-lobak-biru");
    nm_fire(s, "area.enter", "kebunku:kebun-rahasia", "{}", nullptr, 0);
    CHECK(g4.inventory["kebunku:lobak-biru"] == 0);                         // visit flag was kept while mod was absent
    CHECK(std::string(nm_report(s)).find("Mod hilang") == std::string::npos);
    nm_destroy(s);

    // ---------------------------------------------------------------- corrupt save does not take the host down
    Game g5; std::string save5;
    s = start(full, g5, "{\"format\":1,\"mods\":[],\"idtable\":{\"ids\":{},\"next\":{}},\"mod_data\":null}", &save5);
    CHECK(std::string(nm_report(s)).find("save mod ditolak") != std::string::npos);
    CHECK(nm_handle(s, "item", "kebunku:lobak-biru") >= 512);               // session still usable with a fresh table
    nm_destroy(s);
    s = start(full, g5, "garbage{", &save5);
    CHECK(std::string(nm_report(s)).find("rusak") != std::string::npos);
    nm_destroy(s);

    std::printf("\nfake core: %s (%d gagal)\n", g_fail ? "GAGAL" : "LULUS", g_fail);
    return g_fail ? 1 : 0;
}
