-module(payload).
-export([main/1]).

pong() ->
    receive
        {ping, From, Data} -> From ! {pong, Data}, pong();
        stop -> ok
    end.

ping(P, N, N, Data) -> P ! stop, Data;
ping(P, I, N, Data) ->
    P ! {ping, self(), Data},
    receive {pong, Back} -> ping(P, I + 1, N, Back) end.

main([A]) ->
    N = list_to_integer(A),
    Data = ping(spawn(fun pong/0), 0, N, lists:seq(1, 100)),
    io:format("~b~n~b~n", [N, lists:sum(Data)]),
    halt(0).
