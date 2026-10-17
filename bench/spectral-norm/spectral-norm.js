// spectral-norm, the Benchmarks Game's, single-threaded.
function a(i, j) {
    return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1);
}

function timesA(u, v) {
    for (let i = 0; i < v.length; i++) {
        let s = 0;
        for (let j = 0; j < u.length; j++) s += a(i, j) * u[j];
        v[i] = s;
    }
}

function timesAt(u, v) {
    for (let i = 0; i < v.length; i++) {
        let s = 0;
        for (let j = 0; j < u.length; j++) s += a(j, i) * u[j];
        v[i] = s;
    }
}

function timesAta(u, v, w) {
    timesA(u, w);
    timesAt(w, v);
}

const n = Number(process.argv[2]);
const u = new Float64Array(n).fill(1), v = new Float64Array(n), w = new Float64Array(n);
for (let i = 0; i < 10; i++) {
    timesAta(u, v, w);
    timesAta(v, u, w);
}
let vbv = 0, vv = 0;
for (let i = 0; i < n; i++) {
    vbv += u[i] * v[i];
    vv += v[i] * v[i];
}
console.log(Math.floor(Math.sqrt(vbv / vv) * 1e9 + 0.5));
