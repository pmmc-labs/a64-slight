// binary-trees, single-threaded: a node is a struct, collected by Go's GC.
package main

import (
    "fmt"
    "os"
    "strconv"
)

type node struct {
    l, r *node
}

func make(d int) *node {
    if d == 0 {
        return &node{}
    }
    return &node{make(d - 1), make(d - 1)}
}

func (n *node) check() int {
    if n.l == nil {
        return 1
    }
    return 1 + n.l.check() + n.r.check()
}

func main() {
    top, _ := strconv.Atoi(os.Args[1])
    if top < 6 {
        top = 6
    }
    fmt.Println(make(top + 1).check())
    long := make(top)
    for d := 4; d <= top; d += 2 {
        iterations, sum := 1<<(top-d+4), 0
        for i := 0; i < iterations; i++ {
            sum += make(d).check()
        }
        fmt.Println(sum)
    }
    fmt.Println(long.check())
}
