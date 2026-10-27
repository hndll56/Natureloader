#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "capi.h"
#include "util.h"

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            fs::path candidate = fs::temp_directory_path() /
                ("natureloader-boundary-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) { path = candidate; return; }
        }
        throw std::runtime_error("cannot create temporary test directory");
    }
    ~TemporaryDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};

static const char* catalog = R"({"kinds":{"item":{"retail_count":380,"mod_first":512,"mod_last":32767,"names":{"0":"nothing"}}}})";

int main() {
    TemporaryDirectory tmp;
    const fs::path pack = tmp.path / "pack";
    const fs::path outside = tmp.path / "outside";
    fs::create_directories(pack / "content");
    fs::create_directories(outside);
    CHECK(nm::writeWholeFile((pack / "content/inside.json").string(), "inside"));
    CHECK(nm::writeWholeFile((outside / "secret.json").string(), "outside"));
    nm::DirPackSource source(pack.string());
    std::string text;
    CHECK(source.exists("content/inside.json"));
    CHECK(source.read("content/inside.json", text) && text == "inside");
    CHECK(!source.read("../outside/secret.json", text));

    std::error_code ec;
    fs::create_directory_symlink(outside, pack / "content/link", ec);
    if (!ec) {
        CHECK(!source.exists("content/link/secret.json"));
        CHECK(!source.read("content/link/secret.json", text));
    } else {
        std::printf("SKIP directory symlink: %s\n", ec.message().c_str());
    }
    ec.clear();
    fs::create_symlink(outside / "secret.json", pack / "leaf.json", ec);
    if (!ec) {
        CHECK(!source.exists("leaf.json"));
        CHECK(!source.read("leaf.json", text));
    }

    // Sanitizer builds also check that failure during construction releases state.
    CHECK(nm_create(nullptr, nullptr, "{invalid", nullptr) == nullptr);
    char sentinel = 0;
    char* saved = &sentinel;
    CHECK(nm_load(nullptr, nullptr, &saved) == -1 && saved == nullptr);

    NmSession* invalid = nm_create(nullptr, nullptr, "{}", nullptr);
    CHECK(invalid != nullptr);
    saved = &sentinel;
    CHECK(nm_load(invalid, nullptr, &saved) == -1 && saved == nullptr);
    CHECK(std::string(nm_report(invalid)).find("katalog") != std::string::npos);
    CHECK(nm_save(invalid, &saved) == -1);
    nm_destroy(invalid);

    // Invalid numeric catalog keys must not send std::stoi exceptions across C ABI.
    invalid = nm_create(nullptr, nullptr,
        R"({"kinds":{"item":{"retail_count":380,"mod_first":512,"mod_last":32767,"names":{"invalid":"nothing"}}}})", nullptr);
    CHECK(invalid != nullptr);
    CHECK(nm_load(invalid, nullptr, nullptr) == -1);
    CHECK(std::string(nm_report(invalid)).find("gagal dimuat") != std::string::npos);
    nm_destroy(invalid);

    NmSession* missing = nm_create(nullptr, nullptr, catalog, nullptr);
    CHECK(missing != nullptr);
    CHECK(nm_add_mod_dir(missing, (tmp.path / "missing-pack").string().c_str()) != 0);
    CHECK(nm_load(missing, nullptr, nullptr) == -1);
    nm_destroy(missing);

    CHECK(nm::writeWholeFile((pack / "mod.json").string(),
        R"({"schema":2,"id":"test-mod","name":"Test","version":"1.0.0","game":"SLUS_011.15","api":"^1.0","depends":{"missing":"^1.0"}})"));
    // Rejected mods must not hide a simultaneous global catalog failure.
    invalid = nm_create(nullptr, nullptr, "{}", nullptr);
    CHECK(invalid != nullptr);
    nm_add_mod_dir(invalid, pack.string().c_str());
    CHECK(nm_load(invalid, nullptr, nullptr) == -1);
    nm_destroy(invalid);

    NmSession* partial = nm_create(nullptr, nullptr, catalog, nullptr);
    CHECK(partial != nullptr);
    nm_add_mod_dir(partial, pack.string().c_str());
    saved = nullptr;
    CHECK(nm_load(partial, nullptr, &saved) > 0 && saved != nullptr);
    nm_free(saved);
    saved = nullptr;
    CHECK(nm_save(partial, &saved) == 0 && saved != nullptr);
    nm_free(saved);
    nm_destroy(partial);

    std::printf("boundary tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
