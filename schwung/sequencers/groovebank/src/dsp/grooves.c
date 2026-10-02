/*
 * grooves.c — Groove Bank rhythm-template model + ".groove" file loader.
 *
 * See grooves.h for the file format. No randomness, no generation: a groove is
 * exactly the single hit row in the file.
 */

#include "grooves.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

uint8_t gb_char_velocity(char c)
{
    switch (c) {
        case 'A': case 'X': return GB_VEL_ACCENT;
        case 'x':           return GB_VEL_NORMAL;
        case 'g': case 'o': return GB_VEL_GHOST;
        default:            return 0u;   /* '.', '-', anything else = no attack */
    }
}

/* ── .groove parsing ─────────────────────────────────────────────────────── */

/* Copy a value with surrounding whitespace trimmed into dst (size n). */
static void copy_trimmed(char *dst, int n, const char *src, int len)
{
    int i = 0;
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    for (; i < len && i < n - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* Copy the hit row, dropping ALL whitespace, validating chars, normalising
 * 'X'->'A' and 'o'->'g'. Pads/truncates to `steps`. Valid: . x A g - */
static void copy_row(char *dst, const char *src, int len, int steps)
{
    int n = 0;
    if (steps > GB_MAX_STEPS) steps = GB_MAX_STEPS;
    for (int i = 0; i < len && n < steps; i++) {
        char c = src[i];
        if (isspace((unsigned char)c)) continue;
        if      (c == 'X') c = 'A';
        else if (c == 'o') c = 'g';
        if (c != '.' && c != 'x' && c != 'A' && c != 'g' && c != '-') c = '.';
        dst[n++] = c;
    }
    while (n < steps) dst[n++] = '.';
    dst[n] = '\0';
}

static int pattern_has_hits(const GroovePattern *p)
{
    for (int v = 0; v < p->variant_count; v++)
        for (const char *r = p->row[v]; *r; r++)
            if (gb_char_velocity(*r)) return 1;   /* any variant with a real hit */
    return 0;
}

/* Append a variant row (the 'hit:' base, then each 'alt:'). */
static void add_variant(GroovePattern *cur, const char *val, int vlen)
{
    if (cur->variant_count >= GB_MAX_VARIANTS) return;
    copy_row(cur->row[cur->variant_count], val, vlen, cur->steps);
    cur->variant_count++;
}

/* Finalise the in-progress groove and append it to the bank if valid. */
static void finalise(GrooveBank *bank, GroovePattern *cur, int *have)
{
    if (!*have) return;
    *have = 0;
    if (cur->steps < 1 || cur->steps > GB_MAX_STEPS) return;
    if (cur->name[0] == '\0') return;
    if (!pattern_has_hits(cur)) return;
    if (bank->count >= GB_MAX_PATTERNS) return;
    bank->patterns[bank->count++] = *cur;
}

int gb_bank_parse_buffer(GrooveBank *bank, const char *text)
{
    GroovePattern cur;
    int have = 0;
    int added_start = bank->count;
    const char *p = text;

    if (!text) return 0;
    memset(&cur, 0, sizeof(cur));

    while (*p) {
        const char *line = p;
        const char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - line);

        const char *s = line;
        int sl = line_len;
        while (sl > 0 && isspace((unsigned char)*s)) { s++; sl--; }

        if (sl == 0 || s[0] == '#') {
            /* blank line / comment — grooves are delimited by the next "name:" */
        } else {
            const char *colon = memchr(s, ':', sl);
            if (colon) {
                int klen = (int)(colon - s);
                const char *val = colon + 1;
                int vlen = (int)(sl - klen - 1);

                if (klen == 4 && strncmp(s, "name", 4) == 0) {
                    finalise(bank, &cur, &have);
                    memset(&cur, 0, sizeof(cur));
                    cur.steps = 16;
                    copy_trimmed(cur.name, sizeof(cur.name), val, vlen);
                    have = 1;
                } else if (klen == 5 && strncmp(s, "genre", 5) == 0) {
                    copy_trimmed(cur.genre, sizeof(cur.genre), val, vlen);
                } else if (klen == 5 && strncmp(s, "steps", 5) == 0) {
                    char tmp[8];
                    copy_trimmed(tmp, sizeof(tmp), val, vlen);
                    int st = atoi(tmp);
                    cur.steps = (uint8_t)(st < 1 ? 1 : st > GB_MAX_STEPS ? GB_MAX_STEPS : st);
                } else if (klen == 3 && strncmp(s, "hit", 3) == 0) {
                    if (cur.variant_count == 0) add_variant(&cur, val, vlen);
                    else copy_row(cur.row[0], val, vlen, cur.steps);   /* hit is always base */
                } else if (klen == 3 && strncmp(s, "alt", 3) == 0) {
                    add_variant(&cur, val, vlen);                       /* flavor variant */
                }
                /* unknown keys are ignored */
            }
        }

        p = (*eol == '\n') ? eol + 1 : eol;
    }
    finalise(bank, &cur, &have);
    return bank->count - added_start;
}

/* ── File / directory loading ────────────────────────────────────────────── */

static char *read_whole_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long sz;
    char *buf;
    size_t got;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    sz = ftell(fp);
    if (sz < 0 || sz > 4 * 1024 * 1024) { fclose(fp); return NULL; }
    rewind(fp);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    got = fread(buf, 1, (size_t)sz, fp);
    buf[got] = '\0';
    fclose(fp);
    return buf;
}

