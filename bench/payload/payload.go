// payload: ping-pong with something in each message (D237): n round trips
// between two goroutines, on unbuffered channels, each message the same
// slice of 100 integers, which a channel passes on without copying.
package main

import (
    "fmt"
    "os"
    "strconv"
)

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    ping, pong := make(chan []int), make(chan []int)
    go func() {
        for data := range ping {
            pong <- data
        }
    }()
    data := make([]int, 100)
    for i := range data {
        data[i] = i + 1
    }
    for i := 0; i < n; i++ {
        ping <- data
        data = <-pong
    }
    close(ping)
    sum := 0
    for _, v := range data {
        sum += v
    }
    fmt.Println(n)
    fmt.Println(sum)
}
