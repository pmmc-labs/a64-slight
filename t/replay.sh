#!/bin/sh
# Record and replay, and the seeded schedule (D170): what one run of a
# golden test can't show, since each check takes several runs. For $TARGET
# and $RUN, as t/run.sh has them.
#
# Each of t/replay/world.slight, net.slight and exec.slight runs once
# with SLIGHT_RECORD, on the real clock, with what it reads from outside
# (keys, a file, a hosts file, sockets, commands); then again with
# SLIGHT_REPLAY, with other arguments, no stdin and none of those files.
# The replay must print what the recorded run did, exit as it did, and
# leave alone the file the recorded run (or its command) wrote. Nothing here depends on how long anything takes: a
# replay is compared with its own recording, never with a fixed output.
#
# t/replay/order.slight runs first in, first out, and then under
# SLIGHT_SCHED with 20 seeds: each must print the five workers in some
# order, the same each time for the same seed, and the seeds must not
# all give one order. A seeded run must replay as it went.
cd "$(dirname "$0")/.." || exit 2
TARGET=${TARGET-aarch64}
case "$TARGET-$(uname -m)" in
    aarch64-arm64|aarch64-aarch64|x86_64-x86_64) RUN=${RUN-} ;;
    x86_64-arm64)                                RUN=${RUN-} ;;
    *)                                           RUN=${RUN-qemu-$TARGET} ;;
esac
unset SLIGHT_CLOCK SLIGHT_SCHED SLIGHT_RECORD SLIGHT_REPLAY
dir=build/t/$TARGET/replay
rm -rf "$dir"
mkdir -p "$dir"

fail=0
check() {                               # check what ok-or-not
    if [ "$2" = ok ]; then
        echo "ok   $1"
    else
        echo "FAIL $1"
        fail=$((fail + 1))
    fi
}

for name in world net exec order; do
    if ! node bin/slightc.ts --target "$TARGET" -o "$dir/$name" "t/replay/$name.slight" >"$dir/$name.compile" 2>&1; then
        cat "$dir/$name.compile"
        check "$name (compile)" no
    fi
done
[ $fail -eq 0 ] || exit 1

# run [NAME=value ...] prog [args ...]: runs a program, with those
# variables set for it alone, and prints what it printed, then its exit
# status. The variables go through env, never as assignments before a
# call to run: after a call to a function, a shell may keep them set
# (macOS's sh, bash 3.2 in POSIX mode, does), and every run after it
# would have them.
run() {
    vars=
    while :; do
        case $1 in
            *=*) vars="$vars $1"; shift ;;
            *)   break ;;
        esac
    done
    prog=$1
    shift
    env $vars $RUN "$dir/$prog" "$@" 2>&1
    echo "exit: $?"
}

# world: recorded with keys, a variable, a file to read and one to write;
# replayed with none of them.
printf 'line one\nline two\n' >"$dir/in.txt"
printf 'abq' | run SLIGHT_REPLAY_TEST=recorded SLIGHT_RECORD="$dir/world.record" world "$dir/in.txt" "$dir/out.txt" >"$dir/world.recorded"
rm -f "$dir/in.txt" "$dir/out.txt"
run SLIGHT_REPLAY="$dir/world.record" world other args </dev/null >"$dir/world.replayed"
if cmp -s "$dir/world.recorded" "$dir/world.replayed" && grep -q '^(ok ("line one" "line two"))$' "$dir/world.recorded" \
   && grep -q '^(env "recorded")$' "$dir/world.recorded"; then
    check "world replays as recorded" ok
else
    diff -u "$dir/world.recorded" "$dir/world.replayed"
    check "world replays as recorded" no
fi
if [ -e "$dir/out.txt" ]; then check "world's replay writes no file" no; else check "world's replay writes no file" ok; fi

