// io.h -- what the runtime asks of the world outside, through one place
// (replay.c, D170), so that a run can be recorded and played back.
//
// Each rt_io_ function is the system call it's named for, or close to it.
// With SLIGHT_RECORD=file, each makes the call and writes what it gave
// (the result, errno, and any bytes it filled in) to the file, in order.
// With SLIGHT_REPLAY=file, none makes the call: each takes what the next
// record says instead, so the run goes as the recorded one did, without
// the files, sockets, terminal or clock it had. Writes aren't made either;
// only stdout and stderr, which the runtime writes through stdio, are.
// Otherwise each is just the call.
//
// For C only (not shared with assembly), and included after the system
// headers, which it needs.

#ifndef RT_IO_H
#define RT_IO_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

// What a run's log keeps besides what the world gave it: the settings
// that change how a run goes, its arguments and its environment.
// rt_io_start opens the log SLIGHT_RECORD or SLIGHT_REPLAY names, if
// either does; recording, it writes these, and replaying, it reads them
// back, over what was given.
typedef struct {
    int      virtual_clock;
    uint64_t sched_seed;                // SLIGHT_SCHED, or 0
    uint64_t heap_max;                  // SLIGHT_HEAP_MAX, or the default: where a heap faults
} rt_io_settings_t;

void rt_io_start(int *argc, char ***argv, char ***env, rt_io_settings_t *settings);
int  rt_io_replaying(void);
int  rt_io_logging(void);               // recording or replaying

// getenv, for the variables that say where the runtime reads and writes
// (SLIGHT_RECORD, SLIGHT_REPLAY, SLIGHT_HOSTS, SLIGHT_RESOLV_CONF): NULL
// in a program that's set-user-ID or set-group-ID, which takes no such
// say from whoever runs it.
const char *rt_io_getenv(const char *name);

uint64_t rt_io_clock(void);             // the monotonic clock, in nanoseconds
int64_t  rt_io_realtime(void);          // the time of day: microseconds since 1970, UTC (D254)

// Where the virtual clock's time of day starts (D254): the seconds given
// (SLIGHT_EPOCH), or the time of day now, as microseconds, recorded either
// way, so a replay starts where its run did.
int64_t rt_io_epoch(const char *given);
int      rt_io_select(int n, fd_set *in, fd_set *out, struct timeval *timeout);
ssize_t  rt_io_read(int fd, void *buf, size_t len);
ssize_t  rt_io_write(int fd, const void *buf, size_t len);
int      rt_io_open(const char *path, int flags);
int      rt_io_close(int fd);

// Sockets, AF_INET or AF_UNIX. rt_io_setup makes one not block, not be
// inherited, and send at once (TCP_NODELAY when tcp): 0, or an errno.
// rt_io_listen_unix binds to a path and listens: 0, or an errno; a socket
// already there that refuses a connection is left from a program that has
// ended, and is replaced, and anything else there is EADDRINUSE (D231).
int     rt_io_socket(int family, int type);
int     rt_io_setup(int fd, int tcp);
int     rt_io_connect(int fd, const struct sockaddr *a, socklen_t len);
int     rt_io_listen(int fd, uint32_t addr, int port, int *got);   // bind, listen, and the port it got
int     rt_io_listen_unix(int fd, const char *path);
int     rt_io_accept(int fd, char *where, size_t size);   // and "address:port" of the peer, "" for a Unix one
int     rt_io_peer(int fd, int64_t who[3]);         // a Unix peer's uid, gid and pid, as at its connect: 0, or an errno

// path as a Unix socket's address: 0, or an errno. It must fit, with its
// NUL (104 bytes on macOS, 108 on Linux), and not be empty, which Linux
// would take for its abstract names, which have no permissions.
static inline int rt_unix_address(struct sockaddr_un *a, const char *path) {
    size_t len = strlen(path);
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    if (len == 0)                  return ENOENT;
    if (len >= sizeof a->sun_path) return ENAMETOOLONG;
    memcpy(a->sun_path, path, len + 1);
    return 0;
}
int     rt_io_socket_error(int fd);                 // SO_ERROR, or errno if that can't be had
ssize_t rt_io_send(int fd, const void *buf, size_t len);
ssize_t rt_io_recv(int fd, void *buf, size_t len);

// Commands (D185). rt_io_spawn runs argv[0], found as posix_spawnp would
// find it (but here, so that one that isn't there is ENOENT under qemu
// too), with its input and its output (stdout and stderr together) on
// pipes, in a process group of its own: 0, with the child, the input's
// end to write and the output's end to read (both not blocking, not
// inherited, and fit for select()), or an errno. rt_io_wait is waitpid,
// WNOHANG if nohang; rt_io_kill sends SIGKILL to a process, or a group
// (-pid), and isn't made in a replay. rt_io_pipe makes a pipe whose ends don't block
// and aren't inherited: 0, or -1 with errno set.
int rt_io_spawn(char *const argv[], int *child, int *in, int *out);
int rt_io_wait(int child, int *status, int nohang);
int rt_io_kill(int pid);
int rt_io_pipe(int fds[2]);

// Files beyond reading and writing (D254). rt_io_list gives a directory's
// names but . and .., each ended by a NUL, in *names (malloc'd, *len
// bytes): how many, or -1 with errno set. rt_io_stat is lstat, its kind (0
// a file, 1 a directory, 2 a link, 3 anything else), size, modified time
// in microseconds and mode's permission bits in got. rt_io_remove (a file
// or an empty directory), rt_io_rename and rt_io_mkdir (mode 0777, less
// the umask) are the calls, and in a replay aren't made, as a write isn't.
int64_t rt_io_list(const char *path, char **names, uint32_t *len);
int     rt_io_stat(const char *path, int64_t got[4]);
int     rt_io_remove(const char *path);
int     rt_io_rename(const char *from, const char *to);
int     rt_io_mkdir(const char *path);

// A text file read a line at a time (the hosts file, resolv.conf).
FILE *rt_io_fopen(const char *path);
char *rt_io_fgets(char *line, int size, FILE *f);
void  rt_io_fclose(FILE *f);

// Random bits, for what has to differ between runs and not be guessed (a
// lookup's query ids).
uint32_t rt_io_seed(void);

// len bytes from the system's entropy, for keys and tokens (D255).
void rt_io_random_bytes(void *buf, size_t len);

// The run's seed for its random numbers (D222): the number given, if one
// is (SLIGHT_SEED), or else the system's entropy. Recorded either way, so
// a replay has the same, whatever it's given.
uint64_t rt_io_random_seed(const char *given);

// The terminal's size: 0, or -1 if stdout isn't one.
int rt_io_winsize(int *rows, int *cols);

#endif
