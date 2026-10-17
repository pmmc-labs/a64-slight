-module(fib).
-export([main/1]).

fib(N) when N < 2 -> N;
fib(N) -> fib(N - 1) + fib(N - 2).

main([A]) ->
    io:format("~b~n", [fib(list_to_integer(A))]),
    halt(0).
