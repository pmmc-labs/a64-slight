#!/bin/sh
# Runs every scenario that has a t/<name>.expected and diffs its output.
# $RUN prefixes the binary (qemu-aarch64 when cross-compiled, empty when native).
cd "$(dirname "$0")/.." || exit 2
fail=0
for exp in t/*.expected; do
    name=$(basename "$exp" .expected)
    if $RUN ./spike "$name" | diff -u "$exp" -; then
        echo "ok   $name"
    else
        echo "FAIL $name"
        fail=1
    fi
done
exit $fail
