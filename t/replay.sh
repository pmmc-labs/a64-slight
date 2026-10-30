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
# all give one order. A seeded run must replay as it went. And
# t/267-join-any-all.slight, whose results go by the virtual clock and
# not by the order processes run in, must print its golden output under
# five seeds (D220).
#
# t/replay/rand.slight draws random numbers in processes that interleave
# (D222): with SLIGHT_SEED it must print the same under five SLIGHT_SCHED
# orders as first in, first out; without it, a recorded run must replay
# the same, and two runs must differ.
#
# t/replay/islands.slight runs on four islands (D256), workers on three of
# them sending to a collector in an order that varies from run to run: a
# recorded run writes a record for each island, and replaying them, with
# SLIGHT_ISLANDS other than it was, or under SLIGHT_SCHED, prints what
# the run did; an island's record replayed alone stops, saying so.
#
# t/replay/now.slight reads the time of day on the virtual clock (D254):
# without SLIGHT_EPOCH, from the real time of day, so a recorded run must
# replay the same, its start after 2026; with it, from there.
cd "$(dirname "$0")/.." || exit 2
TARGET=${TARGET-aarch64}
case "$TARGET-$(uname -m)" in
    aarch64-arm64|aarch64-aarch64|x86_64-x86_64) RUN=${RUN-} ;;
    x86_64-arm64)                                RUN=${RUN-} ;;
    *)                                           RUN=${RUN-qemu-$TARGET} ;;
esac
unset SLIGHT_CLOCK SLIGHT_SCHED SLIGHT_RECORD SLIGHT_REPLAY SLIGHT_SEED SLIGHT_EPOCH
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

for name in world net exec order rand now islands; do
    if ! node bin/slightc.ts --target "$TARGET" -o "$dir/$name" "t/replay/$name.slight" >"$dir/$name.compile" 2>&1; then
        cat "$dir/$name.compile"
        check "$name (compile)" no
    fi
done
if ! node bin/slightc.ts --target "$TARGET" -o "$dir/joins" t/267-join-any-all.slight >"$dir/joins.compile" 2>&1; then
    cat "$dir/joins.compile"
    check "joins (compile)" no
fi
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
rm -rf "$dir/in.txt" "$dir/out.txt" "$dir/out.txt.d"
run SLIGHT_REPLAY="$dir/world.record" world other args </dev/null >"$dir/world.replayed"
if cmp -s "$dir/world.recorded" "$dir/world.replayed" && grep -q '^(ok ("line one" "line two"))$' "$dir/world.recorded" \
   && grep -q '^(ok ((kind file) (size 18) (modified [0-9]*) (mode [0-9]*)))$' "$dir/world.recorded" \
   && grep -q '^(random "[0-9a-f]\{32\}")$' "$dir/world.recorded" \
   && grep -q '^(env "recorded")$' "$dir/world.recorded" \
   && grep -q '^(clock #true [0-9]*)$' "$dir/world.recorded" && grep -q '^(now #true [0-9]*)$' "$dir/world.recorded"; then
    check "world replays as recorded" ok
else
    diff -u "$dir/world.recorded" "$dir/world.replayed"
    check "world replays as recorded" no
fi
if [ -e "$dir/out.txt" ] || [ -e "$dir/out.txt.d" ]; then check "world's replay writes no file, nor makes a directory" no; else check "world's replay writes no file, nor makes a directory" ok; fi

