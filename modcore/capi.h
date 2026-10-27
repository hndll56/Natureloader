/* C ABI between the ported game core and the mod runtime.
 *
 * The (decomp-derived) core links only this header: it never sees C++ types.
 * Registry = catalogue, API = this file, hooks = nm_fire() call sites in the core. */
#ifndef NATURE_CAPI_H
#define NATURE_CAPI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NmSession NmSession;

/* Services the core implements. Any pointer may be NULL (call is then refused). */
typedef struct NmHost {
    void* user;
    int (*give_item)(void* user, const char* item_id, int count);
    int (*remove_item)(void* user, const char* item_id, int count);
    int (*count_item)(void* user, const char* item_id);
    void (*show_text)(void* user, const char* utf8);
    void (*add_stamina)(void* user, int amount);
    void (*add_money)(void* user, int amount);
    int (*money)(void* user);
    int (*warp)(void* user, const char* area_id, const char* entrance);
    int (*day)(void* user);
    uint32_t (*random)(void* user, uint32_t n);
    void (*log)(void* user, int level, const char* msg);
} NmHost;

/* Row the core uses to fill its ItemInfo table (field widths match the decomp). */
typedef struct NmItemRow {
    int32_t handle;         /* numeric ItemId */
    int32_t inherit_handle; /* copy the retail row of this id first, -1 = none */
    int16_t buy_price, sell_price;
    int8_t stamina, fatigue;
    uint8_t category;       /* ItemCategory 0..4 */
    uint16_t flags;         /* ItemInfoFlag bits */
    char sprite_asset[96];  /* logical asset id, "" = none */
} NmItemRow;

NmSession* nm_create(const char* game_id, const char* exe_sha256, const char* base_catalog_json, const char* locale);
void nm_destroy(NmSession*);
void nm_set_host(NmSession*, const NmHost*);
/* Add one mod directory. Returns 0 on success (manifest readable). */
int nm_add_mod_dir(NmSession*, const char* dir);
void nm_approve_plugin(NmSession*, const char* mod_id, const char* version, const char* sha256);
/* Build the session. save_json may be NULL; *save_out receives updated JSON (free with nm_free).
 * Returns 0 when every discovered mod loaded, >0 = number of rejected mods (session still usable),
 * or -1 on a fatal session error. A failed session cannot be saved. */
int nm_load(NmSession*, const char* save_json, char** save_out);
/* Export the current save extension (call whenever the core writes a save). Fires save.before. */
int nm_save(NmSession*, char** save_out);
/* Faults of mod handlers since load (one per line); muted mods are listed. */
const char* nm_faults(NmSession*);
const char* nm_report(NmSession*);         /* valid until next call */
void nm_free(void*);

int32_t nm_handle(NmSession*, const char* kind, const char* id);               /* -1 if unknown */
const char* nm_id(NmSession*, const char* kind, int32_t handle);                /* NULL if unavailable */
int32_t nm_kind_count(NmSession*, const char* kind);                            /* highest handle+1 to size tables */
int nm_item_row(NmSession*, int32_t handle, NmItemRow* out);                    /* 0 ok */
const char* nm_name(NmSession*, const char* id);                                /* localized, valid until next call */
/* Generic record access: JSON of a record's data (shop stock, area exits, ...), and enumeration. */
const char* nm_record_json(NmSession*, const char* id);                        /* "" if unknown; valid until next call */
int32_t nm_record_count(NmSession*, const char* kind);
const char* nm_record_id(NmSession*, const char* kind, int32_t index);          /* NULL when out of range */
/* Missing-mod content: returns the stable id a handle had in the save ("" if none). */
const char* nm_orphan(NmSession*, const char* kind, int32_t handle);

/* Assets: override for a base asset id (e.g. "base:tex/11/3"). 0 = found. */
int nm_asset_override(NmSession*, const char* base_id, char* logical_out, size_t cap, int* scale, int* pivot_x, int* pivot_y);
int64_t nm_asset_read(NmSession*, const char* logical, void* buf, size_t cap);  /* size, or -1 */

/* Hook call site: returns 1 when a handler cancelled the default behaviour.
 * result_json (optional) receives a JSON object. */
int nm_fire(NmSession*, const char* hook, const char* subject, const char* args_json, char* result_json, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
