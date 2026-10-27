/* Native plugin ABI for NatureLoader (C, stable across compilers).
 *
 * A plugin is a shared library that exports the three functions below. It is only
 * loaded when (1) the mod manifest lists the binary's SHA-256, (2) the manifest lists
 * the SHA-256 of the game executable it was built for, (3) the API range matches the
 * host and (4) the player approved exactly that binary. Prefer scripts: plugins can
 * only be as compatible as the game build they target. */
#ifndef NATURE_PLUGIN_H
#define NATURE_PLUGIN_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define NATURE_PLUGIN_ABI 1
#if defined(_WIN32)
#define NATURE_EXPORT __declspec(dllexport)
#else
#define NATURE_EXPORT __attribute__((visibility("default")))
#endif

typedef struct NatureHookCtx {
    const char* hook;       /* "item.use" */
    const char* subject;    /* record id, "" for global hooks */
    const char* args_json;
    int cancel;             /* set to 1 to cancel (cancelable hooks only) */
    char result_json[512];  /* optional JSON object merged into the hook result */
} NatureHookCtx;

typedef void (*NatureHookFn)(NatureHookCtx* ctx, void* user);

typedef struct NatureHostApi {
    uint32_t abi, api_major, api_minor;
    void* host;
    int (*bind_hook)(void* host, const char* hook, const char* subject, int priority, NatureHookFn fn, void* user);
    void (*log)(void* host, int level, const char* msg);
    int (*give_item)(void* host, const char* item_id, int count);
    int (*show_text)(void* host, const char* text);
} NatureHostApi;

typedef struct NaturePluginInfo {
    uint32_t abi;
    const char* mod_id;
    const char* plugin_version;
    const char* game_id;
} NaturePluginInfo;

NATURE_EXPORT const NaturePluginInfo* nature_plugin_info(void);
NATURE_EXPORT int nature_plugin_init(const NatureHostApi* api);  /* 0 = ok */
NATURE_EXPORT void nature_plugin_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
