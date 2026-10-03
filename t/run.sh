#!/bin/sh
# Golden tests: compile each t/NNN-name.slight (or the files given), run it,
# and diff what it prints against t/NNN-name.expected: stdout and stderr
# together, then "exit: N" if it exits with N other than 0.
# $RUN prefixes the binary: qemu-aarch64 when cross-compiling, empty when native.
cd "$(dirname "$0")/.." || exit 2
case "$(uname -m)" in
    arm64|aarch64) RUN=${RUN-} ;;
    *)             RUN=${RUN-qemu-aarch64} ;;
esac
[ $# -eq 0 ] && set -- t/*.slight
mkdir -p build/t

# A test that runs longer than $TIMEOUT seconds is killed, and fails.
# (macOS has no timeout(1), hence the watchdog.)
TIMEOUT=${TIMEOUT-60}
output() {
    $RUN "./$1" 2>&1 &
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
    bin=build/t/$name
    if ! node bin/slightc.ts -o "$bin" "$src"; then
        echo "FAIL $name (compile)"
        fail=1
    elif output "$bin" | diff -u "t/$name.expected" -; then
        echo "ok   $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit $fail
