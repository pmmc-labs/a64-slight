// replay.c -- recording a run's dealings with the world, and playing them
// back (D170). See io.h.
//
// Everything a run takes from outside comes through here: the clock, what
// select() says is ready, what reads and receives give, whether files and
// sockets open, what a lookup's files say. The rest of a run is
// determined by it: the scheduler counts calls, not time (D86, D91), and
// everything else it does it decides itself. So with what came in, in
// order, a run can be made again, exactly: to see a bug that came from
// the world again, under a debugger, as often as it takes.
//
// The record is a line saying what it is, the settings, arguments and
// environment (rt_io_settings_t), then an entry for each call, in order:
// what kind of
// call, its result, errno, and the bytes it filled in. Numbers are in the
// machine's own order: a record is for replaying where it was made. It's
// flushed after every entry, so a run that crashes leaves a record of all
// it did up to the crash.

#define _GNU_SOURCE                     // Linux's struct ucred, for SO_PEERCRED
#include "rt.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "io.h"

#define MAGIC "slight record 3\n"

enum { OFF, RECORD, REPLAY };

enum {
    IO_CLOCK = 1, IO_SELECT, IO_READ, IO_WRITE, IO_OPEN, IO_CLOSE, IO_SOCKET, IO_SETUP, IO_CONNECT,
    IO_LISTEN, IO_ACCEPT, IO_SOCKET_ERROR, IO_SEND, IO_RECV, IO_FOPEN, IO_FGETS, IO_SEED, IO_WINSIZE,
    IO_SPAWN, IO_WAIT, IO_KILL, IO_PIPE, IO_LISTEN_UNIX, IO_PEER, IO_REALTIME, IO_LIST, IO_STAT, IO_REMOVE,
    IO_RENAME, IO_MKDIR,
};

static const char *const names[] = {
    [IO_CLOCK] = "the clock", [IO_SELECT] = "select", [IO_READ] = "a read", [IO_WRITE] = "a write",
    [IO_OPEN] = "an open", [IO_CLOSE] = "a close", [IO_SOCKET] = "a socket", [IO_SETUP] = "a socket's setup",
    [IO_CONNECT] = "a connect", [IO_LISTEN] = "a listen", [IO_ACCEPT] = "an accept",
    [IO_SOCKET_ERROR] = "a socket's error", [IO_SEND] = "a send", [IO_RECV] = "a receive",
    [IO_FOPEN] = "a text file", [IO_FGETS] = "a line of a text file", [IO_SEED] = "a seed",
    [IO_WINSIZE] = "the screen's size", [IO_SPAWN] = "a command", [IO_WAIT] = "a wait for a command",
    [IO_KILL] = "a kill", [IO_PIPE] = "a pipe", [IO_LISTEN_UNIX] = "a listen at a path",
    [IO_PEER] = "a socket's peer", [IO_REALTIME] = "the time of day",
    [IO_LIST] = "a directory's names", [IO_STAT] = "a file's stat", [IO_REMOVE] = "a remove",
    [IO_RENAME] = "a rename", [IO_MKDIR] = "a mkdir",
};

static int      mode;
static FILE    *record;
static uint64_t count;                  // entries written or read so far
static char     token;                  // what rt_io_fopen gives when replaying: a FILE * never used as one

static const char *name(int kind) {
    return kind > 0 && kind < (int)(sizeof names / sizeof *names) ? names[kind] : "something unknown";
}

__attribute__((noreturn))
static void stop(const char *what) {
    fflush(stdout);
    fprintf(stderr, "replay: %s\n", what);
    exit(2);
}

static void put(const void *p, size_t n) {
    if (fwrite(p, 1, n, record) != n) {
        perror("slight: can't write the record");
        exit(2);
    }
}

static int get(void *p, size_t n) {
    return fread(p, 1, n, record) == n;
}

// --- recording ----------------------------------------------------------------

static void write_entry(int kind, int64_t result, int err, const void *data, uint32_t len) {
    uint8_t k = (uint8_t)kind;
    int32_t e = err;
    put(&k, 1);
    put(&result, sizeof result);
    put(&e, sizeof e);
    put(&len, sizeof len);
    if (len) put(data, len);
    fflush(record);
    count++;
}

