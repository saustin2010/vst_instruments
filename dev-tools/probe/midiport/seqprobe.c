/* seqprobe: open an ALSA sequencer client "MIDI Probe" with one readable port, hold it for N seconds, send a few
 * notes, close. Uses the device's own libasound via dlopen (no headers needed). Checks whether MPC OS sees/subscribes
 * a plugin-style MIDI port on this device.   seqprobe [seconds] */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct snd_seq snd_seq_t;
int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 8;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { printf("no libasound: %s\n", dlerror()); return 1; }
    int (*open_)(snd_seq_t **, const char *, int, int) = dlsym(h, "snd_seq_open");
    int (*name_)(snd_seq_t *, const char *) = dlsym(h, "snd_seq_set_client_name");
    int (*port_)(snd_seq_t *, const char *, unsigned, unsigned) = dlsym(h, "snd_seq_create_simple_port");
    int (*id_)(snd_seq_t *) = dlsym(h, "snd_seq_client_id");
    int (*close_)(snd_seq_t *) = dlsym(h, "snd_seq_close");
    snd_seq_t *s;
    if (open_(&s, "default", 1 /* SND_SEQ_OPEN_OUTPUT */, 0) < 0) { printf("snd_seq_open failed\n"); return 1; }
    name_(s, "MIDI Probe");
    int p = port_(s, "MIDI Out", (1u << 0) | (1u << 5) /* READ | SUBS_READ */, (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
    printf("client %d port %d open for %ds\n", id_(s), p, secs); fflush(stdout);
    sleep(secs);
    close_(s);
    printf("closed\n");
    return 0;
}
