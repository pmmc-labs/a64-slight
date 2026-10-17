-module(startup).
-export([main/1]).

main(_) ->
    io:format("0~n"),
    halt(0).
