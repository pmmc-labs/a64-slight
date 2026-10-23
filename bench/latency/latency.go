// latency (D238): how long a message waits for its turn while other
// goroutines work. Ping k is due k ms after the pinger starts; till it's
// due the pinger yields and looks at the clock again, and its latency is
// from when it was due to when its answer came. Three phases: idle; busy,
// beside a goroutine working out fib(30+s); and heap, beside one holding
// 100s blocks of 10,000 integers and replacing the oldest 1,000s times,
// yielding after each, its pinger started once the blocks are made.
// Prints "# phase p50 p99 max n" for each, in microseconds, then fib's
// result and the heap's checksum.
package main

import (
    "fmt"
    "os"
    "runtime"
    "sort"
    "strconv"
    "time"
)

func fib(n int) int {
    if n < 2 {
        return n
    }
    return fib(n-1) + fib(n-2)
}

// The worker says when it's done, and pings due after that don't count.
func pinger(done <-chan time.Time, last int) []int {
    ping, pong := make(chan struct{}), make(chan struct{})
    go func() {
        for range ping {
            pong <- struct{}{}
        }
    }()
    defer close(ping)
    t0 := time.Now()
    var samples []int
    for k := 1; k <= last; k++ {
        due := t0.Add(time.Duration(k) * time.Millisecond)
        for time.Now().Before(due) {
            runtime.Gosched()
        }
        ping <- struct{}{}
    wait:
        for {
            select {
            case t := <-done:
                last = int(t.Sub(t0) / time.Millisecond)
            case <-pong:
                break wait
            }
        }
        if k <= last {
            samples = append(samples, int(time.Since(due)/time.Microsecond))
        }
    }
    return samples
}

func report(name string, s []int) {
    sort.Ints(s)
    n := len(s)
    if n == 0 {
        fmt.Printf("# %s 0 0 0 0\n", name)
        return
    }
    fmt.Printf("# %s %d %d %d %d\n", name, s[n/2], s[n*99/100], s[n-1], n)
}

func main() {
    s, _ := strconv.Atoi(os.Args[1])
    report("idle", pinger(nil, 500))

    done, result := make(chan time.Time, 1), make(chan int, 1)
    go func() {
        v := fib(30 + s)
        done <- time.Now()
        result <- v
    }()
    report("busy", pinger(done, 1000000000))
    fmt.Println(<-result)

    done = make(chan time.Time, 1)
    ready := make(chan struct{})
    go func() {
        k, n, size := 100*s, 1000*s, 10000
        blocks := make([][]int, k)
        for j := range blocks {
            blocks[j] = make([]int, size)
            for x := range blocks[j] {
                blocks[j][x] = j + x
            }
        }
        close(ready)
        // The oldest block is at i % k.
        for i := 0; i < n; i++ {
            b := make([]int, size)
            for x := range b {
                b[x] = 1000000 + i + x
            }
            blocks[i%k] = b
            runtime.Gosched()
        }
        done <- time.Now()
        sum := 0
        for _, b := range blocks {
            sum += b[0]
        }
        result <- sum
    }()
    <-ready
    report("heap", pinger(done, 1000000000))
    fmt.Println(<-result)
}
