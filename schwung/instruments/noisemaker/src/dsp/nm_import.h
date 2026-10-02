/* nm_import.h — parse a TAL `.noisemakerpreset` file.
 *
 * TAL writes a preset as plain XML: one `<program>` element whose attributes
 * are the lowercased SYNTHPARAMETERS names carrying ENGINE-SPACE values (the
 * same representation `load_preset` raw-applies for the factory bank, so no
 * display conversion happens anywhere in here), followed by a `<splinePoints>`
 * list holding the Envelope Editor shape.
 *
 * This is the on-device twin of tools/gen_factory_bank.mjs + gen_factory_splines.mjs.
 * Everything it knows that is not obvious from the format was measured over a
 * 442-preset corpus; see the notes at each trap below and in param_names.h.
 *
 * Deliberately allocation-free: the caller owns the file buffer and the output
 * struct. Every scan is bounded by `len`, so a truncated or hostile file yields
 * a parse failure rather than a read past the end.
 */
#ifndef NM_IMPORT_H
#define NM_IMPORT_H

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "Params.h"
#include "param_names.h"
#include "factory_splines.h"   /* nm_spline_point_t */

/* Corpus max is 30 points; the editor itself is the real limit. Extra points
 * beyond this are dropped rather than overflowing. */
#define NM_IMPORT_MAX_SPLINE 64
#define NM_IMPORT_MAX_NAME   64

typedef struct {
    char              name[NM_IMPORT_MAX_NAME];
    float             data[NUMPARAM];
    nm_spline_point_t spline[NM_IMPORT_MAX_SPLINE];
    int               spline_count;
} nm_import_preset_t;

/* Counters, not just a bool. An attribute vocabulary we have never seen shows
 * up as a rising `attrs_unknown` instead of as quietly missing sound. */
typedef struct {
    int attrs_seen;
    int attrs_unknown;
    int spline_dropped;      /* points past NM_IMPORT_MAX_SPLINE */
    int remapped_filtertype; /* files that needed the 10->12 item re-encode */
    int spline_synthesized;  /* presets that carried no usable shape */
} nm_import_stats_t;

/* ---- small bounded scanners ---------------------------------------------- */

static inline int nm_imp_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Find `needle` in buf[from..len). Returns index or -1. */
static inline int nm_imp_find(const char *buf, int len, int from, const char *needle) {
    int nl = (int)strlen(needle);
    if (nl == 0 || from < 0) return -1;
    for (int i = from; i + nl <= len; i++) {
        if (memcmp(buf + i, needle, (size_t)nl) == 0) return i;
    }
    return -1;
}

/* Find an element open tag: `<name` followed by whitespace or '/' or '>'.
 *
 * ⚠ A bare prefix search is WRONG here: `<program` matches `<programs>`, the
 * container element, whose span holds no attributes at all. That yields a
 * preset that parses "successfully" with every parameter left at zero. */
static inline int nm_imp_find_tag(const char *buf, int len, int from, const char *tag) {
    int tl = (int)strlen(tag);
    int i = from;
    while (i < len) {
        int at = nm_imp_find(buf, len, i, tag);
        if (at < 0) return -1;
        int after = at + tl;
        if (after < len) {
            char c = buf[after];
            if (nm_imp_is_space(c) || c == '>' || c == '/') return at;
        }
        i = at + 1;
    }
    return -1;
}

/* Read the value of `attr` inside buf[from..to). Returns the offset of the
 * first character of the value, and writes its length to *vlen; -1 if absent.
 * Matching is case-insensitive and requires a real attribute boundary, so
 * `lfo1phase` does not match inside `xlfo1phase`. */
static inline int nm_imp_attr(const char *buf, int from, int to, const char *attr, int *vlen) {
    int al = (int)strlen(attr);
    for (int i = from; i + al + 2 < to; i++) {
        if (i > from && !nm_imp_is_space(buf[i - 1])) continue;
        int j = 0;
        while (j < al) {
            char a = attr[j], b = buf[i + j];
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != b) break;
            j++;
        }
        if (j != al) continue;
        int k = i + al;
        while (k < to && nm_imp_is_space(buf[k])) k++;
        if (k >= to || buf[k] != '=') continue;
        k++;
        while (k < to && nm_imp_is_space(buf[k])) k++;
        if (k >= to || buf[k] != '"') continue;
        k++;
        int end = k;
        while (end < to && buf[end] != '"') end++;
        if (end >= to) return -1;
        *vlen = end - k;
        return k;
    }
    return -1;
}

