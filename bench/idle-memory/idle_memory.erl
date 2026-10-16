-module(idle_memory).
-export([main/1]).

idle() -> receive {ping, From} -> From ! 1 end.

start(0, Acc) -> Acc;
start(N, Acc) -> start(N - 1, [spawn(fun idle/0) | Acc]).

gather(0, Acc) -> Acc;
gather(N, Acc) -> receive I -> gather(N - 1, Acc + I) end.

%% Unlike spawn.erl's, main's queue fills only after it has stopped
%% allocating, so its messages stay on its heap: off it, this takes 7 s
%% rather than 4.
main([A]) ->
    N = list_to_integer(A),
    [P ! {ping, self()} || P <- start(N, [])],
    io:format("~b~n", [gather(N, 0)]),
    halt(0).
