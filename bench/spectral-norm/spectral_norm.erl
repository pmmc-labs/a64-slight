%% spectral-norm, the Benchmarks Game's, single process, a vector a list.
-module(spectral_norm).
-export([main/1]).

a(I, J) -> 1.0 / ((I + J) * (I + J + 1) div 2 + I + 1).

row(_, _, [], S) -> S;
row(I, J, [X | Rest], S) -> row(I, J + 1, Rest, S + a(I, J) * X).

column(_, _, [], S) -> S;
column(I, J, [X | Rest], S) -> column(I, J + 1, Rest, S + a(J, I) * X).

times_ata(U, N) ->
    W = [row(I, 0, U, 0.0) || I <- lists:seq(0, N - 1)],
    [column(I, 0, W, 0.0) || I <- lists:seq(0, N - 1)].

power(U, N, K) ->
    V = times_ata(U, N),
    U2 = times_ata(V, N),
    case K of
        1 -> {U2, V};
        _ -> power(U2, N, K - 1)
    end.

dot([], [], S) -> S;
dot([X | Xr], [Y | Yr], S) -> dot(Xr, Yr, S + X * Y).

main([A]) ->
    N = list_to_integer(A),
    {U, V} = power(lists:duplicate(N, 1.0), N, 10),
    io:format("~b~n", [floor(math:sqrt(dot(U, V, 0.0) / dot(V, V, 0.0)) * 1.0e9 + 0.5)]),
    halt(0).
