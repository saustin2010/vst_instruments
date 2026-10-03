#include <dirent.h>
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>

#include "mrdrums_engine.h"
#include "mrdrums_params.h"

extern "C" {
#define MOVE_PLUGIN_API_VERSION_2 2

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;
}

static const host_api_v1_t *g_host = NULL;

static void plugin_log(const char *msg) {
    if (!msg) return;
    if (g_host && g_host->log) {
        char line[256];
        snprintf(line, sizeof(line), "[mrdrums] %s", msg);
        g_host->log(line);
    }
}

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

typedef struct {
    float *data;
    int length;
    int sample_rate;
} sample_buffer_t;

/* MPC port: sample kits from <module_dir>/kits/<kit>/ (see mpc_kits_* below). */
#define MPC_MAX_KITS 64
typedef struct {
    int count;
    char names[MPC_MAX_KITS][64];
    sample_buffer_t staged[MRDRUMS_ENGINE_PAD_COUNT];
    char staged_paths[MRDRUMS_ENGINE_PAD_COUNT][512];
    int ready;                      /* 1: a staged kit waits for the audio thread (atomic) */
} mpc_kits_t;

typedef struct {
    char module_dir[512];
    char last_error[256];
    int ui_auto_select_pad;
    int ui_current_pad;
    int ui_pad_page; /* 0=main, 1=random */
    char ui_last_sample_dir[512];
    char ui_preset_path[512]; /* last-loaded preset, stored for browser scroll-restore only */

    mrdrums_engine_t engine;
    char pad_sample_paths[MRDRUMS_ENGINE_PAD_COUNT][512];
    sample_buffer_t samples[MRDRUMS_ENGINE_PAD_COUNT];
    mpc_kits_t kits;                /* MPC port */
} mrdrums_instance_t;

static void set_error(mrdrums_instance_t *inst, const char *msg) {
    if (!inst) return;
    if (!msg) {
        inst->last_error[0] = '\0';
        return;
    }
    snprintf(inst->last_error, sizeof(inst->last_error), "%s", msg);
}

static void free_sample(sample_buffer_t *s) {
    if (!s) return;
    free(s->data);
    s->data = NULL;
    s->length = 0;
    s->sample_rate = 0;
}

static uint16_t rd_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16_be(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t rd_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int32_t rd_i24_le(const uint8_t *p) {
    int32_t v = ((int32_t)p[0]) | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
    if (v & 0x00800000) v |= ~0x00FFFFFF;
    return v;
}

static int32_t rd_i24_be(const uint8_t *p) {
    int32_t v = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | (int32_t)p[2];
    if (v & 0x00800000) v |= ~0x00FFFFFF;
    return v;
}

static double rd_ieee_extended_be(const uint8_t *p) {
    uint16_t exponent = rd_u16_be(p);
    uint32_t hi_mant = rd_u32_be(p + 2);
    uint32_t lo_mant = rd_u32_be(p + 6);
    if ((exponent & 0x7FFFu) == 0 && hi_mant == 0 && lo_mant == 0) {
        return 0.0;
    }

    int sign = (exponent & 0x8000u) ? -1 : 1;
    int exp = (int)(exponent & 0x7FFFu) - 16383;
    double value = ldexp((double)hi_mant, exp - 31) + ldexp((double)lo_mant, exp - 63);
    return sign < 0 ? -value : value;
}

static int load_wav_mono(const char *path, sample_buffer_t *out, char *err, int err_len) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err, err_len, "Could not open sample file: %s", path);
        return -1;
    }

    uint8_t riff[12];
    if (fread(riff, 1, 12, fp) != 12) {
        fclose(fp);
        snprintf(err, err_len, "Invalid WAV header");
        return -1;
    }

    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(fp);
        snprintf(err, err_len, "Not a RIFF/WAVE file");
        return -1;
    }

    int have_fmt = 0;
    int have_data = 0;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;
    long data_offset = 0;

    while (!feof(fp)) {
        uint8_t chdr[8];
        if (fread(chdr, 1, 8, fp) != 8) break;

        uint32_t chunk_size = rd_u32_le(chdr + 4);
        long chunk_data_pos = ftell(fp);

        if (memcmp(chdr, "fmt ", 4) == 0) {
            uint8_t fmt[40];
            uint32_t want = (chunk_size < sizeof(fmt)) ? chunk_size : (uint32_t)sizeof(fmt);
            if (fread(fmt, 1, want, fp) != want || want < 16) {
                fclose(fp);
                snprintf(err, err_len, "Corrupt fmt chunk");
                return -1;
            }
            audio_format = rd_u16_le(fmt + 0);
            channels = rd_u16_le(fmt + 2);
            sample_rate = rd_u32_le(fmt + 4);
            block_align = rd_u16_le(fmt + 12);
            bits_per_sample = rd_u16_le(fmt + 14);
            have_fmt = 1;
        } else if (memcmp(chdr, "data", 4) == 0) {
            data_size = chunk_size;
            data_offset = chunk_data_pos;
            have_data = 1;
        }

        long next = chunk_data_pos + (long)chunk_size + (chunk_size & 1u);
        if (fseek(fp, next, SEEK_SET) != 0) {
            break;
        }
    }

    if (!have_fmt || !have_data) {
        fclose(fp);
        snprintf(err, err_len, "WAV missing fmt/data chunk");
        return -1;
    }

    if (channels < 1 || block_align < 1 || data_size < (uint32_t)block_align) {
        fclose(fp);
        snprintf(err, err_len, "Invalid WAV channel/layout");
        return -1;
    }

    if (!((audio_format == 1 && (bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 24 || bits_per_sample == 32)) ||
          (audio_format == 3 && bits_per_sample == 32))) {
        fclose(fp);
        snprintf(err, err_len, "Unsupported WAV format (need PCM 8/16/24/32 or float32)");
        return -1;
    }

    if (fseek(fp, data_offset, SEEK_SET) != 0) {
        fclose(fp);
        snprintf(err, err_len, "Failed to seek WAV data");
        return -1;
    }

    uint8_t *raw = (uint8_t *)malloc(data_size);
    if (!raw) {
        fclose(fp);
        snprintf(err, err_len, "Out of memory reading WAV");
        return -1;
    }

    if (fread(raw, 1, data_size, fp) != data_size) {
        free(raw);
        fclose(fp);
        snprintf(err, err_len, "Failed to read WAV data");
        return -1;
    }
    fclose(fp);

    int frame_count = (int)(data_size / block_align);
    if (frame_count <= 0) {
        free(raw);
        snprintf(err, err_len, "Empty WAV data");
        return -1;
    }

    out->data = (float *)malloc((size_t)frame_count * sizeof(float));
    if (!out->data) {
        free(raw);
        snprintf(err, err_len, "Out of memory allocating sample data");
        return -1;
    }

    int bytes_per_sample = bits_per_sample / 8;
    for (int i = 0; i < frame_count; i++) {
        float mono = 0.0f;
        const uint8_t *frame_ptr = raw + (size_t)i * block_align;

        for (int ch = 0; ch < channels; ch++) {
            const uint8_t *sp = frame_ptr + ch * bytes_per_sample;
            float v = 0.0f;

            if (audio_format == 1) {
                if (bits_per_sample == 8) {
                    int x = (int)sp[0] - 128;
                    v = (float)x / 128.0f;
                } else if (bits_per_sample == 16) {
                    int16_t x = (int16_t)rd_u16_le(sp);
                    v = (float)x / 32768.0f;
                } else if (bits_per_sample == 24) {
                    int32_t x = rd_i24_le(sp);
                    v = (float)x / 8388608.0f;
                } else {
                    int32_t x = (int32_t)rd_u32_le(sp);
                    v = (float)x / 2147483648.0f;
                }
            } else {
                float x;
                memcpy(&x, sp, sizeof(float));
                v = x;
            }

            mono += v;
        }

        mono /= (float)channels;
        out->data[i] = clampf(mono, -1.0f, 1.0f);
    }

    free(raw);

    out->length = frame_count;
    out->sample_rate = (int)sample_rate;
    return 0;
}

