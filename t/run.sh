#!/bin/sh
# Golden tests: compile each t/NNN-name.slight and examples/name.slight
# that has a .expected next to it (or the files given), run it, and diff
# what it prints against the .expected: stdout and stderr together, then
# "exit: N" if it exits with N other than 0. A line "; stdin: bytes" is
# what the test reads from stdin (keys, say), written
# with printf %b's escapes: \n, \r, \t, \\, and \0nnn in octal (\033 is
# ESC); without one, stdin is empty. A line "; args: words" gives the
# program its arguments, split at spaces.
# $TARGET is the architecture to compile for: aarch64 (the default) or
# x86_64. $RUN prefixes the binary: qemu when it isn't the machine's own
# architecture, empty when it is (or on macOS, where Rosetta 2 runs x86-64).
# $JOBS tests run at once (default: one per CPU). Each says ok or FAIL as
# it finishes; what each failure printed comes at the end, in order.
cd "$(dirname "$0")/.." || exit 2
TARGET=${TARGET-aarch64}
case "$TARGET-$(uname -m)" in
    aarch64-arm64|aarch64-aarch64|x86_64-x86_64) RUN=${RUN-} ;;
    x86_64-arm64)                                RUN=${RUN-} ;;
    *)                                           RUN=${RUN-qemu-$TARGET} ;;
esac
# A test that runs longer than $TIMEOUT seconds is killed, and fails.
# (macOS has no timeout(1), hence the watchdog.)
TIMEOUT=${TIMEOUT-60}
export TARGET RUN TIMEOUT

# The collector poisons what it frees, so a pointer it missed fails loudly.
export SLIGHT_POISON=1
# Time starts at 0 and moves only when nothing can run, straight to the
# next timer, so timer tests are exact and take no real time.
export SLIGHT_CLOCK=virtual

# The watchdog leaves a file to say it killed the test, since its own
# output can't go down the pipe to diff: diff would wait for it.
output() {
    rm -f "$1.killed"
    set -f                                  # $3 is split into arguments, not globbed
    $RUN "./$1" $3 <"$2" 2>&1 &
    set +f
    pid=$!
    ( sleep "$TIMEOUT" && : >"$1.killed" && kill "$pid" ) >/dev/null 2>&1 &
    watchdog=$!
    wait "$pid" 2>/dev/null
    status=$?
    kill "$watchdog" 2>/dev/null
    [ -f "$1.killed" ] && echo "killed after ${TIMEOUT}s"
    [ $status -eq 0 ] || echo "exit: $status"
}

# One test, run by xargs below: its status line, and what went wrong in
# $bin.fail.
if [ "$1" = "--one" ]; then
    src=$2
    name=$(basename "$src" .slight)
    bin=build/t/$TARGET/$name
    printf '%b' "$(sed -n 's/^; stdin: //p' "$src")" >"$bin.stdin"
    if ! node bin/slightc.ts --target "$TARGET" -o "$bin" "$src" >"$bin.fail" 2>&1; then
        echo "FAIL $name (compile)"
        exit 1
    elif output "$bin" "$bin.stdin" "$(sed -n 's/^; args: //p' "$src")" | diff -u "${src%.slight}.expected" - >"$bin.fail"; then
        rm -f "$bin.fail"
        echo "ok   $name"
    else
        echo "FAIL $name"
        exit 1
    fi
    exit 0
fi

if [ $# -eq 0 ]; then
    set --
    for src in t/*.slight examples/*.slight; do
        [ -f "${src%.slight}.expected" ] && set -- "$@" "$src"
    done
fi
mkdir -p "build/t/$TARGET"
JOBS=${JOBS-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

# Build the runtime first, so that the tests don't all build it at once.
node bin/slightc.ts --runtime --target "$TARGET" || exit 2
for src in "$@"; do
    rm -f "build/t/$TARGET/$(basename "$src" .slight).fail"
done
printf '%s\n' "$@" | xargs -n 1 -P "$JOBS" ./t/run.sh --one

fail=0
for src in "$@"; do
    name=$(basename "$src" .slight)
    if [ -f "build/t/$TARGET/$name.fail" ]; then
        [ $fail -eq 0 ] && echo
        echo "--- $name"
        cat "build/t/$TARGET/$name.fail"
        fail=$((fail + 1))
    fi
done
echo "$# tests, $fail failed"
[ $fail -eq 0 ]
