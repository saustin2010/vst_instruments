/* Tablor — 2-osc wavetable synth for Ableton Move (Schwung module).
 *
 * plugin_api_v2. Parameter surface + version-tagged state round-trip +
 * the phase 3 engine: 8 voices, poly/mono, glide, mod matrix.
 *
 * Realtime rules (docs/REALTIME_SAFETY.md): render_block never allocates,
 * never touches the filesystem, never logs.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <new>
#include <algorithm>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <atomic>
#include <memory>

#include "../host/plugin_api_v1.h"
#include "params.h"
#include "engine.h"
#include "wt/scanner.h"
#include "wt/loader.h"

static const host_api_v1_t *g_host = nullptr;

/* ------------------------------------------------------------------ */
/* Instance                                                            */
/* ------------------------------------------------------------------ */

struct tablor_instance {
    tb::Engine engine;
    tb::WtScanner scanner;                  /* used for first-run seeding only */
    tb::WtLoader loader { engine };
    char  module_dir[512] = {};

    /* wavetable selection = absolute file paths ("" = built-in Init) */
    char  wt_path[TB_WT_SLOTS][512] = {};

    /* presets: factory (from the module) + UNLIMITED user .tblr files in
     * the user library (filename = preset name, content = TBLR1 blob).
     *
     * The worker rebuilds this list whenever the folder changes while the
     * audio thread reads it in get_param, so it is published as an IMMUTABLE
     * snapshot and swapped atomically — never mutated in place. */
    struct preset { std::string name, blob, path; };   /* path "" = factory */
    struct PresetList { std::vector<preset> items; int factory_count = 0; };
    std::shared_ptr<const PresetList> presets { std::make_shared<PresetList>() };
    std::atomic<int> preset_index { 0 };

    std::shared_ptr<const PresetList> presetList() const
    { return std::atomic_load(&presets); }

    /* state blob scratch */
    char  state_buf[8 * 1024];

    /* ---- Turnable wavetable selection -------------------------------
     * The filepath control (wt1_table/wt2_table) is OPAQUE to the host:
     * it can be opened, never turned, because turning a path would write
     * nonsense into it. So a pot cannot change the wavetable, which is
     * what was reported.
     *
     * These add the turnable half WITHOUT touching the filepath control:
     * a pack chosen from a list, and one enum per oscillator whose
     * options are the entries in that pack. An enum is turnable AND
     * divable, so hold+click opens a scrolling picker for a long pack.
     *
     * Rebuilt only when the scan or the pack changes, on the worker and
     * NEVER on the SPI callback -- neither in get_param (see the note above
     * tb_chain_params_json) nor in set_param, which is the same thread.
     * Published as an immutable snapshot and swapped atomically, exactly
     * like the preset list: the audio thread only ever takes a pointer. */
    struct SelSnapshot {
        std::vector<std::string> packs;    /* "Pack" prefixes, [0] = All    */
        std::vector<std::string> paths;    /* the whole scan, by index      */
        std::vector<int>         filtered; /* indices into paths            */
        std::string chain_params;          /* static head + dynamic options */
        std::string pack_list;             /* wt_pack_list JSON             */
        std::string path_list;             /* wt_paths JSON, index-aligned
                                            * with the wtN_select options  */
    };
    std::shared_ptr<const SelSnapshot> sel { std::make_shared<SelSnapshot>() };
    std::atomic<int> wt_pack { 0 };

    std::shared_ptr<const SelSnapshot> selection() const
    { return std::atomic_load(&sel); }

    /* false until the worker has scanned, loaded presets and published the
     * Init table; the audio path renders silence in the meantime */
    std::atomic<bool> ready { false };

    /* A drawable digest of the table each oscillator actually has loaded, so
     * a UI can show the real waveform instead of a stand-in. Built on the
     * worker when a table lands and published as an immutable string; the
     * audio thread only ever hands out the pointer. */
    std::shared_ptr<const std::string> wt_shape[TB_WT_SLOTS];
    std::shared_ptr<const std::string> shape(int osc) const
    { return std::atomic_load(&wt_shape[osc & 1]); }

    float *params() { return engine.pots; }
};

/* User presets are plain files the user can copy, share and manage:
 *   /data/UserData/UserLibrary/Tablor Presets/<Name>.tblr
 * filename (minus extension) IS the preset name; content is one
 * TBLR1 state blob line. */
static constexpr const char *kPresetDir =
    "/data/UserData/UserLibrary/Tablor Presets";

static std::string sanitize_preset_name(const char *raw)
{
    std::string n;
    for (const char *s = raw; *s && n.size() < 40; s++) {
        char c = *s;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == ';' ||
            c == '=' || c == '\n' || c == '\r')
            c = ' ';
        n += c;
    }
    while (!n.empty() && (n.front() == ' ' || n.front() == '.')) n.erase(0, 1);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    if (n.empty()) n = "Untitled";
    return n;
}

static std::string preset_file_for(const std::string &name)
{
    return std::string(kPresetDir) + "/" + name + ".tblr";
}

/* A name nobody has yet: "Name", "Name 2", "Name 3", … */
static std::string unique_preset_name(const std::string &want)
{
    std::string name = want;
    for (int i = 2; i < 1000; i++) {
        struct stat st;
        if (::stat(preset_file_for(name).c_str(), &st) != 0)
            return name;
        name = want + " " + std::to_string(i);
    }
    return want;
}

static bool read_blob_file(const char *path, std::string &blob)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[8192];
    bool ok = false;
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        const char *b = line;
        const char *bar = strchr(line, '|');    /* legacy "Name|blob" lines */
        if (bar) b = bar + 1;
        if (!strncmp(b, "TBLR1;", 6) || !strncmp(b, "TBLR2;", 6)) { blob = b; ok = true; }
    }
    fclose(f);
    return ok;
}

static void write_preset_file(const std::string &path, const char *blob)
{
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fputs(blob, f);
        fputc('\n', f);
        fclose(f);
    }
}

