// Dependency/conflict resolution and deterministic load ordering.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "manifest.h"

namespace nm {

struct ResolveInput {
    std::vector<Manifest> mods;            // candidates, in user-priority order (tie-breaker)
    std::string gameId;
    Version hostApi;
    std::set<std::string> excluded;        // ids forced out (earlier failures)
};
struct ResolveResult {
    std::vector<std::string> order;                           // load order
    std::map<std::string, std::vector<std::string>> rejected; // id -> reasons
    std::vector<std::string> warnings;
};

ResolveResult resolveMods(const ResolveInput& in);

}  // namespace nm