// The end of a call that gives a number and errno, and fills nothing in.
static int64_t done(int kind, int64_t result) {
    if (mode == RECORD) {
        int err = errno;
        write_entry(kind, result, err, NULL, 0);
        errno = err;
    }
    return result;
}

// The same, for one that filled in len bytes at data.
static int64_t done_with(int kind, int64_t result, const void *data, size_t len) {
    if (mode == RECORD) {
        int err = errno;
        write_entry(kind, result, err, data, (uint32_t)len);
        errno = err;
    }
    return result;
}

// --- replaying ----------------------------------------------------------------

__attribute__((noreturn))
static void spoilt(int kind) {
    char say[160];
    snprintf(say, sizeof say, "the record is cut short or spoilt at entry %" PRIu64 " (%s)", count + 1, name(kind));
    stop(say);
}

// The next entry's head, which must be of this kind: its result, its
// errno and how many bytes follow.
static void head(int kind, int64_t *result, int32_t *err, uint32_t *n) {
    char    say[160];
    uint8_t k;
    if (!get(&k, 1)) {
        snprintf(say, sizeof say, "the record ends at entry %" PRIu64 ", where the run asks for %s", count + 1, name(kind));
        stop(say);
    }
    if (k != kind) {
        snprintf(say, sizeof say, "the run goes another way at entry %" PRIu64 ": it asks for %s, and the record has %s",
                 count + 1, name(kind), name(k));
        stop(say);
    }
    if (!get(result, sizeof *result) || !get(err, sizeof *err) || !get(n, sizeof *n)) spoilt(kind);
}

// The next entry, which must be of this kind, its bytes put at data (at
// most cap of them; *len says how many), and errno as it was.
static int64_t next(int kind, void *data, size_t cap, uint32_t *len) {
    int64_t  result;
    int32_t  err;
    uint32_t n;
    head(kind, &result, &err, &n);
    if (n > cap || (n && !get(data, n))) spoilt(kind);
    if (len) *len = n;
    count++;
    errno = err;
    return result;
}

// The same, its bytes, however many, in memory of their own (*data, to
// free).
static int64_t next_alloc(int kind, char **data, uint32_t *len) {
    int64_t  result;
    int32_t  err;
    uint32_t n;
    head(kind, &result, &err, &n);
    *data = rt_must(malloc(n ? n : 1));
    if (n && !get(*data, n)) spoilt(kind);
    *len = n;
    count++;
    errno = err;
    return result;
}

static int64_t replayed(int kind) {
    return next(kind, NULL, 0, NULL);
}

// --- starting -----------------------------------------------------------------

// n strings: how many, then each one's length and bytes.
static void put_strings(uint32_t n, char *const *xs) {
    put(&n, sizeof n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len = (uint32_t)strlen(xs[i]);
        put(&len, sizeof len);
        put(xs[i], len);
    }
}

// What put_strings wrote, as a NULL-terminated array, its length in *n.
static char **get_strings(uint32_t *n) {
    if (!get(n, sizeof *n)) stop("the record is cut short");
    char **xs = rt_must(calloc((size_t)*n + 1, sizeof *xs));
    for (uint32_t i = 0; i < *n; i++) {
        uint32_t len;
        if (!get(&len, sizeof len)) stop("the record is cut short");
        xs[i] = rt_must(calloc(len + 1, 1));
        if (!get(xs[i], len)) stop("the record is cut short");
    }
    return xs;
}

const char *rt_io_getenv(const char *name) {
    return getuid() != geteuid() || getgid() != getegid() ? NULL : getenv(name);
}

// A record holds all the run read, and its environment, so it's the
// user's alone (0600, even over a file that was there), and isn't written
// through a symbolic link to somewhere else.
static FILE *open_record(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0 && fchmod(fd, 0600) != 0) {
        int err = errno;
        close(fd);
        fd    = -1;
        errno = err;
    }
    return fd >= 0 ? fdopen(fd, "wb") : NULL;
}