static int load_aiff_mono(const char *path, sample_buffer_t *out, char *err, int err_len) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err, err_len, "Could not open sample file: %s", path);
        return -1;
    }

    uint8_t form[12];
    if (fread(form, 1, 12, fp) != 12) {
        fclose(fp);
        snprintf(err, err_len, "Invalid AIFF header");
        return -1;
    }
    if (memcmp(form, "FORM", 4) != 0) {
        fclose(fp);
        snprintf(err, err_len, "Not an AIFF file");
        return -1;
    }

    int is_aifc = 0;
    if (memcmp(form + 8, "AIFF", 4) == 0) {
        is_aifc = 0;
    } else if (memcmp(form + 8, "AIFC", 4) == 0) {
        is_aifc = 1;
    } else {
        fclose(fp);
        snprintf(err, err_len, "Unsupported AIFF container");
        return -1;
    }

    int have_comm = 0;
    int have_ssnd = 0;
    int little_endian_pcm = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;
    uint32_t data_size = 0;
    long data_offset = 0;

    while (!feof(fp)) {
        uint8_t chdr[8];
        if (fread(chdr, 1, 8, fp) != 8) break;

        uint32_t chunk_size = rd_u32_be(chdr + 4);
        long chunk_data_pos = ftell(fp);

        if (memcmp(chdr, "COMM", 4) == 0) {
            if (chunk_size < 18) {
                fclose(fp);
                snprintf(err, err_len, "Corrupt AIFF COMM chunk");
                return -1;
            }
            std::vector<uint8_t> comm(chunk_size);
            if (fread(comm.data(), 1, chunk_size, fp) != chunk_size) {
                fclose(fp);
                snprintf(err, err_len, "Failed to read AIFF COMM chunk");
                return -1;
            }

            channels = rd_u16_be(comm.data() + 0);
            bits_per_sample = rd_u16_be(comm.data() + 6);
            double sr = rd_ieee_extended_be(comm.data() + 8);
            sample_rate = sr > 1.0 ? (uint32_t)(sr + 0.5) : 0;
            little_endian_pcm = 0;

            if (is_aifc) {
                if (chunk_size < 22) {
                    fclose(fp);
                    snprintf(err, err_len, "Corrupt AIFC COMM chunk");
                    return -1;
                }
                const uint8_t *compression = comm.data() + 18;
                if (memcmp(compression, "NONE", 4) == 0) {
                    little_endian_pcm = 0;
                } else if (memcmp(compression, "sowt", 4) == 0) {
                    little_endian_pcm = 1;
                } else {
                    fclose(fp);
                    snprintf(err, err_len, "Unsupported AIFC compression");
                    return -1;
                }
            }

            have_comm = 1;
        } else if (memcmp(chdr, "SSND", 4) == 0) {
            if (chunk_size < 8) {
                fclose(fp);
                snprintf(err, err_len, "Corrupt AIFF SSND chunk");
                return -1;
            }
            uint8_t ssnd[8];
            if (fread(ssnd, 1, 8, fp) != 8) {
                fclose(fp);
                snprintf(err, err_len, "Failed to read AIFF SSND header");
                return -1;
            }
            uint32_t offset = rd_u32_be(ssnd + 0);
            data_offset = chunk_data_pos + 8 + (long)offset;
            if (chunk_size < 8 + offset) {
                fclose(fp);
                snprintf(err, err_len, "Invalid AIFF SSND offset");
                return -1;
            }
            data_size = chunk_size - 8 - offset;
            have_ssnd = 1;
        }

        long next = chunk_data_pos + (long)chunk_size + (chunk_size & 1u);
        if (fseek(fp, next, SEEK_SET) != 0) {
            break;
        }
    }

    if (!have_comm || !have_ssnd) {
        fclose(fp);
        snprintf(err, err_len, "AIFF missing COMM/SSND chunk");
        return -1;
    }

    if (channels < 1 || sample_rate < 1000 || bits_per_sample == 0) {
        fclose(fp);
        snprintf(err, err_len, "Invalid AIFF channel/layout");
        return -1;
    }
    if (!(bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 24 || bits_per_sample == 32)) {
        fclose(fp);
        snprintf(err, err_len, "Unsupported AIFF format (need PCM 8/16/24/32)");
        return -1;
    }

    int bytes_per_sample = bits_per_sample / 8;
    int block_align = (int)channels * bytes_per_sample;
    if (block_align < 1 || data_size < (uint32_t)block_align) {
        fclose(fp);
        snprintf(err, err_len, "Invalid AIFF data size");
        return -1;
    }

    if (fseek(fp, data_offset, SEEK_SET) != 0) {
        fclose(fp);
        snprintf(err, err_len, "Failed to seek AIFF data");
        return -1;
    }

    uint8_t *raw = (uint8_t *)malloc(data_size);
    if (!raw) {
        fclose(fp);
        snprintf(err, err_len, "Out of memory reading AIFF");
        return -1;
    }
    if (fread(raw, 1, data_size, fp) != data_size) {
        free(raw);
        fclose(fp);
        snprintf(err, err_len, "Failed to read AIFF data");
        return -1;
    }
    fclose(fp);

    int frame_count = (int)(data_size / (uint32_t)block_align);
    if (frame_count <= 0) {
        free(raw);
        snprintf(err, err_len, "Empty AIFF data");
        return -1;
    }

    out->data = (float *)malloc((size_t)frame_count * sizeof(float));
    if (!out->data) {
        free(raw);
        snprintf(err, err_len, "Out of memory allocating sample data");
        return -1;
    }

    for (int i = 0; i < frame_count; i++) {
        float mono = 0.0f;
        const uint8_t *frame_ptr = raw + (size_t)i * block_align;

        for (int ch = 0; ch < channels; ch++) {
            const uint8_t *sp = frame_ptr + ch * bytes_per_sample;
            float v = 0.0f;

            if (bits_per_sample == 8) {
                int8_t x = (int8_t)sp[0];
                v = (float)x / 128.0f;
            } else if (bits_per_sample == 16) {
                int16_t x = little_endian_pcm ? (int16_t)rd_u16_le(sp) : (int16_t)rd_u16_be(sp);
                v = (float)x / 32768.0f;
            } else if (bits_per_sample == 24) {
                int32_t x = little_endian_pcm ? rd_i24_le(sp) : rd_i24_be(sp);
                v = (float)x / 8388608.0f;
            } else {
                int32_t x = little_endian_pcm ? (int32_t)rd_u32_le(sp) : (int32_t)rd_u32_be(sp);
                v = (float)x / 2147483648.0f;
            }

            mono += v;
        }

        mono /= (float)channels;
        out->data[i] = clampf(mono, -1.0f, 1.0f);
    }

    free(raw);
    out->length = frame_count;
    out->sample_rate = (int)sample_rate;
    return 0;
}

static int has_extension_ci(const char *path, const char *ext) {
    if (!path || !ext) return 0;
    int path_len = (int)strlen(path);
    int ext_len = (int)strlen(ext);
    if (path_len < ext_len) return 0;
    return strcasecmp(path + path_len - ext_len, ext) == 0;
}

static int has_wav_extension(const char *path) {
    return has_extension_ci(path, ".wav");
}

static int has_aiff_extension(const char *path) {
    return has_extension_ci(path, ".aif") || has_extension_ci(path, ".aiff");
}

static int has_supported_audio_extension(const char *path) {
    return has_wav_extension(path) || has_aiff_extension(path);
}

static int load_audio_mono(const char *path, sample_buffer_t *out, char *err, int err_len) {
    if (has_wav_extension(path)) {
        return load_wav_mono(path, out, err, err_len);
    }
    if (has_aiff_extension(path)) {
        return load_aiff_mono(path, out, err, err_len);
    }
    snprintf(err, err_len, "Unsupported sample file type");
    return -1;
}

static int json_get_number(const char *json, const char *key, float *out) {
    if (!json || !key || !out) return -1;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    char *endp = NULL;
    double v = strtod(p, &endp);
    if (!endp || endp == p) return -1;
    *out = (float)v;
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len <= 1) return -1;

    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;

    int n = 0;
    while (*p && *p != '"' && n < out_len - 1) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = '\0';

    return (*p == '"') ? 0 : -1;
}

static int json_escape(const char *src, char *dst, int dst_len) {
    if (!src || !dst || dst_len <= 0) return 0;
    int o = 0;
    for (int i = 0; src[i] && o < dst_len - 1; i++) {
        char c = src[i];
        if ((c == '\\' || c == '"') && o < dst_len - 2) {
            dst[o++] = '\\';
        }
        dst[o++] = c;
    }
    dst[o] = '\0';
    return o;
}

static void format_filter_json(const char *filter, char *out_buf, size_t out_len) {
    if (!filter || !filter[0]) {
        snprintf(out_buf, out_len, "\".wav\"");
        return;
    }
    if (filter[0] == '[') {
        snprintf(out_buf, out_len, "%s", filter);
    } else {
        snprintf(out_buf, out_len, "\"%s\"", filter);
    }
}

static std::string url_decode(const std::string &src) {
    std::string ret;
    ret.reserve(src.length());
    for (size_t i = 0; i < src.length(); i++) {
        if (src[i] == '%' && i + 2 < src.length()) {
            int hex1 = src[i+1];
            int hex2 = src[i+2];
            char c = 0;
            if (hex1 >= '0' && hex1 <= '9') c += (hex1 - '0') * 16;
            else if (hex1 >= 'a' && hex1 <= 'f') c += (hex1 - 'a' + 10) * 16;
            else if (hex1 >= 'A' && hex1 <= 'F') c += (hex1 - 'A' + 10) * 16;

            if (hex2 >= '0' && hex2 <= '9') c += (hex2 - '0');
            else if (hex2 >= 'a' && hex2 <= 'f') c += (hex2 - 'a' + 10);
            else if (hex2 >= 'A' && hex2 <= 'F') c += (hex2 - 'A' + 10);

            ret.push_back(c);
            i += 2;
        } else if (src[i] == '+') {
            ret.push_back(' ');
        } else {
            ret.push_back(src[i]);
        }
    }
    return ret;
}

typedef enum {
    PRESET_JSON_NULL = 0,
    PRESET_JSON_BOOL,
    PRESET_JSON_NUMBER,
    PRESET_JSON_STRING,
    PRESET_JSON_ARRAY,
    PRESET_JSON_OBJECT,
} preset_json_type_t;

typedef struct preset_json_value {
    preset_json_type_t type;
    std::string string_value;
    double number_value;
    int bool_value;
    std::vector<preset_json_value> array_values;
    std::vector<std::pair<std::string, preset_json_value> > object_values;

    preset_json_value() : type(PRESET_JSON_NULL), number_value(0.0), bool_value(0) {}
} preset_json_value_t;

static void preset_json_skip_ws(const std::string &src, size_t *pos) {
    while (*pos < src.size() && isspace((unsigned char)src[*pos])) {
        (*pos)++;
    }
}

