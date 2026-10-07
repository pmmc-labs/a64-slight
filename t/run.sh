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
cd "$(dirname "$0")/.." || exit 2
TARGET=${TARGET-aarch64}
case "$TARGET-$(uname -m)" in
    aarch64-arm64|aarch64-aarch64|x86_64-x86_64) RUN=${RUN-} ;;
    x86_64-arm64)                                RUN=${RUN-} ;;
    *)                                           RUN=${RUN-qemu-$TARGET} ;;
esac
if [ $# -eq 0 ]; then
    set --
    for src in t/*.slight examples/*.slight; do
        [ -f "${src%.slight}.expected" ] && set -- "$@" "$src"
    done
fi
mkdir -p "build/t/$TARGET"

# A test that runs longer than $TIMEOUT seconds is killed, and fails.
# (macOS has no timeout(1), hence the watchdog.)
TIMEOUT=${TIMEOUT-60}

# The collector poisons what it frees, so a pointer it missed fails loudly.
export SLIGHT_POISON=1
# Time starts at 0 and moves only when nothing can run, straight to the
# next timer, so timer tests are exact and take no real time.
export SLIGHT_CLOCK=virtual
output() {
    set -f                                  # $3 is split into arguments, not globbed
    $RUN "./$1" $3 <"$2" 2>&1 &
    set +f
    pid=$!
    ( sleep "$TIMEOUT" && kill "$pid" && echo "killed after ${TIMEOUT}s" >&2 ) >/dev/null 2>&1 &
    watchdog=$!
    wait "$pid"
    status=$?
    kill "$watchdog" 2>/dev/null
    [ $status -eq 0 ] || echo "exit: $status"
}

fail=0
for src in "$@"; do
    name=$(basename "$src" .slight)
    bin=build/t/$TARGET/$name
    printf '%b' "$(sed -n 's/^; stdin: //p' "$src")" >"$bin.stdin"
    if ! node bin/slightc.ts --target "$TARGET" -o "$bin" "$src"; then
        echo "FAIL $name (compile)"
        fail=1
    elif output "$bin" "$bin.stdin" "$(sed -n 's/^; args: //p' "$src")" | diff -u "${src%.slight}.expected" -; then
        echo "ok   $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit $fail
