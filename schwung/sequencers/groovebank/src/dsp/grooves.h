/*
 * grooves.h
 * Rhythm-template model + loader for Groove Bank.
 *
 * A groove is a SINGLE-lane rhythm (when the hits fall) — the pitch comes from
 * whatever chord/note you hold live, so a pattern is one row, not a drum grid.
 * Grooves are NOT compiled in; they load at startup from external ".groove"
 * text files so users can add their own:
 *
 *     name: Son Clave 3-2
 *     genre: LATIN
 *     steps: 16
 *     hit:  x..x..x...x.x...
 *
 * (blank line between grooves)
 *
 * Row characters:
 *     '.' rest   'x' normal hit   'A' accent   'g' ghost (soft)   '-' tie
 *   ('X' is an alias for 'A', 'o' for 'g'.)  Spaces in a row are ignored.
 *   A tie '-' sustains the previous hit through this step (no new attack).
 *
 * A groove is a "font family": the 'hit:' row is variant 0 (regular), and each
 * following 'alt:' row is an extra flavor (busy / sparse / syncopated / …),
 * morphed live on Knob 4. All rows share the same 'steps'.
 *
 * The loader scans two folders and concatenates them:
 *   1. <module_dir>/patterns/                       shipped defaults
 *   2. /data/UserData/schwung/groovebank/patterns/  user files (persist)
 *
 * Tempo and swing are NOT part of a groove: it plays straight and follows
 * Move's transport; swing is a live global control.
 */

#pragma once
#include <stdint.h>

#define GB_MAX_STEPS     32
#define GB_MAX_PATTERNS  600
#define GB_MAX_VARIANTS  6    /* base 'hit:' + up to 5 'alt:' flavors          */

#define GB_VEL_GHOST     40u
#define GB_VEL_NORMAL    100u
#define GB_VEL_ACCENT    122u

/* User pattern folder (created on first run if absent). */
#define GB_USER_PATTERN_DIR "/data/UserData/schwung/groovebank/patterns"

typedef struct {
    char    name[24];
    char    genre[12];
    uint8_t steps;                    /* 1..32                                */
    uint8_t variant_count;            /* 1..GB_MAX_VARIANTS                    */
    char    row[GB_MAX_VARIANTS][GB_MAX_STEPS + 1];  /* variant 0 = base 'hit' */
} GroovePattern;

typedef struct {
    GroovePattern *patterns;   /* heap array of capacity GB_MAX_PATTERNS      */
    int            count;
} GrooveBank;

/* Load the bank: scans <module_dir>/patterns and the user folder, then falls
 * back to a tiny built-in set if no files were found. Allocates bank->patterns
 * (caller owns it; free with gb_bank_free). Returns bank->count. module_dir
 * may be NULL (only the user folder + fallback are used). */
int  gb_bank_init(GrooveBank *bank, const char *module_dir);
void gb_bank_free(GrooveBank *bank);

/* Stable-group the bank so all patterns of a genre are contiguous (genres in
 * first-appearance order, order within a genre preserved). Called by
 * gb_bank_init; exposed for tests. */
void gb_bank_group_by_genre(GrooveBank *bank);

/* Parse one .groove text buffer, appending patterns to the bank. Returns the
 * number of patterns added. */
int  gb_bank_parse_buffer(GrooveBank *bank, const char *text);

/* Velocity for a row character, or 0 for a rest / tie / unknown char. */
uint8_t gb_char_velocity(char c);