static int preset_json_parse_string(const std::string &src, size_t *pos, std::string *out, std::string *err) {
    if (*pos >= src.size() || src[*pos] != '"') {
        *err = "Expected JSON string";
        return -1;
    }

    (*pos)++;
    std::string result;
    while (*pos < src.size()) {
        char c = src[*pos];
        (*pos)++;
        if (c == '"') {
            *out = result;
            return 0;
        }
        if (c != '\\') {
            result.push_back(c);
            continue;
        }
        if (*pos >= src.size()) {
            *err = "Unterminated JSON escape";
            return -1;
        }

        char esc = src[*pos];
        (*pos)++;
        switch (esc) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
                if (*pos + 4 > src.size()) {
                    *err = "Invalid JSON unicode escape";
                    return -1;
                }
                result.push_back('?');
                *pos += 4;
                break;
            default:
                *err = "Invalid JSON escape";
                return -1;
        }
    }

    *err = "Unterminated JSON string";
    return -1;
}

static int preset_json_parse_value(const std::string &src, size_t *pos, preset_json_value_t *out, std::string *err);

static int preset_json_parse_array(const std::string &src, size_t *pos, preset_json_value_t *out, std::string *err) {
    if (*pos >= src.size() || src[*pos] != '[') {
        *err = "Expected JSON array";
        return -1;
    }

    out->type = PRESET_JSON_ARRAY;
    out->array_values.clear();
    (*pos)++;
    preset_json_skip_ws(src, pos);
    if (*pos < src.size() && src[*pos] == ']') {
        (*pos)++;
        return 0;
    }

    while (*pos < src.size()) {
        preset_json_value_t item;
        if (preset_json_parse_value(src, pos, &item, err) != 0) {
            return -1;
        }
        out->array_values.push_back(item);
        preset_json_skip_ws(src, pos);
        if (*pos >= src.size()) break;
        if (src[*pos] == ']') {
            (*pos)++;
            return 0;
        }
        if (src[*pos] != ',') {
            *err = "Expected ',' or ']' in JSON array";
            return -1;
        }
        (*pos)++;
        preset_json_skip_ws(src, pos);
    }

    *err = "Unterminated JSON array";
    return -1;
}

static int preset_json_parse_object(const std::string &src, size_t *pos, preset_json_value_t *out, std::string *err) {
    if (*pos >= src.size() || src[*pos] != '{') {
        *err = "Expected JSON object";
        return -1;
    }

    out->type = PRESET_JSON_OBJECT;
    out->object_values.clear();
    (*pos)++;
    preset_json_skip_ws(src, pos);
    if (*pos < src.size() && src[*pos] == '}') {
        (*pos)++;
        return 0;
    }

    while (*pos < src.size()) {
        std::string key;
        preset_json_value_t value;
        if (preset_json_parse_string(src, pos, &key, err) != 0) {
            return -1;
        }
        preset_json_skip_ws(src, pos);
        if (*pos >= src.size() || src[*pos] != ':') {
            *err = "Expected ':' in JSON object";
            return -1;
        }
        (*pos)++;
        preset_json_skip_ws(src, pos);
        if (preset_json_parse_value(src, pos, &value, err) != 0) {
            return -1;
        }
        out->object_values.push_back(std::make_pair(key, value));
        preset_json_skip_ws(src, pos);
        if (*pos >= src.size()) break;
        if (src[*pos] == '}') {
            (*pos)++;
            return 0;
        }
        if (src[*pos] != ',') {
            *err = "Expected ',' or '}' in JSON object";
            return -1;
        }
        (*pos)++;
        preset_json_skip_ws(src, pos);
    }

    *err = "Unterminated JSON object";
    return -1;
}

static int preset_json_parse_value(const std::string &src, size_t *pos, preset_json_value_t *out, std::string *err) {
    preset_json_skip_ws(src, pos);
    if (*pos >= src.size()) {
        *err = "Unexpected end of JSON input";
        return -1;
    }

    char c = src[*pos];
    if (c == '"') {
        out->type = PRESET_JSON_STRING;
        return preset_json_parse_string(src, pos, &out->string_value, err);
    }
    if (c == '{') {
        return preset_json_parse_object(src, pos, out, err);
    }
    if (c == '[') {
        return preset_json_parse_array(src, pos, out, err);
    }
    if (c == 't' && src.compare(*pos, 4, "true") == 0) {
        out->type = PRESET_JSON_BOOL;
        out->bool_value = 1;
        *pos += 4;
        return 0;
    }
    if (c == 'f' && src.compare(*pos, 5, "false") == 0) {
        out->type = PRESET_JSON_BOOL;
        out->bool_value = 0;
        *pos += 5;
        return 0;
    }
    if (c == 'n' && src.compare(*pos, 4, "null") == 0) {
        out->type = PRESET_JSON_NULL;
        *pos += 4;
        return 0;
    }
    if ((c >= '0' && c <= '9') || c == '-') {
        char *endp = NULL;
        out->number_value = strtod(src.c_str() + *pos, &endp);
        if (!endp || endp == src.c_str() + *pos) {
            *err = "Invalid JSON number";
            return -1;
        }
        out->type = PRESET_JSON_NUMBER;
        *pos = (size_t)(endp - src.c_str());
        return 0;
    }

    *err = "Unexpected JSON token";
    return -1;
}

static const preset_json_value_t *preset_json_object_get(const preset_json_value_t *obj, const char *key) {
    if (!obj || obj->type != PRESET_JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->object_values.size(); i++) {
        if (obj->object_values[i].first == key) {
            return &obj->object_values[i].second;
        }
    }
    return NULL;
}

static const char *preset_json_get_string(const preset_json_value_t *value) {
    if (!value || value->type != PRESET_JSON_STRING) return NULL;
    return value->string_value.c_str();
}

static float preset_json_get_number_or_default(const preset_json_value_t *obj, const char *key, float default_val) {
    const preset_json_value_t *value = preset_json_object_get(obj, key);
    if (!value || value->type != PRESET_JSON_NUMBER) return default_val;
    return (float)value->number_value;
}

static int preset_json_get_int_or_default(const preset_json_value_t *obj, const char *key, int default_val) {
    const preset_json_value_t *value = preset_json_object_get(obj, key);
    if (!value) return default_val;
    if (value->type == PRESET_JSON_NULL) return default_val;
    if (value->type != PRESET_JSON_NUMBER) return default_val;
    return (int)value->number_value;
}

static const preset_json_value_t *preset_json_find_object_with_kind(const preset_json_value_t *value, const char *kind) {
    if (!value || !kind) return NULL;

    if (value->type == PRESET_JSON_OBJECT) {
        const preset_json_value_t *kind_value = preset_json_object_get(value, "kind");
        if (kind_value && kind_value->type == PRESET_JSON_STRING && kind_value->string_value == kind) {
            return value;
        }
        for (size_t i = 0; i < value->object_values.size(); i++) {
            const preset_json_value_t *found = preset_json_find_object_with_kind(&value->object_values[i].second, kind);
            if (found) return found;
        }
    } else if (value->type == PRESET_JSON_ARRAY) {
        for (size_t i = 0; i < value->array_values.size(); i++) {
            const preset_json_value_t *found = preset_json_find_object_with_kind(&value->array_values[i], kind);
            if (found) return found;
        }
    }

    return NULL;
}

static int has_preset_extension(const char *path) {
    return has_extension_ci(path, ".ablpreset") || has_extension_ci(path, ".json");
}

/* Resolve a drumCell sampleUri to an on-device absolute path.
 * Move uses the ableton:/user-library/ scheme for shared-pool samples;
 * already-absolute and relative URIs are handled as fallbacks. */
static std::string resolve_sample_uri(const std::string &uri, const char *preset_dir) {
    std::string decoded = url_decode(uri);
    const char *ul = "ableton:/user-library/";
    const char *packs_core = "ableton:/packs/abl-core-library/";
    if (decoded.compare(0, strlen(ul), ul) == 0) {
        return std::string("/data/UserData/UserLibrary/") + decoded.substr(strlen(ul));
    }
    if (decoded.compare(0, strlen(packs_core), packs_core) == 0) {
        return std::string("/data/CoreLibrary/") + decoded.substr(strlen(packs_core));
    }
    if (decoded.compare(0, 9, "ableton:/") == 0) {
        return std::string();
    }
    if (!decoded.empty() && decoded[0] == '/') {
        return decoded;
    }
    /* relative URI: resolve against the preset's own directory */
    return std::string(preset_dir) + "/" + decoded;
}

static int set_pad_sample_path(mrdrums_instance_t *inst, int pad_index, const char *path);
static int set_param_value(mrdrums_instance_t *inst, const char *key, const char *val);