static int ends_with_groove(const char *name)
{
    size_t n = strlen(name);
    return n > 7 && strcmp(name + n - 7, ".groove") == 0;
}

/* strdup is POSIX, not C99 — local copy to avoid feature-macro pitfalls. */
static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Scan one directory for *.groove files (sorted), parsing each into the bank. */
static void load_dir(GrooveBank *bank, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *ent;
    char *names[GB_MAX_PATTERNS];
    int n = 0;

    if (!d) return;
    while ((ent = readdir(d)) != NULL && n < GB_MAX_PATTERNS) {
        if (ent->d_name[0] == '.') continue;
        if (!ends_with_groove(ent->d_name)) continue;
        names[n] = dup_str(ent->d_name);
        if (names[n]) n++;
    }
    closedir(d);

    qsort(names, n, sizeof(names[0]), cmp_str);

    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        char *text = read_whole_file(path);
        if (text) { gb_bank_parse_buffer(bank, text); free(text); }
        free(names[i]);
    }
}

static void ensure_user_dir(void)
{
    const char *parts[] = {
        "/data/UserData/schwung/groovebank",
        "/data/UserData/schwung/groovebank/patterns",
    };
    const char *howto = GB_USER_PATTERN_DIR "/_HOWTO.txt";
    FILE *fp;

    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
        mkdir(parts[i], 0775);

    fp = fopen(howto, "r");
    if (fp) { fclose(fp); return; }
    fp = fopen(howto, "w");
    if (!fp) return;
    fputs(
        "Groove Bank — add your own rhythm templates here\n"
        "================================================\n\n"
        "Create a file ending in .groove in this folder. Each groove is a\n"
        "block of lines; separate grooves with a blank line:\n\n"
        "    name: My Groove\n"
        "    genre: HOUSE\n"
        "    steps: 16\n"
        "    hit:  ..x...x...x...x.\n\n"
        "A groove is a SINGLE rhythm row — the pitch comes from the chord you\n"
        "hold, so there is one 'hit:' line, not drum voices.\n\n"
        "steps is 16 (one bar) or 32 (two bars). Row characters:\n"
        "    .  rest    x  hit    A  accent    g  ghost (soft)    -  tie/sustain\n\n"
        "A tie '-' sustains the previous hit through that step (no new attack),\n"
        "so 'A---' is one accented note held for four steps.\n\n"
        "VARIANTS: add 'alt:' rows after 'hit:' for extra flavors of the same\n"
        "groove (busy, sparse, syncopated...). Knob 4 morphs between them live:\n"
        "    hit:  ..x...x...x...x.\n"
        "    alt:  ..x.x.x...x.x.x.\n"
        "    alt:  ..x.......x.....\n"
        "Up to 6 variants (base + 5 alts), all the same length as 'steps'.\n\n"
        "Your files are added after the built-in grooves. Reload the module\n"
        "(or restart Schwung) to pick up changes.\n",
        fp);
    fclose(fp);
}

/* Tiny built-in fallback so the module is never empty if no files load. */
static const char *kFallback =
    "name: Offbeat\n"    "genre: HOUSE\n"  "steps: 16\n"  "hit: ..x...x...x...x.\n\n"
    "name: Four Floor\n" "genre: HOUSE\n"  "steps: 16\n"  "hit: x...x...x...x...\n\n"
    "name: Tresillo\n"   "genre: LATIN\n"  "steps: 16\n"  "hit: x..x..x.x..x..x.\n\n"
    "name: Charleston\n" "genre: FUNK\n"   "steps: 16\n"  "hit: x......x........\n";

#define GB_MAX_GENRES 64

void gb_bank_group_by_genre(GrooveBank *bank)
{
    char order[GB_MAX_GENRES][sizeof(((GroovePattern*)0)->genre)];
    int no = 0;
    GroovePattern *out;
    int k = 0;

    if (!bank || bank->count <= 1 || !bank->patterns) return;

    for (int i = 0; i < bank->count; i++) {
        const char *g = bank->patterns[i].genre;
        int found = 0;
        for (int j = 0; j < no; j++) if (strcmp(order[j], g) == 0) { found = 1; break; }
        if (!found && no < GB_MAX_GENRES) {
            strncpy(order[no], g, sizeof(order[0]) - 1);
            order[no][sizeof(order[0]) - 1] = '\0';
            no++;
        }
    }

    out = (GroovePattern *)malloc((size_t)bank->count * sizeof(GroovePattern));
    if (!out) return;
    for (int j = 0; j < no; j++)
        for (int i = 0; i < bank->count; i++)
            if (strcmp(bank->patterns[i].genre, order[j]) == 0)
                out[k++] = bank->patterns[i];
    memcpy(bank->patterns, out, (size_t)k * sizeof(GroovePattern));
    free(out);
}

int gb_bank_init(GrooveBank *bank, const char *module_dir)
{
    bank->count = 0;
    bank->patterns = (GroovePattern *)calloc(GB_MAX_PATTERNS, sizeof(GroovePattern));
    if (!bank->patterns) return 0;

    if (module_dir) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/patterns", module_dir);
        load_dir(bank, dir);
    }

    ensure_user_dir();
    load_dir(bank, GB_USER_PATTERN_DIR);

    if (bank->count == 0)
        gb_bank_parse_buffer(bank, kFallback);

    gb_bank_group_by_genre(bank);
    return bank->count;
}

void gb_bank_free(GrooveBank *bank)
{
    if (bank && bank->patterns) {
        free(bank->patterns);
        bank->patterns = NULL;
        bank->count = 0;
    }
}
