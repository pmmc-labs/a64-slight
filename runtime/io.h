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

#include <stdint.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

// What a run's log keeps besides what the world gave it: the settings
// that change how a run goes, and its arguments. rt_io_start opens the
// log SLIGHT_RECORD or SLIGHT_REPLAY names, if either does; recording, it
// writes these, and replaying, it reads them back, over what was given.
typedef struct {
    int      virtual_clock;
    uint64_t sched_seed;                // SLIGHT_SCHED, or 0
} rt_io_settings_t;

void rt_io_start(int *argc, char ***argv, rt_io_settings_t *settings);
int  rt_io_replaying(void);

uint64_t rt_io_clock(void);             // the monotonic clock, in nanoseconds
int      rt_io_select(int n, fd_set *in, fd_set *out, struct timeval *timeout);
ssize_t  rt_io_read(int fd, void *buf, size_t len);
ssize_t  rt_io_write(int fd, const void *buf, size_t len);
int      rt_io_open(const char *path, int flags);
int      rt_io_close(int fd);

// Sockets. rt_io_setup makes one not block, not be inherited, and send
// at once (TCP_NODELAY when tcp): 0, or an errno.
int     rt_io_socket(int type);
int     rt_io_setup(int fd, int tcp);
int     rt_io_connect(int fd, const struct sockaddr *a, socklen_t len);
int     rt_io_listen(int fd, int port, int *got);   // bind, listen, and the port it got
int     rt_io_accept(int fd, char *where, size_t size);   // and "address:port" of the peer
int     rt_io_socket_error(int fd);                 // SO_ERROR, or errno if that can't be had
ssize_t rt_io_send(int fd, const void *buf, size_t len);
ssize_t rt_io_recv(int fd, void *buf, size_t len);

// A text file read a line at a time (the hosts file, resolv.conf).
FILE *rt_io_fopen(const char *path);
char *rt_io_fgets(char *line, int size, FILE *f);
void  rt_io_fclose(FILE *f);

// A seed for what has to differ between runs (a lookup's query ids).
uint32_t rt_io_seed(void);

// The terminal's size: 0, or -1 if stdout isn't one.
int rt_io_winsize(int *rows, int *cols);

#endif