/* ---- Schwung's own preset store -------------------------------------------
 *
 * Schwung keeps module presets at
 *   /data/UserData/schwung/presets/<module-id>/<name>.json
 * as {"name","module","version","state"} where `state` is the very blob this
 * module already answers for `state` (shadow_ui_presets.mjs). Its browser is
 * reached from the slot, works for every module, and auditions a preset as
 * you scroll -- so Tablor's own store is the odd one out, not the feature.
 *
 * Both halves run once on the worker at startup: the factory sounds are
 * seeded there so a fresh install has them, and any .tblr the user already
 * saved is copied across. The .tblr files are LEFT ALONE -- a migration that
 * deletes the only copy of someone's sound is not a migration -- and a stamp
 * file stops the copy repeating, so a preset deleted in Schwung stays
 * deleted instead of reappearing on the next boot.
 */
static constexpr const char *kSchwungPresetDir =
    "/data/UserData/schwung/presets/tablor";
static constexpr const char *kInstalledStamp =
    "/data/UserData/schwung/presets/tablor/.installed";

/* A NAME becomes both the display string and the file name, so it loses path
 * separators and control characters -- matching Schwung's own safeFileStem --
 * and loses quotes and backslashes too, since it is read back out with a
 * regex (/"name"\s*:\s*"([^"]+)"/) that neither escapes nor tolerates them. */
static std::string json_safe_name(const std::string &in)
{
    std::string o;
    for (char c : in) {
        if (c == '"' || c == '\\' || (unsigned char) c < 0x20) continue;
        if (c == '/') { o += '-'; continue; }
        o += c;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o.empty() ? std::string("Preset") : o;
}

/* A STATE BLOB is data and must survive intact: it carries absolute wavetable
 * paths, so a slash is content, not a separator. Escaping it as the name is
 * escaped turned "/data/UserData/..." into "-data-UserData-...", which loads
 * as a missing file -- every migrated preset would have fallen back to the
 * Init table, silently. Only the two characters JSON itself reserves are
 * touched, and they are escaped rather than dropped. */
static std::string json_escape(const std::string &in)
{
    std::string o;
    for (char c : in) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; continue; }
        if ((unsigned char) c < 0x20) continue;      /* no newlines in a blob */
        o += c;
    }
    return o;
}

/* Write one preset into Schwung's store. Returns false if it did not land --
 * the caller is about to delete the source file, so "probably worked" is not
 * good enough. */
static bool write_schwung_json(const std::string &path, const std::string &name,
                               const std::string &blob)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "{\"name\":\"%s\",\"module\":\"tablor\",\"version\":1,\"state\":\"%s\"}\n",
            name.c_str(), json_escape(blob).c_str());
    const bool ok = (fflush(f) == 0);
    fclose(f);
    if (!ok) { ::remove(path.c_str()); return false; }
    struct stat st;                                   /* it exists, and it is not empty */
    if (::stat(path.c_str(), &st) != 0 || st.st_size < 16) return false;
    return true;
}

/* Seed one factory sound. Never overwrites: a factory preset the user edited
 * and re-saved under the same name is theirs now. */
static void write_schwung_preset(const std::string &name, const std::string &blob)
{
    const std::string safe = json_safe_name(name);
    const std::string path = std::string(kSchwungPresetDir) + "/" + safe + ".json";
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) return;
    write_schwung_json(path, safe, blob);
}

/* Does this .json already hold exactly this sound? Then the .tblr beside it
 * is a leftover from an earlier migration and can simply go. */
static bool schwung_json_holds(const std::string &path, const std::string &blob)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    std::string raw;
    char chunk[1024];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) raw.append(chunk, got);
    fclose(f);
    const std::string want = "\"state\":\"" + json_escape(blob) + "\"";
    return raw.find(want) != std::string::npos;
}

/* MOVE every .tblr into Schwung's store: convert, verify, then delete the
 * original. Deleting is what makes this self-completing -- an empty folder is
 * a finished migration, so nothing has to remember that it ran, and a preset
 * deleted in Schwung's browser has no .tblr left to come back from.
 *
 * The original is removed ONLY after the .json is on disk and non-empty. */
static void migrate_tblr_into_schwung()
{
    DIR *d = ::opendir(kPresetDir);
    if (!d) return;
    std::vector<std::string> files;
    while (dirent *e = ::readdir(d)) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && strcasecmp(dot, ".tblr") == 0) files.push_back(e->d_name);
    }
    ::closedir(d);

    for (const std::string &fn : files) {
        const std::string src = std::string(kPresetDir) + "/" + fn;
        std::string blob;
        if (!read_blob_file(src.c_str(), blob) || blob.empty()) continue;

        const std::string stem = fn.substr(0, fn.rfind('.'));
        const std::string safe = json_safe_name(stem);

        /* Already migrated under this name? Drop the leftover and move on. */
        std::string path = std::string(kSchwungPresetDir) + "/" + safe + ".json";
        struct stat st;
        if (::stat(path.c_str(), &st) == 0) {
            if (schwung_json_holds(path, blob)) { ::remove(src.c_str()); continue; }
            /* A DIFFERENT sound owns that name -- a factory seed, most
             * likely. Take the next free one, the way Schwung's own save
             * does, rather than overwriting either. */
            for (int n = 2; n < 100; n++) {
                char cand[512];
                snprintf(cand, sizeof cand, "%s/%s %d.json",
                         kSchwungPresetDir, safe.c_str(), n);
                if (::stat(cand, &st) != 0) { path = cand; break; }
            }
            char disp[256];
            snprintf(disp, sizeof disp, "%s", path.c_str());
            const char *slash = strrchr(disp, '/');
            std::string dispName = slash ? slash + 1 : disp;
            dispName = dispName.substr(0, dispName.rfind(".json"));
            if (write_schwung_json(path, dispName, blob)) ::remove(src.c_str());
            continue;
        }
        if (write_schwung_json(path, safe, blob)) ::remove(src.c_str());
    }
}

