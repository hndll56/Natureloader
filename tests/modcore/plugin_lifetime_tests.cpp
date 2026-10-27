#ifdef NATURE_PLUGIN_FAIL_FIXTURE
#include "nature_plugin.h"

static const NatureHostApi* failingApi;
static const NaturePluginInfo failingInfo = {NATURE_PLUGIN_ABI, "native-fail", "1.0.0", "SLUS_011.15"};
static void failedCallback(NatureHookCtx*, void*) {
    failingApi->show_text(failingApi->host, "dangling callback");
}
NATURE_EXPORT const NaturePluginInfo* nature_plugin_info(void) { return &failingInfo; }
NATURE_EXPORT int nature_plugin_init(const NatureHostApi* api) {
    failingApi = api;
    api->bind_hook(api->host, "item.use", "", 0, failedCallback, nullptr);
    return -1;
}
NATURE_EXPORT void nature_plugin_shutdown(void) {
    failingApi->show_text(failingApi->host, "failed plugin cleanup");
    failingApi = nullptr;
}
#else
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "session.h"

namespace fs = std::filesystem;
using namespace nm;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

struct PrivateTempRoot {
    fs::path path;
    struct Variable { std::string name, value; bool present; };
    std::vector<Variable> old;
    static void set(const std::string& name, const char* value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1); else unsetenv(name.c_str());
#endif
    }
    PrivateTempRoot() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path base = fs::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            fs::path candidate = base / ("natureloader-plugin-test-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) { path = candidate; break; }
        }
        if (path.empty()) throw std::runtime_error("cannot create plugin test directory");
        for (const char* name : {"TMPDIR", "TMP", "TEMP"}) {
            const char* value = std::getenv(name);
            old.push_back({name, value ? value : "", value != nullptr});
            set(name, path.string().c_str());
        }
    }
    ~PrivateTempRoot() {
        for (const auto& v : old) set(v.name, v.present ? v.value.c_str() : nullptr);
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    size_t entries() const { return static_cast<size_t>(std::distance(fs::directory_iterator(path), fs::directory_iterator())); }
};

struct RecordingHost : HostApi {
    std::vector<std::string> messages;
    void showText(const std::string& text) override { messages.push_back(text); }
};

static std::unique_ptr<Session> approvedSession(const std::string& binary, RecordingHost& host) {
    const std::string sha = sha256Hex(binary), exe(64, 'a');
    HostInfo info;
    info.exeSha256 = exe;
    auto session = std::make_unique<Session>(info, Json::parse(R"({"kinds":{}})"));
    auto pack = std::make_shared<MemPackSource>("plugin lifetime");
    pack->put("mod.json", "{\"schema\":2,\"id\":\"native-demo\",\"name\":\"Demo\",\"version\":\"1.0.0\","
        "\"game\":\"SLUS_011.15\",\"api\":\"^1.0\",\"plugins\":[{\"file\":\"native/plugin.bin\","
        "\"sha256\":\"" + sha + "\",\"exe_hashes\":[\"" + exe + "\"],\"api\":\"^1.0\"}]}");
    pack->put("native/plugin.bin", binary);
    session->addPack(pack);
    ApprovalStore approvals;
    approvals.approve("native-demo", "1.0.0", sha);
    session->setApprovals(approvals);
    session->setHostApi(&host);
    CHECK(session->load(nullptr));
    return session;
}

static void firePlugin(Session& session) {
    HookCtx ctx;
    ctx.hook = "item.use";
    ctx.subject = "native-demo:granat";
    const auto result = session.fire(ctx);
    CHECK(result.handlersRun == 1 && result.cancelled && ctx.result["native"].asBool());
}

int main() {
    PrivateTempRoot temporary;
    std::string binary, failedBinary;
    CHECK(readWholeFile(PLUGIN_SO, binary));
    CHECK(readWholeFile(FAIL_PLUGIN_SO, failedBinary));
    {
        HookBus bus;
        RecordingHost host;
        PluginHost loader;
        std::string error;
        const std::string invalid = "not a shared library";
        CHECK(!loader.load("native-demo", sha256Hex(invalid), invalid, bus, host, {1, 0, 0}, "SLUS_011.15", error));
        CHECK(loader.count() == 0 && temporary.entries() == 0);
        CHECK(!loader.load("wrong-owner", sha256Hex(binary), binary, bus, host, {1, 0, 0}, "SLUS_011.15", error));
        CHECK(loader.count() == 0 && temporary.entries() == 0);
        CHECK(!loader.load("native-fail", sha256Hex(failedBinary), failedBinary, bus, host, {1, 0, 0}, "SLUS_011.15", error));
        CHECK(loader.count() == 0 && temporary.entries() == 0);
        CHECK(host.messages.size() == 1 && host.messages[0] == "failed plugin cleanup");
        HookCtx ctx;
        ctx.hook = "item.use";
        CHECK(bus.fire(ctx).handlersRun == 0);
        CHECK(host.messages.size() == 1);
    }
    {
        RecordingHost firstHost, secondHost;
        auto first = approvedSession(binary, firstHost);
        CHECK(temporary.entries() == 1);
        auto second = approvedSession(binary, secondHost);
        CHECK(temporary.entries() == 2);
#if !defined(_WIN32)
        for (const auto& entry : fs::directory_iterator(temporary.path)) {
            const auto permissions = entry.status().permissions();
            CHECK((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
        }
#endif
        firePlugin(*first);
        CHECK(firstHost.messages.size() == 1 && secondHost.messages.empty());
        firePlugin(*second);
        CHECK(firstHost.messages.size() == 1 && secondHost.messages.size() == 1);
        second.reset();
        CHECK(temporary.entries() == 1);
        firePlugin(*first);
        CHECK(firstHost.messages.size() == 2 && secondHost.messages.size() == 1);
        first.reset();
        CHECK(temporary.entries() == 0);
    }
    std::printf("plugin lifetime tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
#endif