static int load_preset(mrdrums_instance_t *inst, const char *preset_path) {
    if (!inst || !preset_path || !preset_path[0]) {
        set_error(inst, "Preset path is empty");
        return -1;
    }
    if (!has_preset_extension(preset_path)) {
        set_error(inst, "Selected preset must be .ablpreset or .json");
        return -1;
    }

    /* directory the preset lives in, for resolving any relative sample URIs */
    char preset_dir[1024];
    snprintf(preset_dir, sizeof(preset_dir), "%s", preset_path);
    char *last_slash = strrchr(preset_dir, '/');
    if (last_slash) *last_slash = '\0'; else preset_dir[0] = '.', preset_dir[1] = '\0';

    std::ifstream t(preset_path);
    if (!t.is_open()) {
        set_error(inst, "Could not open preset file");
        return -1;
    }
    std::stringstream buffer;
    buffer << t.rdbuf();
    std::string json_str = buffer.str();
    t.close();

    size_t pos = 0;
    std::string parse_err;
    preset_json_value_t root;
    if (preset_json_parse_value(json_str, &pos, &root, &parse_err) != 0) {
        set_error(inst, parse_err.c_str());
        return -1;
    }
    preset_json_skip_ws(json_str, &pos);
    if (pos != json_str.size()) {
        set_error(inst, "Unexpected trailing JSON content");
        return -1;
    }

    const preset_json_value_t *drum_rack = preset_json_find_object_with_kind(&root, "drumRack");
    if (!drum_rack) {
        set_error(inst, "Preset does not contain a drumRack");
        return -1;
    }

    const preset_json_value_t *chains = preset_json_object_get(drum_rack, "chains");
    if (!chains || chains->type != PRESET_JSON_ARRAY) {
        set_error(inst, "drumRack missing chains");
        return -1;
    }

    std::string first_error;
    int mapped_cells = 0;
    for (size_t i = 0; i < chains->array_values.size(); i++) {
        const preset_json_value_t *chain = &chains->array_values[i];
        if (chain->type != PRESET_JSON_OBJECT) continue;

        const preset_json_value_t *zone_settings = preset_json_object_get(chain, "drumZoneSettings");
        int note = preset_json_get_int_or_default(zone_settings, "receivingNote", -1);
        if (note < 36 || note > 51) {
            continue;
        }

        mapped_cells++;
        int pad_index = note - 35;
        int choke_group = preset_json_get_int_or_default(zone_settings, "chokeGroup", 0);

        const preset_json_value_t *drum_cell = preset_json_find_object_with_kind(chain, "drumCell");
        const preset_json_value_t *device_data = preset_json_object_get(drum_cell, "deviceData");
        const preset_json_value_t *parameters = preset_json_object_get(drum_cell, "parameters");

        const char *sample_uri_str = NULL;
        const preset_json_value_t *sample_uri = preset_json_object_get(device_data, "sampleUri");
        if (sample_uri) {
            sample_uri_str = preset_json_get_string(sample_uri);
        }

        char resolved_wav_path[1024] = "";
        if (sample_uri_str && sample_uri_str[0]) {
            std::string resolved = resolve_sample_uri(sample_uri_str, preset_dir);
            if (resolved.empty()) {
                set_error(inst, "Unsupported ableton:/ sample URI");
                return -1;
            }
            snprintf(resolved_wav_path, sizeof(resolved_wav_path), "%s", resolved.c_str());
        }

        float db = preset_json_get_number_or_default(parameters, "Volume", -12.0f);
        float vol = clampf(powf(10.0f, (db + 12.0f) / 20.0f), 0.0f, 2.0f);
        float pan = preset_json_get_number_or_default(parameters, "Pan", 0.0f);
        float tune = preset_json_get_number_or_default(parameters, "Voice_Transpose", 0.0f);
        float start = preset_json_get_number_or_default(parameters, "Voice_PlaybackStart", 0.0f);
        float attack_ms = preset_json_get_number_or_default(parameters, "Voice_Envelope_Attack", 0.0f) * 1000.0f;
        float decay_ms = preset_json_get_number_or_default(parameters, "Voice_Envelope_Decay", 0.25f) * 1000.0f;

        if (set_pad_sample_path(inst, pad_index, resolved_wav_path) != 0 && first_error.empty()) {
            first_error = inst->last_error[0] ? inst->last_error : "Failed to load preset sample";
        }

        char key[64];
        char val_str[128];

        mrdrums_make_pad_key(pad_index, "vol", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%.6g", vol);
        set_param_value(inst, key, val_str);

        mrdrums_make_pad_key(pad_index, "pan", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%.6g", pan);
        set_param_value(inst, key, val_str);

        mrdrums_make_pad_key(pad_index, "tune", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%.6g", tune);
        set_param_value(inst, key, val_str);

        mrdrums_make_pad_key(pad_index, "start", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%.6g", start);
        set_param_value(inst, key, val_str);

        mrdrums_make_pad_key(pad_index, "attack_ms", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%.6g", attack_ms);
        set_param_value(inst, key, val_str);

        mrdrums_make_pad_key(pad_index, "decay_ms", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%.6g", decay_ms);
        set_param_value(inst, key, val_str);

        mrdrums_make_pad_key(pad_index, "choke_group", key, sizeof(key));
        snprintf(val_str, sizeof(val_str), "%d", choke_group);
        set_param_value(inst, key, val_str);
    }

    if (mapped_cells == 0) {
        set_error(inst, "Preset contains no pads mapped to notes 36..51");
        return -1;
    }
    if (!first_error.empty()) {
        set_error(inst, first_error.c_str());
        return -1;
    }
    set_error(inst, NULL);
    return 0;
}

/* MPC port (2026-10-02): upstream freed a pad's sample while voices could still be playing it; the next render read
 * the freed buffer (ASan heap-use-after-free on a project load over ringing pads, found with steve/tools/fuzz).
 * Silence every voice reading this buffer first. (The wrapper serialises set_param and render.) */
static void stop_voices_reading(mrdrums_instance_t *inst, const float *data) {
    if (!data) return;
    for (int v = 0; v < MRDRUMS_ENGINE_MAX_VOICES; v++)
        if (inst->engine.voices[v].sample_data == data) inst->engine.voices[v].active = 0;
}

static void clear_pad_sample(mrdrums_instance_t *inst, int pad_index) {
    if (!inst || pad_index < 1 || pad_index > MRDRUMS_ENGINE_PAD_COUNT) return;
    int i = pad_index - 1;
    stop_voices_reading(inst, inst->samples[i].data);    /* MPC port */
    free_sample(&inst->samples[i]);
    mrdrums_engine_set_pad_sample(&inst->engine, pad_index, NULL, 0, MRDRUMS_ENGINE_SAMPLE_RATE);
}

static int set_pad_sample_path(mrdrums_instance_t *inst, int pad_index, const char *path) {
    if (!inst || pad_index < 1 || pad_index > MRDRUMS_ENGINE_PAD_COUNT || !path) return -1;
    int i = pad_index - 1;

    snprintf(inst->pad_sample_paths[i], sizeof(inst->pad_sample_paths[i]), "%s", path);

    if (!path[0]) {
        clear_pad_sample(inst, pad_index);
        set_error(inst, NULL);
        return 0;
    }

    if (has_supported_audio_extension(path)) {
        snprintf(inst->ui_last_sample_dir, sizeof(inst->ui_last_sample_dir), "%s", path);
    }

    if (!has_supported_audio_extension(path)) {
        clear_pad_sample(inst, pad_index);
        set_error(inst, "Selected file must be .wav, .aif, or .aiff");
        return -1;
    }

    char resolved[1024];
    if (path[0] == '/') {
        snprintf(resolved, sizeof(resolved), "%s", path);
    } else {
        snprintf(resolved, sizeof(resolved), "%s/%s", inst->module_dir, path);
    }

    sample_buffer_t loaded;
    char err[256];
    if (load_audio_mono(resolved, &loaded, err, sizeof(err)) != 0) {
        clear_pad_sample(inst, pad_index);
        set_error(inst, err);
        return -1;
    }

    stop_voices_reading(inst, inst->samples[i].data);    /* MPC port */
    free_sample(&inst->samples[i]);
    inst->samples[i] = loaded;
    mrdrums_engine_set_pad_sample(&inst->engine,
                                  pad_index,
                                  inst->samples[i].data,
                                  inst->samples[i].length,
                                  inst->samples[i].sample_rate);
    set_error(inst, NULL);
    return 0;
}

/* ---- MPC port: sample kits ----------------------------------------------------------------------------------
 * On the Move each pad's sample is picked in a file browser; a VST parameter on the MPC can't carry a path. So every
 * sub-folder of <module_dir>/kits is a kit, and its .wav/.aif files, sorted by name, go to pads 1..16. "kit" picks
 * one (index into the sorted folder list); "kit_count" and "kit_name" report them. Files are decoded on the calling
 * (UI) thread into a staging set; the audio thread swaps it in at the top of render_block after silencing every
 * voice, so a sample buffer is never freed while a voice reads it. Pad paths are stored relative to module_dir
 * ("kits/<kit>/<file>"), so a saved project reloads its samples through the engine's own state code. */
static int mpc_cmp_str(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static void mpc_kits_scan(mrdrums_instance_t *inst) {
    char dir[768];
    snprintf(dir, sizeof(dir), "%s/kits", inst->module_dir);
    inst->kits.count = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && inst->kits.count < MPC_MAX_KITS) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        DIR *sub = opendir(full);
        if (!sub) continue;
        closedir(sub);
        snprintf(inst->kits.names[inst->kits.count++], 64, "%s", e->d_name);
    }
    closedir(d);
    qsort(inst->kits.names, (size_t)inst->kits.count, 64, mpc_cmp_str);
}

/* Index of the kit the loaded pads came from (by pad 1's "kits/<name>/..." path), or -1. */
static int mpc_kits_current(const mrdrums_instance_t *inst) {
    for (int p = 0; p < MRDRUMS_ENGINE_PAD_COUNT; p++) {
        const char *path = inst->pad_sample_paths[p];
        if (strncmp(path, "kits/", 5) != 0) continue;
        const char *name = path + 5, *slash = strchr(name, '/');
        if (!slash) continue;
        for (int k = 0; k < inst->kits.count; k++)
            if ((size_t)(slash - name) == strlen(inst->kits.names[k]) && !strncmp(name, inst->kits.names[k], (size_t)(slash - name)))
                return k;
    }
    return -1;
}

/* Decode kit idx into the staging set (UI thread). Returns 0 if staged, -1 if busy/invalid. */
static int mpc_kits_stage(mrdrums_instance_t *inst, int idx) {
    if (idx < 0 || idx >= inst->kits.count) return -1;
    if (__atomic_load_n(&inst->kits.ready, __ATOMIC_ACQUIRE)) return -1;   /* the previous kit isn't swapped in yet */
    char dir[768];
    snprintf(dir, sizeof(dir), "%s/kits/%s", inst->module_dir, inst->kits.names[idx]);
    char files[MRDRUMS_ENGINE_PAD_COUNT * 4][64];
    int n = 0;
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < MRDRUMS_ENGINE_PAD_COUNT * 4) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= 64 || !has_supported_audio_extension(e->d_name)) continue;
        snprintf(files[n++], 64, "%s", e->d_name);
    }
    closedir(d);
    qsort(files, (size_t)n, 64, mpc_cmp_str);
    for (int p = 0; p < MRDRUMS_ENGINE_PAD_COUNT; p++) {
        sample_buffer_t *b = &inst->kits.staged[p];
        free_sample(b);
        inst->kits.staged_paths[p][0] = '\0';
        if (p >= n) continue;
        char full[1024], err[256];
        snprintf(full, sizeof(full), "%s/%s", dir, files[p]);
        if (load_audio_mono(full, b, err, sizeof(err)) == 0)
            snprintf(inst->kits.staged_paths[p], 512, "kits/%s/%s", inst->kits.names[idx], files[p]);
        else
            memset(b, 0, sizeof(*b));
    }
    __atomic_store_n(&inst->kits.ready, 1, __ATOMIC_RELEASE);
    return 0;
}

/* Swap a staged kit in (audio thread, or before audio starts). */
static void mpc_kits_apply(mrdrums_instance_t *inst) {
    if (!__atomic_load_n(&inst->kits.ready, __ATOMIC_ACQUIRE)) return;
    mrdrums_engine_all_notes_off(&inst->engine);          /* every voice inactive: nothing reads the old buffers */
    for (int p = 0; p < MRDRUMS_ENGINE_PAD_COUNT; p++) {
        free_sample(&inst->samples[p]);
        inst->samples[p] = inst->kits.staged[p];
        memset(&inst->kits.staged[p], 0, sizeof(inst->kits.staged[p]));
        snprintf(inst->pad_sample_paths[p], sizeof(inst->pad_sample_paths[p]), "%s", inst->kits.staged_paths[p]);
        mrdrums_engine_set_pad_sample(&inst->engine, p + 1, inst->samples[p].data, inst->samples[p].length,
                                      inst->samples[p].sample_rate);
    }
    __atomic_store_n(&inst->kits.ready, 0, __ATOMIC_RELEASE);
}

static int parse_mode_value(const char *val) {
    if (!val) return MRDRUMS_PAD_MODE_ONESHOT;
    if (strcasecmp(val, "gate") == 0 || strcmp(val, "0") == 0) return MRDRUMS_PAD_MODE_GATE;
    return MRDRUMS_PAD_MODE_ONESHOT;
}

static const char *mode_to_string(int mode) {
    return (mode == MRDRUMS_PAD_MODE_GATE) ? "gate" : "oneshot";
}

static int parse_on_off(const char *val) {
    if (!val) return 0;
    if (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0) return 1;
    if (strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0 || strcasecmp(val, "false") == 0) return 0;
    return atoi(val) != 0 ? 1 : 0;
}

static const char *on_off_to_string(int value) {
    return value ? "on" : "off";
}

static int parse_pad_page(const char *val) {
    if (!val) return 0;
    if (strcasecmp(val, "random") == 0 || strcmp(val, "1") == 0) return 1;
    return 0;
}

static const char *pad_page_to_string(int value) {
    return value ? "random" : "main";
}

static int parse_vel_curve(const char *val) {
    if (!val) return 0;
    if (strcasecmp(val, "soft") == 0 || strcmp(val, "1") == 0) return 1;
    if (strcasecmp(val, "hard") == 0 || strcmp(val, "2") == 0) return 2;
    return 0;
}

static const char *vel_curve_to_string(int vel_curve) {
    switch (vel_curve) {
        case 1: return "soft";
        case 2: return "hard";
        default: return "linear";
    }
}

typedef struct {
    const char *alias_key;
    const char *suffix;
} current_pad_alias_t;

static const current_pad_alias_t kCurrentPadAliases[] = {
    {"pad_sample_path", "sample_path"},
    {"pad_vol", "vol"},
    {"pad_pan", "pan"},
    {"pad_tune", "tune"},
    {"pad_start", "start"},
    {"pad_attack_ms", "attack_ms"},
    {"pad_decay_ms", "decay_ms"},
    {"pad_choke_group", "choke_group"},
    {"pad_mode", "mode"},
    {"pad_rand_pan_amt", "rand_pan_amt"},
    {"pad_rand_vol_amt", "rand_vol_amt"},
    {"pad_rand_decay_amt", "rand_decay_amt"},
    {"pad_chance_pct", "chance_pct"},
};

static const char *resolve_current_pad_alias_suffix(const char *key) {
    if (!key) return NULL;
    for (size_t i = 0; i < sizeof(kCurrentPadAliases) / sizeof(kCurrentPadAliases[0]); i++) {
        if (strcmp(key, kCurrentPadAliases[i].alias_key) == 0) {
            return kCurrentPadAliases[i].suffix;
        }
    }
    return NULL;
}

static int ui_note_to_pad(int note) {
    /* Primary mapping from requirements */
    int pad = mrdrums_engine_note_to_pad(note);
    if (pad > 0) return pad;

    /* Internal Move pad range fallback */
    if (note >= 68 && note <= 83) {
        return note - 68 + 1;
    }

    return 0;
}

static int set_param_value(mrdrums_instance_t *inst, const char *key, const char *val) {
    if (!inst || !key || !val) return 0;

    if (strcmp(key, "g_master_vol") == 0) {
        mrdrums_engine_set_master_vol(&inst->engine, (float)atof(val));
        return 1;
    }
    if (strcmp(key, "g_polyphony") == 0) {
        mrdrums_engine_set_polyphony(&inst->engine, atoi(val));
        return 1;
    }
    if (strcmp(key, "g_vel_curve") == 0) {
        inst->engine.vel_curve = clampi(parse_vel_curve(val), 0, 2);
        return 1;
    }
    if (strcmp(key, "g_humanize_ms") == 0) {
        inst->engine.humanize_ms = clampf((float)atof(val), 0.0f, 50.0f);
        return 1;
    }
    if (strcmp(key, "g_rand_seed") == 0) {
        unsigned long s = strtoul(val, NULL, 10);
        inst->engine.rand_seed = (uint32_t)s;
        if (!inst->engine.rand_seed) inst->engine.rand_seed = 1u;
        inst->engine.rng_state = inst->engine.rand_seed;
        return 1;
    }
    if (strcmp(key, "g_rand_loop_steps") == 0) {
        inst->engine.rand_loop_steps = clampi(atoi(val), 1, 128);
        return 1;
    }
    if (strcmp(key, "ui_auto_select_pad") == 0) {
        inst->ui_auto_select_pad = parse_on_off(val);
        return 1;
    }
    if (strcmp(key, "ui_current_pad") == 0) {
        inst->ui_current_pad = clampi(atoi(val), 1, 16);
        return 1;
    }
    if (strcmp(key, "ui_pad_page") == 0) {
        inst->ui_pad_page = parse_pad_page(val);
        return 1;
    }
    if (strcmp(key, "ui_last_sample_dir") == 0) {
        snprintf(inst->ui_last_sample_dir, sizeof(inst->ui_last_sample_dir), "%s", val);
        return 1;
    }
    if (strcmp(key, "ui_preset_path") == 0) {
        /* live UI selection: remember the path (browser scroll-restore) and
         * apply the preset. State restore stores the path WITHOUT loading so
         * local pad tweaks win — see apply_state_json. */
        snprintf(inst->ui_preset_path, sizeof(inst->ui_preset_path), "%s", val);
        if (val[0]) load_preset(inst, val);
        return 1;
    }

    const char *alias_suffix = resolve_current_pad_alias_suffix(key);
    if (alias_suffix) {
        char target_key[64];
        if (!mrdrums_make_pad_key(clampi(inst->ui_current_pad, 1, 16), alias_suffix, target_key, sizeof(target_key))) {
            return 0;
        }
        return set_param_value(inst, target_key, val);
    }

    const mrdrums_param_desc_t *pad = mrdrums_find_pad_param(key);
    if (!pad) return 0;

    int pad_index = pad->pad_index;
    if (strcmp(pad->suffix, "sample_path") == 0) {
        set_pad_sample_path(inst, pad_index, val);
        return 1;
    }
    if (strcmp(pad->suffix, "vol") == 0) {
        mrdrums_engine_set_pad_vol(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "pan") == 0) {
        mrdrums_engine_set_pad_pan(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "tune") == 0) {
        mrdrums_engine_set_pad_tune(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "start") == 0) {
        mrdrums_engine_set_pad_start(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "attack_ms") == 0) {
        mrdrums_engine_set_pad_attack_ms(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "decay_ms") == 0) {
        mrdrums_engine_set_pad_decay_ms(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "choke_group") == 0) {
        mrdrums_engine_set_pad_choke_group(&inst->engine, pad_index, atoi(val));
        return 1;
    }
    if (strcmp(pad->suffix, "mode") == 0) {
        mrdrums_engine_set_pad_mode(&inst->engine, pad_index, parse_mode_value(val));
        return 1;
    }
    if (strcmp(pad->suffix, "rand_pan_amt") == 0) {
        mrdrums_engine_set_pad_rand_pan_amt(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "rand_vol_amt") == 0) {
        mrdrums_engine_set_pad_rand_vol_amt(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "rand_decay_amt") == 0) {
        mrdrums_engine_set_pad_rand_decay_amt(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }
    if (strcmp(pad->suffix, "chance_pct") == 0) {
        mrdrums_engine_set_pad_chance_pct(&inst->engine, pad_index, (float)atof(val));
        return 1;
    }

    return 0;
}

static int get_param_value(mrdrums_instance_t *inst, const char *key, char *buf, int buf_len) {
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "g_master_vol") == 0) return snprintf(buf, buf_len, "%.4f", inst->engine.master_vol);
    if (strcmp(key, "g_polyphony") == 0) return snprintf(buf, buf_len, "%d", inst->engine.polyphony);
    if (strcmp(key, "g_vel_curve") == 0) return snprintf(buf, buf_len, "%s", vel_curve_to_string(inst->engine.vel_curve));
    if (strcmp(key, "g_humanize_ms") == 0) return snprintf(buf, buf_len, "%.4f", inst->engine.humanize_ms);
    if (strcmp(key, "g_rand_seed") == 0) return snprintf(buf, buf_len, "%u", inst->engine.rand_seed);
    if (strcmp(key, "g_rand_loop_steps") == 0) return snprintf(buf, buf_len, "%d", inst->engine.rand_loop_steps);
    if (strcmp(key, "ui_auto_select_pad") == 0) return snprintf(buf, buf_len, "%s", on_off_to_string(inst->ui_auto_select_pad));
    if (strcmp(key, "ui_current_pad") == 0) return snprintf(buf, buf_len, "%d", inst->ui_current_pad);
    if (strcmp(key, "ui_pad_page") == 0) return snprintf(buf, buf_len, "%s", pad_page_to_string(inst->ui_pad_page));
    if (strcmp(key, "ui_last_sample_dir") == 0) return snprintf(buf, buf_len, "%s", inst->ui_last_sample_dir);
    if (strcmp(key, "ui_preset_path") == 0) return snprintf(buf, buf_len, "%s", inst->ui_preset_path);

    const char *alias_suffix = resolve_current_pad_alias_suffix(key);
    if (alias_suffix) {
        char target_key[64];
        if (!mrdrums_make_pad_key(clampi(inst->ui_current_pad, 1, 16), alias_suffix, target_key, sizeof(target_key))) {
            return -1;
        }
        return get_param_value(inst, target_key, buf, buf_len);
    }

    const mrdrums_param_desc_t *pad = mrdrums_find_pad_param(key);
    if (!pad) return -1;
    const mrdrums_pad_t *p = &inst->engine.pads[pad->pad_index - 1];

    if (strcmp(pad->suffix, "sample_path") == 0) {
        return snprintf(buf, buf_len, "%s", inst->pad_sample_paths[pad->pad_index - 1]);
    }
    if (strcmp(pad->suffix, "vol") == 0) return snprintf(buf, buf_len, "%.4f", p->vol);
    if (strcmp(pad->suffix, "pan") == 0) return snprintf(buf, buf_len, "%.4f", p->pan);
    if (strcmp(pad->suffix, "tune") == 0) return snprintf(buf, buf_len, "%.4f", p->tune);
    if (strcmp(pad->suffix, "start") == 0) return snprintf(buf, buf_len, "%.4f", p->start);
    if (strcmp(pad->suffix, "attack_ms") == 0) return snprintf(buf, buf_len, "%.4f", p->attack_ms);
    if (strcmp(pad->suffix, "decay_ms") == 0) return snprintf(buf, buf_len, "%.4f", p->decay_ms);
    if (strcmp(pad->suffix, "choke_group") == 0) return snprintf(buf, buf_len, "%d", p->choke_group);
    if (strcmp(pad->suffix, "mode") == 0) return snprintf(buf, buf_len, "%s", mode_to_string(p->mode));
    if (strcmp(pad->suffix, "rand_pan_amt") == 0) return snprintf(buf, buf_len, "%.4f", p->rand_pan_amt);
    if (strcmp(pad->suffix, "rand_vol_amt") == 0) return snprintf(buf, buf_len, "%.4f", p->rand_vol_amt);
    if (strcmp(pad->suffix, "rand_decay_amt") == 0) return snprintf(buf, buf_len, "%.4f", p->rand_decay_amt);
    if (strcmp(pad->suffix, "chance_pct") == 0) return snprintf(buf, buf_len, "%.4f", p->chance_pct);

    return -1;
}

static void apply_defaults(mrdrums_instance_t *inst) {
    if (!inst) return;

    int global_count = 0;
    const mrdrums_param_desc_t *globals = mrdrums_global_params(&global_count);
    for (int i = 0; i < global_count; i++) {
        char num_buf[64];
        const char *value = globals[i].default_str;
        if (!value) {
            snprintf(num_buf, sizeof(num_buf), "%.6g", globals[i].default_num);
            value = num_buf;
        }
        set_param_value(inst, globals[i].key, value);
    }

    int field_count = 0;
    const mrdrums_pad_field_desc_t *fields = mrdrums_pad_fields(&field_count);
    for (int pad = 1; pad <= MRDRUMS_ENGINE_PAD_COUNT; pad++) {
        for (int fi = 0; fi < field_count; fi++) {
            char key[64];
            if (!mrdrums_make_pad_key(pad, fields[fi].suffix, key, sizeof(key))) continue;

            char num_buf[64];
            const char *value = fields[fi].default_str;
            if (!value) {
                snprintf(num_buf, sizeof(num_buf), "%.6g", fields[fi].default_num);
                value = num_buf;
            }
            set_param_value(inst, key, value);
        }
    }

    inst->ui_current_pad = clampi(inst->ui_current_pad, 1, 16);
    inst->ui_auto_select_pad = clampi(inst->ui_auto_select_pad, 0, 1);
    inst->ui_pad_page = clampi(inst->ui_pad_page, 0, 1);
    inst->ui_last_sample_dir[0] = '\0';
}

static void apply_state_json(mrdrums_instance_t *inst, const char *json) {
    if (!inst || !json || !json[0]) return;

    int global_count = 0;
    const mrdrums_param_desc_t *globals = mrdrums_global_params(&global_count);
    for (int i = 0; i < global_count; i++) {
        if (strcmp(globals[i].type, "enum") == 0) {
            char str_value[512];
            if (json_get_string(json, globals[i].key, str_value, sizeof(str_value)) == 0) {
                set_param_value(inst, globals[i].key, str_value);
            } else {
                float num_value;
                if (json_get_number(json, globals[i].key, &num_value) == 0) {
                    char num_buf[64];
                    snprintf(num_buf, sizeof(num_buf), "%.6g", num_value);
                    set_param_value(inst, globals[i].key, num_buf);
                }
            }
        } else if (strcmp(globals[i].type, "filepath") == 0) {
            char str_value[512];
            if (json_get_string(json, globals[i].key, str_value, sizeof(str_value)) == 0) {
                /* ui_preset_path: store path only — do NOT re-load the preset on
                 * restore, so persisted per-pad tweaks take priority. */
                if (strcmp(globals[i].key, "ui_preset_path") == 0) {
                    snprintf(inst->ui_preset_path, sizeof(inst->ui_preset_path), "%s", str_value);
                } else {
                    set_param_value(inst, globals[i].key, str_value);
                }
            }
        } else {
            float num_value;
            if (json_get_number(json, globals[i].key, &num_value) == 0) {
                char num_buf[64];
                snprintf(num_buf, sizeof(num_buf), "%.6g", num_value);
                set_param_value(inst, globals[i].key, num_buf);
            }
        }
    }

    int field_count = 0;
    const mrdrums_pad_field_desc_t *fields = mrdrums_pad_fields(&field_count);
    for (int pad = 1; pad <= MRDRUMS_ENGINE_PAD_COUNT; pad++) {
        for (int fi = 0; fi < field_count; fi++) {
            char key[64];
            if (!mrdrums_make_pad_key(pad, fields[fi].suffix, key, sizeof(key))) continue;

            if (strcmp(fields[fi].type, "enum") == 0) {
                char str_value[512];
                if (json_get_string(json, key, str_value, sizeof(str_value)) == 0) {
                    set_param_value(inst, key, str_value);
                } else {
                    float num_value;
                    if (json_get_number(json, key, &num_value) == 0) {
                        char num_buf[64];
                        snprintf(num_buf, sizeof(num_buf), "%.6g", num_value);
                        set_param_value(inst, key, num_buf);
                    }
                }
            } else if (strcmp(fields[fi].type, "filepath") == 0) {
                char str_value[512];
                if (json_get_string(json, key, str_value, sizeof(str_value)) == 0) {
                    set_param_value(inst, key, str_value);
                }
            } else {
                float num_value;
                if (json_get_number(json, key, &num_value) == 0) {
                    char num_buf[64];
                    snprintf(num_buf, sizeof(num_buf), "%.6g", num_value);
                    set_param_value(inst, key, num_buf);
                }
            }
        }
    }

    char last_dir[512];
    if (json_get_string(json, "ui_last_sample_dir", last_dir, sizeof(last_dir)) == 0) {
        set_param_value(inst, "ui_last_sample_dir", last_dir);
    }
}

static void *v2_create_instance(const char *module_dir, const char *json_defaults) {
    mrdrums_instance_t *inst = (mrdrums_instance_t *)calloc(1, sizeof(mrdrums_instance_t));
    if (!inst) return NULL;

    snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", module_dir ? module_dir : "");
    mrdrums_engine_init(&inst->engine);
    inst->ui_auto_select_pad = 1;
    inst->ui_current_pad = 1;
    inst->ui_pad_page = 0;
    set_error(inst, NULL);

    apply_defaults(inst);
    apply_state_json(inst, json_defaults);

    mpc_kits_scan(inst);                                   /* MPC port */
    int any = 0;
    for (int p = 0; p < MRDRUMS_ENGINE_PAD_COUNT; p++) any |= inst->samples[p].data != NULL;
    if (!any && mpc_kits_stage(inst, 0) == 0) mpc_kits_apply(inst);

    plugin_log("mrdrums instance created");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    mrdrums_instance_t *inst = (mrdrums_instance_t *)instance;
    if (!inst) return;

    for (int i = 0; i < MRDRUMS_ENGINE_PAD_COUNT; i++) {
        free_sample(&inst->samples[i]);
        free_sample(&inst->kits.staged[i]);               /* MPC port */
    }

    free(inst);
    plugin_log("mrdrums instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    mrdrums_instance_t *inst = (mrdrums_instance_t *)instance;
    if (!inst || !msg || len < 1) return;

    uint8_t status = msg[0] & 0xF0;
    uint8_t data1 = (len > 1) ? msg[1] : 0;
    uint8_t data2 = (len > 2) ? msg[2] : 0;

    if (status == 0x90) {
        if (data2 > 0) {
            int ui_pad = ui_note_to_pad((int)data1);
            if (inst->ui_auto_select_pad && ui_pad > 0) {
                inst->ui_current_pad = ui_pad;
            }
            mrdrums_engine_note_on(&inst->engine, (int)data1, (int)data2);
        } else {
            mrdrums_engine_note_off(&inst->engine, (int)data1);
        }
    } else if (status == 0x80) {
        mrdrums_engine_note_off(&inst->engine, (int)data1);
    } else if (status == 0xB0) {
        if (data1 == 123 || data1 == 120) {
            mrdrums_engine_all_notes_off(&inst->engine);
        }
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    mrdrums_instance_t *inst = (mrdrums_instance_t *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "state") == 0) {
        apply_state_json(inst, val);
        return;
    }

    if (strcmp(key, "all_notes_off") == 0) {
        mrdrums_engine_all_notes_off(&inst->engine);
        return;
    }

    if (strcmp(key, "kit") == 0) {                         /* MPC port */
        int idx = atoi(val);
        if (idx != mpc_kits_current(inst)) mpc_kits_stage(inst, idx);
        return;
    }

    set_param_value(inst, key, val);
}

static int append_state_key_value(mrdrums_instance_t *inst,
                                  char *buf,
                                  int buf_len,
                                  int *offset,
                                  const char *key,
                                  const char *type,
                                  int *is_first) {
    if (!inst || !buf || !offset || !key || !type || !is_first) return -1;

    char value[1024];
    if (get_param_value(inst, key, value, sizeof(value)) < 0) return -1;

    int off = *offset;
    if (!*is_first) {
        off += snprintf(buf + off, buf_len - off, ",");
    }

    if (strcmp(type, "enum") == 0 || strcmp(type, "filepath") == 0) {
        char escaped[1200];
        json_escape(value, escaped, sizeof(escaped));
        off += snprintf(buf + off, buf_len - off, "\"%s\":\"%s\"", key, escaped);
    } else if (strcmp(type, "int") == 0) {
        off += snprintf(buf + off, buf_len - off, "\"%s\":%d", key, atoi(value));
    } else {
        off += snprintf(buf + off, buf_len - off, "\"%s\":%.6g", key, atof(value));
    }

    *offset = off;
    *is_first = 0;
    return 0;
}

static int build_state_json(mrdrums_instance_t *inst, char *buf, int buf_len) {
    if (!inst || !buf || buf_len <= 2) return -1;

    int offset = 0;
    int is_first = 1;
    offset += snprintf(buf + offset, buf_len - offset, "{");

    int global_count = 0;
    const mrdrums_param_desc_t *globals = mrdrums_global_params(&global_count);
    for (int i = 0; i < global_count; i++) {
        append_state_key_value(inst, buf, buf_len, &offset, globals[i].key, globals[i].type, &is_first);
    }

    append_state_key_value(inst, buf, buf_len, &offset, "ui_last_sample_dir", "filepath", &is_first);

    int field_count = 0;
    const mrdrums_pad_field_desc_t *fields = mrdrums_pad_fields(&field_count);
    for (int pad = 1; pad <= MRDRUMS_ENGINE_PAD_COUNT; pad++) {
        for (int fi = 0; fi < field_count; fi++) {
            char key[64];
            if (!mrdrums_make_pad_key(pad, fields[fi].suffix, key, sizeof(key))) continue;
            append_state_key_value(inst, buf, buf_len, &offset, key, fields[fi].type, &is_first);
        }
    }

    offset += snprintf(buf + offset, buf_len - offset, "}");
    return (offset < buf_len) ? offset : -1;
}

static int build_chain_params_json(mrdrums_instance_t *inst, char *buf, int buf_len) {
    if (!buf || buf_len <= 2) return -1;

    int offset = 0;
    int is_first = 1;
    offset += snprintf(buf + offset, buf_len - offset, "[");

    int global_count = 0;
    const mrdrums_param_desc_t *globals = mrdrums_global_params(&global_count);
    for (int i = 0; i < global_count; i++) {
        const mrdrums_param_desc_t *g = &globals[i];
        if (!is_first) offset += snprintf(buf + offset, buf_len - offset, ",");
        is_first = 0;

        if (strcmp(g->type, "enum") == 0) {
            offset += snprintf(buf + offset, buf_len - offset,
                               "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"enum\",\"options\":%s,\"default\":\"%s\"}",
                               g->key,
                               g->name,
                               g->options_json ? g->options_json : "[]",
                               g->default_str ? g->default_str : "");
        } else if (strcmp(g->type, "filepath") == 0) {
            char filter_json[128];
            format_filter_json(g->filter, filter_json, sizeof(filter_json));
            const char *root = g->root ? g->root : "/data/UserData/UserLibrary/Samples";
            const char *start = g->start_path ? g->start_path : root;
            offset += snprintf(buf + offset, buf_len - offset,
                               "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"filepath\",\"root\":\"%s\",\"start_path\":\"%s\",\"filter\":%s}",
                               g->key, g->name, root, start, filter_json);
        } else {
            const char *type = strcmp(g->type, "int") == 0 ? "int" : "float";
            offset += snprintf(buf + offset, buf_len - offset,
                               "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":%g}",
                               g->key,
                               g->name,
                               type,
                               g->min_val,
                               g->max_val,
                               g->step > 0.0f ? g->step : 1.0f);
        }
    }

    int field_count = 0;
    const mrdrums_pad_field_desc_t *fields = mrdrums_pad_fields(&field_count);
    for (int fi = 0; fi < field_count; fi++) {
        const mrdrums_pad_field_desc_t *f = &fields[fi];
        char key[64];
        snprintf(key, sizeof(key), "pad_%s", f->suffix);

        if (!is_first) offset += snprintf(buf + offset, buf_len - offset, ",");
        is_first = 0;

        if (strcmp(f->type, "filepath") == 0) {
            const char *effective_start_path = (inst && inst->ui_last_sample_dir[0])
                ? inst->ui_last_sample_dir
                : (f->start_path ? f->start_path : "/data/UserData/UserLibrary/Samples");
            char filter_json[128];
            format_filter_json(f->filter, filter_json, sizeof(filter_json));
            if (effective_start_path && effective_start_path[0]) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"filepath\",\"root\":\"%s\",\"start_path\":\"%s\",\"filter\":%s,\"live_preview\":true,\"browser_hooks\":{\"on_open\":[{\"key\":\"ui_auto_select_pad\",\"value\":\"off\",\"restore\":true}]}}",
                                   key,
                                   f->name,
                                   f->root ? f->root : "/data/UserData/UserLibrary/Samples",
                                   effective_start_path,
                                   filter_json);
            } else {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"filepath\",\"root\":\"%s\",\"filter\":%s,\"live_preview\":true,\"browser_hooks\":{\"on_open\":[{\"key\":\"ui_auto_select_pad\",\"value\":\"off\",\"restore\":true}]}}",
                                   key,
                                   f->name,
                                   f->root ? f->root : "/data/UserData/UserLibrary/Samples",
                                   filter_json);
            }
        } else if (strcmp(f->type, "enum") == 0) {
            offset += snprintf(buf + offset, buf_len - offset,
                               "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"enum\",\"options\":%s,\"default\":\"%s\"}",
                               key,
                               f->name,
                               f->options_json ? f->options_json : "[]",
                               f->default_str ? f->default_str : "");
        } else {
            const char *type = strcmp(f->type, "int") == 0 ? "int" : "float";
            if (strcmp(f->suffix, "start") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"wav_position\",\"mode\":\"start\",\"filepath_param\":\"pad_sample_path\",\"min\":0,\"max\":1,\"step\":0.01,\"shift_increment_multiplier\":0.25}",
                                   key,
                                   f->name);
            } else {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":%g}",
                                   key,
                                   f->name,
                                   type,
                                   f->min_val,
                                   f->max_val,
                                   f->step > 0.0f ? f->step : 1.0f);
            }
        }
    }

    for (int pad = 1; pad <= MRDRUMS_ENGINE_PAD_COUNT; pad++) {
        for (int fi = 0; fi < field_count; fi++) {
            const mrdrums_pad_field_desc_t *f = &fields[fi];
            char key[64];
            if (!mrdrums_make_pad_key(pad, f->suffix, key, sizeof(key))) continue;

            char name[96];
            snprintf(name, sizeof(name), "P%02d %s", pad, f->name);

            if (!is_first) offset += snprintf(buf + offset, buf_len - offset, ",");
            is_first = 0;

            if (strcmp(f->type, "filepath") == 0) {
                const char *effective_start_path = (inst && inst->ui_last_sample_dir[0])
                    ? inst->ui_last_sample_dir
                    : (f->start_path ? f->start_path : "/data/UserData/UserLibrary/Samples");
                char filter_json[128];
                format_filter_json(f->filter, filter_json, sizeof(filter_json));
                if (effective_start_path && effective_start_path[0]) {
                    offset += snprintf(buf + offset, buf_len - offset,
                                       "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"filepath\",\"root\":\"%s\",\"start_path\":\"%s\",\"filter\":%s,\"live_preview\":true,\"browser_hooks\":{\"on_open\":[{\"key\":\"ui_auto_select_pad\",\"value\":\"off\",\"restore\":true}]}}",
                                       key,
                                       name,
                                       f->root ? f->root : "/data/UserData/UserLibrary/Samples",
                                       effective_start_path,
                                       filter_json);
                } else {
                    offset += snprintf(buf + offset, buf_len - offset,
                                       "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"filepath\",\"root\":\"%s\",\"filter\":%s,\"live_preview\":true,\"browser_hooks\":{\"on_open\":[{\"key\":\"ui_auto_select_pad\",\"value\":\"off\",\"restore\":true}]}}",
                                       key,
                                       name,
                                       f->root ? f->root : "/data/UserData/UserLibrary/Samples",
                                       filter_json);
                }
            } else if (strcmp(f->type, "enum") == 0) {
                offset += snprintf(buf + offset, buf_len - offset,
                                   "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"enum\",\"options\":%s,\"default\":\"%s\"}",
                                   key,
                                   name,
                                   f->options_json ? f->options_json : "[]",
                                   f->default_str ? f->default_str : "");
            } else {
                const char *type = strcmp(f->type, "int") == 0 ? "int" : "float";
                if (strcmp(f->suffix, "start") == 0) {
                    char linked_file_key[64];
                    if (!mrdrums_make_pad_key(pad, "sample_path", linked_file_key, sizeof(linked_file_key))) {
                        snprintf(linked_file_key, sizeof(linked_file_key), "p%02d_sample_path", pad);
                    }
                    offset += snprintf(buf + offset, buf_len - offset,
                                       "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"ui_type\":\"wav_position\",\"mode\":\"start\",\"filepath_param\":\"%s\",\"min\":%g,\"max\":%g,\"step\":%g,\"shift_increment_multiplier\":0.25}",
                                       key,
                                       name,
                                       type,
                                       linked_file_key,
                                       f->min_val,
                                       f->max_val,
                                       f->step > 0.0f ? f->step : 1.0f);
                } else {
                    offset += snprintf(buf + offset, buf_len - offset,
                                       "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"step\":%g}",
                                       key,
                                       name,
                                       type,
                                       f->min_val,
                                       f->max_val,
                                       f->step > 0.0f ? f->step : 1.0f);
                }
            }
        }
    }

    offset += snprintf(buf + offset, buf_len - offset, "]");
    return (offset < buf_len) ? offset : -1;
}

