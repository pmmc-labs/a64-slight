%% n-body, after the C version: a body is a tuple, the five a list, as in
%% the slight version.
-module(n_body).
-export([main/1]).

-define(PI, 3.141592653589793).
-define(SOLAR_MASS, (4 * ?PI * ?PI)).
-define(YEAR, 365.24).

planet(X, Y, Z, VX, VY, VZ, M) ->
    {X, Y, Z, VX * ?YEAR, VY * ?YEAR, VZ * ?YEAR, M * ?SOLAR_MASS}.

system() ->
    [{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, ?SOLAR_MASS},
     planet(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
            1.66007664274403694e-03, 7.69901118419740425e-03, -6.90460016972063023e-05,
            9.54791938424326609e-04),
     planet(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
            -2.76742510726862411e-03, 4.99852801234917238e-03, 2.30417297573763929e-05,
            2.85885980666130812e-04),
     planet(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
            2.96460137564761618e-03, 2.37847173959480950e-03, -2.96589568540237556e-05,
            4.36624404335156298e-05),
     planet(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
            2.68067772490389322e-03, 1.62824170038242295e-03, -9.51592254519715870e-05,
            5.15138902046611451e-05)].

offset([{X, Y, Z, _, _, _, M} | _] = Bodies) ->
    {PX, PY, PZ} = lists:foldl(fun({_, _, _, VX, VY, VZ, BM}, {PX, PY, PZ}) ->
                                       {PX + VX * BM, PY + VY * BM, PZ + VZ * BM}
                               end, {0.0, 0.0, 0.0}, Bodies),
    [{X, Y, Z, -PX / ?SOLAR_MASS, -PY / ?SOLAR_MASS, -PZ / ?SOLAR_MASS, M} | tl(Bodies)].

advance(Bodies, Dt) ->
    [{X + Dt * VX, Y + Dt * VY, Z + Dt * VZ, VX, VY, VZ, M}
     || {X, Y, Z, VX, VY, VZ, M} <- interact(Bodies, Dt)].

interact([], _) -> [];
interact([B | Rest], Dt) ->
    {Moved, Others} = pull(B, Rest, Dt, []),
    [Moved | interact(Others, Dt)].

pull(B, [], _, Done) -> {B, lists:reverse(Done)};
pull({X, Y, Z, VX, VY, VZ, M}, [{OX, OY, OZ, OVX, OVY, OVZ, OM} | Rest], Dt, Done) ->
    DX = X - OX, DY = Y - OY, DZ = Z - OZ,
    Dist = math:sqrt(DX * DX + DY * DY + DZ * DZ),
    Mag = Dt / (Dist * Dist * Dist),
    pull({X, Y, Z, VX - DX * OM * Mag, VY - DY * OM * Mag, VZ - DZ * OM * Mag, M}, Rest, Dt,
         [{OX, OY, OZ, OVX + DX * M * Mag, OVY + DY * M * Mag, OVZ + DZ * M * Mag, OM} | Done]).

energy([], E) -> E;
energy([{_, _, _, VX, VY, VZ, M} = B | Rest], E) ->
    energy(Rest, potential(B, Rest, E + 0.5 * M * (VX * VX + VY * VY + VZ * VZ))).

potential(_, [], E) -> E;
potential({X, Y, Z, _, _, _, M} = B, [{OX, OY, OZ, _, _, _, OM} | Rest], E) ->
    DX = X - OX, DY = Y - OY, DZ = Z - OZ,
    potential(B, Rest, E - (M * OM) / math:sqrt(DX * DX + DY * DY + DZ * DZ)).

steps(Bodies, 0) -> Bodies;
steps(Bodies, N) -> steps(advance(Bodies, 0.01), N - 1).

shown(E) -> floor(E * 1.0e9 + 0.5).

main([A]) ->
    Start = offset(system()),
    io:format("~b~n", [shown(energy(Start, 0.0))]),
    io:format("~b~n", [shown(energy(steps(Start, list_to_integer(A)), 0.0))]),
    halt(0).
