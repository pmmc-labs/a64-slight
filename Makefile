# make test     the compiler's unit tests, the runtime header check, then
#               the golden tests (t/run.sh), the record and replay checks
#               (t/replay.sh) and the live checks (t/live.py) for each of
#               $(TARGETS), and that the guide's code is the code tested
#               (t/guide.py)
# make check    type-check the compiler (needs `npm install` first)
# make bench    the benchmarks, against other languages (bench/run.py,
#               docs/BENCHMARKS.md); BENCH passes it options: BENCH=--quick
#
# There are two targets, aarch64 and x86_64. The golden tests run for
# aarch64 everywhere, and for x86_64 too on an x86-64 machine; on an M2,
# `make golden TARGETS=x86_64` runs them under Rosetta 2. A target that
# isn't the machine's own is cross-compiled with clang for Linux and run
# under qemu (needs clang, lld, gcc-aarch64-linux-gnu for the sysroot, and
# qemu-user). The golden tests run JOBS at a time (one per CPU by default).

ARCH := $(shell uname -m)

ifneq ($(filter arm64 aarch64,$(ARCH)),)
  CC ?= cc
  TARGETS ?= aarch64
else
  CC := clang --target=aarch64-linux-gnu
  TARGETS ?= aarch64 x86_64
endif

# The C compiler for x86-64, for the header check.
ifeq ($(ARCH),x86_64)
  CC_X86_64 := clang
else ifeq ($(shell uname -s),Darwin)
  CC_X86_64 := $(CC) -arch x86_64
else
  CC_X86_64 := clang --target=x86_64-linux-gnu
endif

sketch:
	node bin/slightc.ts -o "./sketch" "author/sketch.slight"

test: unit headers guide golden replay live

unit:
	node --test "compiler/tests/**/*.test.ts"

golden:
	@fail=0; for target in $(TARGETS); do \
		echo "# $$target"; TARGET=$$target ./t/run.sh || fail=1; \
	done; exit $$fail

replay:
	@fail=0; for target in $(TARGETS); do \
		echo "# $$target replay"; TARGET=$$target ./t/replay.sh || fail=1; \
	done; exit $$fail

# Every lisp block in docs/guide/ and README.md is in a golden test
guide:
	python3 t/guide.py

live:
	@fail=0; for target in $(TARGETS); do \
		echo "# $$target live"; TARGET=$$target python3 t/live.py || fail=1; \
	done; exit $$fail

# rt.h must not collide with the system headers (see t/headers.c)
headers:
	$(CC) -std=gnu11 -Wall -Wextra -fsyntax-only t/headers.c
ifneq ($(filter x86_64,$(TARGETS)),)
	$(CC_X86_64) -std=gnu11 -Wall -Wextra -fsyntax-only t/headers.c
endif

check:
	npx tsc --noEmit

bench:
	python3 bench/run.py $(BENCH)

clean:
	rm -rf build
	@if [ -f "./sketch" ]; then \
		rm "./sketch";      \
		rm "./sketch.S";    \
		rm -rf "./sketch.dSYM"; \
    fi

.PHONY: test unit guide golden replay live headers check bench clean
