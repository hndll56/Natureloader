#include "plugin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>

#include "nature_plugin.h"
#include "util.h"

#if defined(_WIN32)
#include <windows.h>
#include <sddl.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace nm {

PluginCheck checkPlugin(const PluginDecl& d, const std::string& actual, const std::string& exe, const Version& api, bool approved) {
    if (actual != d.sha256) return {false, "hash biner tidak cocok dengan manifest (file berubah?)"};
    bool exeOk = false;
    for (const auto& h : d.exeHashes) if (h == exe) exeOk = true;
    if (!exeOk) return {false, "plugin dibuat untuk executable game lain; menolak memuat agar patch tidak salah sasaran"};
    if (!d.api.satisfies(api)) return {false, "plugin butuh API " + d.api.str() + ", host " + api.str()};
    if (!approved) return {false, "belum disetujui pengguna"};
    return {true, ""};
}

Json ApprovalStore::toJson() const {
    Json a = Json::array();
    for (const auto& k : keys_) a.push(k);
    Json j = Json::object();
    j["approved"] = a;
    return j;
}
bool ApprovalStore::fromJson(const Json& j, ApprovalStore& out) {
    if (!j["approved"].isArray()) return false;
    ApprovalStore s;
    for (const auto& e : j["approved"].items()) { if (!e.isString()) return false; s.keys_.insert(e.asString()); }
    out = s;
    return true;
}

struct PluginHost::Lib {
    std::string modId, tmpPath;
    std::filesystem::path tmpDirectory;
    void* handle = nullptr;
    void (*shutdown)() = nullptr;
    bool initStarted = false;
    struct Bridge { std::string modId; HookBus* bus = nullptr; HostApi* host = nullptr; } bridge;
    NatureHostApi api{};

    ~Lib() {
        if (bridge.bus) bridge.bus->unbindOwner(modId);
        if (initStarted && shutdown) {
            try { shutdown(); } catch (...) { /* Never throw while releasing a plugin. */ }
        }
        if (bridge.bus) bridge.bus->unbindOwner(modId);
#if defined(_WIN32)
        if (handle) FreeLibrary(static_cast<HMODULE>(handle));
#else
        if (handle) dlclose(handle);
#endif
        std::error_code ec;
        if (!tmpDirectory.empty()) std::filesystem::remove_all(tmpDirectory, ec);
    }
};

namespace {
bool createPluginDirectory(std::filesystem::path& directory, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) { err = "direktori sementara tidak tersedia"; return false; }
#if defined(_WIN32)
    // Give access only to this directory's owner and SYSTEM, with inheritance.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr)) {
        err = "gagal menyiapkan izin direktori plugin";
        return false;
    }
    auto freeDescriptor = [](void* p) { LocalFree(p); };
    std::unique_ptr<void, decltype(freeDescriptor)> guard(descriptor, freeDescriptor);
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    std::random_device random;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const fs::path candidate = base / ("natureloader-plugins-" + std::to_string(random()) + "-" + std::to_string(random()));
        if (CreateDirectoryW(candidate.c_str(), &attributes)) { directory = candidate; return true; }
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
#else
    // mkdtemp creates a fresh directory atomically with owner-only mode 0700.
    const std::string pattern = (base / "natureloader-plugins-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    if (char* created = mkdtemp(writable.data())) { directory = created; return true; }
#endif
    err = "gagal membuat direktori plugin pribadi";
    return false;
}
}

PluginHost::PluginHost() = default;

PluginHost::~PluginHost() = default;

void PluginHost::unloadAll(HookBus& bus) {
    for (auto& l : libs_) bus.unbindOwner(l->modId);
    libs_.clear();
}