/* Seed the factory sounds and move any .tblr across -- ON UPDATE, once.
 *
 * The stamp holds the module version that last ran this. A fresh install has
 * no stamp; an update changes it; every ordinary boot in between matches and
 * returns immediately, which is the point -- there is nothing to do on a
 * device that has already been converted, so it should not go looking.
 *
 * Version-keyed rather than a plain "done" flag so a release that adds a
 * factory sound still delivers it, and so a user who somehow still has a
 * .tblr gets it moved at the next update rather than never.
 */
static void export_presets_to_schwung(tablor_instance *inst)
{
    ::mkdir("/data/UserData/schwung/presets", 0755);
    ::mkdir(kSchwungPresetDir, 0755);

    char stamp[64] = {};
    FILE *f = fopen(kInstalledStamp, "r");
    if (f) { if (!fgets(stamp, sizeof stamp, f)) stamp[0] = 0; fclose(f); }
    for (char *c = stamp; *c; c++) if (*c == '\n') { *c = 0; break; }
    if (strcmp(stamp, TB_VERSION) == 0) return;      /* already done for this build */

    /* Factory sounds. Never overwrites: one you edited and re-saved under the
     * same name is yours now, and one you deleted stays deleted until a
     * release that changes this version brings it back. */
    auto ps = inst->presetList();
    const int nf = ps->factory_count;
    for (int i = 0; i < nf && i < (int) ps->items.size(); i++) {
        if (ps->items[(size_t) i].name == "Init") continue;   /* every module has one */
        write_schwung_preset(ps->items[(size_t) i].name, ps->items[(size_t) i].blob);
    }

    migrate_tblr_into_schwung();

    f = fopen(kInstalledStamp, "w");
    if (f) { fprintf(f, "%s\n", TB_VERSION); fclose(f); }
}

/* One-time migration of the old fixed-slot files (presets/user/uN.tbl). */
static void migrate_old_user_slots(tablor_instance *inst)
{
    for (int i = 0; i < 8; i++) {
        char old[600];
        snprintf(old, sizeof old, "%s/presets/user/u%d.tbl",
                 inst->module_dir, i + 1);
        FILE *f = fopen(old, "r");
        if (!f) continue;
        char line[8192];
        std::string name = "User " + std::to_string(i + 1), blob;
        if (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char *bar = strchr(line, '|');
            const char *b = line;
            if (bar) { *bar = 0; if (line[0]) name = line; b = bar + 1; }
            if (!strncmp(b, "TBLR1;", 6) || !strncmp(b, "TBLR2;", 6)) blob = b;
        }
        fclose(f);
        if (!blob.empty() && blob != "TBLR1;" && blob != "TBLR2;")
            write_preset_file(preset_file_for(unique_preset_name(
                sanitize_preset_name(name.c_str()))), blob.c_str());
        ::remove(old);
    }
}

/* Re-list the user preset folder (sorted by name, case-insensitive).
 * WORKER THREAD ONLY: does directory I/O, then publishes a new snapshot. */
static void scan_user_presets(tablor_instance *inst)
{
    auto cur = inst->presetList();
    auto next = std::make_shared<tablor_instance::PresetList>();
    next->factory_count = cur->factory_count;
    next->items.assign(cur->items.begin(),
                       cur->items.begin() + cur->factory_count);

    std::vector<tablor_instance::preset> user;
    DIR *d = ::opendir(kPresetDir);
    if (d) {
        while (dirent *e = ::readdir(d)) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcasecmp(dot, ".tblr") != 0) continue;
            std::string path = std::string(kPresetDir) + "/" + e->d_name;
            std::string blob;
            if (!read_blob_file(path.c_str(), blob)) continue;
            user.push_back({ std::string(e->d_name, (size_t) (dot - e->d_name)),
                             blob, path });
        }
        ::closedir(d);
    }
    std::sort(user.begin(), user.end(),
              [](const tablor_instance::preset &a,
                 const tablor_instance::preset &b) {
                  return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
              });
    for (auto &u : user)
        next->items.push_back(std::move(u));

    if (inst->preset_index.load() >= (int) next->items.size())
        inst->preset_index = 0;
    std::atomic_store(&inst->presets, std::shared_ptr<const tablor_instance::PresetList>(next));
}

static void select_preset_by_path(tablor_instance *inst, const std::string &path)
{
    auto ps = inst->presetList();
    for (size_t i = 0; i < ps->items.size(); i++)
        if (ps->items[i].path == path) {
            inst->preset_index = (int) i;
            return;
        }
}

/* WORKER THREAD ONLY. */
static void load_presets(tablor_instance *inst)
{
    auto boot = std::make_shared<tablor_instance::PresetList>();
    char path[600];
    snprintf(path, sizeof path, "%s/presets/factory.tbl", inst->module_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        char line[4096];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            char *bar = strchr(line, '|');
            if (!bar) continue;
            *bar = 0;
            char *blob = bar + 1;
            blob[strcspn(blob, "\r\n")] = 0;
            boot->items.push_back({ line, blob, "" });
        }
        fclose(f);
    }
    boot->factory_count = (int) boot->items.size();
    std::atomic_store(&inst->presets, std::shared_ptr<const tablor_instance::PresetList>(boot));

    ::mkdir(kPresetDir, 0755);
    migrate_old_user_slots(inst);
    scan_user_presets(inst);
}

static int param_index(const char *key)
{
    for (int i = 0; i < TB_PARAM_COUNT; i++)
        if (!strcmp(tb_params[i].key, key))
            return i;
    return -1;
}

/* Which wavetable slot a param drives: wt1_table -> 0, wt2_table -> 1. */
static int tb_path_slot(int param_idx)
{
    return param_idx == TB_P_WT1_TABLE ? 0 : 1;
}

