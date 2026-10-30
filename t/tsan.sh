#!/bin/sh
# ThreadSanitizer over what runs on more than one island (D256): the
# island golden tests (t/run.sh, so their output must still be what's
# expected, a race's report being more of it), then the every-island
# benchmarks and t/replay/islands.slight, which must say nothing. For the
# machine's own target alone, built with SLIGHT_CC as the compiler with
# -fsanitize=thread, which Linux can't link statically. Not part of make
# test, since it needs clang's sanitizer runtime (Xcode's has it; on
# Ubuntu, apt-get install libclang-rt-18-dev).
cd "$(dirname "$0")/.." || exit 2
case "$(uname -s)-$(uname -m)" in
    Darwin-*)        TARGET=aarch64 CC=cc ;;
    *-aarch64)       TARGET=aarch64 CC=clang ;;
    *)               TARGET=x86_64  CC=clang ;;
esac
SLIGHT_CC="$CC -fsanitize=thread"
RUN=
export TARGET SLIGHT_CC RUN
dir=build/t/$TARGET/tsan
mkdir -p "$dir"

fail=0
./t/run.sh t/32[4-9]-*.slight t/33[0-3]-*.slight || fail=1

# n: the size each program is run at, small, since TSan is slow.
for prog in bench/parallel-fib/parallel-fib-mc.slight:20 bench/fan-in/fan-in-mc.slight:200000 \
            bench/spectral-norm/spectral-norm-mc.slight:200 t/replay/islands.slight:; do
    src=${prog%:*}
    n=${prog##*:}
    name=$(basename "$src" .slight)
    if ! node bin/slightc.ts --target "$TARGET" -o "$dir/$name" "$src" >"$dir/$name.compile" 2>&1; then
        cat "$dir/$name.compile"
        echo "FAIL $name (compile)"
        fail=1
        continue
    fi
    SLIGHT_ISLANDS=4 "$dir/$name" $n >"$dir/$name.out" 2>&1
    if grep -q 'ThreadSanitizer' "$dir/$name.out"; then
        cat "$dir/$name.out"
        echo "FAIL $name"
        fail=1
    else
        echo "ok   $name"
    fi
done
[ $fail -eq 0 ] && echo "tsan: 0 failed" || echo "tsan: failed"
exit $fail
