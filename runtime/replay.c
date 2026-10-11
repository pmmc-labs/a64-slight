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
// The record is a line saying what it is, the settings and arguments
// (rt_io_settings_t), then an entry for each call, in order: what kind of
// call, its result, errno, and the bytes it filled in. Numbers are in the
// machine's own order: a record is for replaying where it was made. It's
// flushed after every entry, so a run that crashes leaves a record of all
// it did up to the crash.

#include "rt.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "io.h"

#define MAGIC "slight record 1\n"

enum { OFF, RECORD, REPLAY };

enum {
    IO_CLOCK = 1, IO_SELECT, IO_READ, IO_WRITE, IO_OPEN, IO_CLOSE, IO_SOCKET, IO_SETUP, IO_CONNECT,
    IO_LISTEN, IO_ACCEPT, IO_SOCKET_ERROR, IO_SEND, IO_RECV, IO_FOPEN, IO_FGETS, IO_SEED, IO_WINSIZE,
};

static const char *const names[] = {
    [IO_CLOCK] = "the clock", [IO_SELECT] = "select", [IO_READ] = "a read", [IO_WRITE] = "a write",
    [IO_OPEN] = "an open", [IO_CLOSE] = "a close", [IO_SOCKET] = "a socket", [IO_SETUP] = "a socket's setup",
    [IO_CONNECT] = "a connect", [IO_LISTEN] = "a listen", [IO_ACCEPT] = "an accept",
    [IO_SOCKET_ERROR] = "a socket's error", [IO_SEND] = "a send", [IO_RECV] = "a receive",
    [IO_FOPEN] = "a text file", [IO_FGETS] = "a line of a text file", [IO_SEED] = "a seed",
    [IO_WINSIZE] = "the screen's size",
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

// The next entry, which must be of this kind, its bytes put at data (at
// most cap of them; *len says how many), and errno as it was.
static int64_t next(int kind, void *data, size_t cap, uint32_t *len) {
    char    say[160];
    uint8_t k;
    int64_t result;
    int32_t err;
    uint32_t n;
    if (!get(&k, 1)) {
        snprintf(say, sizeof say, "the record ends at entry %" PRIu64 ", where the run asks for %s", count + 1, name(kind));
        stop(say);
    }
    if (k != kind) {
        snprintf(say, sizeof say, "the run goes another way at entry %" PRIu64 ": it asks for %s, and the record has %s",
                 count + 1, name(kind), name(k));
        stop(say);
    }
    if (!get(&result, sizeof result) || !get(&err, sizeof err) || !get(&n, sizeof n) || n > cap || (n && !get(data, n))) {
        snprintf(say, sizeof say, "the record is cut short or spoilt at entry %" PRIu64 " (%s)", count + 1, name(kind));
        stop(say);
    }
    if (len) *len = n;
    count++;
    errno = err;
    return result;
}

static int64_t replayed(int kind) {
    return next(kind, NULL, 0, NULL);
}

// --- starting -----------------------------------------------------------------

void rt_io_start(int *argc, char ***argv, rt_io_settings_t *s) {
    const char *rec = getenv("SLIGHT_RECORD"), *play = getenv("SLIGHT_REPLAY");
    if (rec && play) {
        fputs("slight: SLIGHT_RECORD and SLIGHT_REPLAY can't both be set\n", stderr);
        exit(2);
    }
    if (!rec && !play) return;
    record = fopen(rec ? rec : play, rec ? "wb" : "rb");
    if (!record) {
        fprintf(stderr, "slight: can't %s %s: %s\n", rec ? "record to" : "replay", rec ? rec : play, strerror(errno));
        exit(2);
    }
    mode = rec ? RECORD : REPLAY;
    uint8_t  clock;
    uint32_t n;
    if (mode == RECORD) {
        put(MAGIC, strlen(MAGIC));
        clock = (uint8_t)s->virtual_clock;
        put(&clock, 1);
        put(&s->sched_seed, sizeof s->sched_seed);
        n = (uint32_t)*argc;
        put(&n, sizeof n);
        for (int i = 0; i < *argc; i++) {
            uint32_t len = (uint32_t)strlen((*argv)[i]);
            put(&len, sizeof len);
            put((*argv)[i], len);
        }
        fflush(record);
        return;
    }
    char magic[sizeof MAGIC - 1];
    if (!get(magic, sizeof magic) || memcmp(magic, MAGIC, sizeof magic) != 0) stop("that isn't a record of a run");
    if (!get(&clock, 1) || !get(&s->sched_seed, sizeof s->sched_seed) || !get(&n, sizeof n)) stop("the record is cut short");
    s->virtual_clock = clock;
    char **args = rt_must(calloc((size_t)n + 1, sizeof *args));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len;
        if (!get(&len, sizeof len)) stop("the record is cut short");
        args[i] = rt_must(calloc(len + 1, 1));
        if (!get(args[i], len)) stop("the record is cut short");
    }
    *argc = (int)n;
    *argv = args;
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

int rt_io_socket(int type) {
    if (mode == REPLAY) return (int)replayed(IO_SOCKET);
    return (int)done(IO_SOCKET, socket(AF_INET, type, 0));
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

// Binds fd to port on every interface, and listens: 0, with the port it
// got in *got (port 0 lets the system pick), or an errno.
int rt_io_listen(int fd, int port, int *got) {
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
    a.sin_addr.s_addr = htonl(INADDR_ANY);
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

// A connection from fd's queue: its descriptor, with the peer as
// "address:port" in where; or -1, with errno set.
int rt_io_accept(int fd, char *where, size_t size) {
    if (mode == REPLAY) {
        uint32_t len;
        int      r = (int)next(IO_ACCEPT, where, size - 1, &len);
        where[r >= 0 ? len : 0] = '\0';
        return r;
    }
    struct sockaddr_in a;
    socklen_t          alen = sizeof a;
    int                r    = accept(fd, (struct sockaddr *)&a, &alen);
    where[0] = '\0';
    if (r >= 0) {
        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a.sin_addr, addr, sizeof addr);
        snprintf(where, size, "%s:%d", addr, ntohs(a.sin_port));
    }
    return (int)done_with(IO_ACCEPT, r, where, strlen(where));
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

uint32_t rt_io_seed(void) {
    if (mode == REPLAY) return (uint32_t)replayed(IO_SEED);
    static char here;
    return (uint32_t)done(IO_SEED, (uint32_t)time(NULL) ^ (uint32_t)getpid() << 16 ^ (uint32_t)(uintptr_t)&here);
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
