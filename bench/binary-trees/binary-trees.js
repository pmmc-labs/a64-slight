// binary-trees, single-threaded: a node is an object.
function make(d) {
    return d === 0 ? { l: null, r: null } : { l: make(d - 1), r: make(d - 1) };
}

function check(n) {
    return n.l === null ? 1 : 1 + check(n.l) + check(n.r);
}

const top = Math.max(6, Number(process.argv[2]));
console.log(check(make(top + 1)));
const long = make(top);
for (let d = 4; d <= top; d += 2) {
    const iterations = 2 ** (top - d + 4);
    let sum = 0;
    for (let i = 0; i < iterations; i++) sum += check(make(d));
    console.log(sum);
}
console.log(check(long));
