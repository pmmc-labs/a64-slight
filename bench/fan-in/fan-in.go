// fan-in: 100 goroutines each send their number n/100 times on one
// channel, which main sums. Unbuffered, every message would be a switch
// between goroutines (4.7 times slower); from 100 up the size makes no
// difference, so it holds 1024.
package main

import (
    "fmt"
    "os"
    "strconv"
)

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    ch := make(chan int, 1024)
    for s := 1; s <= 100; s++ {
        go func(s int) {
            for i := 0; i < n/100; i++ {
                ch <- s
            }
        }(s)
    }
    sum := 0
    for i := 0; i < n; i++ {
        sum += <-ch
    }
    fmt.Println(sum)
}
