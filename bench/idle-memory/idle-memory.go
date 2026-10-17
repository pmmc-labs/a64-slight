// idle-memory: n goroutines, each waiting on a channel of its own, then
// each asked for 1; the answers summed. The answers' channel holds them
// all, as a mailbox would (see spawn.go).
package main

import (
    "fmt"
    "os"
    "strconv"
)

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    reply := make(chan int, n)
    chans := make([]chan struct{}, 0)
    for i := 0; i < n; i++ {
        c := make(chan struct{})
        go func() {
            <-c
            reply <- 1
        }()
        chans = append(chans, c)
    }
    for _, c := range chans {
        c <- struct{}{}
    }
    sum := 0
    for i := 0; i < n; i++ {
        sum += <-reply
    }
    fmt.Println(sum)
}
