// thread-ring: 503 goroutines in a ring, each reading from its own channel.
package main

import (
    "fmt"
    "os"
    "strconv"
)

const ring = 503

func member(id int, in <-chan int, out chan<- int, done chan<- int) {
    for {
        n := <-in
        if n == 0 {
            done <- id
            return
        }
        out <- n - 1
    }
}

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    chans := make([]chan int, ring)
    for i := range chans {
        chans[i] = make(chan int)
    }
    done := make(chan int)
    for i := 0; i < ring; i++ {
        go member(i+1, chans[i], chans[(i+1)%ring], done)
    }
    chans[0] <- n
    fmt.Println(<-done)
}
