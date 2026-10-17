// nqueens, after Gabriel's: the lists are slices, copied where the list
// version makes new lists.
package main

import (
    "fmt"
    "os"
    "strconv"
)

func ok(row, dist int, placed []int) bool {
    for _, q := range placed {
        if q == row+dist || q == row-dist {
            return false
        }
        dist++
    }
    return true
}

// x the rows still to try, y those passed over for this column, z those
// placed, the last placed first.
func try(x, y, z []int) int {
    if len(x) == 0 {
        if len(y) == 0 {
            return 1
        }
        return 0
    }
    q, rest, count := x[0], x[1:], 0
    if ok(q, 1, z) {
        count += try(append(append([]int{}, rest...), y...), nil, append([]int{q}, z...))
    }
    return count + try(rest, append([]int{q}, y...), z)
}

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    rows := make([]int, n)
    for i := range rows {
        rows[i] = i + 1
    }
    fmt.Println(try(rows, nil, nil))
}
