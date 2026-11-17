// memory.c -- what the system says the program holds (D294): the bytes of
// it in memory, and how much of a range of its addresses is. Apart from
// the rest, and with none of the runtime's own names, since macOS answers
// the first through Mach's headers. Called through replay.c, so a run
// replays with the numbers it had.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#include "io.h"

// The program's resident set, in bytes, or 0 if the system won't say.
// Under qemu it's qemu's own, the program's and the emulator's together.
int64_t rt_resident(void) {
#if defined(__APPLE__)
    struct mach_task_basic_info info;
    mach_msg_type_number_t      n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return (int64_t)info.resident_size;
#else
    long long size = 0, resident = 0;
    FILE     *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    if (fscanf(f, "%lld %lld", &size, &resident) != 2) resident = 0;
    fclose(f);
    return (int64_t)resident * sysconf(_SC_PAGESIZE);
#endif
}

// How many of the len bytes at `at`, whole pages, are in memory: for a
// stack, the pages it has touched and not given back. On macOS a page
// given back with MADV_FREE counts till the system takes it.
int64_t rt_in_memory(void *at, size_t len) {
#if defined(__APPLE__)
    typedef char vec_t;
#else
    typedef unsigned char vec_t;
#endif
    size_t page = (size_t)sysconf(_SC_PAGESIZE), n = (len + page - 1) / page, in = 0;
    vec_t  vec[1024];
    for (size_t i = 0; i < n; i += sizeof vec) {
        size_t some = n - i < sizeof vec ? n - i : sizeof vec;
        if (mincore((char *)at + i * page, some * page, vec) != 0) return 0;
        for (size_t j = 0; j < some; j++) in += vec[j] & 1;
    }
    return (int64_t)(in * page);
}
