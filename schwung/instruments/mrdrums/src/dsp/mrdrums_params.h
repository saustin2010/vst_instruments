#ifndef MRDRUMS_PARAMS_H
#define MRDRUMS_PARAMS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MRDRUMS_PAD_COUNT = 16,
    MRDRUMS_FIRST_PAD_NOTE = 36,
    MRDRUMS_LAST_PAD_NOTE = 51
};

typedef struct {
    const char *key;
    const char *name;
    const char *type;
    const char *suffix;
    int pad_index;
    float min_val;
    float max_val;
    float step;
    float default_num;
    const char *default_str;
    const char *options_json;
    const char *root;
    const char *filter;
    const char *start_path;
} mrdrums_param_desc_t;

typedef struct {
    const char *suffix;
    const char *name;
    const char *type;
    float min_val;
    float max_val;
    float step;
    float default_num;
    const char *default_str;
    const char *options_json;
    const char *root;
    const char *filter;
    const char *start_path;
} mrdrums_pad_field_desc_t;

int mrdrums_make_pad_key(int pad_index, const char *suffix, char *out, size_t out_len);
int mrdrums_parse_pad_key(const char *key, int *pad_index, const char **suffix);

const mrdrums_param_desc_t *mrdrums_global_params(int *count_out);
const mrdrums_pad_field_desc_t *mrdrums_pad_fields(int *count_out);

const mrdrums_param_desc_t *mrdrums_find_global_param(const char *key);
const mrdrums_param_desc_t *mrdrums_find_pad_param(const char *key);
const mrdrums_param_desc_t *mrdrums_find_param(const char *key);

#ifdef __cplusplus
}
#endif

#endif
