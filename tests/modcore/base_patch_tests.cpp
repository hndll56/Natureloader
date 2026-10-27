#include <cstdio>
#include <memory>
#include <string>

#include "session.h"

using namespace nm;
static int failures = 0;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); ++failures; } } while (0)

static Json catalog() {
    return Json::parse(R"({"kinds":{
      "item":{"retail_count":380,"mod_first":512,"mod_last":32767,"names":{"0":"nothing","65":"turnip"}},
      "crop":{"retail_count":22,"mod_first":22,"mod_last":126,"names":{"0":"turnip-crop"}},
      "area":{"retail_count":10,"mod_first":10,"mod_last":32767,"names":{"0":"town"}},
      "shop":{"retail_count":10,"mod_first":10,"mod_last":32767,"names":{"0":"market"}}
    }})");
}

static Registry registry() {
    Registry r;
    std::string error;
    CHECK(r.loadBase(catalog(), error));
    return r;
}

static std::shared_ptr<MemPackSource> pack(const std::string& id, const std::string& patch) {
    auto result = std::make_shared<MemPackSource>(id);
    result->put("mod.json", "{\"schema\":2,\"id\":\"" + id + "\",\"name\":\"" + id +
        "\",\"version\":\"1.0.0\",\"game\":\"SLUS_011.15\",\"api\":\"^1.0\","
        "\"content\":{\"$patch\":{\"base:turnip\":" + patch + "}}}");
    return result;
}

int main() {
    std::string error;
    {
        auto r = registry();
        CHECK(r.validate({}).empty()); // Retail catalog rows do not contain required fields.
        CHECK(r.patch("prices", "base:turnip", Json::parse(R"({"sell_price":250,"buy_price":32767,"stamina":127,"fatigue":-128})"), error));
        CHECK(r.validate({}).empty());
        CHECK(r.find("base:turnip")->data["sell_price"].asInt() == 250);
        const auto before = r.find("base:turnip")->data;
        const auto patchCount = r.patches().size();
        for (const char* bad : {R"({"sell_price":32768})", R"({"buy_price":-32769})",
                 R"({"stamina":128})", R"({"fatigue":-129})", R"({"sell_price":"250"})",
                 R"({"sell_price":1.5})", R"({"shippable":1})", R"({"category":"invalid"})",
                 R"({"flags":["invalid"]})", R"({"buy_price":100,"fatigue":128})"}) {
            CHECK(!r.patch("bad", "base:turnip", Json::parse(bad), error));
            CHECK(r.find("base:turnip")->data == before);
            CHECK(r.patches().size() == patchCount);
        }
    }
    {
        auto r = registry();
        CHECK(r.patch("references", "base:turnip", Json::parse(R"({"inherit":"base:missing"})"), error));
        const auto errors = r.validate({});
        CHECK(errors.count("references") == 1);
        CHECK(errors.count("base") == 0);
        CHECK(errors.at("references")[0].find("base:missing") != std::string::npos);
    }
    {
        auto r = registry();
        CHECK(r.add("foreign", "item", "thing", Json::parse(R"({"name":"Thing"})"), error));
        CHECK(r.patch("references", "base:turnip", Json::parse(R"({"inherit":"foreign:thing"})"), error));
        CHECK(r.validate({}).count("references") == 1);
        CHECK(r.validate({{"references", {"foreign"}}}).empty());
        CHECK(r.patch("references", "base:turnip", Json::parse(R"({"crop":"base:turnip"})"), error));
        CHECK(r.validate({{"references", {"foreign"}}}).count("references") == 1);
    }
    {
        auto r = registry();
        CHECK(r.add("source", "item", "thing", Json::parse(R"({"name":"Thing"})"), error));
        CHECK(r.patch("patcher", "source:thing", Json::parse(R"({"inherit":"base:missing"})"), error));
        const auto errors = r.validate({});
        CHECK(errors.count("patcher") == 1);
        CHECK(errors.count("source") == 0);
    }
    {
        auto r = registry();
        CHECK(r.patch("art", "base:turnip", Json::parse(R"({"sprite":"art:sprites/turnip.png"})"), error));
        CHECK(r.validate({}).empty());
        CHECK(r.assetRefs().size() == 1);
        CHECK(r.assetRefs()[0].owner == "art");
        CHECK(r.assetRefs()[0].record == "base:turnip");
    }
    {
        auto r = registry();
        CHECK(r.patch("world", "base:town", Json::parse(R"({"exits":[{"rect":[0,0,1,1],"to":"base:missing","entrance":"in"}]})"), error));
        CHECK(r.patch("shops", "base:market", Json::parse(R"({"stock":[{"item":"base:missing"}]})"), error));
        const auto errors = r.validate({});
        CHECK(errors.count("world") == 1);
        CHECK(errors.count("shops") == 1);
        CHECK(errors.count("base") == 0);
    }
    {
        // The bad patch is rejected before it mutates state, even if a later mod
        // overrides that field. Session rebuilding keeps the independent good mod.
        Session session(HostInfo{}, catalog());
        session.addPack(pack("bad", R"({"sell_price":40000})"));
        session.addPack(pack("good", R"({"sell_price":250})"));
        SaveExtension save;
        CHECK(!session.load(&save));
        CHECK(session.report().loaded == std::vector<std::string>{"good"});
        CHECK(session.report().rejected.count("bad") == 1);
        CHECK(session.report().rejected.count("good") == 0);
        CHECK(session.registry().find("base:turnip")->data["sell_price"].asInt() == 250);
    }
    {
        // Semantic errors are attributed to the mod that supplied the field;
        // excluding it must not remove a separate valid numeric override.
        Session session(HostInfo{}, catalog());
        session.addPack(pack("bad", R"({"inherit":"base:missing"})"));
        session.addPack(pack("good", R"({"sell_price":250})"));
        SaveExtension save;
        CHECK(!session.load(&save));
        CHECK(session.report().loaded == std::vector<std::string>{"good"});
        CHECK(session.report().rejected.count("bad") == 1);
        CHECK(session.registry().find("base:turnip")->data["sell_price"].asInt() == 250);
        CHECK(!session.registry().find("base:turnip")->data.has("inherit"));
    }
    std::printf("base patch tests: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
