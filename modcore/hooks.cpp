#include "hooks.h"

#include <algorithm>
#include <limits>

#include "util.h"

namespace nm {

// ------------------------------------------------------------------ HookBus
const std::set<std::string>& HookBus::knownHooks() {
    static const std::set<std::string> h = {
        "game.start", "day.start", "day.end", "item.use", "item.buy", "item.sell", "area.enter", "area.exit",
        "npc.talk", "npc.gift", "crop.harvest", "event.run", "save.before", "save.after", "save.migrate"};
    return h;
}

bool HookBus::isCancelable(const std::string& hook) {
    return hook == "item.use" || hook == "item.buy" || hook == "item.sell" || hook == "area.enter" || hook == "npc.talk" || hook == "event.run";
}

void HookBus::bind(const std::string& owner, const std::string& hook, const std::string& subject, int priority, HookFn fn) {
    bindings_.push_back({owner, hook, subject, priority, seq_++, std::move(fn)});
}

void HookBus::unbindOwner(const std::string& owner) {
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(), [&](const Binding& b) { return b.owner == owner; }), bindings_.end());
}

bool HookBus::muted(const std::string& owner) const {
    auto it = faults_.find(owner);
    return it != faults_.end() && it->second >= faultLimit_;
}

HookReport HookBus::fire(HookCtx& ctx) {
    HookReport rep;
    std::vector<const Binding*> run;
    for (const auto& b : bindings_)
        if (b.hook == ctx.hook && (b.subject.empty() || b.subject == ctx.subject)) run.push_back(&b);
    std::stable_sort(run.begin(), run.end(), [](const Binding* a, const Binding* b) {
        return a->priority != b->priority ? a->priority < b->priority : a->seq < b->seq;
    });
    for (const Binding* b : run) {
        if (muted(b->owner)) continue;
        try {
            b->fn(ctx);
            ++rep.handlersRun;
        } catch (const std::exception& e) {
            ++rep.faults;
            ++faults_[b->owner];
            log_.push_back("[" + b->owner + "] hook " + ctx.hook + " gagal: " + e.what());
        } catch (...) {
            ++rep.faults;
            ++faults_[b->owner];
            log_.push_back("[" + b->owner + "] hook " + ctx.hook + " gagal (tidak diketahui)");
        }
        if (ctx.cancelled && isCancelable(ctx.hook)) break;
    }
    rep.cancelled = ctx.cancelled && isCancelable(ctx.hook);
    return rep;
}

// ------------------------------------------------------------------ ActionEngine
namespace {
struct BudgetExceeded { };            // not derived from std::exception: scripts cannot catch it
struct ScriptFault : std::runtime_error { using std::runtime_error::runtime_error; };
struct ScopedDepth {
    int& depth;
    explicit ScopedDepth(int& value) : depth(value) { ++depth; }
    ~ScopedDepth() { --depth; }
};

const std::set<std::string>& actionNames() {
    static const std::set<std::string> a = {"give_item", "remove_item", "show_text", "add_stamina", "add_money", "set_flag", "set_var",
        "if", "repeat", "warp", "cancel", "set_result", "call", "emit", "log"};
    return a;
}
std::string permFor(const std::string& act) {
    if (act == "give_item" || act == "remove_item") return "inventory";
    if (act == "add_stamina" || act == "add_money") return "player";
    if (act == "warp") return "world";
    if (act == "show_text") return "ui";
    if (act == "set_flag") return "save";
    return "";
}
}  // namespace

struct ActionEngine::Exec {
    ActionEngine& eng; HookCtx& ctx; HostApi& host; FlagStore* flags;
    std::string mod; std::set<std::string> perms;
    std::map<std::string, Json> vars;

    void tick() {
        if (!eng.invocationFault_.empty()) throw ScriptFault(eng.invocationFault_);
        if (eng.sharedSteps_ >= eng.limits.maxSteps) {
            eng.invocationFault_ = "anggaran langkah (" + std::to_string(eng.limits.maxSteps) + ") habis";
            throw BudgetExceeded();
        }
        ++eng.sharedSteps_;
    }

