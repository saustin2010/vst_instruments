#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin_api_v1.h"
#include "mono_core.h"

static const host_api_v1_t *g_host;

#ifdef MPC_PORT
/* MPC port: the patch library in MPC's PRESET menu, after an Init (the engine's own starting sound, which a new
 * instance has). "patch" picks one (Move loads them with "patch_init" / "patch_load" from its own UI); the instance
 * remembers which, and keeps it in "state", so a reopened project (MPC sets every parameter back after the chunk)
 * doesn't take PRESET's value as a new choice and load the patch over its edits. */
typedef struct { mono_t *m; int patch; } mpc_voice_t;
#define MONO(i) (((mpc_voice_t *)(i))->m)

static int library_count(mono_t *m) {
    char count[16];
    return mono_get_param(m, "patch_count", count, sizeof count) > 0 ? atoi(count) : 0;
}

static int patch_name(mono_t *m, int n, char *buf, int buf_len) {   /* 0 = Init, then the library */
    char names[1024];
    if (buf_len < 1) return -1;
    if (n <= 0) return snprintf(buf, (size_t)buf_len, "Init");
    if (mono_get_param(m, "patch_names", names, sizeof names) <= 0) return -1;
    const char *s = names;
    for (int i = 1; i < n && s; ++i) { s = strchr(s, '|'); if (s) ++s; }
    if (!s) { buf[0] = 0; return 0; }
    int len = (int)strcspn(s, "|");
    if (len >= buf_len) len = buf_len - 1;
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return len;
}
#else
#define MONO(i) ((mono_t *)(i))
#endif

static void *voice_create(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults;
    mono_t *m = mono_create_with_storage(g_host, 1,
                                         "/data/UserData/schwung/mono-user-waves-v1.bin");
#ifdef MPC_PORT
    if (!m) return NULL;
    mpc_voice_t *v = calloc(1, sizeof *v);
    if (!v) { mono_destroy(m); return NULL; }
    v->m = m;
    return v;
#else
    return m;
#endif
}

static void voice_destroy(void *instance) {
    mono_destroy(MONO(instance));
#ifdef MPC_PORT
    free(instance);
#endif
}

static void voice_midi(void *instance, const uint8_t *msg, int len, int source) {
    mono_on_midi(MONO(instance), msg, len, source);
}

static void voice_set(void *instance, const char *key, const char *val) {
#ifdef MPC_PORT
    mpc_voice_t *v = instance;
    if (!strcmp(key, "patch")) {
        char num[16];
        int n = atoi(val), max = library_count(v->m);
        n = n < 0 ? 0 : n > max ? max : n;
        if (n == v->patch) return;
        if (n == 0) {
            mono_set_param(v->m, "patch_init", "1");
        } else {
            snprintf(num, sizeof num, "%d", n - 1);
            mono_set_param(v->m, "patch_load", num);
        }
        v->patch = n;
        return;
    }
    if (!strcmp(key, "state")) {
        const char *p = strstr(val, "\"mpc_patch\":");
        if (p) v->patch = atoi(p + 12);
    }
#endif
    mono_set_param(MONO(instance), key, val);
}

static int voice_get(void *instance, const char *key, char *buf, int buf_len) {
    if (!strcmp(key, "module_id"))
        return snprintf(buf, (size_t)buf_len, "mono-voice");
#ifdef MPC_PORT
    mpc_voice_t *v = instance;
    if (!strcmp(key, "patch")) return snprintf(buf, (size_t)buf_len, "%d", v->patch);
    if (!strcmp(key, "patch_count")) return snprintf(buf, (size_t)buf_len, "%d", library_count(v->m) + 1);
    if (!strcmp(key, "patch_name")) return patch_name(v->m, v->patch, buf, buf_len);
    if (!strncmp(key, "patch_name_at:", 14)) return patch_name(v->m, atoi(key + 14), buf, buf_len);
    if (!strcmp(key, "state")) {   /* the engine's own JSON object, with "mpc_patch":N first */
        char *own = malloc((size_t)buf_len);
        if (!own) return -1;
        int n = mono_get_param(v->m, key, own, buf_len);
        if (n <= 0 || own[0] != '{') {
            if (n > 0) memcpy(buf, own, (size_t)n + 1);
            free(own);
            return n;
        }
        const char *rest = own + 1;
        while (*rest == ' ' || *rest == '\n') ++rest;
        n = snprintf(buf, (size_t)buf_len, "{\"mpc_patch\":%d%s%s", v->patch, *rest == '}' ? "" : ",", rest);
        free(own);
        return n < buf_len ? n : -1;
    }
#endif
    return mono_get_param(MONO(instance), key, buf, buf_len);
}

static int voice_error(void *instance, char *buf, int buf_len) {
    (void)instance; (void)buf; (void)buf_len;
    return 0;
}

static void voice_render(void *instance, int16_t *out_lr, int frames) {
    mono_render(MONO(instance), out_lr, frames);
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
