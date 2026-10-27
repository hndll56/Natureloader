/* Sample native plugin used by the tests: cancels item.use for a mod item and adds a result field. */
#include <string.h>
#include "nature_plugin.h"

static const NatureHostApi* g_api;

static const NaturePluginInfo info = {NATURE_PLUGIN_ABI, "native-demo", "1.0.0", "SLUS_011.15"};

static void on_use(NatureHookCtx* ctx, void* user) {
    (void)user;
    ctx->cancel = 1;
    strcpy(ctx->result_json, "{\"native\":true}");
    g_api->show_text(g_api->host, "plugin native aktif");
}

NATURE_EXPORT const NaturePluginInfo* nature_plugin_info(void) { return &info; }
NATURE_EXPORT int nature_plugin_init(const NatureHostApi* api) {
    g_api = api;
    return api->bind_hook(api->host, "item.use", "native-demo:granat", 0, on_use, 0);
}
NATURE_EXPORT void nature_plugin_shutdown(void) { g_api = 0; }
