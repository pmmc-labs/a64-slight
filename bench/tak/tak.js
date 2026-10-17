function tak(x, y, z) {
    return y < x ? tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y)) : z;
}

const n = Number(process.argv[2]);
console.log(tak(3 * n, 2 * n, n));
