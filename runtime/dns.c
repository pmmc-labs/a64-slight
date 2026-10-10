// dns.c -- finding a host's IPv4 address without blocking (D153, D165). A
// dotted address is used as it is; a name in the hosts file gets the
// address there; any other name is asked of the name servers in
// resolv.conf, over UDP (TCP when the answer doesn't fit in a datagram, or
// with "options use-vc", as glibc has it), and the scheduler waits for each
// answer in select(), as for any socket, with a timer for when to give up
// on it. Both files are read at each lookup, so a change to them counts at
// once. SLIGHT_HOSTS and SLIGHT_RESOLV_CONF name others to read instead,
// for tests, which can also give a name server a port: 127.0.0.1:5353.

#include "rt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_SERVERS 3                   // as resolv.conf's MAXNS
#define MAX_SEARCH  6
#define MAX_NAME    254                 // a name in a query, dotted, with its NUL

struct rt_lookup {
    char               names[MAX_SEARCH + 1][MAX_NAME];    // what to ask about, in order
    int                nnames;
    struct sockaddr_in servers[MAX_SERVERS];
    int                nservers, attempts, use_vc;
    uint64_t           timeout;                            // ms to wait for each answer
    int                name, server, attempt;              // the query now
    int                fd, tcp, connecting;                // its socket
    uint16_t           id;
    unsigned char      query[2 + 12 + MAX_NAME + 1 + 4];  // with TCP's 2-byte length in front
    size_t             query_len;
    unsigned char     *answer;                             // TCP's, as it comes
    size_t             answer_len, answer_cap;
    int                answered;                           // a server has said a name doesn't exist
};

// --- the files ----------------------------------------------------------------

static FILE *open_file(const char *env, const char *path) {
    const char *p = getenv(env);
    return fopen(p ? p : path, "r");
}

// The next word in *p, NUL-terminated in place, or NULL at the end of the
// line or a comment.
static char *word(char **p) {
    char *s = *p + strspn(*p, " \t\r\n");
    if (*s == '\0' || *s == '#' || *s == ';') return NULL;
    char *e = s + strcspn(s, " \t\r\n");
    if (*e) *e++ = '\0';
    *p = e;
    return s;
}

// name's address in the hosts file: 1, or 0 if it isn't there. Only IPv4
// addresses count (D139).
static int in_hosts(const char *name, uint32_t *addr) {
    FILE *f = open_file("SLIGHT_HOSTS", "/etc/hosts");
    char  line[1024];
    int   found = 0;
    while (f && !found && fgets(line, sizeof line, f)) {
        char          *p = line, *a = word(&p), *n;
        struct in_addr in;
        if (!a || inet_pton(AF_INET, a, &in) != 1) continue;
        while (!found && (n = word(&p)) != NULL) found = strcasecmp(n, name) == 0;
        if (found) *addr = in.s_addr;
    }
    if (f) fclose(f);
    return found;
}

