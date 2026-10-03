// rt.h -- the runtime's data structures, shared by C and assembly.
//
// The constants at the top are struct offsets the assembly side depends on;
// rt.c checks every one of them with _Static_assert, so they can't drift.
// Everything this header defines is prefixed (RT_, rt_) because it's
// included before the system headers: macOS's <stdlib.h> pulls in
// <sys/wait.h>, whose enum has a P_PID that an unprefixed macro would clobber.
// t/headers.c checks this against the local system's headers.

#ifndef RT_H
#define RT_H

#define RT_MAX_ARGS          7    // payload registers x1..x7

#define RT_PROC_REDUCTIONS   0    // proc_t.reductions  (REDUCE: ldr/str [x28, #0])
#define RT_PROC_PID          8    // proc_t.pid         (trampoline: x0 for .init)
#define RT_PROC_ARGS        16    // proc_t.args[7]     (trampoline: x1..x7 for .init)
#define RT_PROC_CTX         72    // proc_t.ctx

#define RT_ENV_ARGS         64    // env_t.args[7]      (MSG_ACCEPT: x1..x7)

#define RT_CTX_X19           0    // ctx_t: x19..x28, x29, x30, sp, d8..d15
#define RT_CTX_FP           80
#define RT_CTX_LR           88
#define RT_CTX_SP           96
#define RT_CTX_D8          104
#define RT_CTX_SIZE        168

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

// Give a C symbol the exact name the assembly uses, on Linux and on macOS
// (where C names would otherwise get a leading underscore).
#define RT_ASM(name) __asm__(#name)

typedef struct rt_module rt_module_t;

// A tag is the address of one of these. Tags are emitted by the TAG macro
// in actor.h, in index order, right after their module.
typedef struct rt_tag {
    const rt_module_t *module;
    uint64_t           index;
    uint64_t           arity;
    const char        *name;
} rt_tag_t;

struct rt_module {
    const char     *name;
    void          (*init)(void);
    uint64_t        arity;       // number of spawn args
    uint64_t        ntags;
    const rt_tag_t *tags;        // ntags descriptors, in index order
};

#define RT_DECLARE_MODULE(name) extern const rt_module_t mod_##name RT_ASM(mod_##name)

typedef struct rt_stats {
    uint64_t ticks;
    uint64_t spawned;
    uint64_t activations;    // switches from the scheduler into a process
    uint64_t preemptions;
    uint64_t messages;
    uint64_t faults;
    uint64_t waiting;        // processes still alive when the run ended
} rt_stats_t;

// --- the harness API -------------------------------------------------------

void       rt_init(int64_t quota, size_t stack_bytes);
uint64_t   rt_spawn_root(const rt_module_t *m, uint64_t argc, const uint64_t *args);
void       rt_run(void);
void       rt_report(void);
rt_stats_t rt_get_stats(void);

#endif // __ASSEMBLER__
#endif // RT_H
