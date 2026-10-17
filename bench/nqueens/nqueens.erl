%% nqueens, Gabriel's, with lists.
-module(nqueens).
-export([main/1]).

ok(_, _, []) -> true;
ok(Row, Dist, [Q | Rest]) ->
    Q =/= Row + Dist andalso Q =/= Row - Dist andalso ok(Row, Dist + 1, Rest).

%% X the rows still to try, Y those passed over for this column, Z those placed.
try_it([], [], _) -> 1;
try_it([], _, _) -> 0;
try_it([Q | Rest], Y, Z) ->
    case ok(Q, 1, Z) of
        true  -> try_it(Rest ++ Y, [], [Q | Z]);
        false -> 0
    end + try_it(Rest, [Q | Y], Z).

main([A]) ->
    io:format("~b~n", [try_it(lists:seq(1, list_to_integer(A)), [], [])]),
    halt(0).