/* strtof over a bounded span. The span is not NUL-terminated, so copy it into
 * a small stack buffer first; values in this format are short floats. */
static inline float nm_imp_f(const char *buf, int at, int vlen) {
    char tmp[64];
    if (at < 0 || vlen <= 0) return 0.0f;
    if (vlen > (int)sizeof(tmp) - 1) vlen = (int)sizeof(tmp) - 1;
    memcpy(tmp, buf + at, (size_t)vlen);
    tmp[vlen] = '\0';
    return (float)atof(tmp);
}

static inline float nm_imp_attr_f(const char *buf, int from, int to, const char *attr) {
    int vlen = 0;
    int at = nm_imp_attr(buf, from, to, attr, &vlen);
    return at < 0 ? 0.0f : nm_imp_f(buf, at, vlen);
}

/* ---- the parser ---------------------------------------------------------- */

/* Returns 1 on success, 0 if the file is not a parseable preset.
 * `buf` must be NUL-terminated at buf[len]. `stats` may be NULL. */
static inline int nm_import_parse(const char *buf, int len,
                                  nm_import_preset_t *out,
                                  nm_import_stats_t *stats) {
    if (!buf || !out || len <= 0) return 0;
    memset(out, 0, sizeof(*out));

    /* ---- version, which decides the filtertype encoding -----------------
     * ⚠ Parse as a FLOAT and compare with a threshold. 16 of the 442 corpus
     * files carry version="1.6999999999999999556" -- float noise for 1.7. A
     * string compare (or a startswith("1.6")) buckets those as the older
     * 10-item scheme and silently gives every one of them the wrong filter. */
    int talAt = nm_imp_find_tag(buf, len, 0, "<tal");
    if (talAt < 0) return 0;
    int talEnd = nm_imp_find(buf, len, talAt, ">");
    if (talEnd < 0) return 0;
    float version = nm_imp_attr_f(buf, talAt, talEnd, "version");

    /* ---- the <program> element ------------------------------------------ */
    int progAt = nm_imp_find_tag(buf, len, 0, "<program");
    if (progAt < 0) return 0;
    int progEnd = nm_imp_find(buf, len, progAt, ">");
    if (progEnd < 0) return 0;

    int vlen = 0;
    int nameAt = nm_imp_attr(buf, progAt, progEnd, "programname", &vlen);
    if (nameAt >= 0) {
        int n = vlen < NM_IMPORT_MAX_NAME - 1 ? vlen : NM_IMPORT_MAX_NAME - 1;
        memcpy(out->name, buf + nameAt, (size_t)n);
        out->name[n] = '\0';
    }

    /* Walk every attribute in the element span and resolve it to a slot. */
    int found = 0;
    int i = progAt + 8;   /* past "<program" */
    while (i < progEnd) {
        while (i < progEnd && nm_imp_is_space(buf[i])) i++;
        int ks = i;
        while (i < progEnd && buf[i] != '=' && !nm_imp_is_space(buf[i])) i++;
        int klen = i - ks;
        if (klen <= 0) { i++; continue; }
        while (i < progEnd && nm_imp_is_space(buf[i])) i++;
        if (i >= progEnd || buf[i] != '=') continue;
        i++;
        while (i < progEnd && nm_imp_is_space(buf[i])) i++;
        if (i >= progEnd || buf[i] != '"') continue;
        i++;
        int vs = i;
        while (i < progEnd && buf[i] != '"') i++;
        int vl = i - vs;
        i++;   /* past the closing quote */

        if (klen == 11 && nm_param_index(buf + ks, klen) < 0 &&
            memcmp(buf + ks, "programname", 11) == 0) continue;

        if (stats) stats->attrs_seen++;
        int idx = nm_param_index(buf + ks, klen);
        if (idx < 0) {
            /* programname is handled above; everything else unresolved is
             * GUI state (midilearn / midilock / midiclear / unknown). */
            if (!(klen == 11 && memcmp(buf + ks, "programname", 11) == 0)) {
                if (stats) stats->attrs_unknown++;
            }
            continue;
        }
        if (idx >= NUMPARAM) continue;
        out->data[idx] = nm_imp_f(buf, vs, vl);
        found++;
    }
    if (found == 0) return 0;   /* an element with no resolvable parameters */

    /* ---- filtertype: 10-item -> 12-item ---------------------------------
     * The engine has 12 filter types; presets written before 1.7 encode an
     * index into a 10-item list. The first 10 entries are identical, so decode
     * the authored index k = round(v*9) and re-encode as k/11 so
     * calcComboBoxValue(v, 12) picks the filter the preset meant.
     * Measured: v1.6 files fit k/9 with zero residual, v1.7 files fit k/11. */
    if (version < 1.65f) {
        float v = out->data[FILTERTYPE];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        int k = (int)(v * 9.0f + 0.5f);
        out->data[FILTERTYPE] = (float)k / 11.0f;
        if (stats) stats->remapped_filtertype++;
    }

    /* PANIC is a momentary command, not a stored setting; load_preset already
     * skips it for the factory bank. Clear it so an imported preset can never
     * arrive holding the synth in panic. */
    out->data[PANIC] = 0.0f;

    /* ---- spline points --------------------------------------------------- */
    int sp = progEnd;
    while (out->spline_count < NM_IMPORT_MAX_SPLINE) {
        int at = nm_imp_find_tag(buf, len, sp, "<splinePoint");
        if (at < 0) break;
        int end = nm_imp_find(buf, len, at, ">");
        if (end < 0) break;
        nm_spline_point_t *p = &out->spline[out->spline_count++];
        p->isStart = nm_imp_attr_f(buf, at, end, "isStartPoint") != 0.0f;
        p->isEnd   = nm_imp_attr_f(buf, at, end, "isEndPoint")   != 0.0f;
        p->cx  = nm_imp_attr_f(buf, at, end, "centerPointX");
        p->cy  = nm_imp_attr_f(buf, at, end, "centerPointY");
        p->clx = nm_imp_attr_f(buf, at, end, "controlPointLeftX");
        p->cly = nm_imp_attr_f(buf, at, end, "controlPointLeftY");
        p->crx = nm_imp_attr_f(buf, at, end, "controlPointRightX");
        p->cry = nm_imp_attr_f(buf, at, end, "controlPointRightY");
        sp = end + 1;
    }
    /* Count anything past the cap so a truncated shape is visible. */
    if (out->spline_count >= NM_IMPORT_MAX_SPLINE) {
        int extra = 0, at = sp;
        while ((at = nm_imp_find_tag(buf, len, at, "<splinePoint")) >= 0) { extra++; at++; }
        if (stats) stats->spline_dropped += extra;
    }
    /* ⚠ 40 of the 442 corpus presets carry an EMPTY <splinePoints> container.
     * Returning "no shape" for those would be a trap, not a saving: installing
     * a shape is the ONLY thing that overwrites the editor's current spline, so
     * a shapeless preset would silently INHERIT the previous patch's envelope
     * and sound different depending on what you loaded before it. Synthesize
     * the neutral flat line (a start and an end at y=0.5, which is what TAL
     * itself writes for an untouched editor) so every imported preset installs
     * a deterministic shape. */
    if (out->spline_count < 2) {
        memset(out->spline, 0, sizeof(out->spline));
        nm_spline_point_t *a = &out->spline[0];
        nm_spline_point_t *b = &out->spline[1];
        a->isStart = 1; a->isEnd = 0;
        a->cx = 0.0f; a->cy = 0.5f;
        a->clx = 0.0f; a->cly = 0.5f; a->crx = 0.1f; a->cry = 0.5f;
        b->isStart = 0; b->isEnd = 1;
        b->cx = 1.0f; b->cy = 0.5f;
        b->clx = 0.9f; b->cly = 0.5f; b->crx = 1.0f; b->cry = 0.5f;
        out->spline_count = 2;
        if (stats) stats->spline_synthesized++;
    }

    return 1;
}

#endif /* NM_IMPORT_H */
