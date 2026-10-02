/* Wavetable scanner — builds the name-indexed table list that becomes the
 * WT1/WT2 enum options. Scans directory LISTINGS only (names, no audio
 * decode), so it is cheap enough to run at create_instance and on rescan.
 *
 * Sources, in order:
 *   0            "Init"  (the built-in table, always present)
 *   module dir   <module>/wavetables/** (factory packs, .wt2048 FLAC + .wav)
 *   user dir     /data/UserData/UserLibrary/Wavetables/** (.wav; what the
 *                user uploads via Move Manager / schwung-manager)
 */
#pragma once

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace tb {

struct WtEntry {
    std::string name;     /* shown in the enum list: "Pack/Name" or "Name" */
    std::string path;     /* absolute; empty for the built-in Init         */
    int flacFrameSize = 0;/* >0: frame size named by the .wtNNNN extension  */
    bool flac = false;    /* a .wt* file: FLAC, whatever the number says    */
};

class WtScanner {
public:
#ifdef MPC_PORT   /* MPC port: user wavetables live next to the plugin, not in the Move's user library */
    static constexpr const char *kUserDir = "/sdcard/vst/tablor/wavetables";   /* the shipped packs; add yours in a subfolder */
#else
    static constexpr const char *kUserDir = "/data/UserData/UserLibrary/Wavetables";
#endif

    /* One-time: copy the module's factory packs into the USER folder, so the
     * user sees and manages everything in one place (Move Manager /
     * schwung-manager both browse it). A marker file means "already seeded":
     * a user who deletes a factory pack stays rid of it on reinstall. */
    static void seedUserFolder(const std::string &moduleDir)
    {
        ::mkdir(kUserDir, 0755);
        std::string marker = std::string(kUserDir) + "/.tablor-seeded";
        struct stat st;
        if (::stat(marker.c_str(), &st) == 0) return;
        if (moduleDir.empty()) return;

        copyTree(moduleDir + "/wavetables", kUserDir);

        FILE *f = ::fopen(marker.c_str(), "w");
        if (f) { ::fputs("v1\n", f); ::fclose(f); }
    }

    void scan()
    {
        entries.clear();
        entries.push_back({ "Init", "", 0 });

        ::mkdir(kUserDir, 0755);
        scanDir(kUserDir, "", 0);

        /* keep Init first, sort the rest by name */
        std::sort(entries.begin() + 1, entries.end(),
                  [](const WtEntry &a, const WtEntry &b) { return a.name < b.name; });
    }

    const std::vector<WtEntry> &list() const { return entries; }

    int indexOfName(const char *name) const
    {
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].name == name)
                return (int) i;
        return -1;
    }

private:
    static void copyTree(const std::string &from, const std::string &to)
    {
        DIR *d = ::opendir(from.c_str());
        if (!d) return;
        while (dirent *e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string src = from + "/" + e->d_name;
            std::string dst = to + "/" + e->d_name;
            struct stat st;
            if (::stat(src.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                ::mkdir(dst.c_str(), 0755);
                copyTree(src, dst);
            } else if (::stat(dst.c_str(), &st) != 0) {   /* don't overwrite */
                FILE *in = ::fopen(src.c_str(), "rb");
                if (!in) continue;
                FILE *out = ::fopen(dst.c_str(), "wb");
                if (out) {
                    char buf[64 * 1024];
                    size_t n;
                    while ((n = ::fread(buf, 1, sizeof buf, in)) > 0)
                        ::fwrite(buf, 1, n, out);
                    ::fclose(out);
                }
                ::fclose(in);
            }
        }
        ::closedir(d);
    }

    void scanDir(const std::string &dir, const std::string &prefix, int depth)
    {
        if (depth > 3) return;                     /* sanity bound */
        DIR *d = ::opendir(dir.c_str());
        if (!d) return;

        while (dirent *e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            std::string full = dir + "/" + e->d_name;

            struct stat st;
            if (::stat(full.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                scanDir(full, prefix.empty() ? std::string(e->d_name)
                                             : prefix + "/" + e->d_name, depth + 1);
                continue;
            }

            const char *dot = std::strrchr(e->d_name, '.');
            if (!dot) continue;

            int flacSize = 0;
            bool isFlac = false;
            if (!strcasecmp(dot, ".wav")) {
                /* plain WAV */
            } else if (!strncasecmp(dot, ".wt", 3)) {
                /* .wtNNNN is FLAC and NAMES its frame size. Take the number
                 * when it is one wtBuild can use (32..4096, power of two) and
                 * otherwise still list the file -- the loader infers the size
                 * from the decoded length, the same as it does for a WAV.
                 * The floor used to be 256, so a .wt64 was skipped entirely
                 * and the file was invisible rather than merely unlabelled. */
                flacSize = std::atoi(dot + 3);
                if (flacSize < 32 || flacSize > 4096 ||
                    (flacSize & (flacSize - 1)) != 0)
                    flacSize = 0;
                isFlac = true;
            } else {
                continue;
            }

            std::string base(e->d_name, (size_t) (dot - e->d_name));
            entries.push_back({ prefix.empty() ? base : prefix + "/" + base,
                                full, flacSize, isFlac });
        }
        ::closedir(d);
    }

    std::vector<WtEntry> entries;
};

} // namespace tb
