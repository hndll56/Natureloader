#include "native_save.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "util.h"

namespace fs = std::filesystem;

namespace nm {

namespace {
constexpr size_t kMaxStateFile = 8u * 1024 * 1024;
}

std::string NativeSaveStore::corePath() const { return (fs::path(dir_) / "natureloader.json").string(); }
std::string NativeSaveStore::modPath(const std::string& mod) const { return (fs::path(dir_) / "mods" / (mod + ".json")).string(); }

bool NativeSaveStore::writeFileAtomic(const std::string& path, const std::string& data, std::string& err) {
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) { err = "tidak bisa membuat folder " + target.parent_path().string() + ": " + ec.message(); return false; }
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "tidak bisa menulis " + tmp; return false; }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) { err = "gagal menulis " + tmp; fs::remove(tmp, ec); return false; }
    }
    if (fs::exists(target, ec)) {
        fs::copy_file(target, path + ".bak", fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "gagal membuat cadangan " + path + ".bak: " + ec.message(); fs::remove(tmp, ec); return false; }
    }
#ifdef _WIN32
    // std::filesystem::rename cannot replace an existing file on Windows.
    if (!MoveFileExW(fs::path(tmp).c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    fs::rename(tmp, target, ec);
#endif
    if (ec) { err = "gagal mengganti " + path + ": " + ec.message(); fs::remove(tmp, ec); return false; }
    return true;
}

bool NativeSaveStore::loadCore(SaveExtension& out, bool* existed, std::string& err) const {
    std::error_code ec;
    const std::string path = corePath();
    const bool present = fs::exists(path, ec);
    if (ec) { err = "Cannot inspect " + path + ": " + ec.message(); return false; }
    if (!present) { out = SaveExtension(); if (existed) *existed = false; return true; }
    if (existed) *existed = true;
    std::string text;
    if (!readWholeFile(path, text, kMaxStateFile)) { err = "tidak bisa membaca " + path; return false; }
    try {
        SaveExtension s;
        if (!SaveExtension::fromJson(Json::parse(text), s, err)) { err = path + ": " + err; return false; }
        out = s;
    } catch (const JsonError& e) { err = path + ": " + e.what(); return false; }
    return true;
}

bool NativeSaveStore::saveCore(const SaveExtension& save, std::string& err) const {
    return writeFileAtomic(corePath(), save.toJson().dump(2) + "\n", err);
}

bool NativeSaveStore::scanMods(std::vector<ModStateFile>& out, std::vector<std::string>& warnings) const {
    out.clear();
    std::error_code ec;
    const fs::path dir = fs::path(dir_) / "mods";
    const bool present = fs::exists(dir, ec);
    if (ec) { warnings.push_back("Cannot inspect " + dir.string() + ": " + ec.message()); return false; }
    if (!present) return true;
    if (!fs::is_directory(dir, ec) || ec) { warnings.push_back(dir.string() + " is not a readable directory"); return false; }
    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->is_symlink(e2)) continue;
        if (p.extension() != ".json") continue;  // *.bak, *.damaged, *.tmp are never state files
        files.push_back(p);
    }
    if (ec) { warnings.push_back("Cannot scan " + dir.string() + ": " + ec.message()); return false; }
    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        ModStateFile f;
        f.mod = p.stem().string();
        f.path = p.string();
        if (!validLocalId(f.mod)) { warnings.push_back("nama file state tidak valid, diabaikan: " + p.filename().string()); continue; }
        std::string text;
        if (!readWholeFile(f.path, text, kMaxStateFile)) { f.damaged = true; f.problem = "tidak terbaca atau terlalu besar"; }
        else {
            try { f.doc = Json::parse(text); }
            catch (const JsonError& e) { f.damaged = true; f.problem = e.what(); }
        }
        out.push_back(std::move(f));
    }
    return true;
}

bool NativeSaveStore::writeMod(const std::string& mod, const Json& doc, std::string& err) const {
    if (!validLocalId(mod)) { err = "id mod tidak valid: " + mod; return false; }
    return writeFileAtomic(modPath(mod), doc.dump(2) + "\n", err);
}

bool NativeSaveStore::quarantineMod(const std::string& mod, std::string& movedTo, std::string& err) const {
    if (!validLocalId(mod)) { err = "Invalid mod ID: " + mod; return false; }
    std::error_code ec;
    const std::string src = modPath(mod);
    if (!fs::exists(src, ec)) { movedTo.clear(); return true; }
    for (int n = 1; n < 1000; ++n) {
        const std::string dst = src + (n == 1 ? ".damaged" : ".damaged-" + std::to_string(n));
        if (fs::exists(dst, ec)) continue;
        fs::rename(src, dst, ec);
        if (ec) { err = "gagal memindahkan " + src + ": " + ec.message(); return false; }
        movedTo = dst;
        return true;
    }
    err = "terlalu banyak file .damaged untuk " + mod;
    return false;
}

}  // namespace nm