/* Store a path and post the background load. "" or "Init" = built-in. */
/* The scanner list IS the enum: index 0 = "Init" (built-in), then every
 * file found. Keep the param's index and the loaded path in step. */
static void sync_table_index(tablor_instance *inst, int osc)
{
    auto snap = inst->selection();
    const auto &l = snap->paths;
    int idx = 0;
    for (size_t i = 0; i < l.size(); i++)
        if (l[i] == inst->wt_path[osc]) { idx = (int) i; break; }
    inst->params()[osc == 0 ? TB_P_WT1_TABLE : TB_P_WT2_TABLE] = (float) idx;
}

static void set_table_path(tablor_instance *inst, int osc, const char *path)
{
    if (!strcmp(path, "Init")) path = "";
#ifdef MPC_PORT   /* the factory presets name Move's library; on the MPC the same packs are in <module>/wavetables */
    static const char kMoveWt[] = "/data/UserData/UserLibrary/Wavetables/";
    char mapped[600];
    if (!strncmp(path, kMoveWt, sizeof kMoveWt - 1)) {
        snprintf(mapped, sizeof mapped, "%s/wavetables/%s", inst->module_dir, path + sizeof kMoveWt - 1);
        path = mapped;
    }
#endif
    snprintf(inst->wt_path[osc], sizeof inst->wt_path[osc], "%s", path);

    tb::WtEntry e;
    e.path = path;
    if (const char *dot = strrchr(path, '.'); dot && !strncasecmp(dot, ".wt", 3)) {
        int fs = atoi(dot + 3);
        /* 32, not 256: the same floor wtBuild accepts. A .wt64 was rejected
         * here, fell through to the WAV reader, failed to parse as WAV and
         * loaded nothing at all. An unusable number leaves the size at 0 and
         * the loader infers it -- the file is still FLAC either way. */
        e.flac = true;
        if (fs >= 32 && fs <= 4096 && (fs & (fs - 1)) == 0)
            e.flacFrameSize = fs;
    }
    inst->loader.requestLoad(osc, e);
    sync_table_index(inst, osc);
}

/* ------------------------------------------------------------------ */
/* Pack list + per-pack enum options                                    */
/*                                                                      */
/* WtEntry.name is already "Pack/Name" or "Name", so the pack list costs */
/* no extra filesystem work -- it is a pass over a list we already hold. */
/* ------------------------------------------------------------------ */

