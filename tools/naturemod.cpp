// naturemod: command-line preflight/inspection for mod packs (no Qt, no game data needed).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>

#include "../modcore/session.h"
#include "modcore_catalog.h"

using namespace nm;

static int usage() {
    std::fprintf(stderr,
        "naturemod check   [--catalog base_catalog.json] [--exe SHA256] <mod-dir>...\n"
        "naturemod list    [--catalog ...] <mod-dir>...      (registry dump after loading)\n"
        "Setiap <mod-dir> adalah folder berisi mod.json; jika folder berisi subfolder mod, semuanya dipindai.\n");
    return 2;
}

static int run(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1], catalogPath, exe;
    if (cmd != "check" && cmd != "list") return usage();
    std::vector<std::string> dirs;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--catalog") || !std::strcmp(argv[i], "--exe")) {
            std::string option = argv[i];
            if (i + 1 >= argc) return usage();
            if (option == "--catalog") catalogPath = argv[++i];
            else exe = argv[++i];
        } else if (argv[i][0] == '-') return usage();
        else dirs.push_back(argv[i]);
    }
    if (dirs.empty()) return usage();
    std::string text = natureBaseCatalog;
    if (!catalogPath.empty() && !readWholeFile(catalogPath, text)) {
        std::fprintf(stderr, "katalog dasar tidak terbaca: %s\n", catalogPath.c_str());
        return 2;
    }
    HostInfo hi;
    hi.exeSha256 = exe;
    Session s(hi, Json::parse(text));
    namespace fs = std::filesystem;
    for (const auto& d : dirs) {
        if (fs::exists(fs::path(d) / "mod.json")) s.addPack(std::make_shared<DirPackSource>(d));
        else {
            std::vector<fs::path> packs;
            for (const auto& e : fs::directory_iterator(d))
                if (e.is_directory() && fs::exists(e.path() / "mod.json")) packs.push_back(e.path());
            if (packs.empty()) {
                std::fprintf(stderr, "folder tidak berisi mod.json atau subfolder mod: %s\n", d.c_str());
                return 2;
            }
            std::sort(packs.begin(), packs.end());
            for (const auto& p : packs) s.addPack(std::make_shared<DirPackSource>(p.string()));
        }
    }
    SaveExtension save;
    bool ok = s.load(&save);
    std::printf("%s", s.report().text().c_str());
    std::printf("Dimuat (%zu):", s.report().loaded.size());
    for (const auto& id : s.report().loaded) std::printf(" %s", id.c_str());
    std::printf("\n");
    if (cmd == "list") {
        for (const char* k : {"item", "npc", "area", "shop", "recipe", "event", "building"}) {
            for (const Record* r : s.registry().all(k)) {
                if (r->owner == "base") continue;
                std::printf("%-8s %-32s handle=%-5d %s\n", k, r->id.c_str(), r->handle, Registry::localized(r->data["name"], "en").c_str());
            }
        }
        for (const auto& o : s.vfs().overrides())
            std::printf("override %s -> %s (x%d) [%s]\n", o.target.c_str(), o.asset.c_str(), o.scale, o.owner.c_str());
    }
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) {
        std::fprintf(stderr, "naturemod: %s\n", e.what());
        return 2;
    }
}