bool PluginHost::load(const std::string& modId, const std::string& expectedSha, const std::string& binary, HookBus& bus, HostApi& host,
                      const Version& api, const std::string& gameId, std::string& err) {
    if (sha256Hex(binary) != expectedSha) { err = "hash biner berubah antara pemeriksaan dan pemuatan"; return false; }
    namespace fs = std::filesystem;
    auto lib = std::make_unique<Lib>();
    lib->modId = modId;
    if (!createPluginDirectory(lib->tmpDirectory, err)) return false;
#if defined(_WIN32)
    std::string ext = ".dll";
#else
    std::string ext = ".so";
#endif
    fs::path file = lib->tmpDirectory / ("plugin" + ext);
    lib->tmpPath = file.string();
    if (!writeWholeFile(file.string(), binary)) { err = "gagal menulis plugin sementara"; return false; }
    std::string back;
    if (!readWholeFile(file.string(), back) || sha256Hex(back) != expectedSha) { err = "file sementara plugin tidak cocok"; return false; }

#if defined(_WIN32)
    lib->handle = LoadLibraryW(file.c_str());
    auto sym = [&](const char* n) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib->handle), n)); };
#else
    lib->handle = dlopen(file.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    auto sym = [&](const char* n) { return dlsym(lib->handle, n); };
#endif
    if (!lib->handle) { err = "gagal membuka library plugin"; return false; }
    auto infoFn = reinterpret_cast<const NaturePluginInfo* (*)()>(sym("nature_plugin_info"));
    auto initFn = reinterpret_cast<int (*)(const NatureHostApi*)>(sym("nature_plugin_init"));
    lib->shutdown = reinterpret_cast<void (*)()>(sym("nature_plugin_shutdown"));
    if (!infoFn || !initFn) { err = "plugin tidak mengekspor nature_plugin_info/init"; return false; }
    const NaturePluginInfo* info = nullptr;
    try { info = infoFn(); }
    catch (...) { err = "nature_plugin_info gagal"; return false; }
    if (!info || info->abi != NATURE_PLUGIN_ABI) { err = "ABI plugin tidak cocok"; return false; }
    if (!info->mod_id || modId != info->mod_id) { err = "plugin mengaku milik mod lain"; return false; }
    if (!info->game_id || gameId != info->game_id) { err = "plugin untuk game lain"; return false; }

    lib->bridge = {modId, &bus, &host};
    lib->api.abi = NATURE_PLUGIN_ABI;
    lib->api.api_major = static_cast<uint32_t>(api.major);
    lib->api.api_minor = static_cast<uint32_t>(api.minor);
    lib->api.host = &lib->bridge;
    lib->api.bind_hook = [](void* h, const char* hook, const char* subject, int prio, NatureHookFn fn, void* user) -> int {
        auto* b = static_cast<Lib::Bridge*>(h);
        if (!hook || !fn || !HookBus::knownHooks().count(hook)) return -1;
        std::string subj = subject ? subject : "";
        b->bus->bind(b->modId, hook, subj, prio, [fn, user](HookCtx& ctx) {
            NatureHookCtx c{};
            c.hook = ctx.hook.c_str();
            c.subject = ctx.subject.c_str();
            std::string args = ctx.args.dump();
            c.args_json = args.c_str();
            fn(&c, user);
            if (c.cancel) ctx.cancelled = true;
            if (c.result_json[0]) {
                c.result_json[sizeof c.result_json - 1] = 0;
                Json r = Json::parse(c.result_json);
                for (const auto& kv : r.members()) ctx.result[kv.first] = kv.second;
            }
        });
        return 0;
    };
    lib->api.log = [](void* h, int lvl, const char* m) { auto* b = static_cast<Lib::Bridge*>(h); b->host->log(lvl, "[" + b->modId + "] " + (m ? m : "")); };
    lib->api.give_item = [](void* h, const char* id, int n) -> int { auto* b = static_cast<Lib::Bridge*>(h); return id && b->host->giveItem(id, n) ? 0 : -1; };
    lib->api.show_text = [](void* h, const char* t) -> int { auto* b = static_cast<Lib::Bridge*>(h); if (!t) return -1; b->host->showText(t); return 0; };
    lib->initStarted = true;
    try {
        if (initFn(&lib->api) != 0) { err = "nature_plugin_init gagal"; return false; }
    } catch (...) { err = "nature_plugin_init gagal"; return false; }
    libs_.push_back(std::move(lib));
    return true;
}

}  // namespace nm