static std::string tb_pack_of(const std::string &name)
{
    size_t slash = name.find('/');
    return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

static std::string tb_leaf_of(const std::string &name)
{
    size_t slash = name.rfind('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

/* JSON-escape into a std::string. The option names are filenames, so a
 * quote or a backslash is unlikely but entirely legal on disk -- and one
 * unescaped byte turns the whole contract into a failed read. */
static void tb_json_append(std::string &out, const std::string &in)
{
    for (char c : in) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((unsigned char) c < 0x20) continue;
        else out += c;
    }
}

/* Everything below runs ON THE WORKER: it walks the scan, copies it and
 * formats ~24 KB of JSON. The audio thread only ever reads the finished
 * snapshot through inst->selection(). */

/* The host's chain-host parser caps an enum at MAX_ENUM_OPTIONS (128).
 * Past that the knob grid still lists them (the JS parses the JSON
 * itself) but CC knob mapping and modulation targets truncate, which
 * fails silently -- so stop at the cap rather than publish a list only
 * half of the host can see. */
static const size_t TB_MAX_ENUM_OPTIONS = 128;

/* Rebuild the whole selection contract off the current scan and pack.
 * Worker thread only. Publishes atomically when done. */
static void tb_publish_selection(tablor_instance *inst)
{
    auto snap = std::make_shared<tablor_instance::SelSnapshot>();
    const auto &l = inst->scanner.list();

    /* the scan, copied — so the audio thread never reads the scanner while
     * a rescan is mutating it */
    snap->paths.reserve(l.size());
    for (const auto &e : l)
        snap->paths.push_back(e.path);

    /* packs: the distinct "Pack/" prefixes, All first */
    snap->packs.push_back("All");
    for (const auto &e : l) {
        std::string pk = tb_pack_of(e.name);
        if (pk.empty()) continue;
        bool seen = false;
        for (size_t i = 1; i < snap->packs.size(); i++)
            if (snap->packs[i] == pk) { seen = true; break; }
        if (!seen) snap->packs.push_back(pk);
    }

    int pack = inst->wt_pack.load(std::memory_order_relaxed);
    if (pack < 0 || pack >= (int) snap->packs.size()) {
        pack = 0;
        inst->wt_pack.store(0, std::memory_order_relaxed);
    }

    /* the entries this pack offers */
    const bool all = (pack <= 0);
    const std::string want = all ? std::string() : snap->packs[(size_t) pack];
    for (size_t i = 0; i < l.size(); i++) {
        if (i == 0) { snap->filtered.push_back(0); continue; }   /* Init */
        if (all || tb_pack_of(l[i].name) == want)
            snap->filtered.push_back((int) i);
        if (snap->filtered.size() >= TB_MAX_ENUM_OPTIONS) break;
    }

    /* wt_pack_list: the items-level rows */
    std::string &pl = snap->pack_list;
    pl = "[";
    for (size_t i = 0; i < snap->packs.size(); i++) {
        char head[32];
        snprintf(head, sizeof head, "%s{\"index\":%d,\"label\":\"",
                 i ? "," : "", (int) i);
        pl += head;
        tb_json_append(pl, snap->packs[i]);
        pl += "\"}";
    }
    pl += "]";

    /* wt_paths: the FILE for each wtN_select index. A browser cannot read a
     * value the device did not push, and nothing pushes when a pot changes a
     * table -- but the select INDEX does push, so with the paths in hand the
     * page can fetch and draw the file itself instead of waiting on us. */
    std::string &pl2 = snap->path_list;
    pl2 = "[";
    for (size_t i = 0; i < snap->filtered.size(); i++) {
        int g = snap->filtered[i];
        if (i) pl2 += ",";
        pl2 += "\"";
        if (g > 0 && g < (int) l.size()) tb_json_append(pl2, l[(size_t) g].path);
        pl2 += "\"";
    }
    pl2 += "]";

    /* chain_params: the static head plus the three dynamic entries */
    std::string &out = snap->chain_params;
    out.assign(tb_chain_params_json);
    if (out.size() < 2 || out.back() != ']')
        return;                              /* not the shape we expect */
    out.pop_back();                          /* drop the closing ] */

    out += ",{\"key\":\"wt_pack\",\"name\":\"WT Pack\",\"type\":\"enum\",\"options\":[";
    for (size_t i = 0; i < snap->packs.size(); i++) {
        if (i) out += ",";
        out += "\""; tb_json_append(out, snap->packs[i]); out += "\"";
    }
    out += "]}";

    for (int osc = 0; osc < 2; osc++) {
        char head[96];
        snprintf(head, sizeof head,
                 ",{\"key\":\"wt%d_select\",\"name\":\"WT%d Table\",\"type\":\"enum\",\"options\":[",
                 osc + 1, osc + 1);
        out += head;
        for (size_t i = 0; i < snap->filtered.size(); i++) {
            int g = snap->filtered[i];
            if (i) out += ",";
            out += "\"";
            tb_json_append(out, g == 0 ? std::string("Init")
                                       : tb_leaf_of(l[(size_t) g].name));
            out += "\"";
        }
        out += "]}";
    }

    /* The shape digests are DECLARED here so they reach a browser at all:
     * schwung-manager only streams values for keys chain_params declares
     * (its standalone getParam answers from that cache and never asks the
     * device), so an undeclared key reads as undefined forever.
     *
     * They are STRINGS -- a shape digest reads "24,64,Wjvx..." and wt_paths
     * is a path list -- and they are READ-ONLY, so that is what they now say.
     * They were declared as an int with min==max==0, which was an invented
     * way to mean "no knob can turn this" from before `access` existed. It
     * described them wrongly twice over: not integers, and an empty numeric
     * range is a defect in anything that really is a number, which is what
     * Schwung's contract checker correctly flagged (issue #2).
     *
     * They remain on no ui_hierarchy page deliberately: they are a data
     * channel for the web panel, not a control. */
    out += ",{\"key\":\"wt1_shape\",\"name\":\"WT1 Shape\",\"type\":\"string\",\"access\":\"read\"}"
           ",{\"key\":\"wt2_shape\",\"name\":\"WT2 Shape\",\"type\":\"string\",\"access\":\"read\"}"
           ",{\"key\":\"wt_paths\",\"name\":\"WT Paths\",\"type\":\"string\",\"access\":\"read\"}";
    out += "]";

    std::atomic_store(&inst->sel,
                      std::shared_ptr<const tablor_instance::SelSnapshot>(snap));
}

/* Where the currently loaded table sits in the filtered list, or 0. */
static int tb_select_index(const tablor_instance::SelSnapshot &snap,
                           const char *path)
{
    for (size_t i = 0; i < snap.filtered.size(); i++) {
        size_t g = (size_t) snap.filtered[i];
        if (g < snap.paths.size() && snap.paths[g] == path)
            return (int) i;
    }
    return 0;
}

/* ---- drawable digest of a loaded wavetable ------------------------------
 * "<frames>,<samples>,<data>" with one character per sample: the amplitude
 * quantised to 64 levels over [-1,1] in a base64-ish alphabet. 24 frames of
 * 64 samples is ~1.5 KB -- enough to draw a waterfall, small enough to sit
 * in a param string, and cheap enough to build on the worker while the table
 * is already in cache. WORKER THREAD ONLY. */
static const char kQ[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";

static std::string tb_wt_digest(const tb::Wavetable &wt)
{
    const int nf = wt.size();
    if (nf < 1) return std::string();
    const int OF = nf < 24 ? nf : 24;      /* frames we draw   */
    const int OS = 64;                     /* samples per frame */

    std::string out;
    char head[32];
    snprintf(head, sizeof head, "%d,%d,", OF, OS);
    out.reserve((size_t) OF * OS + 16);
    out.assign(head);

    for (int f = 0; f < OF; f++) {
        int src = (OF == 1) ? 0 : (int) ((long) f * (nf - 1) / (OF - 1));
        const tb::FrameTable *ft = wt.frame(src);
        if (!ft || ft->levels.empty()) { out.append(OS, kQ[32]); continue; }
        const tb::MipLevel &lv = ft->levels[0];   /* full band level */
        if (lv.size < 1 || lv.data.empty()) { out.append(OS, kQ[32]); continue; }
        for (int i = 0; i < OS; i++) {
            int j = (int) ((long) i * lv.size / OS);
            if (j >= (int) lv.data.size()) j = (int) lv.data.size() - 1;
            float v = lv.data[(size_t) j];
            if (v < -1.0f) v = -1.0f;
            if (v >  1.0f) v =  1.0f;
            int q = (int) ((v + 1.0f) * 0.5f * 63.0f + 0.5f);
            if (q < 0) q = 0;
            if (q > 63) q = 63;
            out += kQ[q];
        }
    }
    return out;
}

static void tb_publish_shape(tablor_instance *inst, int osc, const tb::Wavetable &wt)
{
    auto s = std::make_shared<const std::string>(tb_wt_digest(wt));
    std::atomic_store(&inst->wt_shape[osc & 1], s);
}

/* Select by index into the published snapshot. */
static void set_table_index(tablor_instance *inst, int osc, int idx)
{
    auto snap = inst->selection();
    if (idx < 0 || idx >= (int) snap->paths.size()) return;
    set_table_path(inst, osc, snap->paths[(size_t) idx].c_str());
}

static float clamp_param(const tb_param_t *p, float v)
{
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    return v;
}

/* Serialize the current sound into inst->state_buf (TBLR1 blob). */
static void build_state_blob(tablor_instance *inst)
{
    /* TBLR2, not TBLR1: v1.0.5 reordered the LFO shape options (the host
     * draws the glyph from the index), so the same lfoN_shape number means a
     * different waveform on either side of that release. The tag is how a
     * loader can tell which order a blob was saved under. */
    int o = snprintf(inst->state_buf, sizeof inst->state_buf, "TBLR2;");
    for (int i = 0; i < TB_PARAM_COUNT; i++) {
        if (tb_params[i].type == TB_PATH) {   /* wavetable: store the path */
            const char *path = inst->wt_path[tb_path_slot(i)];
            if (path[0])
                o += snprintf(inst->state_buf + o,
                              sizeof inst->state_buf - (size_t) o,
                              "%s=%s;", tb_params[i].key, path);
            continue;
        }
        float v = inst->params()[i];
        if (v == tb_params[i].def) continue;            /* defaults are implicit */
        o += snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o,
                      "%s=%g;", tb_params[i].key, (double) v);
        if (o >= (int) sizeof inst->state_buf - 64) break;
    }
#ifdef MPC_PORT   /* the preset chosen, so a reopened project doesn't take MPC's PRESET value as a new choice */
    int pi = inst->preset_index.load(std::memory_order_relaxed);
    if (pi > 0 && o < (int) sizeof inst->state_buf - 32)
        snprintf(inst->state_buf + o, sizeof inst->state_buf - (size_t) o, "mpc_preset=%d;", pi);
#endif
}

static void reset_to_defaults(tablor_instance *inst)
{
    for (int i = 0; i < TB_PARAM_COUNT; i++)
        if (tb_params[i].type != TB_PATH)
            inst->params()[i] = tb_params[i].def;
    set_table_path(inst, 0, "");
    set_table_path(inst, 1, "");
}

/* Version-tagged blob: "TBLR1;…" or "TBLR2;…". Anything without a known
 * tag is from another life — ignore it entirely rather than half-apply
 * it (the ER-99 total-silence lesson). */
static void apply_state_blob(tablor_instance *inst, const char *val)
{
    bool v1 = strncmp(val, "TBLR1;", 6) == 0;
    if (!v1 && strncmp(val, "TBLR2;", 6) != 0) return;
    const char *p = val + 6;
    char kbuf[64], vbuf[512];
    while (*p) {
        const char *eq = strchr(p, '=');
        if (!eq) break;
        const char *semi = strchr(eq, ';');
        if (!semi) semi = eq + strlen(eq);
        size_t kl = (size_t) (eq - p), vl = (size_t) (semi - eq - 1);
        if (kl < sizeof kbuf && vl < sizeof vbuf) {
            memcpy(kbuf, p, kl); kbuf[kl] = 0;
            memcpy(vbuf, eq + 1, vl); vbuf[vl] = 0;
#ifdef MPC_PORT
            if (!strcmp(kbuf, "mpc_preset")) inst->preset_index.store(atoi(vbuf), std::memory_order_relaxed);
#endif
            int idx = param_index(kbuf);
            if (idx >= 0) {
                if (tb_params[idx].type == TB_PATH)
                    set_table_path(inst, tb_path_slot(idx), vbuf);
                else
                    inst->params()[idx] = clamp_param(&tb_params[idx],
                                                      strtof(vbuf, nullptr));
            }
        }
        p = (*semi) ? semi + 1 : semi;
    }
}

/* ------------------------------------------------------------------ */
/* User macros: u{i} pots write through to their selected target;      */
/* picking a new target back-syncs the pot so nothing jumps.           */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* set / get                                                           */
/* ------------------------------------------------------------------ */

static void tb_set_param(void *instance, const char *key, const char *val)
{
    auto *inst = (tablor_instance *) instance;
    if (!inst || !key || !val) return;

    /* state restore: host sends "<prefix>:state" resolved to "state" upstream
     * or the raw prefixed key — accept both spellings. */
    const char *k = key;
    const char *colon = strrchr(key, ':');
    if (colon) k = colon + 1;

    if (!strcmp(k, "state")) {
        apply_state_blob(inst, val);
        return;
    }
#ifdef MPC_PORT   /* the PRESET menu: preset n = the defaults, then its state blob (what Move's preset browser does) */
    if (!strcmp(k, "preset")) {
        auto ps = inst->presetList();
        if (ps->items.empty()) return;
        int n = atoi(val);
        if (n < 0) n = 0;
        if (n >= (int) ps->items.size()) n = (int) ps->items.size() - 1;
        /* the preset already chosen: nothing to do, so a project that restores every parameter after its chunk (as
         * MPC does) keeps its edits instead of re-applying the preset over them (as Noisemaker's) */
        if (n == inst->preset_index.load(std::memory_order_relaxed)) return;
        reset_to_defaults(inst);
        apply_state_blob(inst, ps->items[n].blob.c_str());
        inst->preset_index.store(n, std::memory_order_relaxed);
        return;
    }
#endif

    /* Turnable wavetable selection. Indices only: get_param reports an
     * index for these, so the host learns WIRE_INDEX and never sends a
     * name. Nothing here touches the filesystem. */
    if (!strcmp(k, "wt_pack")) {
        auto snap = inst->selection();
        int n = atoi(val);
        if (n < 0) n = 0;
        if (n >= (int) snap->packs.size()) n = (int) snap->packs.size() - 1;
        if (n == inst->wt_pack.load(std::memory_order_relaxed)) return;
        inst->wt_pack.store(n, std::memory_order_relaxed);
        /* Republish: wt1_select / wt2_select now have different options.
         * The host re-reads the contract after an items selection settles
         * (page_controller commitItem -> armContractSettle), which is why
         * the pack is offered as an items level and not as a plain enum
         * knob -- a plain enum commit does NOT arm the re-read.
         *
         * The rebuild itself is ~24 KB of formatting: it goes to the worker,
         * because THIS is the SPI callback. The host waits 500 ms and then
         * wants two agreeing reads (CONTRACT_SETTLE_MS), and is_loading below
         * holds it off meanwhile, so the snapshot is always long since
         * published by the time anyone reads it. */
        inst->loader.post([inst] { tb_publish_selection(inst); });
        return;
    }
    if (!strcmp(k, "wt1_select") || !strcmp(k, "wt2_select")) {
        int osc = (k[2] == '2') ? 1 : 0;
        auto snap = inst->selection();
        int n = atoi(val);
        if (n < 0 || n >= (int) snap->filtered.size()) return;
        set_table_index(inst, osc, snap->filtered[(size_t) n]);
        return;
    }

    int idx = param_index(k);
    if (idx < 0) return;
    const tb_param_t *p = &tb_params[idx];

    if (p->type == TB_PATH) {
        set_table_path(inst, tb_path_slot(idx), val);
        return;
    }

    float v;
    if (p->type == TB_ENUM) {
        /* Accept an option NAME or an integer index — never trust one
         * spelling (the Forge atoi-collapse gotcha). */
        v = -1;
        for (int i = 0; i < p->n_options; i++)
            if (!strcmp(p->options[i], val)) { v = (float) i; break; }
        if (v < 0) {                            /* not a name — try a number */
            char *end = nullptr;
            float n = strtof(val, &end);
            if (end == val) return;             /* unknown name: ignore */
            v = (float) (int) n;
        }
    } else {
        v = strtof(val, nullptr);
    }

    inst->params()[idx] = clamp_param(p, v);
}

static int write_str(char *buf, int buf_len, const char *s)
{
    int n = (int) strlen(s);
    if (n >= buf_len) n = buf_len - 1;
    memcpy(buf, s, (size_t) n);
    buf[n] = 0;
    return n;
}

static int tb_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    auto *inst = (tablor_instance *) instance;
    if (!inst || !key || !buf || buf_len <= 1) return -1;

    /* Static string: a copy, nothing more. get_param is serviced from the
     * SPI callback (chain_internal.h) — anything that formats or reads the
     * filesystem here jams the param bus. */
    if (!strcmp(key, "chain_params")) {
        auto snap = inst->selection();
        return write_str(buf, buf_len,
                         snap->chain_params.empty() ? tb_chain_params_json
                                                    : snap->chain_params.c_str());
    }

    /* Turnable wavetable selection. All three are answered from memory --
     * no scan, no allocation beyond the small formatting buffer. */
    if (!strcmp(key, "wt_pack")) {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%d", inst->wt_pack.load(std::memory_order_relaxed));
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "wt_paths")) {
        auto snap = inst->selection();
        return write_str(buf, buf_len,
                         snap->path_list.empty() ? "[]" : snap->path_list.c_str());
    }
    if (!strcmp(key, "wt_pack_list")) {
        auto snap = inst->selection();
        return write_str(buf, buf_len,
                         snap->pack_list.empty() ? "[]" : snap->pack_list.c_str());
    }
#ifdef MPC_PORT   /* the PRESET menu: current preset, its name, the count, and any preset's name */
    if (!strcmp(key, "preset") || !strcmp(key, "preset_count")) {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%d", key[6] ? (int) inst->presetList()->items.size()
                                                : inst->preset_index.load(std::memory_order_relaxed));
        return write_str(buf, buf_len, tmp);
    }
    if (!strcmp(key, "preset_name") || !strncmp(key, "preset_name_at:", 15)) {
        auto ps = inst->presetList();
        int n = key[11] ? atoi(key + 15) : inst->preset_index.load(std::memory_order_relaxed);
        if (n < 0 || n >= (int) ps->items.size()) return write_str(buf, buf_len, "");
        return write_str(buf, buf_len, ps->items[n].name.c_str());
    }
