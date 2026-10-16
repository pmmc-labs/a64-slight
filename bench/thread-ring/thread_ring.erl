-module(thread_ring).
-export([main/1]).

member(Id, Root) ->
    receive {next, Next} -> relay(Id, Next, Root) end.

relay(Id, Next, Root) ->
    receive
        0 -> Root ! Id;
        N -> Next ! N - 1, relay(Id, Next, Root)
    end.

main([A]) ->
    Me = self(),
    Ring = [spawn(fun() -> member(Id, Me) end) || Id <- lists:seq(1, 503)],
    [P ! {next, Q} || {P, Q} <- lists:zip(Ring, tl(Ring) ++ [hd(Ring)])],
    hd(Ring) ! list_to_integer(A),
    receive Id -> io:format("~b~n", [Id]) end,
    halt(0).
