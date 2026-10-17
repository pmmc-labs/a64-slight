// deriv, after Gabriel's: an expression is an array, a symbol a string.
function deriv(a) {
    if (!Array.isArray(a)) return a === 'x' ? 1 : 0;
    switch (a[0]) {
        case '+':
        case '-':
            return [a[0], ...a.slice(1).map(deriv)];
        case '*':
            return ['*', a, ['+', ...a.slice(1).map(e => ['/', deriv(e), e])]];
        case '/':
            return ['-', ['/', deriv(a[1]), a[2]], ['/', a[1], ['*', a[2], a[2], deriv(a[2])]]];
    }
    throw new Error('no derivative of ' + a[0]);
}

function atoms(a) {
    return Array.isArray(a) ? a.reduce((n, e) => n + atoms(e), 0) : 1;
}

const e = ['+', ['*', 3, 'x', 'x'], ['*', 'a', 'x', 'x'], ['*', 'b', 'x'], 5];
let total = 0, last = [];
for (let i = Number(process.argv[2]); i > 0; i--) {
    last = deriv(e);
    total += last.length;
}
console.log(total);
console.log(atoms(last));
