/* Realtime hygiene for a Schwung module.
 *
 * Schwung's threading contract (docs/MODULES.md, "Threading: there is no
 * control thread"): EVERY plugin entry point — create_instance,
 * destroy_instance, set_param, get_param, on_midi, render_block — runs on the
 * SPI audio callback, at SCHED_FIFO 90 on core 3, with ~900 us per block. No
 * file I/O, no allocation, no logging, no unbounded work in any of them.
 *
 * A thread started from one of those calls is born FIFO 90: POSIX defaults to
 * PTHREAD_INHERIT_SCHED, so it takes the creating thread's policy AND its
 * name, which is why such a worker shows up as "Audio Main/SPI" and hides
 * from a thread audit. At that priority it starves Move's own audio — its
 * Link publisher runs at FIFO 35 — and a wavetable build is hundreds of
 * milliseconds against a 2.72 ms frame.
 *
 * Demoting from inside the worker (sched_setscheduler as its first line)
 * leaves a window where it is still FIFO 90, and leaves the name wrong. So
 * the policy is set on the ATTRIBUTES instead: the thread is never FIFO at
 * all, and it is named before it can be mistaken for the audio thread.
 *
 * Raised by Schwung upstream against this module, whose worker demoted itself
 * but was still born hot and still unnamed. An audit there found seven
 * plugins spawning FIFO-90 threads this way.
 */
#pragma once

#include <pthread.h>
#include <sched.h>
#include <cstring>

namespace tb {

/* Start a worker that is SCHED_OTHER from birth, off the SPI core, and
 * carries its own name. Returns 0 on success, like pthread_create.
 *
 * `name` is truncated to the 15 characters Linux allows a thread name. */
inline int rtStartWorker(pthread_t *tid, void *(*fn)(void *), void *arg,
                         const char *name)
{
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return -1;

    /* The whole point: do NOT inherit the audio thread's scheduling. */
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    struct sched_param sp = { };
    sp.sched_priority = 0;                 /* SCHED_OTHER takes only 0 */
    pthread_attr_setschedparam(&attr, &sp);

    /* Leave core 3 to the SPI callback. */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    pthread_attr_setaffinity_np(&attr, sizeof set, &set);

    int rc = pthread_create(tid, &attr, fn, arg);
    pthread_attr_destroy(&attr);

    if (rc == 0 && name && *name) {
        char nm[16];
        snprintf(nm, sizeof nm, "%s", name);
        pthread_setname_np(*tid, nm);      /* else it inherits "Audio Main" */
    }
    return rc;
}

} // namespace tb
