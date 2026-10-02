#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "plugin_api_v1.h"
#include "mono_core.h"

static const host_api_v1_t *g_host;

static void *voice_create(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults;
    return mono_create_with_storage(g_host, 1,
                                    "/data/UserData/schwung/mono-user-waves-v1.bin");
}

static void voice_destroy(void *instance) { mono_destroy((mono_t *)instance); }

static void voice_midi(void *instance, const uint8_t *msg, int len, int source) {
    mono_on_midi((mono_t *)instance, msg, len, source);
}

static void voice_set(void *instance, const char *key, const char *val) {
    mono_set_param((mono_t *)instance, key, val);
}

static int voice_get(void *instance, const char *key, char *buf, int buf_len) {
    if (!strcmp(key, "module_id"))
        return snprintf(buf, (size_t)buf_len, "mono-voice");
    return mono_get_param((mono_t *)instance, key, buf, buf_len);
}

static int voice_error(void *instance, char *buf, int buf_len) {
    (void)instance; (void)buf; (void)buf_len;
    return 0;
}

static void voice_render(void *instance, int16_t *out_lr, int frames) {
    mono_render((mono_t *)instance, out_lr, frames);
}

static plugin_api_v2_t api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = voice_create,
    .destroy_instance = voice_destroy,
    .on_midi = voice_midi,
    .set_param = voice_set,
    .get_param = voice_get,
    .get_error = voice_error,
    .render_block = voice_render
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &api;
}
