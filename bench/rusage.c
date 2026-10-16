// rusage report cmd args...: runs cmd, and writes its wall time, CPU time
// (user and system) and peak resident set to the file report, for
// bench/run.py; it exits as cmd did. On Linux a process forked from Python
// starts with Python's memory counted in its peak, even after exec, so the
// peak is measured from this small process instead.
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double since(const struct timespec *t) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - t->tv_sec) + (now.tv_nsec - t->tv_nsec) / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: rusage report cmd args...\n", stderr);
        return 2;
    }
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 2;
    }
    if (pid == 0) {
        execvp(argv[2], argv + 2);
        perror(argv[2]);
        _exit(127);
    }
    int           status;
    struct rusage usage;
    if (wait4(pid, &status, 0, &usage) < 0) {
        perror("wait4");
        return 2;
    }
    double wall   = since(&start);
    FILE  *report = fopen(argv[1], "w");
    if (!report) {
        perror(argv[1]);
        return 2;
    }
    // ru_maxrss is in kilobytes on Linux, bytes on macOS.
#ifdef __APPLE__
    long rss = usage.ru_maxrss;
#else
    long rss = usage.ru_maxrss * 1024;
#endif
    fprintf(report, "%.6f %.6f %ld\n", wall,
            usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6,
            rss);
    fclose(report);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