# net: a server and a client, the client finding it in a hosts file; and
# the same over a Unix socket, whose path the recorded run removes as its
# listener closes, and the replay neither makes nor removes: a file put
# there before it stays.
printf '127.0.0.1 echo.test\n' >"$dir/hosts"
run SLIGHT_HOSTS="$dir/hosts" SLIGHT_RECORD="$dir/net.record" net "$dir/net.sock" </dev/null >"$dir/net.recorded"
rm -f "$dir/hosts"
if [ -e "$dir/net.sock" ]; then check "net's socket goes with its listener" no; else check "net's socket goes with its listener" ok; fi
echo kept >"$dir/net.sock"
run SLIGHT_REPLAY="$dir/net.record" net other </dev/null >"$dir/net.replayed"
if cmp -s "$dir/net.recorded" "$dir/net.replayed" && grep -q '^(ok ("echo one" "echo two" "echo three"))$' "$dir/net.recorded" \
   && grep -q '^(ok ("echo four" "echo five"))$' "$dir/net.recorded" && grep -q '^(ok ((uid [0-9]*) (gid [0-9]*) (pid [0-9]*)))$' "$dir/net.recorded"; then
    check "net replays as recorded" ok
else
    diff -u "$dir/net.recorded" "$dir/net.replayed"
    check "net replays as recorded" no
fi
if [ "$(cat "$dir/net.sock" 2>/dev/null)" = kept ]; then check "net's replay leaves its socket's path alone" ok; else check "net's replay leaves its socket's path alone" no; fi

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

# islands: each island's record, replayed together.
run SLIGHT_ISLANDS=4 SLIGHT_RECORD="$dir/islands.record" islands </dev/null >"$dir/islands.recorded"
run SLIGHT_ISLANDS=1 SLIGHT_REPLAY="$dir/islands.record" islands </dev/null >"$dir/islands.replayed"
if cmp -s "$dir/islands.recorded" "$dir/islands.replayed" && grep -q '^(runs [0-9]* -*[0-9]*)$' "$dir/islands.recorded" \
   && grep -q '^((0 #true) (1 #true) (2 #true) (3 #true))$' "$dir/islands.recorded" \
   && [ -s "$dir/islands.record.1" ] && [ -s "$dir/islands.record.3" ] && [ ! -e "$dir/islands.record.4" ]; then
    check "islands replay as recorded, each from its own record" ok
else
    diff -u "$dir/islands.recorded" "$dir/islands.replayed"
    check "islands replay as recorded, each from its own record" no
fi
run SLIGHT_ISLANDS=4 SLIGHT_SCHED=7 SLIGHT_RECORD="$dir/islands.sched" islands </dev/null >"$dir/islands.sched.recorded"
run SLIGHT_REPLAY="$dir/islands.sched" islands </dev/null >"$dir/islands.sched.replayed"
if cmp -s "$dir/islands.sched.recorded" "$dir/islands.sched.replayed" && grep -q '^exit: 0$' "$dir/islands.sched.recorded"; then
    check "islands under SLIGHT_SCHED replay as recorded" ok
else
    diff -u "$dir/islands.sched.recorded" "$dir/islands.sched.replayed"
    check "islands under SLIGHT_SCHED replay as recorded" no
fi
run SLIGHT_REPLAY="$dir/islands.record.2" islands </dev/null >"$dir/islands.alone"
if grep -q "^replay: that's island 2's record: a run's islands replay together, from island 0's$" "$dir/islands.alone" \
   && grep -q '^exit: 2$' "$dir/islands.alone"; then
    check "an island's record alone doesn't replay" ok
else
    cat "$dir/islands.alone"
    check "an island's record alone doesn't replay" no
fi

# A record of one program, replayed by another, stops where they part.
run SLIGHT_REPLAY="$dir/world.record" net </dev/null >"$dir/net.wrong"
if grep -q '^replay: the run goes another way at entry [0-9]*:' "$dir/net.wrong" && grep -q '^exit: 2$' "$dir/net.wrong"; then
    check "a record that isn't the program's stops the replay" ok
else
    cat "$dir/net.wrong"
    check "a record that isn't the program's stops the replay" no
fi

# A record holds all a run read, and its environment, so it's the user's
# alone, even written over a file others could read; and it isn't written
# through a symbolic link (D205).
: >"$dir/mode.record"
chmod 644 "$dir/mode.record"
run SLIGHT_RECORD="$dir/mode.record" order >/dev/null
case $(ls -l "$dir/mode.record") in
    -rw-------*) check "a record is its user's alone" ok ;;
    *)           ls -l "$dir/mode.record"; check "a record is its user's alone" no ;;
