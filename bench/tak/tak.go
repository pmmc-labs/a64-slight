package main

import (
    "fmt"
    "os"
    "strconv"
)

func tak(x, y, z int) int {
    if y < x {
        return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y))
    }
    return z
}

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    fmt.Println(tak(3*n, 2*n, n))
}