void rt_io_start(int *argc, char ***argv, char ***env, rt_io_settings_t *s) {
    const char *rec = rt_io_getenv("SLIGHT_RECORD"), *play = rt_io_getenv("SLIGHT_REPLAY");
    if (rec && play) {
        fputs("slight: SLIGHT_RECORD and SLIGHT_REPLAY can't both be set\n", stderr);
        exit(2);
    }
    if (!rec && !play) return;
    record = rec ? open_record(rec) : fopen(play, "rb");
    if (!record) {
        fprintf(stderr, "slight: can't %s %s: %s\n", rec ? "record to" : "replay", rec ? rec : play, strerror(errno));
        exit(2);
    }
    fcntl(fileno(record), F_SETFD, FD_CLOEXEC);    // not a command's
    mode = rec ? RECORD : REPLAY;
    uint8_t  clock;
    uint32_t n = 0;
    if (mode == RECORD) {
        put(MAGIC, strlen(MAGIC));
        clock = (uint8_t)s->virtual_clock;
        put(&clock, 1);
        put(&s->sched_seed, sizeof s->sched_seed);
        put(&s->heap_max, sizeof s->heap_max);
        put_strings((uint32_t)*argc, *argv);
        while ((*env)[n]) n++;
        put_strings(n, *env);
        fflush(record);
        return;
    }
    char magic[sizeof MAGIC - 1];
    if (!get(magic, sizeof magic) || memcmp(magic, MAGIC, sizeof magic) != 0) stop("that isn't a record of a run");
    if (!get(&clock, 1) || !get(&s->sched_seed, sizeof s->sched_seed) || !get(&s->heap_max, sizeof s->heap_max)) {
        stop("the record is cut short");
    }
    s->virtual_clock = clock;
    *argv = get_strings(&n);
    *argc = (int)n;
    *env  = get_strings(&n);
}

int rt_io_replaying(void) {
    return mode == REPLAY;
}

// --- the calls ----------------------------------------------------------------

uint64_t rt_io_clock(void) {
    if (mode == REPLAY) return (uint64_t)replayed(IO_CLOCK);
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)done(IO_CLOCK, (int64_t)((uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec));
}

static int64_t realtime(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

int64_t rt_io_realtime(void) {
    if (mode == REPLAY) return replayed(IO_REALTIME);
    return done(IO_REALTIME, realtime());
}

int64_t rt_io_epoch(const char *given) {
    if (mode == REPLAY) return replayed(IO_REALTIME);
    return done(IO_REALTIME, given ? strtoll(given, NULL, 10) * 1000000 : realtime());
}

// --- files (D254) -------------------------------------------------------------

int64_t rt_io_list(const char *path, char **names, uint32_t *len) {
    if (mode == REPLAY) return next_alloc(IO_LIST, names, len);
    DIR    *d = opendir(path);
    rt_buf_t b = { 0 };
    int64_t n = 0;
    if (d) {
        struct dirent *e;
        errno = 0;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            rt_buf_add(&b, e->d_name, strlen(e->d_name) + 1);
            n++;
        }
        int err = errno;
        closedir(d);
        if (err) n = -1, errno = err;
    } else {
        n = -1;
    }
    *names = rt_must(malloc(b.len ? b.len : 1));
    if (b.len) memcpy(*names, b.bytes, b.len);
    *len = (uint32_t)b.len;
    rt_buf_free(&b);
    return done_with(IO_LIST, n, *names, *len);
}

int rt_io_stat(const char *path, int64_t got[4]) {
    if (mode == REPLAY) return (int)next(IO_STAT, got, 4 * sizeof *got, NULL);
    struct stat st;
    int r = lstat(path, &st);
    if (r == 0) {
        got[0] = S_ISREG(st.st_mode) ? 0 : S_ISDIR(st.st_mode) ? 1 : S_ISLNK(st.st_mode) ? 2 : 3;
        got[1] = (int64_t)st.st_size;
#ifdef __APPLE__
        got[2] = (int64_t)st.st_mtimespec.tv_sec * 1000000 + st.st_mtimespec.tv_nsec / 1000;
#else
        got[2] = (int64_t)st.st_mtim.tv_sec * 1000000 + st.st_mtim.tv_nsec / 1000;
#endif
        got[3] = (int64_t)(st.st_mode & 07777);
    }
    return (int)done_with(IO_STAT, r, got, r == 0 ? 4 * sizeof *got : 0);
}