esac
ln -s mode.target "$dir/link.record"
run SLIGHT_RECORD="$dir/link.record" order >"$dir/link.out"
if grep -q "^slight: can't record to $dir/link.record: " "$dir/link.out" && grep -q '^exit: 2$' "$dir/link.out" \
   && [ ! -e "$dir/mode.target" ]; then
    check "a record isn't written through a link" ok
else
    cat "$dir/link.out"
    check "a record isn't written through a link" no
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

# join/any and join/all: the same, whatever the order.
for seed in 1 2 3 4 5; do
    run SLIGHT_CLOCK=virtual SLIGHT_SCHED=$seed joins >"$dir/joins.$seed"
    if printf 'exit: 0\n' | cat t/267-join-any-all.expected - | cmp -s - "$dir/joins.$seed"; then
        check "join/any and join/all, seed $seed" ok
    else
        diff -u t/267-join-any-all.expected "$dir/joins.$seed"
        check "join/any and join/all, seed $seed" no
    fi
done

# Random numbers: the same, whatever the order, from one seed.
run SLIGHT_SEED=5 rand >"$dir/rand.fifo"
same=ok
for seed in 1 2 3 4 5; do
    run SLIGHT_SEED=5 SLIGHT_SCHED=$seed rand >"$dir/rand.$seed"
    cmp -s "$dir/rand.fifo" "$dir/rand.$seed" || { diff -u "$dir/rand.fifo" "$dir/rand.$seed"; same=no; }
done
grep -q '^((0 [0-9]* [0-9]* [0-9]*) (1 ' "$dir/rand.fifo" && grep -q '^exit: 0$' "$dir/rand.fifo" || same=no
check "random numbers, the same under five orders from one seed" $same
# Without a seed, the system's: replayed as recorded, and new each run.
run SLIGHT_RECORD="$dir/rand.record" rand >"$dir/rand.recorded"
run SLIGHT_REPLAY="$dir/rand.record" rand >"$dir/rand.replayed"
if cmp -s "$dir/rand.recorded" "$dir/rand.replayed" && grep -q '^exit: 0$' "$dir/rand.recorded"; then
    check "random numbers without a seed replay as recorded" ok
else
    diff -u "$dir/rand.recorded" "$dir/rand.replayed"
    check "random numbers without a seed replay as recorded" no
fi
run rand >"$dir/rand.again"
if cmp -s "$dir/rand.recorded" "$dir/rand.again"; then
    check "random numbers without a seed differ from run to run" no
else
    check "random numbers without a seed differ from run to run" ok
fi

# The time of day on the virtual clock: from the real one, recorded and
# replayed; and from SLIGHT_EPOCH, whatever the real one is.
run SLIGHT_CLOCK=virtual SLIGHT_RECORD="$dir/now.record" now >"$dir/now.recorded"
run SLIGHT_REPLAY="$dir/now.record" now >"$dir/now.replayed"
start=$(sed -n 's/^(\([0-9]*\) 1500000)$/\1/p' "$dir/now.recorded")
if cmp -s "$dir/now.recorded" "$dir/now.replayed" && [ -n "$start" ] && [ "$start" -gt 1767225600000000 ]; then
    check "the virtual clock's time of day starts at the real one, and replays" ok
else
    diff -u "$dir/now.recorded" "$dir/now.replayed"
    check "the virtual clock's time of day starts at the real one, and replays" no
fi
run SLIGHT_CLOCK=virtual SLIGHT_EPOCH=86400 now >"$dir/now.epoch"
if [ "$(cat "$dir/now.epoch")" = "$(printf '(86400000000 1500000)\nexit: 0')" ]; then
    check "the virtual clock's time of day starts at SLIGHT_EPOCH" ok
else
    cat "$dir/now.epoch"
    check "the virtual clock's time of day starts at SLIGHT_EPOCH" no
fi

echo "replay: $fail failed"
[ $fail -eq 0 ]
