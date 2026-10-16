// ping-pong: n round trips between two goroutines, on unbuffered channels.
package main

import (
    "fmt"
    "os"
    "strconv"
)

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    ping, pong := make(chan int), make(chan int)
    go func() {
        for i := range ping {
            pong <- i
        }
    }()
    for i := 0; i < n; i++ {
        ping <- i
        <-pong
    }
    close(ping)
    fmt.Println(n)
}
