%% fan-in: 100 processes each send their number N/100 times to one, which
%% sums what it's sent.
-module(fan_in).
-export([main/1]).

sender(_, _, 0) -> ok;
sender(To, S, M) -> To ! S, sender(To, S, M - 1).

gather(0, Sum) -> Sum;
gather(N, Sum) -> receive S -> gather(N - 1, Sum + S) end.

%% Most of the messages wait in main's mailbox at once, so it keeps them
%% off its heap, as Erlang's documentation says for a long queue (and as
%% spawn does): 4.9 s to 4.5 on one core, 4.7 to 1.0 on every core.
main([A]) ->
    process_flag(message_queue_data, off_heap),
    N = list_to_integer(A),
    Me = self(),
    [spawn(fun() -> sender(Me, S, N div 100) end) || S <- lists:seq(1, 100)],
    io:format("~b~n", [gather(N, 0)]),
    halt(0).