// The servers, search domains and options in resolv.conf, and from them the
// names to ask about: with fewer dots than ndots, name in each search domain
// first, then as it is; otherwise the other way round. A name that ends in a
// dot is only itself.
static void read_resolv_conf(rt_lookup_t *l, const char *name) {
    char search[MAX_SEARCH][MAX_NAME];
    int  nsearch = 0, ndots = 1;
    l->attempts  = 2;
    l->timeout   = 5000;
    FILE *f = open_file("SLIGHT_RESOLV_CONF", "/etc/resolv.conf");
    char  line[1024];
    while (f && fgets(line, sizeof line, f)) {
        char *p = line, *key = word(&p), *v;
        if (!key) continue;
        if (strcmp(key, "nameserver") == 0 && (v = word(&p)) && l->nservers < MAX_SERVERS) {
            char *colon = strchr(v, ':');
            int   port  = colon ? atoi(colon + 1) : 53;
            if (colon) *colon = '\0';
            struct sockaddr_in *s = &l->servers[l->nservers];
            memset(s, 0, sizeof *s);
            s->sin_family = AF_INET;
            s->sin_port   = htons((uint16_t)port);
            if (inet_pton(AF_INET, v, &s->sin_addr) == 1 && port > 0 && port < 65536) l->nservers++;
        } else if (strcmp(key, "search") == 0 || strcmp(key, "domain") == 0) {
            for (nsearch = 0; nsearch < MAX_SEARCH && (v = word(&p)); nsearch++) snprintf(search[nsearch], MAX_NAME, "%s", v);
        } else if (strcmp(key, "options") == 0) {
            while ((v = word(&p)) != NULL) {
                if (strncmp(v, "ndots:", 6) == 0)         ndots       = atoi(v + 6);
                else if (strncmp(v, "timeout:", 8) == 0)  l->timeout  = (uint64_t)atoi(v + 8) * 1000;
                else if (strncmp(v, "attempts:", 9) == 0) l->attempts = atoi(v + 9);
                else if (strcmp(v, "use-vc") == 0)        l->use_vc   = 1;
            }
        }
    }
    if (f) fclose(f);
    if (l->nservers == 0) {                     // none: the local one, as glibc and Go have it
        struct sockaddr_in *s = &l->servers[l->nservers++];
        memset(s, 0, sizeof *s);
        s->sin_family      = AF_INET;
        s->sin_port        = htons(53);
        s->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (l->attempts < 1) l->attempts = 1;
    if (l->timeout < 1000) l->timeout = 1000;

    size_t len  = strlen(name);
    int    dots = 0;
    for (size_t i = 0; i < len; i++) dots += name[i] == '.';
    if (len > 0 && name[len - 1] == '.') {
        snprintf(l->names[l->nnames++], MAX_NAME, "%.*s", (int)len - 1, name);
        return;
    }
    if (dots >= ndots) snprintf(l->names[l->nnames++], MAX_NAME, "%s", name);
    for (int i = 0; i < nsearch; i++) {
        if (len + 1 + strlen(search[i]) < MAX_NAME) snprintf(l->names[l->nnames++], MAX_NAME, "%s.%s", name, search[i]);
    }
    if (dots < ndots) snprintf(l->names[l->nnames++], MAX_NAME, "%s", name);
}

// --- queries ------------------------------------------------------------------

static uint16_t next_id(void) {
    static uint32_t x;
    if (!x) x = (uint32_t)time(NULL) ^ (uint32_t)getpid() << 16 ^ (uint32_t)(uintptr_t)&x;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (uint16_t)x;
}

// The query for name's A record, with a length in front for TCP: 0, or -1
// if name can't be asked about (a label empty, or longer than 63).
static int make_query(rt_lookup_t *l, const char *name) {
    unsigned char *q = l->query + 2, *p = q + 12;
    l->id = next_id();
    memset(q, 0, 12);
    q[0] = (unsigned char)(l->id >> 8);
    q[1] = (unsigned char)l->id;
    q[2] = 0x01;                                // recursion desired
    q[5] = 1;                                   // one question
    for (const char *s = name; *s; ) {
        size_t n = strcspn(s, ".");
        if (n == 0 || n > 63) return -1;
        *p++ = (unsigned char)n;
        memcpy(p, s, n);
        p += n;
        s += n + (s[n] == '.');
    }
    *p++ = 0;
    memcpy(p, "\0\1\0\1", 4);                   // type A, class IN
    p += 4;
    l->query_len = (size_t)(p - q);
    l->query[0]  = (unsigned char)(l->query_len >> 8);
    l->query[1]  = (unsigned char)l->query_len;
    return 0;
}

static void close_query(rt_lookup_t *l) {
    if (l->fd >= 0) close(l->fd);
    l->fd         = -1;
    l->answer_len = 0;
}

// Opens the query's socket to the server, and sends the query if it can
// yet: 0, or -1 if the socket failed, and the server is to be skipped.
static int send_query(rt_lookup_t *l) {
    close_query(l);
    int fd = socket(AF_INET, l->tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    l->fd = fd;
    int flags = fcntl(fd, F_GETFL);
    if (fd >= FD_SETSIZE || flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return -1;
    const struct sockaddr_in *s = &l->servers[l->server];
    if (connect(fd, (const struct sockaddr *)s, sizeof *s) < 0 && errno != EINPROGRESS) return -1;
    l->connecting = l->tcp;
    if (!l->tcp && send(fd, l->query + 2, l->query_len, 0) < 0) return -1;
    return 0;
}

// --- the steps ----------------------------------------------------------------
//
// Each step returns what's become of the lookup: RT_LOOKUP_ASKED, a query
// has gone, and its timer starts now; RT_LOOKUP_WAIT, the same query waits
// on; RT_LOOKUP_DONE, with the address; or RT_LOOKUP_NOTFOUND, every name
// asked about doesn't exist (or has no IPv4 address), or RT_LOOKUP_TIMEOUT,
// a name got no answer from any server, attempts times round.

static int fail_with(rt_lookup_t *l, int result) {
    close_query(l);
    return result;
}

// The query got no answer from this server: on to the next, then round
// again.
static int next_server(rt_lookup_t *l) {
    for (;;) {
        l->tcp = l->use_vc;
        if (++l->server == l->nservers) {
            l->server = 0;
            if (++l->attempt == l->attempts) return fail_with(l, RT_LOOKUP_TIMEOUT);
        }
        if (send_query(l) == 0) return RT_LOOKUP_ASKED;
    }
}

// On to the next name to ask about, from the first server.
static int next_name(rt_lookup_t *l) {
    for (;;) {
        if (++l->name >= l->nnames) return fail_with(l, l->answered ? RT_LOOKUP_NOTFOUND : RT_LOOKUP_TIMEOUT);
        l->server  = 0;
        l->attempt = 0;
        l->tcp     = l->use_vc;
        if (make_query(l, l->names[l->name]) < 0) {
            l->answered = 1;                    // a name that can't exist
            continue;
        }
        return send_query(l) == 0 ? RT_LOOKUP_ASKED : next_server(l);
    }
}

// Skips a name in a message from at: where it ends, or 0 if it runs off.
static size_t skip_name(const unsigned char *m, size_t len, size_t at) {
    while (at < len) {
        if (m[at] == 0)           return at + 1;
        if ((m[at] & 0xc0) == 0xc0) return at + 2 <= len ? at + 2 : 0;    // a pointer ends it
        at += 1 + m[at];
    }
    return 0;
}

// What the answer m says: the address in its first A record, or the next
// name, server or transport to try.
static int take_answer(rt_lookup_t *l, const unsigned char *m, size_t len, uint32_t *addr) {
    if (len < 12 || m[0] != (unsigned char)(l->id >> 8) || m[1] != (unsigned char)l->id || !(m[2] & 0x80)) {
        return l->tcp ? next_server(l) : RT_LOOKUP_WAIT;   // not this query's: a stray datagram, or a broken server
    }
    if ((m[2] & 0x02) && !l->tcp) {             // truncated: ask the same server over TCP
        l->tcp = 1;
        return send_query(l) == 0 ? RT_LOOKUP_ASKED : next_server(l);
    }
    int rcode = m[3] & 0x0f;
    if (rcode == 3) {                           // no such name
        l->answered = 1;
        return next_name(l);
    }
    if (rcode != 0) return next_server(l);      // the server failed, or refused
    size_t at = 12, end;
    int    qd = m[4] << 8 | m[5], an = m[6] << 8 | m[7];
    for (int i = 0; i < qd && at; i++) at = (end = skip_name(m, len, at)) ? end + 4 : 0;
    // the first A record, wherever a CNAME before it led
    for (int i = 0; i < an && at && at + 10 <= len; i++) {
        at = skip_name(m, len, at);
        if (!at || at + 10 > len) break;
        int    type = m[at] << 8 | m[at + 1], class = m[at + 2] << 8 | m[at + 3];
        size_t rdlen = (size_t)(m[at + 8] << 8 | m[at + 9]);
        at += 10;
        if (at + rdlen > len) break;
        if (type == 1 && class == 1 && rdlen == 4) {
            memcpy(addr, m + at, 4);
            return fail_with(l, RT_LOOKUP_DONE);
        }
        at += rdlen;
    }
    l->answered = 1;                            // the name is there, but has no IPv4 address
    return next_name(l);
}

int rt_lookup_begin(rt_lookup_t **out, const char *host, uint32_t *addr) {
    struct in_addr in;
    *out = NULL;
    if (inet_pton(AF_INET, host, &in) == 1) {
        *addr = in.s_addr;
        return RT_LOOKUP_DONE;
    }
    if (in_hosts(host, addr)) return RT_LOOKUP_DONE;
    if (strcasecmp(host, "localhost") == 0) {  // as RFC 6761 has it, if the hosts file doesn't say
        *addr = htonl(INADDR_LOOPBACK);
        return RT_LOOKUP_DONE;
    }
    if (strlen(host) >= MAX_NAME - 1) return RT_LOOKUP_NOTFOUND;
    rt_lookup_t *l = calloc(1, sizeof *l);
    if (!l) abort();
    l->fd   = -1;
    l->name = -1;
    read_resolv_conf(l, host);
    int r = next_name(l);
    if (r == RT_LOOKUP_ASKED) *out = l;
    else                     rt_lookup_free(l);
    return r;
}

int rt_lookup_fd(const rt_lookup_t *l, int *write) {
    *write = l->connecting;
    return l->fd;
}

uint64_t rt_lookup_timeout(const rt_lookup_t *l) {
    return l->timeout;
}

int rt_lookup_ready(rt_lookup_t *l, int timed_out, uint32_t *addr) {
    if (timed_out) return next_server(l);
    if (!l->tcp) {
        unsigned char m[512];
        ssize_t       n = recv(l->fd, m, sizeof m, 0);
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? RT_LOOKUP_WAIT : next_server(l);
        return take_answer(l, m, (size_t)n, addr);
    }
    if (l->connecting) {                        // connected, or not: then the query goes
        int       err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(l->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) return next_server(l);
        l->connecting = 0;
        if (send(l->fd, l->query, l->query_len + 2, 0) != (ssize_t)(l->query_len + 2)) return next_server(l);
        return RT_LOOKUP_WAIT;
    }
    if (l->answer_len == l->answer_cap) {
        l->answer_cap = l->answer_cap ? l->answer_cap * 2 : 1024;
        l->answer     = realloc(l->answer, l->answer_cap);
        if (!l->answer) abort();
    }
    ssize_t n = recv(l->fd, l->answer + l->answer_len, l->answer_cap - l->answer_len, 0);
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? RT_LOOKUP_WAIT : next_server(l);
    if (n == 0) return next_server(l);         // closed before it answered
    l->answer_len += (size_t)n;
    if (l->answer_len < 2) return RT_LOOKUP_WAIT;
    size_t want = (size_t)(l->answer[0] << 8 | l->answer[1]);
    if (l->answer_len < 2 + want) return RT_LOOKUP_WAIT;
    return take_answer(l, l->answer + 2, want, addr);
}

void rt_lookup_free(rt_lookup_t *l) {
    if (!l) return;
    close_query(l);
    free(l->answer);
    free(l);
}