#endif
    if (!strcmp(key, "wt1_name") || !strcmp(key, "wt2_name")) {   /* MPC port: the loaded table's name, for a readout */
        const std::string &path = inst->wt_path[key[2] == '2' ? 1 : 0];
        if (path.empty()) return write_str(buf, buf_len, "Init");
        std::string leaf = path.substr(path.find_last_of('/') + 1);
        size_t dot = leaf.find_last_of('.');
        if (dot != std::string::npos && dot > 0) leaf.resize(dot);
        return write_str(buf, buf_len, leaf.c_str());
    }
    if (!strcmp(key, "wt1_select") || !strcmp(key, "wt2_select")) {
        auto snap = inst->selection();
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%d",
                 tb_select_index(*snap, inst->wt_path[key[2] == '2' ? 1 : 0]));
        return write_str(buf, buf_len, tmp);
    }

    /* The table each oscillator has loaded, drawable: "frames,samples,data".
     * Served from a string the worker built when the table landed -- no walk
     * of the wavetable here, this is the SPI callback. */
    if (!strcmp(key, "wt1_shape") || !strcmp(key, "wt2_shape")) {
        auto sp = inst->shape(key[2] == '2' ? 1 : 0);
        return write_str(buf, buf_len, sp ? sp->c_str() : "");
    }

    /* Triggers read as their idle option, so the host learns the NAME wire
     * and a page shows a quiet "-" rather than junk. */
    /* The host asks this while a contract re-read is pending and holds off
     * for as long as it says "1" (page_controller isLoadingSays). Ours is
     * true while the worker still owes a scan, a table or a republish. */
    if (!strcmp(key, "is_loading"))
        return write_str(buf, buf_len,
                         (!inst->ready.load(std::memory_order_relaxed) ||
                          inst->loader.busy()) ? "1" : "0");

    /* Schwung 0.12+: the stock hierarchy editor (viz graphics, preset
     * browser, file browser, keyboard) IS Tablor's on-device UI. */
    if (!strcmp(key, "ui_hierarchy"))
        return write_str(buf, buf_len, tb_ui_hierarchy_json);

    /* Shadow UI preset browser convention: preset_count / preset /
     * preset_name (the "No presets" screen reads exactly these). */
    /* "1" while the worker still owes us file work (used by tests and by
     * anything that wants to read back a save it just asked for) */
    if (!strcmp(key, "busy"))
        return write_str(buf, buf_len, inst->loader.busy() ? "1" : "0");

    if (!strcmp(key, "ready"))
        return write_str(buf, buf_len,
                         inst->ready.load(std::memory_order_relaxed) ? "1" : "0");

    if (!strcmp(key, "state")) {
        build_state_blob(inst);
        return write_str(buf, buf_len, inst->state_buf);
    }

    int idx = param_index(key);
    if (idx < 0) return -1;
    const tb_param_t *p = &tb_params[idx];

    if (p->type == TB_PATH)
        return write_str(buf, buf_len, inst->wt_path[tb_path_slot(idx)]);

    if (p->type == TB_ENUM) {
        const char *name = p->options[(int) inst->params()[idx]];
        return write_str(buf, buf_len, name ? name : "?");
    }
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%g", (double) inst->params()[idx]);
    return write_str(buf, buf_len, tmp);
}