int rt_io_remove(const char *path) {
    if (mode == REPLAY) return (int)replayed(IO_REMOVE);
    return (int)done(IO_REMOVE, remove(path));
}

int rt_io_rename(const char *from, const char *to) {
    if (mode == REPLAY) return (int)replayed(IO_RENAME);
    return (int)done(IO_RENAME, rename(from, to));
}

int rt_io_mkdir(const char *path) {
    if (mode == REPLAY) return (int)replayed(IO_MKDIR);
    return (int)done(IO_MKDIR, mkdir(path, 0777));
}

// The sets as select() left them, when it says something is ready.
int rt_io_select(int n, fd_set *in, fd_set *out, struct timeval *timeout) {
    fd_set sets[2];
    if (mode == REPLAY) {
        uint32_t len;
        int      r = (int)next(IO_SELECT, sets, sizeof sets, &len);
        if (len == sizeof sets) {
            *in  = sets[0];
            *out = sets[1];
        }
        return r;
    }
    int r = select(n, in, out, NULL, timeout);
    if (r > 0) {
        sets[0] = *in;
        sets[1] = *out;
    }
    return (int)done_with(IO_SELECT, r, sets, r > 0 ? sizeof sets : 0);
}

ssize_t rt_io_read(int fd, void *buf, size_t len) {
    if (mode == REPLAY) return (ssize_t)next(IO_READ, buf, len, NULL);
    ssize_t r = read(fd, buf, len);
    return (ssize_t)done_with(IO_READ, r, buf, r > 0 ? (size_t)r : 0);
}

ssize_t rt_io_write(int fd, const void *buf, size_t len) {
    if (mode == REPLAY) return (ssize_t)replayed(IO_WRITE);
    return (ssize_t)done(IO_WRITE, write(fd, buf, len));
}

int rt_io_open(const char *path, int flags) {
    if (mode == REPLAY) return (int)replayed(IO_OPEN);
    return (int)done(IO_OPEN, open(path, flags | O_CLOEXEC, 0666));
}

int rt_io_close(int fd) {
    if (mode == REPLAY) return (int)replayed(IO_CLOSE);
    return (int)done(IO_CLOSE, close(fd));
}

int rt_io_socket(int family, int type) {
    if (mode == REPLAY) return (int)replayed(IO_SOCKET);
    return (int)done(IO_SOCKET, socket(family, type, 0));
}

int rt_io_setup(int fd, int tcp) {
    if (mode == REPLAY) return (int)replayed(IO_SETUP);
    int err = 0, flags = fcntl(fd, F_GETFL);
    if (fd >= FD_SETSIZE) {
        err = EMFILE;
    } else if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        err = errno;
    } else if (tcp) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);  // messages are small; don't wait to fill a packet
    }
    return (int)done(IO_SETUP, err);
}

int rt_io_connect(int fd, const struct sockaddr *a, socklen_t len) {
    if (mode == REPLAY) return (int)replayed(IO_CONNECT);
    return (int)done(IO_CONNECT, connect(fd, a, len));
}

// Binds fd to port at addr (network order; INADDR_ANY for every
// interface), and listens: 0, with the port it got in *got (port 0 lets
// the system pick), or an errno.
int rt_io_listen(int fd, uint32_t addr, int port, int *got) {
    int32_t p = 0;
    if (mode == REPLAY) {
        int err = (int)next(IO_LISTEN, &p, sizeof p, NULL);
        *got = p;
        return err;
    }
    int one = 1, err = 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { 0 };
    a.sin_family      = AF_INET;
    a.sin_port        = htons((uint16_t)port);
    a.sin_addr.s_addr = addr;
    socklen_t alen    = sizeof a;
    if ((bind(fd, (struct sockaddr *)&a, sizeof a) < 0 && (err = errno))
        || (listen(fd, 128) < 0 && (err = errno))
        || (getsockname(fd, (struct sockaddr *)&a, &alen) < 0 && (err = errno))) {
        p = 0;
    } else {
        p = ntohs(a.sin_port);
    }
    *got = p;
    return (int)done_with(IO_LISTEN, err, &p, sizeof p);
}

