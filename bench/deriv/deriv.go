// deriv, after Gabriel's: an expression is a slice or an atom, a symbol a
// sym.
package main

import (
    "fmt"
    "os"
    "strconv"
)

type sym string

func deriv(a any) any {
    l, ok := a.([]any)
    if !ok {
        if a == sym("x") {
            return 1
        }
        return 0
    }
    switch l[0] {
    case sym("+"), sym("-"):
        d := make([]any, len(l))
        d[0] = l[0]
        for i, e := range l[1:] {
            d[i+1] = deriv(e)
        }
        return d
    case sym("*"):
        q := make([]any, len(l))
        q[0] = sym("+")
        for i, e := range l[1:] {
            q[i+1] = []any{sym("/"), deriv(e), e}
        }
        return []any{sym("*"), a, q}
    case sym("/"):
        u, v := l[1], l[2]
        return []any{sym("-"), []any{sym("/"), deriv(u), v}, []any{sym("/"), u, []any{sym("*"), v, v, deriv(v)}}}
    }
    panic("no derivative of " + fmt.Sprint(l[0]))
}

func atoms(a any) int {
    l, ok := a.([]any)
    if !ok {
        return 1
    }
    n := 0
    for _, e := range l {
        n += atoms(e)
    }
    return n
}

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    e := []any{sym("+"), []any{sym("*"), 3, sym("x"), sym("x")}, []any{sym("*"), sym("a"), sym("x"), sym("x")}, []any{sym("*"), sym("b"), sym("x")}, 5}
    total, last := 0, []any{}
    for i := 0; i < n; i++ {
        last = deriv(e).([]any)
        total += len(last)
    }
    fmt.Println(total)
    fmt.Println(atoms(last))
}
