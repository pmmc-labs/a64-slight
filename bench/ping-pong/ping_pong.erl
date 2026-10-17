-module(ping_pong).
-export([main/1]).

pong() ->
    receive
        {ping, From, I} -> From ! {pong, I}, pong();
        stop -> ok
    end.

ping(P, N, N) -> P ! stop;
ping(P, I, N) ->
    P ! {ping, self(), I},
    receive {pong, _} -> ping(P, I + 1, N) end.

main([A]) ->
    N = list_to_integer(A),
    ping(spawn(fun pong/0), 0, N),
    io:format("~b~n", [N]),
    halt(0).