static int build_ui_hierarchy(mrdrums_instance_t *inst, char *buf, int buf_len) {
    if (!buf || buf_len <= 0) return -1;

    (void)inst;

    int n = snprintf(
        buf,
        (size_t)buf_len,
        "{"
            "\"levels\":{"
                "\"root\":{"
                    "\"name\":\"MrDrums\","
                    "\"params\":["
                        "{\"label\":\"Global\",\"level\":\"global\"},"
                        "{\"label\":\"Pad Settings\",\"level\":\"pad_settings\"},"
                        "\"ui_preset_path\""
                    "],"
                    "\"knobs\":[\"ui_auto_select_pad\",\"pad_vol\",\"pad_pan\",\"pad_tune\",\"pad_start\",\"pad_attack_ms\",\"pad_decay_ms\",\"pad_choke_group\",\"pad_mode\"]"
                "},"
                "\"global\":{"
                    "\"name\":\"Global\","
                    "\"params\":[\"g_master_vol\",\"g_polyphony\",\"g_vel_curve\",\"g_humanize_ms\",\"g_rand_seed\",\"g_rand_loop_steps\"],"
                    "\"knobs\":[\"g_master_vol\",\"g_polyphony\",\"g_vel_curve\",\"g_humanize_ms\",\"g_rand_seed\",\"g_rand_loop_steps\"]"
                "},"
                "\"pad_settings\":{"
                    "\"name\":\"Pad Settings\","
                    "\"params\":[\"ui_auto_select_pad\",\"ui_current_pad\",\"pad_sample_path\",\"pad_vol\",\"pad_pan\",\"pad_tune\",\"pad_start\",\"pad_attack_ms\",\"pad_decay_ms\",\"pad_choke_group\",\"pad_mode\",\"pad_rand_pan_amt\",\"pad_rand_vol_amt\",\"pad_rand_decay_amt\",\"pad_chance_pct\"],"
                    "\"knobs\":[\"ui_auto_select_pad\",\"pad_vol\",\"pad_pan\",\"pad_tune\",\"pad_start\",\"pad_attack_ms\",\"pad_decay_ms\",\"pad_choke_group\",\"pad_mode\"]"
                "}"
            "}"
        "}"
    );

    return (n >= 0 && n < buf_len) ? n : -1;
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    mrdrums_instance_t *inst = (mrdrums_instance_t *)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "name") == 0) return snprintf(buf, buf_len, "MrDrums");
    if (strcmp(key, "state") == 0) return build_state_json(inst, buf, buf_len);
    if (strcmp(key, "chain_params") == 0) return build_chain_params_json(inst, buf, buf_len);
    if (strcmp(key, "ui_hierarchy") == 0) return build_ui_hierarchy(inst, buf, buf_len);

    if (strcmp(key, "kit") == 0) {                         /* MPC port */
        int k = mpc_kits_current(inst);
        return snprintf(buf, buf_len, "%d", k < 0 ? 0 : k);
    }
    if (strcmp(key, "kit_count") == 0) return snprintf(buf, buf_len, "%d", inst->kits.count);
    /* MPC port: kit N's name without loading it, for MPC's PRESET menu (vst.json "programs" "name_at"). */
    if (strncmp(key, "kit_name_at:", 12) == 0) {
        int k = atoi(key + 12);
        if (k < 0 || k >= inst->kits.count) return -1;
        return snprintf(buf, buf_len, "%s", inst->kits.names[k]);
    }
    if (strcmp(key, "kit_name") == 0) {
        int k = mpc_kits_current(inst);
        return snprintf(buf, buf_len, "%s", k < 0 ? (inst->kits.count ? "(custom)" : "(no kits)") : inst->kits.names[k]);
    }

    return get_param_value(inst, key, buf, buf_len);
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    mrdrums_instance_t *inst = (mrdrums_instance_t *)instance;
    if (!inst || !buf || buf_len <= 0) return -1;
    if (!inst->last_error[0]) return 0;
    return snprintf(buf, buf_len, "%s", inst->last_error);
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    mrdrums_instance_t *inst = (mrdrums_instance_t *)instance;
    if (!out_interleaved_lr || frames <= 0) return;

    if (!inst) {
        memset(out_interleaved_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }

    mpc_kits_apply(inst);                                  /* MPC port */

    std::vector<float> left((size_t)frames, 0.0f);
    std::vector<float> right((size_t)frames, 0.0f);

    mrdrums_engine_render(&inst->engine, left.data(), right.data(), frames);

    for (int i = 0; i < frames; i++) {
        float l = left[(size_t)i];
        float r = right[(size_t)i];

        if (l > 0.95f || l < -0.95f) l = tanhf(l);
        if (r > 0.95f || r < -0.95f) r = tanhf(r);

        int32_t sl = (int32_t)(l * 32767.0f);
        int32_t sr = (int32_t)(r * 32767.0f);
        if (sl > 32767) sl = 32767;
        if (sl < -32768) sl = -32768;
        if (sr > 32767) sr = 32767;
        if (sr < -32768) sr = -32768;

        out_interleaved_lr[i * 2] = (int16_t)sl;
        out_interleaved_lr[i * 2 + 1] = (int16_t)sr;
    }
}

static plugin_api_v2_t g_api;

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;

    memset(&g_api, 0, sizeof(g_api));
    g_api.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_api.create_instance = v2_create_instance;
    g_api.destroy_instance = v2_destroy_instance;
    g_api.on_midi = v2_on_midi;
    g_api.set_param = v2_set_param;
    g_api.get_param = v2_get_param;
    g_api.get_error = v2_get_error;
    g_api.render_block = v2_render_block;

    plugin_log("mrdrums plugin initialized");
    return &g_api;
}
