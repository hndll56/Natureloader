// Content registry: namespaced stable string ids <-> runtime numeric handles.
// The registry is the catalogue; handles are only allocated for kinds the retail
// code addresses by number, and are persisted in the save (IdTable) so they never
// depend on mod load order across sessions.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "json.h"

namespace nm {

struct Record {
    std::string id;     // "mod:local"
    std::string kind;
    std::string owner;  // mod id, or "base"
    Json data;
    int32_t handle = -1;
};

struct KindRange { int32_t retailCount = 0, modFirst = 0, modLast = -1; };

// Persisted id -> handle assignments (part of the save extension).
struct IdTable {
    std::map<std::string, std::map<std::string, int32_t>> ids;  // kind -> id -> handle
    std::map<std::string, int32_t> next;                        // kind -> next free handle
    Json toJson() const;
    static bool fromJson(const Json& j, IdTable& out, std::string& err);
};

struct PatchReport { std::string target, field, previousOwner, owner; };

class Registry {
public:
    // Base (retail) catalogue: {"kinds":{"item":{retail_count,mod_first,mod_last,names:{"0":"nothing"}}}}
    bool loadBase(const Json& catalog, std::string& err);

    // Phase "register ids": adds a record. Returns false (and err) on invalid/duplicate id.
    bool add(const std::string& owner, const std::string& kind, const std::string& local, const Json& data, std::string& err);
    // Field-level patch of an existing record (base or other mod) by a mod.
    bool patch(const std::string& owner, const std::string& target, const Json& fields, std::string& err);

    // Validate every record's schema and cross references. deps: mod id -> set of mods it may reference.
    // Returns errors keyed by owning mod.
    std::map<std::string, std::vector<std::string>> validate(const std::map<std::string, std::set<std::string>>& allowedRefs,
                                                             std::vector<std::string>* warnings = nullptr) const;

    // Allocate handles for mod records in the given load order; table keeps earlier assignments.
    bool allocateHandles(const std::vector<std::string>& loadOrder, IdTable& table, std::string& err);

    const Record* find(const std::string& id) const;
    const Record* byHandle(const std::string& kind, int32_t handle) const;
    std::vector<const Record*> all(const std::string& kind) const;
    const std::vector<PatchReport>& patches() const { return patches_; }
    // Asset references found while validating: owner, record id, field, reference
    struct AssetRef { std::string owner, record, field, ref; };
    const std::vector<AssetRef>& assetRefs() const { return assetRefs_; }
    const std::map<std::string, KindRange>& ranges() const { return ranges_; }
    int32_t baseCount(const std::string& kind) const;

    // Localised text helper: string, or {"en":..,"id":..} with locale fallback.
    static std::string localized(const Json& v, const std::string& locale);
    // Hook field -> hook name for a record kind (e.g. item: use -> item.use).
    static const std::map<std::string, std::string>& hookFields(const std::string& kind);
    static bool isKind(const std::string& kind);
    static bool isNumericKind(const std::string& kind);

private:
    std::map<std::string, Record> records_;                          // id -> record
    std::map<std::string, std::vector<std::string>> order_;          // kind -> ids in registration order
    std::map<std::string, KindRange> ranges_;
    std::map<std::string, std::map<int32_t, std::string>> byHandle_; // kind -> handle -> id
    std::vector<PatchReport> patches_;
    std::map<std::string, std::string> fieldOwner_;                  // "target#field" -> last owner
    mutable std::vector<AssetRef> assetRefs_;
};

}  // namespace nm
