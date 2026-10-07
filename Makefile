# make test     the compiler's unit tests, the runtime header check, then
#               the golden tests (t/run.sh)
# make check    type-check the compiler (needs `npm install` first)
#
# On an arm64 machine everything runs natively. Anywhere else, binaries are
# cross-compiled with clang for AArch64 Linux and run under qemu-aarch64
# (needs clang, lld, gcc-aarch64-linux-gnu for the sysroot, and qemu-user).

ARCH := $(shell uname -m)

ifneq ($(filter arm64 aarch64,$(ARCH)),)
  CC ?= cc
else
  CC := clang --target=aarch64-linux-gnu
endif

sketch:
	node bin/slightc.ts -o "./sketch" "author/sketch.slight"

test: unit headers golden

unit:
	node --test "compiler/tests/**/*.test.ts"

golden:
	./t/run.sh

# rt.h must not collide with the system headers (see t/headers.c)
headers:
	$(CC) -std=gnu11 -Wall -Wextra -fsyntax-only t/headers.c

check:
	npx tsc --noEmit

clean:
	rm -rf build
	@if [ -f "./sketch" ]; then \
		rm "./sketch";      \
		rm "./sketch.S";    \
		rm "./sketch.dSYM"; \
    fi

.PHONY: test unit golden headers check clean