// Whether the socket at a is one that nothing accepts on: a program that
// listened there has ended without removing it. Asked without waiting,
// so one whose queue is full (EAGAIN on Linux) counts as alive; macOS
// says ECONNREFUSED for that too, so there a socket so busy that a second
// copy of its program starts can be taken for dead.
static int stale(const struct sockaddr_un *a) {
    struct stat st;
    if (lstat(a->sun_path, &st) < 0 || !S_ISSOCK(st.st_mode)) return 0;
    int s = socket(AF_UNIX, SOCK_STREAM, 0), dead = 0;
    if (s < 0) return 0;
    int flags = fcntl(s, F_GETFL);
    if (flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0) {
        dead = connect(s, (const struct sockaddr *)a, sizeof *a) < 0 && errno == ECONNREFUSED;
    }
    close(s);
    return dead;
}

int rt_io_listen_unix(int fd, const char *path) {
    if (mode == REPLAY) return (int)replayed(IO_LISTEN_UNIX);
    struct sockaddr_un a;
    int                err = rt_unix_address(&a, path);
    if (!err && bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        err = errno;
        if (err == EADDRINUSE && stale(&a)) err = unlink(path) < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0 ? errno : 0;
    }
    if (!err && listen(fd, 128) < 0) err = errno;
    return (int)done(IO_LISTEN_UNIX, err);
}

// A connection from fd's queue: its descriptor, with the peer as
// "address:port" in where, or "" for a Unix socket's, which has none; or
// -1, with errno set.
int rt_io_accept(int fd, char *where, size_t size) {
    if (mode == REPLAY) {
        uint32_t len;
        int      r = (int)next(IO_ACCEPT, where, size - 1, &len);
        where[r >= 0 ? len : 0] = '\0';
        return r;
    }
    struct sockaddr_storage a;
    socklen_t               alen = sizeof a;
    int                     r    = accept(fd, (struct sockaddr *)&a, &alen);
    where[0] = '\0';
    if (r >= 0 && a.ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)&a;
        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &in->sin_addr, addr, sizeof addr);
        snprintf(where, size, "%s:%d", addr, ntohs(in->sin_port));
    }
    return (int)done_with(IO_ACCEPT, r, where, strlen(where));
}

// Linux's SO_PEERCRED; macOS's getpeereid, and LOCAL_PEERPID for the pid,
// which an older one may not give (0).
int rt_io_peer(int fd, int64_t who[3]) {
    if (mode == REPLAY) return (int)next(IO_PEER, who, 3 * sizeof *who, NULL);
    int err = 0;
#ifdef SO_PEERCRED
    struct ucred c;
    socklen_t    len = sizeof c;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &c, &len) < 0) err = errno;
    else who[0] = c.uid, who[1] = c.gid, who[2] = c.pid;
#else
    uid_t     u;
    gid_t     g;
    pid_t     p   = 0;
    socklen_t len = sizeof p;
    if (getpeereid(fd, &u, &g) < 0) err = errno;
    else who[0] = u, who[1] = g, who[2] = getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &p, &len) == 0 ? p : 0;
#endif
    return (int)done_with(IO_PEER, err, who, 3 * sizeof *who);
}

int rt_io_socket_error(int fd) {
    if (mode == REPLAY) return (int)replayed(IO_SOCKET_ERROR);
    int       err = 0;
    socklen_t len = sizeof err;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = errno;
    return (int)done(IO_SOCKET_ERROR, err);
}

ssize_t rt_io_send(int fd, const void *buf, size_t len) {
    if (mode == REPLAY) return (ssize_t)replayed(IO_SEND);
    return (ssize_t)done(IO_SEND, send(fd, buf, len, 0));
}

ssize_t rt_io_recv(int fd, void *buf, size_t len) {
    if (mode == REPLAY) return (ssize_t)next(IO_RECV, buf, len, NULL);
    ssize_t r = recv(fd, buf, len, 0);
    return (ssize_t)done_with(IO_RECV, r, buf, r > 0 ? (size_t)r : 0);
}

