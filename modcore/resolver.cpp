#include "resolver.h"

#include <algorithm>

namespace nm {

ResolveResult resolveMods(const ResolveInput& in) {
    ResolveResult res;
    std::map<std::string, const Manifest*> byId;
    std::map<std::string, int> count;
    for (const auto& m : in.mods) ++count[m.id];

    for (const auto& m : in.mods) {
        if (count[m.id] > 1) { res.rejected[m.id].push_back("id mod ganda terpasang (" + std::to_string(count[m.id]) + " salinan)"); continue; }
        if (in.excluded.count(m.id)) { res.rejected[m.id].push_back("dikeluarkan karena gagal dimuat"); continue; }
        if (m.game != in.gameId) { res.rejected[m.id].push_back("untuk game " + m.game + ", bukan " + in.gameId); continue; }
        if (!m.api.satisfies(in.hostApi)) {
            res.rejected[m.id].push_back("butuh API " + m.api.str() + ", host menyediakan " + in.hostApi.str());
            continue;
        }
        byId[m.id] = &m;
    }

    // Conflicts first, symmetrically: when A and B conflict, both are rejected. Rejecting only one side
    // would make the outcome depend on iteration order.
    {
        std::set<std::string> bad;
        for (const auto& a : byId)
            for (const auto& cf : a.second->conflicts) {
                auto b = byId.find(cf.first);
                if (b == byId.end() || !cf.second.satisfies(b->second->version)) continue;
                res.rejected[a.first].push_back("konflik dengan mod " + b->first + " " + b->second->version.str());
                res.rejected[b->first].push_back("konflik dengan mod " + a.first + " (dideklarasikan olehnya)");
                bad.insert(a.first);
                bad.insert(b->first);
            }
        for (const auto& id : bad) byId.erase(id);
    }

    // Cascade: missing/unsatisfied hard deps until stable.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = byId.begin(); it != byId.end();) {
            const Manifest& m = *it->second;
            std::vector<std::string> why;
            for (const auto& d : m.depends) {
                if (d.optional) continue;
                auto f = byId.find(d.id);
                if (f == byId.end()) why.push_back("dependensi hilang/ditolak: " + d.id + " " + d.range.str());
                else if (!d.range.satisfies(f->second->version))
                    why.push_back("dependensi " + d.id + " versi " + f->second->version.str() + " tidak memenuhi " + d.range.str());
            }
            if (!why.empty()) {
                res.rejected[m.id] = why;
                it = byId.erase(it);
                changed = true;
            } else ++it;
        }
    }
    for (const auto& kv : byId)
        for (const auto& d : kv.second->depends) {
            if (!d.optional) continue;
            auto f = byId.find(d.id);
            if (f != byId.end() && !d.range.satisfies(f->second->version))
                res.warnings.push_back(kv.first + ": dependensi opsional " + d.id + " versi tidak cocok, diabaikan");
        }

    // Build edges (before -> after).
    std::map<std::string, std::set<std::string>> out;
    std::map<std::string, int> indeg;
    for (const auto& kv : byId) { out[kv.first]; indeg[kv.first] = 0; }
    auto edge = [&](const std::string& a, const std::string& b) {
        if (a == b || !byId.count(a) || !byId.count(b)) return;
        if (out[a].insert(b).second) ++indeg[b];
    };
    for (const auto& kv : byId) {
        const Manifest& m = *kv.second;
        for (const auto& d : m.depends) {
            auto f = byId.find(d.id);
            if (f != byId.end() && d.range.satisfies(f->second->version)) edge(d.id, m.id);
        }
        for (const auto& a : m.loadAfter) edge(a, m.id);
        for (const auto& b : m.loadBefore) edge(m.id, b);
    }
    // Kahn with user-priority tie-break.
    std::map<std::string, size_t> prio;
    for (size_t i = 0; i < in.mods.size(); ++i) prio.emplace(in.mods[i].id, i);
    std::vector<std::string> ready;
    for (const auto& kv : indeg) if (kv.second == 0) ready.push_back(kv.first);
    auto cmp = [&](const std::string& a, const std::string& b) { return prio[a] > prio[b]; };
    while (!ready.empty()) {
        std::sort(ready.begin(), ready.end(), cmp);
        std::string n = ready.back();
        ready.pop_back();
        res.order.push_back(n);
        for (const auto& t : out[n]) if (--indeg[t] == 0) ready.push_back(t);
    }
    if (res.order.size() != byId.size()) {
        std::string members;
        for (const auto& kv : indeg) if (kv.second > 0) members += (members.empty() ? "" : ", ") + kv.first;
        for (const auto& kv : indeg)
            if (kv.second > 0) res.rejected[kv.first].push_back("siklus urutan muat/dependensi: " + members);
    }
    return res;
}

}  // namespace nm
