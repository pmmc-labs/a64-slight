// harness.c -- picks a scenario, spawns its root process, runs, reports.
//
//   ./spike multiplier      4 * 10 with a forged reply           -> say: 40
//   ./spike dying-adder     same, but the adders die             -> the multiplier died
//   ./spike wrong-tag       SEND a tag to a process that doesn't own it -> fault
//   ./spike preempt         a busy loop can't starve a REQUEST/AWAIT
//   ./spike deadlock        two processes AWAIT each other       -> deadlock
//   ./spike bench [n] [k]   n round trips, then k idle processes (timing, memory)

#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

RT_DECLARE_MODULE(main);
RT_DECLARE_MODULE(wrongtag);
RT_DECLARE_MODULE(preempt);
RT_DECLARE_MODULE(deadlock);
RT_DECLARE_MODULE(bench);
RT_DECLARE_MODULE(idle);

// both can be overridden from the environment: RT_QUOTA, RT_STACK_KB
#define QUOTA       2000
#define STACK_KB    64

static long env_or(const char *name, long dflt) {
    const char *v = getenv(name);
    return v && *v ? strtol(v, NULL, 10) : dflt;
}

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static long rss_kib(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return ru.ru_maxrss / 1024;                 // bytes on macOS
#else
    return ru.ru_maxrss;                        // KiB on Linux
#endif
}

static int bench(int argc, char **argv) {
    uint64_t n = argc > 2 ? strtoull(argv[2], NULL, 10) : 1000000;
    uint64_t k = argc > 3 ? strtoull(argv[3], NULL, 10) : 10000;

    double t0 = now_ns();
    rt_spawn_root(&mod_bench, 1, (uint64_t[]){ n });
    rt_run();
    double     t1 = now_ns();
    rt_stats_t s  = rt_get_stats();
    printf("round trips:  %llu in %.1f ms = %.1f ns each  (%llu ticks, %llu activations, %llu messages)\n",
           (unsigned long long)n, (t1 - t0) / 1e6, (t1 - t0) / n,
           (unsigned long long)s.ticks, (unsigned long long)s.activations,
           (unsigned long long)s.messages);

    long r0 = rss_kib();
    t0 = now_ns();
    for (uint64_t i = 0; i < k; i++) rt_spawn_root(&mod_idle, 0, NULL);
    rt_run();                                   // each one runs to its RECV and suspends
    t1 = now_ns();
    long r1 = rss_kib();
    printf("idle procs:   %llu spawned and suspended in %.1f ms = %.0f ns each, +%ld KiB resident = %.0f bytes each\n",
           (unsigned long long)k, (t1 - t0) / 1e6, (t1 - t0) / k,
           r1 - r0, (r1 - r0) * 1024.0 / k);
    return 0;
}

int main(int argc, char **argv) {
    const char *s = argc > 1 ? argv[1] : "multiplier";
    rt_init(env_or("RT_QUOTA", QUOTA), env_or("RT_STACK_KB", STACK_KB) * 1024);

    if      (!strcmp(s, "multiplier"))  rt_spawn_root(&mod_main, 3, (uint64_t[]){ 4, 10, 0 });
    else if (!strcmp(s, "dying-adder")) rt_spawn_root(&mod_main, 3, (uint64_t[]){ 4, 10, 1 });
    else if (!strcmp(s, "wrong-tag"))   rt_spawn_root(&mod_wrongtag, 0, NULL);
    else if (!strcmp(s, "preempt"))     rt_spawn_root(&mod_preempt, 1, (uint64_t[]){ 1000000 });
    else if (!strcmp(s, "deadlock"))    rt_spawn_root(&mod_deadlock, 0, NULL);
    else if (!strcmp(s, "bench"))       return bench(argc, argv);
    else {
        fprintf(stderr, "usage: %s [multiplier|dying-adder|wrong-tag|preempt|deadlock|bench [n] [k]]\n", argv[0]);
        return 2;
    }

    rt_run();
    rt_report();
    return 0;
}
