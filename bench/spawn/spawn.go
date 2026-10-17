// spawn: n goroutines started one after another, each sending its number
// back; the numbers summed. The channel holds them all, as a mailbox
// would: unbuffered, each goroutine would wait, alive, till main took its
// number.
package main

import (
    "fmt"
    "os"
    "strconv"
)

func main() {
    n, _ := strconv.Atoi(os.Args[1])
    ch := make(chan int, n)
    for i := 0; i < n; i++ {
        go func(i int) { ch <- i }(i)
    }
    sum := 0
    for i := 0; i < n; i++ {
        sum += <-ch
    }
    fmt.Println(sum)
}
