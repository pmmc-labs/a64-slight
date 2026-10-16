-module(spawn).
-export([main/1]).

start(N, N, _) -> ok;
start(I, N, Me) ->
    spawn(fun() -> Me ! I end),
    start(I + 1, N, Me).

gather(0, Acc) -> Acc;
gather(N, Acc) -> receive I -> gather(N - 1, Acc + I) end.

%% A million messages may wait in main's mailbox. Kept on its heap, as
%% they are by default, every collection walks them, which takes 36 s for
%% a million; off the heap, as Erlang's documentation says for a process
%% with a long queue, they don't.
main([A]) ->
    process_flag(message_queue_data, off_heap),
    N = list_to_integer(A),
    start(0, N, self()),
    io:format("~b~n", [gather(N, 0)]),
    halt(0).
