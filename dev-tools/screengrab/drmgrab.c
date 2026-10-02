/* drmgrab: copy what the MPC's screen is showing (steve/tools/screengrab, 2026-10-02). Read-only: it finds the
 * framebuffer each active display plane scans out (DRM GETPLANE/GETFB as root), maps it (MAP_DUMB) and writes it
 * to stdout after a one-line text header "w h pitch bpp depth plane\n". /dev/fb0 stays black on MPC OS: MPC draws
 * through DRM planes, not fbdev. Raw kernel ioctls, no libdrm.
 *   drmgrab [/dev/dri/cardN] [list] > shot.raw      (list: print planes/framebuffers only) */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* The few DRM UAPI definitions used (linux/include/uapi/drm/drm.h, drm_mode.h; stable kernel ABI). */
struct drm_set_client_cap { uint64_t capability, value; };
struct drm_gem_close { uint32_t handle, pad; };
struct drm_mode_fb_cmd { uint32_t fb_id, width, height, pitch, bpp, depth, handle; };
struct drm_mode_map_dumb { uint32_t handle, pad; uint64_t offset; };
struct drm_mode_get_plane_res { uint64_t plane_id_ptr; uint32_t count_planes; };
struct drm_mode_get_plane { uint32_t plane_id, crtc_id, fb_id, possible_crtcs, gamma_size, count_format_types;
                            uint64_t format_type_ptr; };
#define DRM_CLIENT_CAP_UNIVERSAL_PLANES 2
#define DRM_IOCTL_SET_CLIENT_CAP          _IOW('d', 0x0D, struct drm_set_client_cap)
#define DRM_IOCTL_GEM_CLOSE               _IOW('d', 0x09, struct drm_gem_close)
#define DRM_IOCTL_MODE_GETFB              _IOWR('d', 0xAD, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_MAP_DUMB           _IOWR('d', 0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_GETPLANERESOURCES  _IOWR('d', 0xB5, struct drm_mode_get_plane_res)
#define DRM_IOCTL_MODE_GETPLANE           _IOWR('d', 0xB6, struct drm_mode_get_plane)

int main(int argc, char **argv) {
    const char *dev = argc > 1 && argv[1][0] == '/' ? argv[1] : "/dev/dri/card0";
    int list = (argc > 1 && !strcmp(argv[argc - 1], "list"));
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror(dev); return 1; }
    struct drm_set_client_cap cap = { DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1 };
    ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap);
    uint32_t ids[32];
    struct drm_mode_get_plane_res pr = {0};
    pr.plane_id_ptr = (uintptr_t)ids; pr.count_planes = 32;
    if (ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr)) { perror("GETPLANERESOURCES"); return 1; }
    int best = -1; struct drm_mode_fb_cmd bestfb = {0};
    for (uint32_t i = 0; i < pr.count_planes && i < 32; i++) {
        struct drm_mode_get_plane p = {0};
        p.plane_id = ids[i];
        if (ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p)) continue;
        struct drm_mode_fb_cmd fb = {0};
        fb.fb_id = p.fb_id;
        int ok = p.fb_id && !ioctl(fd, DRM_IOCTL_MODE_GETFB, &fb);
        fprintf(stderr, "plane %u crtc %u fb %u", p.plane_id, p.crtc_id, p.fb_id);
        if (ok) fprintf(stderr, " %ux%u pitch %u bpp %u depth %u handle %u", fb.width, fb.height, fb.pitch, fb.bpp, fb.depth, fb.handle);
        fprintf(stderr, "\n");
        if (ok && fb.handle && (best < 0 || fb.width * fb.height > bestfb.width * bestfb.height)) { best = (int)p.plane_id; bestfb = fb; }
    }
    if (list) return 0;
    if (best < 0) { fprintf(stderr, "no mappable framebuffer\n"); return 1; }
    struct drm_mode_map_dumb md = {0};
    md.handle = bestfb.handle;
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { perror("MAP_DUMB"); return 1; }
    size_t len = (size_t)bestfb.pitch * bestfb.height;
    void *m = mmap(0, len, PROT_READ, MAP_SHARED, fd, md.offset);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    printf("%u %u %u %u %u %d\n", bestfb.width, bestfb.height, bestfb.pitch, bestfb.bpp, bestfb.depth, best);
    fflush(stdout);
    fwrite(m, 1, len, stdout);
    munmap(m, len);
    struct drm_gem_close gc = { bestfb.handle, 0 };
    ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
    close(fd);
    return 0;
}
