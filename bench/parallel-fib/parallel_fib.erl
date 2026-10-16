-module(parallel_fib).
-export([main/1]).

fib(N) when N < 2 -> N;
fib(N) -> fib(N - 1) + fib(N - 2).

gather(0, Acc) -> Acc;
gather(N, Acc) -> receive I -> gather(N - 1, Acc + I) end.

main([A]) ->
    K = list_to_integer(A),
    Me = self(),
    [spawn(fun() -> Me ! fib(K) end) || _ <- lists:seq(1, 64)],
    io:format("~b~n", [gather(64, 0)]),
    halt(0).
