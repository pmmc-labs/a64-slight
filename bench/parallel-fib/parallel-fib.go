// parallel-fib: 64 goroutines each work out fib(k); the answers summed.
package main

import (
    "fmt"
    "os"
    "strconv"
)

func fib(n int) int {
    if n < 2 {
        return n
    }
    return fib(n-1) + fib(n-2)
}

func main() {
    k, _ := strconv.Atoi(os.Args[1])
    results := make(chan int)
    for i := 0; i < 64; i++ {
        go func() { results <- fib(k) }()
    }
    sum := 0
    for i := 0; i < 64; i++ {
        sum += <-results
    }
    fmt.Println(sum)
}
