// t/live.py's delete race: swaps the tree's subdirectory, argv[1], with a
// link to a directory outside it, kept at argv[2], pointing to argv[3],
// back and forth, as fast as it can, till it's killed; and puts the link
// back whenever fs/delete-all has deleted it, leaving a directory there.
// An exchange is atomic, so a delete that finds sub by its path sees a
// directory one moment and the link the next (D275).

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#define EXCHANGE(a, b) renamex_np(a, b, RENAME_SWAP)
#else
#define EXCHANGE(a, b) renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE)
#endif

int main(int argc, char **argv) {
    (void)argc;
    char        aside[4096];
    long        n = 0;
    struct stat st;
    for (;;) {
        if (lstat(argv[2], &st) == 0 && !S_ISLNK(st.st_mode)) {
            snprintf(aside, sizeof aside, "%s.%ld", argv[2], n++);
            rename(argv[2], aside);
        }
        if (symlink(argv[3], argv[2]) != 0) {}
        EXCHANGE(argv[1], argv[2]);
        EXCHANGE(argv[1], argv[2]);
    }
}