FILE *rt_io_fopen(const char *path) {
    if (mode == REPLAY) return replayed(IO_FOPEN) ? (FILE *)(void *)&token : NULL;
    FILE *f = fopen(path, "r");
    done(IO_FOPEN, f != NULL);
    return f;
}

char *rt_io_fgets(char *line, int size, FILE *f) {
    if (mode == REPLAY) {
        uint32_t len;
        int64_t  got = next(IO_FGETS, line, (size_t)size - 1, &len);
        line[len] = '\0';
        return got ? line : NULL;
    }
    char *got = fgets(line, size, f);
    done_with(IO_FGETS, got != NULL, line, got ? strlen(line) : 0);
    return got;
}

void rt_io_fclose(FILE *f) {
    if (mode != REPLAY) fclose(f);
}

// From the system's generator; the clock, the pid and an address only if
// that fails, which they're too easy to guess for anything else.
uint32_t rt_io_seed(void) {
    if (mode == REPLAY) return (uint32_t)replayed(IO_SEED);
    static char here;
    uint32_t    bits;
    if (getentropy(&bits, sizeof bits) != 0) bits = (uint32_t)time(NULL) ^ (uint32_t)getpid() << 16 ^ (uint32_t)(uintptr_t)&here;
    return (uint32_t)done(IO_SEED, bits);
}

uint64_t rt_io_random_seed(const char *given) {
    if (mode == REPLAY) return (uint64_t)replayed(IO_SEED);
    static char here;
    uint64_t    bits;
    if (given) bits = strtoull(given, NULL, 10);
    else if (getentropy(&bits, sizeof bits) != 0) bits = (uint64_t)time(NULL) << 32 ^ (uint64_t)getpid() << 16 ^ (uint64_t)(uintptr_t)&here;
    return (uint64_t)done(IO_SEED, (int64_t)bits);
}

int rt_io_winsize(int *rows, int *cols) {
    int32_t size[2] = { 0, 0 };
    int     r;
    if (mode == REPLAY) {
        r = (int)next(IO_WINSIZE, size, sizeof size, NULL);
    } else {
        struct winsize w;
        r = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row && w.ws_col ? 0 : -1;
        if (r == 0) {
            size[0] = w.ws_row;
            size[1] = w.ws_col;
        }
        done_with(IO_WINSIZE, r, size, sizeof size);
    }
    *rows = size[0];
    *cols = size[1];
    return r;
}

// --- commands -----------------------------------------------------------------

extern char **environ;

// Whether path can be run: 0, or the errno that says why not.
static int runnable(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return errno;
    if (!S_ISREG(st.st_mode)) return EACCES;
    return access(path, X_OK) < 0 ? errno : 0;
}

// Where the command name is, in path: the name itself if it has a slash
// in it; or else the first file of that name in a directory of PATH that
// can be run, an empty entry being the current directory, as execvp looks.
// 0, or ENOENT, or EACCES if there were only ones that can't be run.
static int find(const char *name, char *path, size_t size) {
    if (strchr(name, '/')) return snprintf(path, size, "%s", name) >= (int)size ? ENAMETOOLONG : runnable(path);
    const char *dirs = getenv("PATH");
    int         err  = ENOENT;
    if (!dirs) dirs = "/usr/bin:/bin";
    for (const char *d = dirs; *name; d++) {
        const char *end = strchr(d, ':');
        int         len = end ? (int)(end - d) : (int)strlen(d);
        int         n   = snprintf(path, size, "%.*s%s%s", len, d, len ? "/" : "", name);
        int         e   = n < (int)size ? runnable(path) : ENAMETOOLONG;
        if (e == 0) return 0;
        if (e == EACCES) err = EACCES;
        if (!end) break;
        d = end;
    }
    return err;
}

// Moves fd above stdin, stdout and stderr, which a command's pipes take
// the place of in the child, so a runtime started without them still
// passes the right ends on.
static int above_stdio(int fd) {
    if (fd > STDERR_FILENO) return fd;
    int moved = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    close(fd);
    return moved;
}

