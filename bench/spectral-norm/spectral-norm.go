// spectral-norm, the Benchmarks Game's, in one goroutine.
package main

import (
    "fmt"
    "math"
    "os"
    "strconv"
)

func a(i, j int) float64 {
    return 1.0 / float64((i+j)*(i+j+1)/2+i+1)
}

func timesA(u, v []float64) {
    for i := range v {
        s := 0.0
        for j, x := range u {
            s += a(i, j) * x
        }
        v[i] = s
    }
}

func timesAt(u, v []float64) {
    for i := range v {
        s := 0.0
        for j, x := range u {
            s += a(j, i) * x
        }
        v[i] = s
    }
}

func timesAta(u, v, w []float64) {
    timesA(u, w)
    timesAt(w, v)
}

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    u, v, w := make([]float64, n), make([]float64, n), make([]float64, n)
    for i := range u {
        u[i] = 1
    }
    for i := 0; i < 10; i++ {
        timesAta(u, v, w)
        timesAta(v, u, w)
    }
    vbv, vv := 0.0, 0.0
    for i := range u {
        vbv += u[i] * v[i]
        vv += v[i] * v[i]
    }
    fmt.Printf("%.0f\n", math.Floor(math.Sqrt(vbv/vv)*1e9+0.5))
}