    Json eval(const Json& e, int d = 0) {
        tick();
        if (d > eng.limits.maxExprDepth) throw ScriptFault("ekspresi terlalu dalam");
        if (!e.isObject()) return e;
        if (e.has("var")) { auto it = vars.find(e["var"].asString()); return it == vars.end() ? Json() : it->second; }
        if (e.has("ctx")) return ctx.args.path(e["ctx"].asString());
        if (e.has("flag")) return flags ? flags->getFlag(mod + ":" + e["flag"].asString()) : Json();
        if (e.has("subject")) return Json(ctx.subject);
        if (e.has("has_item")) {
            if (!perms.count("inventory")) throw ScriptFault("izin 'inventory' diperlukan");
            return Json(static_cast<int64_t>(host.countItem(eval(e["has_item"], d + 1).asString())));
        }
        if (e.has("money")) { if (!perms.count("player")) throw ScriptFault("izin 'player' diperlukan"); return Json(static_cast<int64_t>(host.money())); }
        if (e.has("day")) { if (!perms.count("world")) throw ScriptFault("izin 'world' diperlukan"); return Json(static_cast<int64_t>(host.day())); }
        if (e.has("random")) {
            int64_t n = eval(e["random"], d + 1).asInt(1);
            if (n < 1 || n > 1000000) throw ScriptFault("random di luar jangkauan");
            return Json(static_cast<int64_t>(host.random(static_cast<uint32_t>(n))));
        }
        if (e.has("concat")) {
            std::string s;
            for (const auto& p : e["concat"].items()) { Json v = eval(p, d + 1); s += v.isString() ? v.asString() : (v.isNull() ? "" : v.dump()); }
            return Json(s);
        }
        if (e.has("not")) return Json(!truthy(eval(e["not"], d + 1)));
        if (e.has("op")) {
            const std::string& op = e["op"].asString();
            Json a = eval(e["a"], d + 1);
            if (op == "and") return Json(truthy(a) && truthy(eval(e["b"], d + 1)));
            if (op == "or") return Json(truthy(a) || truthy(eval(e["b"], d + 1)));
            Json b = eval(e["b"], d + 1);
            if (op == "==") return Json(a == b);
            if (op == "!=") return Json(a != b);
            if (a.isNull()) a = Json(0);   // unset flags/vars count as 0 in arithmetic
            if (b.isNull()) b = Json(0);
            if (!a.isNumber() || !b.isNumber()) throw ScriptFault("operator '" + op + "' butuh angka");
            int64_t x = a.asInt(), y = b.asInt();
            const int64_t lo = std::numeric_limits<int64_t>::min(), hi = std::numeric_limits<int64_t>::max();
            if (op == "+") {
                if ((y > 0 && x > hi - y) || (y < 0 && x < lo - y)) throw ScriptFault("overflow penjumlahan");
                return Json(x + y);
            }
            if (op == "-") {
                if ((y > 0 && x < lo + y) || (y < 0 && x > hi + y)) throw ScriptFault("overflow pengurangan");
                return Json(x - y);
            }
            if (op == "*") {
                if ((x > 0 && ((y > 0 && x > hi / y) || (y < 0 && y < lo / x))) ||
                    (x < 0 && ((y > 0 && x < lo / y) || (y < 0 && x < hi / y))))
                    throw ScriptFault("overflow perkalian");
                return Json(x * y);
            }
            if (op == "/" || op == "%") {
                if (y == 0) throw ScriptFault(op == "/" ? "pembagian nol" : "modulo nol");
                if (x == lo && y == -1) throw ScriptFault("overflow pembagian");
                return Json(op == "/" ? x / y : x % y);
            }
            if (op == "<") return Json(x < y);
            if (op == "<=") return Json(x <= y);
            if (op == ">") return Json(x > y);
            if (op == ">=") return Json(x >= y);
            throw ScriptFault("operator tidak dikenal: " + op);
        }
        return e;  // plain object literal
    }
    static bool truthy(const Json& v) {
        if (v.isBool()) return v.asBool();
        if (v.isNumber()) return v.asDouble() != 0;
        if (v.isString()) return !v.asString().empty();
        return !v.isNull();
    }
    std::string text(const Json& v) {
        Json e = eval(v);
        if (e.isObject()) return Registry_localized(e);
        return e.isString() ? e.asString() : e.dump();
    }
    std::string Registry_localized(const Json& o) {
        std::string loc = ctx.args["locale"].isString() ? ctx.args["locale"].asString() : "en";
        if (const Json* l = o.find(loc)) if (l->isString()) return l->asString();
        if (const Json* l = o.find("en")) if (l->isString()) return l->asString();
        for (const auto& kv : o.members()) if (kv.second.isString()) return kv.second.asString();
        return "";
    }

