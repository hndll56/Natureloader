// Native plugin gate: compatibility checks, user approvals, and the loader.
#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "hooks.h"
#include "manifest.h"

namespace nm {

struct PluginCheck { bool ok = false; std::string reason; };

// Pure decision: may this binary run against this game build and host API?
PluginCheck checkPlugin(const PluginDecl& decl, const std::string& actualSha256, const std::string& exeSha256,
                        const Version& hostApi, bool approvedByUser);

// Approvals bind (mod id, mod version, binary sha256): any change needs approval again.
class ApprovalStore {
public:
    static std::string key(const std::string& mod, const std::string& version, const std::string& sha) { return mod + "@" + version + "#" + sha; }
    void approve(const std::string& mod, const std::string& version, const std::string& sha) { keys_.insert(key(mod, version, sha)); }
    void revoke(const std::string& mod, const std::string& version, const std::string& sha) { keys_.erase(key(mod, version, sha)); }
    bool approved(const std::string& mod, const std::string& version, const std::string& sha) const { return keys_.count(key(mod, version, sha)) != 0; }
    Json toJson() const;
    static bool fromJson(const Json& j, ApprovalStore& out);
private:
    std::set<std::string> keys_;
};

class PluginHost {
public:
    PluginHost();
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    // Loads an already-checked binary. Writes it to a private temp file, hashes what it wrote,
    // dlopen()s, validates exported info against the manifest, then calls init.
    bool load(const std::string& modId, const std::string& expectedSha256, const std::string& binary, HookBus& bus, HostApi& host,
              const Version& api, const std::string& gameId, std::string& err);
    size_t count() const { return libs_.size(); }
    void unloadAll(HookBus& bus);
private:
    struct Lib;
    std::vector<std::unique_ptr<Lib>> libs_;
};

}  // namespace nm
