%% deriv, Gabriel's, with lists and atoms.
-module(deriv).
-export([main/1]).

deriv(x) -> 1;
deriv(A) when not is_list(A) -> 0;
deriv(['+' | Args]) -> ['+' | [deriv(E) || E <- Args]];
deriv(['-' | Args]) -> ['-' | [deriv(E) || E <- Args]];
deriv(['*' | Args] = A) -> ['*', A, ['+' | [['/', deriv(E), E] || E <- Args]]];
deriv(['/', U, V]) -> ['-', ['/', deriv(U), V], ['/', U, ['*', V, V, deriv(V)]]].

atoms(A) when is_list(A) -> lists:foldl(fun(E, N) -> N + atoms(E) end, 0, A);
atoms(_) -> 1.

run(0, _, Total, Last) -> io:format("~b~n~b~n", [Total, atoms(Last)]);
run(N, E, Total, _) ->
    D = deriv(E),
    run(N - 1, E, Total + length(D), D).

main([A]) ->
    run(list_to_integer(A), ['+', ['*', 3, x, x], ['*', a, x, x], ['*', b, x], 5], 0, []),
    halt(0).