    // Item ids in actions are resolved against the mod's own namespace when bare.
    std::string itemId(const Json& v) {
        std::string s = eval(v).asString();
        if (s.find(':') == std::string::npos) s = mod + ":" + s;
        return s;
    }

    // returns true when execution of the handler must stop (cancel does not stop)
    void block(const Json& actions) {
        for (const auto& a : actions.items()) step(a);
    }

    void step(const Json& a) {
        tick();
        const std::string& act = a["do"].asString();
        std::string p = permFor(act);
        if (!p.empty() && !perms.count(p)) throw ScriptFault("aksi '" + act + "' butuh izin '" + p + "'");
        if (act == "give_item") {
            int64_t n = a.has("count") ? eval(a["count"]).asInt() : 1;
            if (n < 1 || n > 999) throw ScriptFault("count di luar 1..999");
            host.giveItem(itemId(a["item"]), static_cast<int>(n));
        } else if (act == "remove_item") {
            int64_t n = a.has("count") ? eval(a["count"]).asInt() : 1;
            if (n < 1 || n > 999) throw ScriptFault("count di luar 1..999");
            host.removeItem(itemId(a["item"]), static_cast<int>(n));
        } else if (act == "show_text") host.showText(text(a["text"]));
        else if (act == "add_stamina") host.addStamina(static_cast<int>(eval(a["amount"]).asInt()));
        else if (act == "add_money") host.addMoney(static_cast<int>(eval(a["amount"]).asInt()));
        else if (act == "set_flag") {
            if (!flags) throw ScriptFault("penyimpanan flag tidak tersedia");
            flags->setFlag(mod + ":" + a["flag"].asString(), eval(a["value"]));
        } else if (act == "set_var") vars[a["name"].asString()] = eval(a["value"]);
        else if (act == "if") {
            if (truthy(eval(a["cond"]))) block(a["then"]); else block(a["else"]);
        } else if (act == "repeat") {
            int64_t n = eval(a["times"]).asInt();
            if (n < 0 || n > eng.limits.maxRepeat) throw ScriptFault("repeat di luar batas");
            for (int64_t i = 0; i < n; ++i) { vars["i"] = Json(i); block(a["do_list"]); }
        } else if (act == "warp") {
            host.warp(eval(a["area"]).asString(), a["entrance"].asString());
        } else if (act == "cancel") ctx.cancelled = true;
        else if (act == "set_result") ctx.result[a["key"].asString()] = eval(a["value"]);
        else if (act == "call") {
            if (eng.sharedDepth_ >= eng.limits.maxCallDepth) {
                eng.invocationFault_ = "kedalaman call melebihi batas";
                throw ScriptFault(eng.invocationFault_);
            }
            ScopedDepth guard(eng.sharedDepth_);
            std::string id = a["handler"].asString();
            if (id.find(':') == std::string::npos) id = mod + ":" + id;
            auto it = eng.handlers_.find(id);
            if (it == eng.handlers_.end()) throw ScriptFault("handler tidak ada: " + id);
            auto savedMod = mod; auto savedPerms = perms;
            mod = it->second.mod; perms = it->second.perms;
            block(it->second.actions);
            mod = savedMod; perms = savedPerms;
        } else if (act == "emit") {
            if (eng.sharedDepth_ >= eng.limits.maxCallDepth) {
                eng.invocationFault_ = "kedalaman emit melebihi batas";
                throw ScriptFault(eng.invocationFault_);
            }
            ScopedDepth guard(eng.sharedDepth_);
            if (!eng.emitFn) throw ScriptFault("emit tidak tersedia");
            HookCtx c2;
            c2.hook = a["hook"].asString();
            if (!HookBus::knownHooks().count(c2.hook)) throw ScriptFault("hook tidak dikenal: " + c2.hook);
            c2.subject = eval(a["subject"]).asString();
            c2.args = a["args"].isObject() ? a["args"] : Json::object();
            eng.emitFn(c2);
            // HookBus contains nested handler faults; exhaustion still stops the caller.
            if (!eng.invocationFault_.empty()) throw ScriptFault(eng.invocationFault_);
        } else if (act == "log") host.log(1, "[" + mod + "] " + text(a["text"]));
        else throw ScriptFault("aksi tidak dikenal: " + act);
    }
};

