// Native save directory for the hybrid runtime. Never touches the retail save.
//
//   <dir>/vanilla.mcr          retail memory card image: NOT read or written here
//   <dir>/natureloader.json    SaveExtension (mods+versions, IdTable, flags, mod_data)
//   <dir>/mods/<mod>.json      native entity state owned by one mod (items, ...)
//
// Rules implemented here:
//   * Writes are atomic (temp file + rename) and keep the previous file as <name>.bak.
//   * A mod file is only rewritten by its caller when its content actually changed.
//   * Unparsable files are never overwritten; callers may quarantine them first.
//   * Files of mods that are not loaded are returned verbatim for the caller to keep.
// Limitation: no fsync; a power loss between write and rename can lose the newest save,
// never corrupt the previous one.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "json.h"
#include "save.h"

namespace nm {

struct ModStateFile {
    std::string mod;     // file stem, a valid local id
    std::string path;
    Json doc;            // valid only when !damaged
    bool damaged = false;
    std::string problem; // why it could not be parsed
};

class NativeSaveStore {
public:
    explicit NativeSaveStore(std::string dir) : dir_(std::move(dir)) {}
    const std::string& dir() const { return dir_; }
    std::string corePath() const;
    std::string modPath(const std::string& mod) const;

    // natureloader.json. A missing file yields a fresh SaveExtension and *existed=false.
    bool loadCore(SaveExtension& out, bool* existed, std::string& err) const;
    bool saveCore(const SaveExtension& save, std::string& err) const;

    // Read every mods/<id>.json, sorted by name. Files with an invalid name are skipped with a warning.
    bool scanMods(std::vector<ModStateFile>& out, std::vector<std::string>& warnings) const;
    bool writeMod(const std::string& mod, const Json& doc, std::string& err) const;
    // Move mods/<mod>.json aside (<mod>.json.damaged, .damaged-2, ...) so nothing is lost.
    bool quarantineMod(const std::string& mod, std::string& movedTo, std::string& err) const;

    static bool writeFileAtomic(const std::string& path, const std::string& data, std::string& err);

private:
    std::string dir_;
};

}  // namespace nm
