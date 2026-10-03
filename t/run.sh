#!/bin/sh
# Golden tests: compile each t/NNN-name.slight (or the files given), run it,
# and diff its stdout against t/NNN-name.expected.
# $RUN prefixes the binary: qemu-aarch64 when cross-compiling, empty when native.
cd "$(dirname "$0")/.." || exit 2
case "$(uname -m)" in
    arm64|aarch64) RUN=${RUN-} ;;
    *)             RUN=${RUN-qemu-aarch64} ;;
esac
[ $# -eq 0 ] && set -- t/*.slight
mkdir -p build/t
fail=0
for src in "$@"; do
    name=$(basename "$src" .slight)
    bin=build/t/$name
    if ! node bin/slightc.ts -o "$bin" "$src"; then
        echo "FAIL $name (compile)"
        fail=1
    elif $RUN "./$bin" | diff -u "t/$name.expected" -; then
        echo "ok   $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit $fail
