// A modding session: preflight, ordered loading phases, runtime services.
//
// Phases (order matters: dialogs/events may reference NPCs/items of other mods):
//   1 manifests + resolve   (versions, deps, conflicts, load order)
//   2 register ids          (every mod's content -> registry, then patches)
//   3 validate              (schemas, cross references, allowed-dependency rule)
//   4 allocate handles      (persisted IdTable keeps numbers stable across saves)
//   5 assets                (index packs, validate files, register overrides)
//   6 scripts + hooks       (record hooks, global hooks, handler resolution)
//   7 native plugins        (hash + exe + API + user approval gate)
// A mod that fails any phase is removed (with its dependents) and the whole
// build restarts without it, so the result never depends on half-loaded state.
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "hooks.h"
#include "manifest.h"
#include "plugin.h"
#include "registry.h"
#include "resolver.h"
#include "save.h"
#include "vfs.h"

namespace nm {

struct HostInfo {
    std::string gameId = "SLUS_011.15";
    std::string exeSha256;
    Version api{1, 0, 0};
    std::string locale = "en";
};

struct Message { enum Level { Info, Warning, Error } level; std::string phase, mod, text; };

struct Report {
    std::vector<Message> messages;
    std::vector<std::string> loaded;                          // in load order
    std::map<std::string, std::vector<std::string>> rejected; // id -> reasons
    bool hasErrors() const { for (const auto& m : messages) if (m.level == Message::Error) return true; return false; }
    std::string text() const;
};

class Session {
public:
    Session(HostInfo host, const Json& baseCatalog);
    ~Session();

    void addPack(std::shared_ptr<PackSource> pack);   // reads mod.json; invalid manifests are reported
    void setUserOrder(const std::vector<std::string>& ids) { userOrder_ = ids; }
    void setHostApi(HostApi* h) { host_ = h ? h : &nullHost_; }
    void setApprovals(const ApprovalStore& a) { approvals_ = a; }

    // Resolve only (phase 1): what would load, and why not. No content is built.
    Report preflight() const;
    // Full build. `save` (optional) supplies persisted id handles/flags and is updated in place.
    bool load(SaveExtension* save);

    const Report& report() const { return report_; }
    const Registry& registry() const { return reg_; }
    Vfs& vfs() { return vfs_; }
    const Vfs& vfs() const { return vfs_; }
    HookBus& hooks() { return bus_; }
    ActionEngine& scripts() { return actions_; }
    const HostInfo& host() const { return info_; }
    std::vector<SaveModEntry> loadedMods() const;
    const Manifest* manifest(const std::string& id) const;

    // Fire a hook through the bus (core -> mods). Fills ctx.result / ctx.cancelled.
    HookReport fire(HookCtx& ctx);
    // Describe content of a handle that is not available (mod missing): for placeholders in UI.
    std::string orphanDescription(const std::string& kind, int32_t handle) const;

private:
    struct Candidate { Manifest m; std::shared_ptr<PackSource> pack; };
    bool build(const std::set<std::string>& excluded, SaveExtension* save, Report& rep, std::set<std::string>& failed);
    std::vector<Manifest> candidates() const;

    HostInfo info_;
    Json base_;
    std::vector<Candidate> cands_;
    std::vector<std::string> bootErrors_;     // "mod: error" for unreadable manifests
    std::vector<std::string> userOrder_;
    HostApi nullHost_;
    HostApi* host_ = &nullHost_;
    ApprovalStore approvals_;
    SaveExtension* save_ = nullptr;
    SaveExtension localSave_;

    Registry reg_;
    Vfs vfs_;
    HookBus bus_;
    ActionEngine actions_;
    std::unique_ptr<PluginHost> plugins_ = std::make_unique<PluginHost>();
    Report report_;
    std::map<std::string, std::string> invoking_;  // modless guard
    IdTable lastIds_;
};

}  // namespace nm
