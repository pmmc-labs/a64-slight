-module(latency).
-export([main/1]).

%% latency (D238): how long a message waits for its turn while other
%% processes work. Ping K is due K ms after the pinger starts; till it's
%% due the pinger yields and looks at the clock again, and its latency is
%% from when it was due to when its answer came. Three phases: idle; busy,
%% beside a process working out fib(30+S); and heap, beside one holding
%% 100S blocks of 10,000 integers and replacing the oldest 1,000S times,
%% yielding after each, its pinger started once the blocks are made.
%% Prints "# phase p50 p99 max n" for each, in microseconds, then fib's
%% result and the heap's checksum.

now_us() -> erlang:monotonic_time(microsecond).

responder() ->
    receive
        {ping, From} -> From ! pong, responder();
        stop -> ok
    end.

pinger(R, _T0, K, Last, Samples) when K > Last -> R ! stop, Samples;
pinger(R, T0, K, Last, Samples) ->
    Due = T0 + 1000 * K,
    case now_us() < Due of
        true  -> erlang:yield(), pinger(R, T0, K, Last, Samples);
        false -> R ! {ping, self()}, answer(R, T0, K, Last, Samples, Due)
    end.

%% The worker says when it's done, and pings due after that don't count.
answer(R, T0, K, Last, Samples, Due) ->
    receive
        {done, T} -> answer(R, T0, K, (T - T0) div 1000, Samples, Due);
        pong when K > Last -> pinger(R, T0, K + 1, Last, Samples);
        pong -> pinger(R, T0, K + 1, Last, [now_us() - Due | Samples])
    end.

%% A pinger in a process of its own, which sends its samples to Parent.
pings(Parent, Last) ->
    spawn(fun() -> Parent ! {samples, self(), pinger(spawn(fun responder/0), now_us(), 1, Last, [])} end).

samples(P) -> receive {samples, P, S} -> S end.
samples() -> receive {samples, _, S} -> S end.

report(Name, Samples) ->
    S = lists:sort(Samples),
    case length(S) of
        0 -> io:format("# ~s 0 0 0 0~n", [Name]);
        N -> io:format("# ~s ~b ~b ~b ~b~n", [Name, lists:nth(N div 2 + 1, S), lists:nth(N * 99 div 100 + 1, S), lists:last(S), N])
    end.

fib(N) when N < 2 -> N;
fib(N) -> fib(N - 1) + fib(N - 2).

block(B, Size) -> lists:seq(B, B + Size - 1).

%% The oldest block leaves the front of the queue as each new one joins.
churn(Q, N, N, _Size) -> Q;
churn(Q, I, N, Size) ->
    Next = queue:in(block(1000000 + I, Size), queue:drop(Q)),
    erlang:yield(),
    churn(Next, I + 1, N, Size).

main([A]) ->
    S = list_to_integer(A),
    Me = self(),
    report("idle", samples(pings(Me, 500))),

    P1 = pings(Me, 1000000000),
    spawn(fun() -> V = fib(30 + S), P1 ! {done, now_us()}, Me ! {fib, V} end),
    report("busy", samples(P1)),
    receive {fib, V} -> io:format("~b~n", [V]) end,

    spawn(fun() ->
        Blocks = queue:from_list([block(J, 10000) || J <- lists:seq(0, 100 * S - 1)]),
        P = pings(Me, 1000000000),
        Q = churn(Blocks, 0, 1000 * S, 10000),
        P ! {done, now_us()},
        Me ! {heap, lists:sum([hd(B) || B <- queue:to_list(Q)])}
    end),
    report("heap", samples()),
    receive {heap, Sum} -> io:format("~b~n", [Sum]) end,
    halt(0).
