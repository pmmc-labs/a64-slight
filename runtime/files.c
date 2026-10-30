// files.c -- files beyond reading and writing (D254): a directory's
// names, a file's kind, size, time and mode, and deleting, renaming and
// making directories. Each is a system call made where it's called, as
// opening a file is, through replay.c, so a run replays without the
// files; with no thread in the runtime, a device would wait the same way.
// Each gives (:ok x), or (:error (name path)), errno's name as a device's
// failure has it.

#include "rt.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "io.h"

static rt_value_t cell(rt_value_t car, rt_value_t cdr, const char *site) {
    rt_value_t *c = rt_alloc(16, site);
    c[0] = car;
    c[1] = cdr;
    return (rt_value_t)c | RT_TAG_LIST;
}

static rt_value_t pair(rt_value_t a, rt_value_t b, const char *site) {
    return cell(a, cell(b, RT_NIL, site), site);
}

static rt_value_t ok(rt_value_t x, const char *site) {
    return pair(rt_symbol(RT_SYM_OK), x, site);
}

static rt_value_t failed(int err, rt_value_t path, const char *site) {
    return pair(rt_symbol(RT_SYM_ERROR), pair(rt_symbol(RT_SYM_ERRS + rt_errno_name(err)), path, site), site);
}

// The path as C has it: a string's bytes end in a NUL already, so only
// one inside it, which C would take for the end, is refused (EINVAL, an
// :io-error).
static int path_of(rt_value_t path, const char *site, const char **c) {
    if (!rt_is_string(path)) rt_fault(RT_FAULT_NOT_STRING, path, site);
    *c = rt_string_bytes(path);
    return memchr(*c, 0, rt_string_len(path)) ? EINVAL : 0;
}

// Names in the order of their bytes, the same on every file system.
static int by_bytes(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

rt_value_t rt_fs_list(rt_value_t path, const char *site) {
    const char *p;
    int         err = path_of(path, site, &p);
    if (err) return failed(err, path, site);
    char    *names;
    uint32_t len;
    int64_t  n = rt_io_list(p, &names, &len);
    if (n < 0) {
        err = errno;
        free(names);
        return failed(err, path, site);
    }
    const char **sorted = rt_must(malloc((size_t)(n ? n : 1) * sizeof *sorted));
    const char  *at     = names;
    for (int64_t i = 0; i < n; i++, at += strlen(at) + 1) sorted[i] = at;
    qsort(sorted, (size_t)n, sizeof *sorted, by_bytes);
    rt_value_t list = RT_NIL;
    for (int64_t i = n - 1; i >= 0; i--) list = cell(rt_new_string(sorted[i], strlen(sorted[i]), site), list, site);
    free(sorted);
    free(names);
    return ok(list, site);
}

rt_value_t rt_fs_stat(rt_value_t path, const char *site) {
    const char *p;
    int64_t     got[4];
    int         err = path_of(path, site, &p);
    if (err) return failed(err, path, site);
    if (rt_io_stat(p, got) != 0) return failed(errno, path, site);
    rt_value_t info = RT_NIL;
    info = cell(pair(rt_symbol(RT_SYM_STAT + RT_STAT_MODE), rt_int(got[3]), site), info, site);
    info = cell(pair(rt_symbol(RT_SYM_STAT + RT_STAT_MODIFIED), rt_int(got[2]), site), info, site);
    info = cell(pair(rt_symbol(RT_SYM_STAT + RT_STAT_SIZE), rt_int(got[1]), site), info, site);
    info = cell(pair(rt_symbol(RT_SYM_KIND + RT_KIND_KIND), rt_symbol(RT_SYM_STAT + (uint64_t)got[0]), site), info, site);
    return ok(info, site);
}

rt_value_t rt_fs_delete(rt_value_t path, const char *site) {
    const char *p;
    int         err = path_of(path, site, &p);
    if (err) return failed(err, path, site);
    return rt_io_remove(p) == 0 ? ok(RT_NIL, site) : failed(errno, path, site);
}

// A failure names the path it was about: from, unless to is the one with
// a NUL in it.
rt_value_t rt_fs_rename(rt_value_t from, rt_value_t to, const char *site) {
    const char *f, *t;
    int         err = path_of(from, site, &f);
    if (err) return failed(err, from, site);
    if ((err = path_of(to, site, &t)) != 0) return failed(err, to, site);
    return rt_io_rename(f, t) == 0 ? ok(RT_NIL, site) : failed(errno, from, site);
}

rt_value_t rt_fs_mkdir(rt_value_t path, const char *site) {
    const char *p;
    int         err = path_of(path, site, &p);
    if (err) return failed(err, path, site);
    return rt_io_mkdir(p) == 0 ? ok(RT_NIL, site) : failed(errno, path, site);
}
