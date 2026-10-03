// Compiled (never run) by `make test`: rt.h first, then a broad sweep of
// system headers, so a macro in rt.h that collides with anything in them
// fails the build here instead of somewhere confusing. (macOS's <stdlib.h>
// includes <sys/wait.h>, whose idtype_t enum once met a `#define P_PID`.)

#include "../rt.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#endif

int main(void) { return 0; }
