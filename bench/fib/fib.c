#include <stdio.h>
#include <stdlib.h>

static long fib(long n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(int argc, char **argv) {
    printf("%ld\n", fib(atol(argv[1])));
    return 0;
}
