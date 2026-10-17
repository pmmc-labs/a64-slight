// nqueens, after Gabriel's: the lists are arrays, copied where the list
// version makes new lists.
function ok(row, dist, placed) {
    for (const q of placed) {
        if (q === row + dist || q === row - dist) return false;
        dist++;
    }
    return true;
}

// x the rows still to try, y those passed over for this column, z those
// placed, the last placed first.
function tryIt(x, y, z) {
    if (x.length === 0) return y.length === 0 ? 1 : 0;
    const q = x[0], rest = x.slice(1);
    return (ok(q, 1, z) ? tryIt(rest.concat(y), [], [q].concat(z)) : 0) + tryIt(rest, [q].concat(y), z);
}

const n = Number(process.argv[2]);
console.log(tryIt(Array.from({ length: n }, (_, i) => i + 1), [], []));
