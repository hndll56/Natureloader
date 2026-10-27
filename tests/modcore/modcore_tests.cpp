// Unit tests for the Qt-free mod runtime. Plain asserts with a tiny harness.
#include <cstdio>
#include <functional>
#include <algorithm>
#include <filesystem>

#include "session.h"

using namespace nm;

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
static void test(const char* name, std::function<void()> f) { ++g_run; std::printf("[test] %s\n", name); f(); }

static Json catalog() {
    return Json::parse(R"({"kinds":{
      "item":{"retail_count":380,"mod_first":512,"mod_last":32767,"names":{"0":"nothing","65":"turnip","34":"turnip-seeds"}},
      "area":{"retail_count":1024,"mod_first":1024,"mod_last":65533,"names":{}},
      "npc":{"retail_count":256,"mod_first":256,"mod_last":32767,"names":{}},
      "crop":{"retail_count":22,"mod_first":22,"mod_last":126,"names":{}}}})");
}

static std::string mod(const std::string& id, const std::string& extra = "", const std::string& ver = "1.0.0") {
    return "{\"schema\":2,\"id\":\"" + id + "\",\"name\":\"" + id + "\",\"version\":\"" + ver +
           "\",\"game\":\"SLUS_011.15\",\"api\":\">=1.0 <2\"" + (extra.empty() ? "" : "," + extra) + "}";
}
static std::shared_ptr<MemPackSource> pack(const std::string& manifest, std::map<std::string, std::string> files = {}) {
    auto p = std::make_shared<MemPackSource>("mem");
    p->put("mod.json", manifest);
    for (auto& f : files) p->put(f.first, f.second);
    return p;
}
static std::string errsOf(const Session& s) { return s.report().text(); }
static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
static std::string tinyPng() {
    std::string s("\x89PNG\r\n\x1a\n", 8);
    s += std::string("\0\0\0\x0d", 4) + "IHDR" + std::string("\0\0\0\x10\0\0\0\x10", 8) + std::string("\x08\x06\0\0\0", 5) + std::string(4, '\0');
    return s;
}
static const char* ITEM_OK = R"({"item":{"a":{"name":"A","sell_price":10}}})";

struct FakeHost : HostApi {
    std::map<std::string, int> inv; std::vector<std::string> said; int stamina = 0, gold = 0, dayNo = 1;
    bool giveItem(const std::string& i, int n) override { inv[i] += n; return true; }
    bool removeItem(const std::string& i, int n) override { inv[i] -= n; return true; }
    int countItem(const std::string& i) override { return inv[i]; }
    void showText(const std::string& t) override { said.push_back(t); }
    void addStamina(int a) override { stamina += a; }
    void addMoney(int a) override { gold += a; }
    int money() override { return gold; }
    int day() override { return dayNo; }
};