// A pipe, neither end inherited.
static int pipe_of(int fds[2]) {
    if (pipe(fds) < 0) return -1;
    fds[0] = above_stdio(fds[0]);
    fds[1] = above_stdio(fds[1]);
    if (fds[0] < 0 || fds[1] < 0 || fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) {
        int err = errno;
        if (fds[0] >= 0) close(fds[0]);
        if (fds[1] >= 0) close(fds[1]);
        errno = err;
        return -1;
    }
    return 0;
}

// The runtime's end of a pipe: it doesn't block, and fits in select().
static int runtime_end(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (fd >= FD_SETSIZE) return EMFILE;
    return flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ? errno : 0;
}

static int spawn(char *const argv[], int *child, int *in, int *out) {
    char path[4096];
    int  err = find(argv[0], path, sizeof path), ip[2], op[2];
    if (err) return err;
    if (pipe_of(ip) < 0) return errno;
    if (pipe_of(op) < 0) {
        err = errno;
        close(ip[0]);
        close(ip[1]);
        return err;
    }
    if ((err = runtime_end(ip[1])) == 0 && (err = runtime_end(op[0])) == 0) {
        posix_spawn_file_actions_t fa;
        posix_spawnattr_t          at;
        sigset_t                   none, def;
        pid_t                      pid;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, ip[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&fa, op[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&fa, op[1], STDERR_FILENO);
        sigemptyset(&none);
        sigemptyset(&def);
        sigaddset(&def, SIGPIPE);               // ignored here, and an ignored signal stays so across exec
        posix_spawnattr_init(&at);
        posix_spawnattr_setsigmask(&at, &none);
        posix_spawnattr_setsigdefault(&at, &def);
        posix_spawnattr_setpgroup(&at, 0);
        posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
        err = posix_spawn(&pid, path, &fa, &at, argv, environ);
        posix_spawnattr_destroy(&at);
        posix_spawn_file_actions_destroy(&fa);
        *child = (int)pid;
    }
    close(ip[0]);
    close(op[1]);
    if (err) {
        close(ip[1]);
        close(op[0]);
        return err;
    }
    *in  = ip[1];
    *out = op[0];
    return 0;
}

int rt_io_spawn(char *const argv[], int *child, int *in, int *out) {
    int32_t got[3] = { 0, -1, -1 };
    int     err;
    if (mode == REPLAY) {
        err = (int)next(IO_SPAWN, got, sizeof got, NULL);
    } else {
        int c = 0, i = -1, o = -1;
        err = spawn(argv, &c, &i, &o);
        got[0] = c;
        got[1] = i;
        got[2] = o;
        done_with(IO_SPAWN, err, got, sizeof got);
    }
    *child = got[0];
    *in    = got[1];
    *out   = got[2];
    return err;
}

int rt_io_wait(int child, int *status, int nohang) {
    int32_t st = 0;
    int     r;
    if (mode == REPLAY) {
        r = (int)next(IO_WAIT, &st, sizeof st, NULL);
    } else {
        int s = 0;
        do r = waitpid(child, &s, nohang ? WNOHANG : 0); while (r < 0 && errno == EINTR);
        st = s;
        done_with(IO_WAIT, r, &st, sizeof st);
    }
    *status = st;
    return r;
}

int rt_io_kill(int pid) {
    if (mode == REPLAY) return (int)replayed(IO_KILL);
    return (int)done(IO_KILL, kill(pid, SIGKILL));
}

int rt_io_pipe(int fds[2]) {
    int32_t got[2] = { -1, -1 };
    int     r;
    if (mode == REPLAY) {
        r = (int)next(IO_PIPE, got, sizeof got, NULL);
    } else {
        int p[2] = { -1, -1 }, err = 0;
        r = pipe_of(p);
        if (r == 0 && ((err = runtime_end(p[0])) != 0 || (err = runtime_end(p[1])) != 0)) {
            close(p[0]);
            close(p[1]);
            errno = err;
            r     = -1;
        }
        got[0] = r == 0 ? p[0] : -1;
        got[1] = r == 0 ? p[1] : -1;
        done_with(IO_PIPE, r, got, sizeof got);
    }
    fds[0] = got[0];
    fds[1] = got[1];
    return r;
}
