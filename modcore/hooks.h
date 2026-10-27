// Hook bus, host services interface and the sandboxed declarative action engine.
//
// A hook is a point where the game core asks "does any mod want to act here?".
// Handlers are either declarative action scripts (this file) or native plugin
// callbacks. Script execution has a hard, non-recoverable step budget and every
// host call is checked against the permissions the mod declared.
#pragma once
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "json.h"

namespace nm {

// Services the game core provides to mods. Defaults do nothing / fail.
class HostApi {
public:
    virtual ~HostApi() = default;
    virtual bool giveItem(const std::string& itemId, int count) { (void)itemId; (void)count; return false; }
    virtual bool removeItem(const std::string& itemId, int count) { (void)itemId; (void)count; return false; }
    virtual int countItem(const std::string& itemId) { (void)itemId; return 0; }
    virtual void showText(const std::string& text) { (void)text; }
    virtual void addStamina(int amount) { (void)amount; }
    virtual void addMoney(int amount) { (void)amount; }
    virtual int money() { return 0; }
    virtual bool warp(const std::string& areaId, const std::string& entrance) { (void)areaId; (void)entrance; return false; }
    virtual int day() { return 0; }
    virtual uint32_t random(uint32_t n) { (void)n; return 0; }
    virtual void log(int level, const std::string& msg) { (void)level; (void)msg; }
};

// Mod-owned persistent flags/values (backed by the save extension).
class FlagStore {
public:
    virtual ~FlagStore() = default;
    virtual Json getFlag(const std::string& key) const = 0;
    virtual void setFlag(const std::string& key, const Json& v) = 0;
};

struct HookCtx {
    std::string hook;        // "item.use"
    std::string subject;     // record id the hook is about ("" for global)
    Json args = Json::object();
    Json result = Json::object();
    bool cancelled = false;  // handler asked the core to skip the default behaviour
};

using HookFn = std::function<void(HookCtx&)>;

struct HookReport { int handlersRun = 0; int faults = 0; bool cancelled = false; };

class HookBus {
public:
    static const std::set<std::string>& knownHooks();
    static bool isCancelable(const std::string& hook);

    // Lower priority runs first; ties keep registration (= load) order.
    void bind(const std::string& owner, const std::string& hook, const std::string& subject, int priority, HookFn fn);
    HookReport fire(HookCtx& ctx);
    void setFaultLimit(int n) { faultLimit_ = n; }
    void unbindOwner(const std::string& owner);
    bool muted(const std::string& owner) const;
    const std::map<std::string, int>& faults() const { return faults_; }
    const std::vector<std::string>& log() const { return log_; }
    size_t bindingCount() const { return bindings_.size(); }

private:
    struct Binding { std::string owner, hook, subject; int priority; size_t seq; HookFn fn; };
    std::vector<Binding> bindings_;
    std::map<std::string, int> faults_;
    std::vector<std::string> log_;
    int faultLimit_ = 5;
    size_t seq_ = 0;
};

// Declarative scripts: {"handlers": {"name": [ {"do":"give_item",...}, ... ]}}
class ActionEngine {
public:
    struct Limits { int maxSteps = 20000; int maxCallDepth = 8; int maxRepeat = 1000; int maxExprDepth = 16; };
    ActionEngine() = default;

    // Parse + statically validate. Returns errors (empty on success).
    std::vector<std::string> load(const std::string& mod, const Json& script, const std::set<std::string>& permissions);
    bool has(const std::string& handlerId) const { return handlers_.count(handlerId) != 0; }
    // Handler ids a script wants to call/emit that do not exist (checked after all scripts load).
    std::vector<std::string> unresolvedCalls() const;

    // Run a handler. Never throws; budget exhaustion/denied permission/bad action = fault.
    // Returns false on fault (state changes already made by the host are not rolled back).
    bool run(const std::string& handlerId, HookCtx& ctx, HostApi& host, FlagStore* flags, std::string* error);

    Limits limits;
    // Used by `emit` actions; set by the session.
    std::function<void(HookCtx&)> emitFn;

private:
    struct Handler { std::string mod; Json actions; std::set<std::string> perms; };
    struct Exec;
    std::map<std::string, Handler> handlers_;
    // Nested emit dispatch shares one budget with the initiating script.
    int activeRuns_ = 0;
    int sharedSteps_ = 0;
    int sharedDepth_ = 0;
    std::string invocationFault_;
    friend struct Exec;
};

}  // namespace nm
