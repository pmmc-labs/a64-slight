-module(tak).
-export([main/1]).

tak(X, Y, Z) when Y < X -> tak(tak(X - 1, Y, Z), tak(Y - 1, Z, X), tak(Z - 1, X, Y));
tak(_, _, Z) -> Z.

main([A]) ->
    N = list_to_integer(A),
    io:format("~b~n", [tak(3 * N, 2 * N, N)]),
    halt(0).
