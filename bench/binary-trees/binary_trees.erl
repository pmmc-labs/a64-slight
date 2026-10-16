%% binary-trees, single process: a node is a tuple, a leaf's {nil, nil}.
-module(binary_trees).
-export([main/1]).

make(0) -> {nil, nil};
make(D) -> {make(D - 1), make(D - 1)}.

check({nil, nil}) -> 1;
check({L, R}) -> 1 + check(L) + check(R).

trees(0, _, Acc) -> Acc;
trees(N, D, Acc) -> trees(N - 1, D, Acc + check(make(D))).

depths(D, Top) when D > Top -> ok;
depths(D, Top) ->
    io:format("~b~n", [trees(1 bsl (Top - D + 4), D, 0)]),
    depths(D + 2, Top).

main([A]) ->
    Top = max(6, list_to_integer(A)),
    io:format("~b~n", [check(make(Top + 1))]),
    Long = make(Top),
    depths(4, Top),
    io:format("~b~n", [check(Long)]),
    halt(0).
