#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "capi.h"
#include "util.h"
#include "registry.h"

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("nature-item-fields-" + std::to_string(stamp));
    if (!fs::create_directory(dir)) return 1;
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{dir};
    CHECK(nm::writeWholeFile((dir / "mod.json").string(), R"({
      "schema":2,"id":"content-test","name":"Content test","version":"1.0.0",
      "game":"SLUS_011.15","api":"^1.0","content":{
        "item":{
          "inherited":{"name":"Inherited","inherit":"base:turnip","sell_price":0},
          "cleared":{"name":"Cleared","inherit":"base:turnip","flags":[],"stamina":0,"fatigue":-10}
        },
        "npc":{"visitor":{"name":"Visitor"}},
        "$patch":{"base:turnip":{"buy_price":0,"sell_price":120,"stamina":-128}}
      }})"));
    const char* catalog = R"({"kinds":{
      "item":{"retail_count":380,"mod_first":512,"mod_last":32767,"names":{"65":"turnip"}},
      "npc":{"retail_count":256,"mod_first":256,"mod_last":32767,"names":{}}
    }})";
    NmSession* session = nm_create(nullptr, nullptr, catalog, "en");
    CHECK(session != nullptr);
    if (!session) return 1;
    NmItemRow row{};
    row.handle = -123;
    uint32_t fields = 0xFFFFFFFFu;
    CHECK(nm_item_fields(session, 65, &row, &fields) == -1);
    CHECK(row.handle == -123 && fields == 0xFFFFFFFFu);
    CHECK(nm_add_mod_dir(session, dir.string().c_str()) == 0);
    CHECK(nm_load(session, nullptr, nullptr) == 0);
    CHECK(nm_item_fields(session, 65, &row, &fields) == 0);
    CHECK(row.handle == 65 && row.inherit_handle == -1);
    CHECK(fields == (NM_ITEM_BUY_PRICE | NM_ITEM_SELL_PRICE | NM_ITEM_STAMINA));
    CHECK(row.buy_price == 0 && row.sell_price == 120 && row.stamina == -128);
    const int32_t item = nm_handle(session, "item", "content-test:inherited");
    const int32_t npc = nm_handle(session, "npc", "content-test:visitor");
    CHECK(item >= 512 && npc >= 256);
    CHECK(nm_item_fields(session, item, &row, &fields) == 0);
    CHECK(row.inherit_handle == 65 && fields == NM_ITEM_SELL_PRICE && row.sell_price == 0);
    // Applying only present fields preserves inherited values, including flags.
    NmItemRow inherited{};
    inherited.buy_price = 80;
    inherited.sell_price = 60;
    inherited.flags = 0x8000;
    if (fields & NM_ITEM_BUY_PRICE) inherited.buy_price = row.buy_price;
    if (fields & NM_ITEM_SELL_PRICE) inherited.sell_price = row.sell_price;
    if (fields & NM_ITEM_FLAGS) inherited.flags = row.flags;
    CHECK(inherited.buy_price == 80 && inherited.sell_price == 0 && inherited.flags == 0x8000);
    const int32_t cleared = nm_handle(session, "item", "content-test:cleared");
    CHECK(nm_item_fields(session, cleared, &row, &fields) == 0);
    CHECK(fields == (NM_ITEM_FLAGS | NM_ITEM_STAMINA | NM_ITEM_FATIGUE));
    CHECK(row.flags == 0 && row.stamina == 0 && row.fatigue == -10);
    const NmItemRow previous = row;
    const uint32_t previousFields = fields;
    CHECK(nm_item_fields(session, -1, &row, &fields) == -1);
    CHECK(std::memcmp(&row, &previous, sizeof row) == 0 && fields == previousFields);
    CHECK(nm_item_fields(session, item, nullptr, &fields) == -1);
    CHECK(nm_item_fields(session, item, &row, nullptr) == -1);
    char* saved = nullptr;
    CHECK(nm_save(session, &saved) == 0 && saved);
    const std::string state = saved ? saved : "";
    nm_free(saved);
    nm_destroy(session);
    // Reject loops before a runtime adapter attempts to copy inherited rows.
    nm::Registry registry;
    std::string error;
    CHECK(registry.loadBase(nm::Json::parse(catalog), error));
    CHECK(registry.add("cycle", "item", "a", nm::Json::parse(R"({"name":"A","inherit":"cycle:b"})"), error));
    CHECK(registry.add("cycle", "item", "b", nm::Json::parse(R"({"name":"B","inherit":"cycle:a"})"), error));
    CHECK(registry.add("self", "item", "a", nm::Json::parse(R"({"name":"Self","inherit":"self:a"})"), error));
    CHECK(registry.add("valid", "item", "a", nm::Json::parse(R"({"name":"Valid","inherit":"base:turnip"})"), error));
    auto errors = registry.validate({});
    CHECK(errors["cycle"].size() == 2 && errors["self"].size() == 1 && !errors.count("valid"));
    nm::Registry patched;
    CHECK(patched.loadBase(nm::Json::parse(catalog), error));
    CHECK(patched.patch("patcher", "base:turnip", nm::Json::parse(R"({"inherit":"base:turnip"})"), error));
    errors = patched.validate({});
    CHECK(errors.count("patcher") && !errors.count("base"));
    session = nm_create(nullptr, nullptr, catalog, "en");
    CHECK(nm_add_mod_dir(session, dir.string().c_str()) == 0);
    CHECK(nm_load(session, state.c_str(), nullptr) == 0);
    CHECK(nm_handle(session, "item", "content-test:inherited") == item);
    CHECK(nm_handle(session, "npc", "content-test:visitor") == npc);
    CHECK(nm_item_fields(session, item, &row, &fields) == 0 && fields == NM_ITEM_SELL_PRICE);
    nm_destroy(session);
    // Both new item and NPC identities survive a missing mod and its return.
    session = nm_create(nullptr, nullptr, catalog, "en");
    CHECK(nm_load(session, state.c_str(), nullptr) == 0);
    CHECK(nm_id(session, "item", item) == nullptr);
    CHECK(nm_id(session, "npc", npc) == nullptr);
    CHECK(std::string(nm_orphan(session, "item", item)) == "content-test:inherited");
    CHECK(std::string(nm_orphan(session, "npc", npc)) == "content-test:visitor");
    saved = nullptr;
    CHECK(nm_save(session, &saved) == 0 && saved);
    const std::string missingState = saved ? saved : "";
    nm_free(saved);
    nm_destroy(session);
    session = nm_create(nullptr, nullptr, catalog, "en");
    CHECK(nm_add_mod_dir(session, dir.string().c_str()) == 0);
    CHECK(nm_load(session, missingState.c_str(), nullptr) == 0);
    CHECK(nm_handle(session, "item", "content-test:inherited") == item);
    CHECK(nm_handle(session, "npc", "content-test:visitor") == npc);
    nm_destroy(session);
    std::printf("item fields: %s (C ABI only; not BTN gameplay)\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