static int tb_get_error(void *, char *, int) { return 0; }

/* ------------------------------------------------------------------ */
/* MIDI + audio                                                        */
/* ------------------------------------------------------------------ */

static void tb_on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    auto *inst = (tablor_instance *) instance;
    if (!inst || !msg) return;
    inst->engine.onMidi(msg, len);
}

static void tb_render_block(void *instance, int16_t *out_lr, int frames)
{
    auto *inst = (tablor_instance *) instance;
    if (!inst || !inst->ready.load(std::memory_order_relaxed)) {
        memset(out_lr, 0, (size_t) frames * 2 * sizeof(int16_t));
        return;
    }
    if (g_host && g_host->get_bpm)
        inst->engine.setHostBpm(g_host->get_bpm());
    inst->engine.renderBlock(out_lr, frames);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void *tb_create_instance(const char *module_dir, const char *json_defaults)
{
    (void) json_defaults;
    auto *inst = new (std::nothrow) tablor_instance();
    if (!inst) return nullptr;

    if (module_dir)
        snprintf(inst->module_dir, sizeof inst->module_dir, "%s", module_dir);

    /* create_instance runs on the SPI callback too (MODULES.md, "Threading:
     * there is no control thread"). Everything below is file I/O, a directory
     * walk, a ~20 MB first-run copy and an FFT — none of which may happen
     * here. Start the worker and let it do all of it; the engine renders
     * silence until the Init table is published. */
#ifdef MPC_PORT   /* the presets now, not on the worker: MPC reads the PRESET menu's size when it inserts the plugin
                     (a ~2 KB file; the worker's own load_presets below just reads it again) */
    load_presets(inst);
#endif
    inst->loader.start();
    inst->loader.post([inst] {
#ifndef MPC_PORT   /* MPC port: no first-run copy of the factory packs into a user folder */
        tb::WtScanner::seedUserFolder(inst->module_dir);   /* first-run copy */
#endif
        inst->scanner.scan(inst->module_dir);              /* opendir walk   */
        /* Derive the pack list and the published option lists ONCE, off the
         * single scan — so get_param never has to. */
        tb_publish_selection(inst);
        load_presets(inst);                                /* fopen + scan   */
        export_presets_to_schwung(inst);   /* into Schwung's own preset store */
        /* AGAIN, now that the presets exist. The first call publishes the
         * wavetable lists, but the PRESET cell's options ARE the preset list,
         * and load_presets runs after it -- so a single early call left that
         * enum with no options at all: the cell read 0 and stepping it found
         * nothing. */
        tb_publish_selection(inst);
        /* The digest is built from the real samples, on this thread, as each
         * table lands -- including these built-in ones. */
        inst->loader.onTable = [inst](int osc, const tb::Wavetable &wt) {
            tb_publish_shape(inst, osc, wt);
        };
        auto init0 = tb::makeInitTable();                  /* FFT + ~13 MB */
        tb_publish_shape(inst, 0, *init0);
        tb_publish_shape(inst, 1, *init0);
        inst->engine.setTable(0, init0);
        inst->engine.setTable(1, tb::makeInitTable());
        /* a state restore may have landed while we were scanning: re-apply
         * whatever paths it stored, now that the list and loader are live */
        for (int osc = 0; osc < TB_WT_SLOTS; osc++)
            if (inst->wt_path[osc][0]) {
                char keep[512];
                snprintf(keep, sizeof keep, "%s", inst->wt_path[osc]);
                set_table_path(inst, osc, keep);
            }
        inst->ready = true;
        if (g_host && g_host->log) {
            char msg[160];
            snprintf(msg, sizeof msg,
                     "tablor: v" TB_VERSION " ready, %d wavetables, %d packs, %d presets",
                     (int) inst->scanner.list().size(),
                     (int) inst->selection()->packs.size(),
                     (int) inst->presetList()->items.size());
            g_host->log(msg);
        }
    });
    return inst;
}

static void tb_destroy_instance(void *instance)
{
#ifdef MPC_PORT   /* join the loader before the members it publishes into go (see WtLoader::stop) */
    if (instance) ((tablor_instance *) instance)->loader.stop();
#endif
    delete (tablor_instance *) instance;
}

static plugin_api_v2_t g_api = {
    /* .api_version      = */ 2,
    /* .create_instance  = */ tb_create_instance,
    /* .destroy_instance = */ tb_destroy_instance,
    /* .on_midi          = */ tb_on_midi,
    /* .set_param        = */ tb_set_param,
    /* .get_param        = */ tb_get_param,
    /* .get_error        = */ tb_get_error,
    /* .render_block     = */ tb_render_block,
};

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