static void validateActions(const Json& list, const std::string& where, std::vector<std::string>& errs, std::set<std::string>& calls,
                            const std::set<std::string>& perms, int depth) {
    if (depth > 16) { errs.push_back(where + ": blok bersarang terlalu dalam"); return; }
    if (!list.isArray()) { errs.push_back(where + ": harus array aksi"); return; }
    for (size_t i = 0; i < list.size(); ++i) {
        const Json& a = list[i];
        std::string w = where + "[" + std::to_string(i) + "]";
        if (!a.isObject() || !a["do"].isString() || !actionNames().count(a["do"].asString())) { errs.push_back(w + ": aksi tidak dikenal"); continue; }
        const std::string act = a["do"].asString();
        std::string p = permFor(act);
        if (!p.empty() && !perms.count(p)) errs.push_back(w + ": '" + act + "' butuh izin '" + p + "' di manifest");
        if (act == "if") {
            validateActions(a["then"], w + ".then", errs, calls, perms, depth + 1);
            if (a.has("else")) validateActions(a["else"], w + ".else", errs, calls, perms, depth + 1);
        }
        if (act == "repeat") validateActions(a["do_list"], w + ".do_list", errs, calls, perms, depth + 1);
        if (act == "call") calls.insert(a["handler"].asString());
    }
}

std::vector<std::string> ActionEngine::load(const std::string& mod, const Json& script, const std::set<std::string>& permissions) {
    std::vector<std::string> errs;
    if (!script["handlers"].isObject()) { errs.push_back("script harus punya objek 'handlers'"); return errs; }
    for (const auto& kv : script["handlers"].members()) {
        if (!validLocalId(kv.first)) { errs.push_back("nama handler tidak valid: " + kv.first); continue; }
        std::string id = mod + ":" + kv.first;
        if (handlers_.count(id)) { errs.push_back("handler ganda: " + id); continue; }
        std::set<std::string> calls;
        validateActions(kv.second, id, errs, calls, permissions, 0);
        handlers_[id] = Handler{mod, kv.second, permissions};
    }
    return errs;
}

std::vector<std::string> ActionEngine::unresolvedCalls() const {
    std::vector<std::string> out;
    std::function<void(const Json&, const std::string&, const std::string&)> walk = [&](const Json& l, const std::string& mod, const std::string& where) {
        for (const auto& a : l.items()) {
            if (a["do"].asString() == "call") {
                std::string id = a["handler"].asString();
                if (id.find(':') == std::string::npos) id = mod + ":" + id;
                if (!handlers_.count(id)) out.push_back(where + " memanggil handler yang tidak ada: " + id);
            }
            walk(a["then"], mod, where); walk(a["else"], mod, where); walk(a["do_list"], mod, where);
        }
    };
    for (const auto& kv : handlers_) walk(kv.second.actions, kv.second.mod, kv.first);
    return out;
}

bool ActionEngine::run(const std::string& handlerId, HookCtx& ctx, HostApi& host, FlagStore* flags, std::string* error) {
    auto it = handlers_.find(handlerId);
    if (it == handlers_.end()) { if (error) *error = "handler tidak ada: " + handlerId; return false; }
    if (activeRuns_ == 0) { sharedSteps_ = 0; sharedDepth_ = 0; invocationFault_.clear(); }
    // The emitting invocation is already aborting. Skip its remaining nested
    // callbacks so exhaustion does not count as a fault in unrelated mods.
    else if (!invocationFault_.empty()) return true;
    if (activeRuns_ > limits.maxCallDepth) {
        invocationFault_ = "kedalaman emit melebihi batas";
        if (error) *error = handlerId + ": " + invocationFault_;
        return false;
    }
    struct RunGuard {
        int& count;
        explicit RunGuard(int& value) : count(value) { ++count; }
        ~RunGuard() { --count; }
    } guard(activeRuns_);
    Exec ex{*this, ctx, host, flags, it->second.mod, it->second.perms, {}};
    try {
        ex.block(it->second.actions);
        return true;
    } catch (const BudgetExceeded&) {
        if (error) *error = handlerId + ": anggaran langkah (" + std::to_string(limits.maxSteps) + ") habis";
    } catch (const ScriptFault& e) {
        if (error) *error = handlerId + ": " + e.what();
    } catch (const std::exception& e) {
        if (error) *error = handlerId + ": " + e.what();
    } catch (...) {
        if (error) *error = handlerId + ": kegagalan host tidak diketahui";
    }
    return false;
}

}  // namespace nm