int main() {
    test("json: strict parsing", [] {
        CHECK(Json::parse(R"({"a":[1,2.5,"x\u00e9\ud83d\ude00",true,null]})")["a"].size() == 5);
        bool threw = false;
        try { Json::parse(R"({"a":1,"a":2})"); } catch (const JsonError&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { Json::parse(std::string(200, '[') + std::string(200, ']')); } catch (const JsonError&) { threw = true; }
        CHECK(threw);
        for (const char* bad : {"{\"a\":1,}", "[1,]", "{'a':1}", "\"\\ud800\"", "01", "{\"a\":1} x", "\"\t\""}) {
            threw = false;
            try { Json::parse(bad); } catch (const JsonError&) { threw = true; }
            CHECK(threw);
        }
        Json j = Json::parse(R"({"b":1,"a":{"c":[1,2]}})");
        CHECK(Json::parse(j.dump()) == j);
        CHECK(j.path("a.c")[1].asInt() == 2);
    });

    test("version ranges", [] {
        Version v; CHECK(Version::parse("1.2.3", v)); CHECK(!Version::parse("1..2", v)); CHECK(!Version::parse("1.2.3.4", v)); CHECK(!Version::parse("", v));
        VersionRange r;
        CHECK(VersionRange::parse(">=1.0 <2", r)); CHECK(r.satisfies({1, 5, 0})); CHECK(!r.satisfies({2, 0, 0})); CHECK(!r.satisfies({0, 9, 0}));
        CHECK(VersionRange::parse("^0.2.1", r)); CHECK(r.satisfies({0, 2, 5})); CHECK(!r.satisfies({0, 3, 0}));
        CHECK(VersionRange::parse("~1.2", r)); CHECK(r.satisfies({1, 2, 9})); CHECK(!r.satisfies({1, 3, 0}));
        CHECK(VersionRange::parse("*", r)); CHECK(r.satisfies({9, 9, 9}));
        CHECK(!VersionRange::parse(">=x", r));
    });

    test("manifest validation and v1 adapter", [] {
        auto bad = [](const std::string& s) { return !parseManifest(Json::parse(s)).ok; };
        auto fractional = Json::parse(mod("fractional"));
        fractional["schema"] = 2.5;
        CHECK(!parseManifest(fractional).ok);
        CHECK(bad(mod("Bad Id")));
        CHECK(bad(mod("base")));
        CHECK(bad(mod("con")));
        CHECK(bad(mod("x", "\"mystery\":1")));
        CHECK(bad(mod("x", "\"permissions\":[\"root\"]")));
        CHECK(bad(mod("x", "\"content\":[\"../evil.json\"]")));
        CHECK(bad(mod("x", "\"depends\":{\"y\":\"???\"}")));
        CHECK(bad(R"({"schema":2,"id":"x","name":"x","version":"1.0.0","game":"SLUS_011.15"})"));  // no api
        CHECK(bad(mod("x", "\"plugins\":[{\"file\":\"p.so\",\"sha256\":\"00\"}]")));
        CHECK(parseManifest(Json::parse(mod("ok-mod_1"))).ok);
        auto v1 = parseManifest(Json::parse(R"({"schema":1,"id":"old","name":"Old","version":"1.0.0","game":"SLUS_011.15",
            "content":{"old:thing":{"type":"item","name":"Thing","data":{"sell_price":5}}},"texts":{"messages.promptYesNo":"x"}})"));
        CHECK(v1.ok); CHECK(v1.manifest.inlineContent["item"]["thing"]["sell_price"].asInt() == 5);
        CHECK(!parseManifest(Json::parse(R"({"schema":1,"id":"old","name":"Old","version":"1","game":"X","content":{"other:thing":{"type":"item","name":"T"}}})")).ok);
    });

    test("resolver: deps, cascade, conflicts, cycles, ordering", [] {
        auto M = [](const std::string& id, const std::string& extra = "", const std::string& v = "1.0.0") {
            return parseManifest(Json::parse(mod(id, extra, v))).manifest;
        };
        ResolveInput in; in.gameId = "SLUS_011.15"; in.hostApi = {1, 0, 0};
        in.mods = {M("c", "\"depends\":{\"b\":\"*\"}"), M("b", "\"depends\":{\"a\":\">=2\"}"), M("a", "", "1.0.0"), M("free")};
        auto r = resolveMods(in);
        CHECK(r.rejected.count("b")); CHECK(r.rejected.count("c")); CHECK(!r.rejected.count("a")); CHECK(!r.rejected.count("free"));
        // ordering
        in.mods = {M("z", "\"depends\":{\"y\":\"*\"}"), M("y"), M("x", "\"load_after\":[\"z\"]"), M("w", "\"load_before\":[\"y\"]")};
        r = resolveMods(in);
        auto pos = [&](const char* id) { return std::find(r.order.begin(), r.order.end(), id) - r.order.begin(); };
        CHECK(r.order.size() == 4); CHECK(pos("y") < pos("z")); CHECK(pos("z") < pos("x")); CHECK(pos("w") < pos("y"));
        // user priority tie-break: independent mods keep given order
        in.mods = {M("m2"), M("m1"), M("m3")};
        r = resolveMods(in);
        CHECK(r.order == (std::vector<std::string>{"m2", "m1", "m3"}));
        // conflicts reject both sides
        in.mods = {M("p", "\"conflicts\":{\"q\":\"<2\"}"), M("q"), M("ok")};
        r = resolveMods(in);
        CHECK(r.rejected.count("p") && r.rejected.count("q") && !r.rejected.count("ok"));
        // cycle
        in.mods = {M("c1", "\"load_after\":[\"c2\"]"), M("c2", "\"load_after\":[\"c1\"]"), M("fine")};
        r = resolveMods(in);
        CHECK(r.rejected.count("c1") && r.rejected.count("c2")); CHECK(r.order == std::vector<std::string>{"fine"});
        // api + game mismatch, duplicates, optional dep
        in.mods = {parseManifest(Json::parse(R"({"schema":2,"id":"future","name":"f","version":"1.0.0","game":"SLUS_011.15","api":">=2"})")).manifest,
                   M("dup"), M("dup"), M("opt", "\"optional\":{\"ghost\":\"*\"}")};
        r = resolveMods(in);
        CHECK(r.rejected.count("future")); CHECK(r.rejected.count("dup")); CHECK(!r.rejected.count("opt"));
    });

    test("registry: schemas, ranges, refs, dependency rule", [] {
        auto load = [](std::vector<std::shared_ptr<MemPackSource>> packs) {
            auto s = std::make_unique<Session>(HostInfo{}, catalog());
            for (auto& p : packs) s->addPack(p);
            s->load(nullptr);
            return s;
        };
        auto s = load({pack(mod("a", "\"content\":{\"item\":{\"i\":{\"name\":\"I\",\"sell_price\":40000}}}"))});
        CHECK(has(errsOf(*s), "sell_price"));                       // s16 overflow rejected
        s = load({pack(mod("a", "\"content\":{\"item\":{\"i\":{\"name\":\"I\",\"category\":\"weapon\"}}}"))});
        CHECK(has(errsOf(*s), "category"));
        s = load({pack(mod("a", "\"content\":{\"item\":{\"i\":{\"name\":\"I\",\"inherit\":\"base:ghost\"}}}"))});
        CHECK(has(errsOf(*s), "tujuan tidak ada"));
        s = load({pack(mod("a", "\"content\":{\"item\":{\"i\":{\"name\":\"I\"}}}")),
                  pack(mod("b", "\"content\":{\"recipe\":{\"r\":{\"ingredients\":[\"a:i\"],\"result\":\"a:i\"}}}"))});
        CHECK(has(errsOf(*s), "tanpa mendeklarasikannya"));        // reference without declared dependency
        CHECK(!s->registry().find("b:r"));
        s = load({pack(mod("a", "\"content\":{\"item\":{\"i\":{\"name\":\"I\"}}}")),
                  pack(mod("b", "\"depends\":{\"a\":\"*\"},\"content\":{\"recipe\":{\"r\":{\"ingredients\":[\"a:i\"],\"result\":\"a:i\"}}}"))});
        CHECK(s->registry().find("b:r") != nullptr);
        // area exit to unknown entrance
        s = load({pack(mod("w", "\"content\":{\"area\":{\"one\":{\"map\":\"w:m.map\",\"entrances\":[{\"id\":\"in\",\"x\":1,\"y\":1}],\"exits\":[{\"rect\":[0,0,1,1],\"to\":\"w:two\",\"entrance\":\"nope\"}]},"
                          "\"two\":{\"map\":\"w:m.map\",\"entrances\":[{\"id\":\"in\",\"x\":1,\"y\":1}]}}}"), {{"assets/m.map", "x"}})});
        CHECK(has(errsOf(*s), "entrance 'nope'"));
        // unknown field warns, does not reject
        s = load({pack(mod("a", "\"content\":{\"item\":{\"i\":{\"name\":\"I\",\"typo_field\":1}}}"))});
        CHECK(s->registry().find("a:i") && has(errsOf(*s), "typo_field"));
    });

    test("handles: stable across load order and missing mods", [] {
        auto run = [](std::vector<std::string> ids, SaveExtension& save) {
            Session s(HostInfo{}, catalog());
            for (auto& id : ids) s.addPack(pack(mod(id, std::string("\"content\":") + ITEM_OK)));
            s.setUserOrder(ids);           // explicit priority = load order
            s.load(&save);
            std::map<std::string, int> h;
            for (auto& id : ids) if (auto* r = s.registry().find(id + ":a")) h[id] = r->handle;
            return h;
        };
        SaveExtension save;
        auto h1 = run({"aa", "bb"}, save);
        CHECK(h1["aa"] == 512 && h1["bb"] == 513);
        auto h2 = run({"bb", "aa"}, save);                // different load order, same save
        CHECK(h2 == h1);
        auto h3 = run({"cc"}, save);                      // bb/aa missing: their handles stay reserved
        CHECK(h3["cc"] == 514);
        auto h4 = run({"aa", "bb", "cc"}, save);
        CHECK(h4["aa"] == 512 && h4["bb"] == 513 && h4["cc"] == 514);
        // fresh save with other order differs: proves it was the save that kept numbers stable
        SaveExtension fresh;
        auto h5 = run({"bb", "aa"}, fresh);
        CHECK(h5["bb"] == 512);
    });

    test("actions: budget, permissions, recursion, isolation", [] {
        FakeHost host;
        SaveExtension flags;
        ActionEngine eng;
        std::set<std::string> perms = {"inventory", "player"};
        auto errs = eng.load("m", Json::parse(R"({"handlers":{
            "burn":[{"do":"repeat","times":1000,"do_list":[{"do":"repeat","times":1000,"do_list":[{"do":"set_var","name":"x","value":1}]}]}],
            "loop":[{"do":"call","handler":"loop"}],
            "ok":[{"do":"give_item","item":"thing","count":2},{"do":"add_stamina","amount":5}]}})"), perms);
        CHECK(errs.empty());
        HookCtx ctx; std::string err;
        CHECK(!eng.run("m:burn", ctx, host, &flags, &err)); CHECK(has(err, "anggaran"));   // cannot be caught or pcall'ed
        CHECK(!eng.run("m:loop", ctx, host, &flags, &err)); CHECK(has(err, "kedalaman"));
        CHECK(eng.run("m:ok", ctx, host, &flags, &err)); CHECK(host.inv["m:thing"] == 2);
        // permission checked statically at load
        ActionEngine e2;
        errs = e2.load("m", Json::parse(R"({"handlers":{"x":[{"do":"warp","area":"m:z"}]}})"), perms);
        CHECK(!errs.empty() && has(errs[0], "izin"));
        errs = e2.load("m", Json::parse(R"({"handlers":{"y":[{"do":"format_disk"}]}})"), perms);
        CHECK(!errs.empty());
        // hook bus: faulting handler is muted after the limit while others keep running
        HookBus bus; int good = 0;
        bus.bind("bad", "day.start", "", 0, [](HookCtx&) { throw std::runtime_error("boom"); });
        bus.bind("good", "day.start", "", 1, [&](HookCtx&) { ++good; });
        for (int i = 0; i < 8; ++i) { HookCtx c; c.hook = "day.start"; bus.fire(c); }
        CHECK(good == 8); CHECK(bus.muted("bad")); CHECK(!bus.muted("good"));
        // cancelable hook stops later handlers; non-cancelable ignores cancel
        HookBus b2; int later = 0;
        b2.bind("a", "item.use", "", 0, [](HookCtx& c) { c.cancelled = true; });
        b2.bind("b", "item.use", "", 1, [&](HookCtx&) { ++later; });
        HookCtx c; c.hook = "item.use"; auto rep = b2.fire(c);
        CHECK(rep.cancelled && later == 0);
        b2.bind("a", "day.start", "", 0, [](HookCtx& c2) { c2.cancelled = true; });
        b2.bind("b", "day.start", "", 1, [&](HookCtx&) { ++later; });
        HookCtx d; d.hook = "day.start"; rep = b2.fire(d);
        CHECK(!rep.cancelled && later == 1);
    });

    test("save extension: roundtrip, corruption, missing-mod report", [] {
        SaveExtension s;
        s.mods = {{"kebunku", "1.0.0", 1}};
        s.ids.ids["item"]["kebunku:lobak"] = 513; s.ids.next["item"] = 514;
        s.flags["kebunku:x"] = Json(3); s.modData["kebunku"] = Json::parse(R"({"visited":["a"]})");
        SaveExtension back; std::string err;
        CHECK(SaveExtension::fromJson(Json::parse(s.toJson().dump()), back, err));
        CHECK(back.ids.ids["item"]["kebunku:lobak"] == 513 && back.flags["kebunku:x"].asInt() == 3);
        for (const char* mut : {"{}", R"({"format":1})", R"({"format":2,"mods":[],"idtable":{"ids":{},"next":{}},"mod_data":{},"flags":{}})"}) {
            SaveExtension t; CHECK(!SaveExtension::fromJson(Json::parse(mut), t, err));
        }
        Json bad = s.toJson(); bad["idtable"]["ids"]["item"]["kebunku:dua"] = 513;      // duplicate handle
        CHECK(!SaveExtension::fromJson(bad, back, err));
        bad = s.toJson(); bad["idtable"]["ids"]["nonsense"]["a:b"] = 1;
        CHECK(!SaveExtension::fromJson(bad, back, err));
        bad = s.toJson(); bad["flags"]["noprefix"] = 1;
        CHECK(!SaveExtension::fromJson(bad, back, err));
        auto c = checkSave(s, {});
        CHECK(c.missing.size() == 1 && c.missing[0].content.size() == 1 && has(c.describe(), "item kebunku:lobak"));
        c = checkSave(s, {{"kebunku", "0.9.0", 1}});
        CHECK(c.changed.size() == 1 && c.changed[0].newerSave);
    });

    test("vfs: png validation, overrides, path safety", [] {
        CHECK(safePackPath("assets/a.png")); CHECK(!safePackPath("../a")); CHECK(!safePackPath("/a")); CHECK(!safePackPath("a//b"));
        CHECK(!safePackPath("a\\b")); CHECK(!safePackPath("c:evil")); CHECK(!safePackPath("a/./b"));
        int w = 0, h = 0; CHECK(pngDimensions(tinyPng(), w, h) && w == 16 && h == 16); CHECK(!pngDimensions("nope", w, h));
        Session s(HostInfo{}, catalog());
        s.addPack(pack(mod("g", "\"content\":{\"sprite\":{\"s\":{\"image\":\"g:bad.png\"}}}"), {{"assets/bad.png", "notapng"}}));
        s.load(nullptr);
        CHECK(has(errsOf(s), "PNG tidak valid")); CHECK(s.report().loaded.empty());
        Session s2(HostInfo{}, catalog());
        s2.addPack(pack(mod("g", "\"content\":{\"asset_override\":{\"o\":{\"target\":\"base:tex/1/1\",\"file\":\"g:ok.png\",\"scale\":2}}}"), {{"assets/ok.png", tinyPng()}}));
        s2.addPack(pack(mod("h", "\"load_after\":[\"g\"],\"content\":{\"asset_override\":{\"o\":{\"target\":\"base:tex/1/1\",\"file\":\"h:ok.png\",\"scale\":4}}}"), {{"assets/ok.png", tinyPng()}}));
        s2.load(nullptr);
        CHECK(s2.vfs().overrideFor("base:tex/1/1") && s2.vfs().overrideFor("base:tex/1/1")->owner == "h");   // last wins
        CHECK(has(errsOf(s2), "menimpa override"));
        // overrides may only target base assets
        Session s3(HostInfo{}, catalog());
        s3.addPack(pack(mod("g", "\"content\":{\"asset_override\":{\"o\":{\"target\":\"other:x\",\"file\":\"g:ok.png\"}}}"), {{"assets/ok.png", tinyPng()}}));
        s3.load(nullptr);
        CHECK(has(errsOf(s3), "target override"));
        DirPackSource dp(std::filesystem::temp_directory_path().string());
        CHECK(!dp.exists("../etc/passwd"));
    });

    test("session: failing mod is isolated, dependents follow, others load", [] {
        Session s(HostInfo{}, catalog());
        s.addPack(pack(mod("good", std::string("\"content\":") + ITEM_OK)));
        s.addPack(pack(mod("broken", "\"scripts\":[\"s.json\"]"), {{"s.json", R"({"handlers":{"h":[{"do":"nonsense"}]}})"}}));
        s.addPack(pack(mod("child", "\"depends\":{\"broken\":\"*\"}")));
        CHECK(!s.load(nullptr));
        CHECK(s.report().loaded == std::vector<std::string>{"good"});
        CHECK(s.report().rejected.count("broken") && s.report().rejected.count("child"));
        CHECK(s.registry().find("good:a") && !s.registry().find("broken:x"));
        Session pre(HostInfo{}, catalog());
        pre.addPack(pack(mod("a", "\"depends\":{\"missing\":\"*\"}")));
        CHECK(!pre.preflight().rejected.empty());
        Session junk(HostInfo{}, catalog());
        junk.addPack(pack("{ not json"));
        junk.load(nullptr);
        CHECK(has(errsOf(junk), "JSON"));
    });

    test("patches: base patch, conflicting patch warning, cross-mod patch needs dependency", [] {
        auto P = [](const std::string& id, const std::string& deps, int price) {
            return pack(mod(id, deps + "\"content\":{\"$patch\":{\"base:turnip\":{\"sell_price\":" + std::to_string(price) + "}}}"));
        };
        Session s(HostInfo{}, catalog());
        s.addPack(P("one", "", 50)); s.addPack(P("two", "\"load_after\":[\"one\"],", 60));
        s.load(nullptr);
        CHECK(s.registry().find("base:turnip")->data["sell_price"].asInt() == 60);
        CHECK(has(errsOf(s), "menimpa field 'sell_price'"));
        Session bad(HostInfo{}, catalog());
        bad.addPack(pack(mod("p", "\"content\":{\"$patch\":{\"base:turnip\":{\"made_up\":1}}}")));
        bad.load(nullptr);
        CHECK(has(errsOf(bad), "field tidak dikenal"));
    });

    test("native plugin gate", [] {
        std::string bin;
        CHECK(readWholeFile(PLUGIN_SO, bin));
        std::string sha = sha256Hex(bin), exe(64, 'a');
        auto manifest = [&](const std::string& pluginSha, const std::string& exeHash) {
            return mod("native-demo", "\"plugins\":[{\"file\":\"native/p.so\",\"sha256\":\"" + pluginSha + "\",\"exe_hashes\":[\"" + exeHash + "\"],\"api\":\">=1.0 <2\"}]");
        };
        auto run = [&](const std::string& pluginSha, const std::string& exeHash, bool approve, bool* fired = nullptr) {
            HostInfo hi; hi.exeSha256 = exe;
            auto s = std::make_unique<Session>(hi, catalog());
            auto p = pack(manifest(pluginSha, exeHash), {{"native/p.so", bin}});
            s->addPack(p);
            ApprovalStore a;
            if (approve) a.approve("native-demo", "1.0.0", sha);
            s->setApprovals(a);
            FakeHost* host = new FakeHost();
            static std::vector<std::unique_ptr<FakeHost>> keep; keep.emplace_back(host);
            s->setHostApi(host);
            bool ok = s->load(nullptr);
            if (fired && ok) {
                HookCtx c; c.hook = "item.use"; c.subject = "native-demo:granat";
                auto r = s->fire(c);
                *fired = r.cancelled && c.result["native"].asBool() && !host->said.empty();
            }
            return std::make_pair(ok, errsOf(*s));
        };
        bool fired = false;
        auto r = run(sha, exe, true, &fired);
        CHECK(r.first); CHECK(fired);
        r = run(sha, exe, false);            CHECK(!r.first && has(r.second, "belum disetujui"));
        r = run(sha, std::string(64, 'b'), true); CHECK(!r.first && has(r.second, "executable game lain"));
        r = run(std::string(64, '0'), exe, true); CHECK(!r.first && has(r.second, "hash biner"));
        CHECK(checkPlugin({"f", sha, {exe}, VersionRange::any()}, sha, exe, {1, 0, 0}, true).ok);
        CHECK(!checkPlugin({"f", sha, {exe}, VersionRange::any()}, sha, exe, {1, 0, 0}, false).ok);
        // approval is bound to version + hash
        ApprovalStore a; a.approve("m", "1.0.0", sha);
        CHECK(a.approved("m", "1.0.0", sha) && !a.approved("m", "1.0.1", sha) && !a.approved("m", "1.0.0", std::string(64, 'c')));
        ApprovalStore a2; CHECK(ApprovalStore::fromJson(a.toJson(), a2) && a2.approved("m", "1.0.0", sha));
    });

    test("sha256 known vectors", [] {
        CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(sha256Hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    });

    std::printf("\n%d tests, %d failed checks\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