# net: a server and a client, the client finding it in a hosts file.
printf '127.0.0.1 echo.test\n' >"$dir/hosts"
run SLIGHT_HOSTS="$dir/hosts" SLIGHT_RECORD="$dir/net.record" net </dev/null >"$dir/net.recorded"
rm -f "$dir/hosts"
run SLIGHT_REPLAY="$dir/net.record" net </dev/null >"$dir/net.replayed"
if cmp -s "$dir/net.recorded" "$dir/net.replayed" && grep -q '^(ok ("echo one" "echo two" "echo three"))$' "$dir/net.recorded"; then
    check "net replays as recorded" ok
else
    diff -u "$dir/net.recorded" "$dir/net.replayed"
    check "net replays as recorded" no
fi

# exec: commands, recorded; replayed without running them, so the file
# the first wrote isn't written again.
run SLIGHT_RECORD="$dir/exec.record" exec "$dir/exec.out" </dev/null >"$dir/exec.recorded"
written=$(cat "$dir/exec.out" 2>/dev/null)
rm -f "$dir/exec.out"
run SLIGHT_REPLAY="$dir/exec.record" exec other args </dev/null >"$dir/exec.replayed"
if cmp -s "$dir/exec.recorded" "$dir/exec.replayed" && [ "$written" = written ] \
   && grep -q '^(ok (3 ("fed in" "[0-9]*")))$' "$dir/exec.recorded" && grep -q '^(ok "first")$' "$dir/exec.recorded"; then
    check "exec replays as recorded" ok
else
    diff -u "$dir/exec.recorded" "$dir/exec.replayed"
    check "exec replays as recorded" no
fi
if [ -e "$dir/exec.out" ]; then check "exec's replay runs no command" no; else check "exec's replay runs no command" ok; fi

# A record of one program, replayed by another, stops where they part.
run SLIGHT_REPLAY="$dir/world.record" net </dev/null >"$dir/net.wrong"
if grep -q '^replay: the run goes another way at entry [0-9]*:' "$dir/net.wrong" && grep -q '^exit: 2$' "$dir/net.wrong"; then
    check "a record that isn't the program's stops the replay" ok
else
    cat "$dir/net.wrong"
    check "a record that isn't the program's stops the replay" no
fi

# order, first in, first out, and under 20 seeds.
fifo=$(run order | head -1)
[ "$fifo" = "1 2 3 4 5 " ] && check "first in, first out: $fifo" ok || check "first in, first out: $fifo" no
orders=""
for seed in $(seq 1 20); do
    one=$(run SLIGHT_SCHED=$seed order | head -1)
    two=$(run SLIGHT_SCHED=$seed order | head -1)
    sorted=$(echo "$one" | tr ' ' '\n' | grep . | sort | tr '\n' ' ')
    if [ "$one" != "$two" ] || [ "$sorted" != "1 2 3 4 5 " ]; then
        check "seed $seed: '$one', then '$two'" no
    fi
    orders="$orders$one
"
done
distinct=$(printf '%s' "$orders" | sort -u | wc -l)
[ "$distinct" -gt 1 ] && check "20 seeds give $distinct orders, each the same every time" ok \
                      || check "20 seeds give $distinct orders, each the same every time" no
run SLIGHT_SCHED=7 SLIGHT_RECORD="$dir/order.record" order >"$dir/order.recorded"
run SLIGHT_REPLAY="$dir/order.record" order >"$dir/order.replayed"
recorded=$(head -1 "$dir/order.recorded" | tr ' ' '\n' | grep . | sort | tr '\n' ' ')
if cmp -s "$dir/order.recorded" "$dir/order.replayed" && [ "$recorded" = "1 2 3 4 5 " ] && grep -q '^exit: 0$' "$dir/order.recorded"; then
    check "a seeded run replays as recorded" ok
else
    diff -u "$dir/order.recorded" "$dir/order.replayed"
    check "a seeded run replays as recorded" no
fi

echo "replay: $fail failed"
[ $fail -eq 0 ]
