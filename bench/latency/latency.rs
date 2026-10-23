// latency (D238): how long a message waits for its turn while other tasks
// work. Ping k is due k ms after the pinger starts; till it's due the
// pinger yields and looks at the clock again, and its latency is from when
// it was due to when its answer came. Three phases: idle; busy, beside a
// task working out fib(30+s), which never waits, as tokio doesn't preempt
// (it would be given a thread of its own with spawn_blocking, which isn't
// one core); and heap, beside one holding 100s blocks of 10,000 integers
// and replacing the oldest 1,000s times, yielding after each, its pinger
// started once the blocks are made. Prints "# phase p50 p99 max n" for
// each, in microseconds, then fib's result and the heap's checksum. The
// pinger's mailbox takes the responder's answers and the worker's word
// that it's done, as a process's would. Only on one thread (D238): on
// every core, yield_now leaves the pinger on its worker, which the busy
// task then holds.
use std::collections::VecDeque;
use std::time::{Duration, Instant};
use tokio::sync::mpsc::{UnboundedReceiver, UnboundedSender, unbounded_channel};
use tokio::task::yield_now;

enum Msg {
    Pong,
    Done(Instant),
}

fn fib(n: u64) -> u64 {
    if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

// The worker says when it's done, and pings due after that don't count.
async fn pinger(me: UnboundedSender<Msg>, mut mail: UnboundedReceiver<Msg>, mut last: u64) -> Vec<u64> {
    let (ping, mut pings) = unbounded_channel::<()>();
    tokio::spawn(async move {
        while pings.recv().await.is_some() {
            me.send(Msg::Pong).unwrap();
        }
    });
    let t0 = Instant::now();
    let mut samples = Vec::new();
    let mut k = 1;
    while k <= last {
        let due = t0 + Duration::from_millis(k);
        while Instant::now() < due {
            yield_now().await;
        }
        ping.send(()).unwrap();
        while let Some(Msg::Done(t)) = mail.recv().await {
            last = t.saturating_duration_since(t0).as_millis() as u64;
        }
        if k <= last {
            samples.push(due.elapsed().as_micros() as u64);
        }
        k += 1;
    }
    samples
}

fn report(name: &str, mut s: Vec<u64>) {
    s.sort();
    let n = s.len();
    if n == 0 {
        println!("# {name} 0 0 0 0");
    } else {
        println!("# {name} {} {} {} {n}", s[n / 2], s[n * 99 / 100], s[n - 1]);
    }
}

fn main() {
    let s = rt::arg();
    rt::runtime().block_on(async {
        let (me, mail) = unbounded_channel();
        report("idle", tokio::spawn(pinger(me, mail, 500)).await.unwrap());

        let (me, mail) = unbounded_channel();
        let p = tokio::spawn(pinger(me.clone(), mail, 1_000_000_000));
        let w = tokio::spawn(async move {
            let v = fib(30 + s);
            let _ = me.send(Msg::Done(Instant::now()));
            v
        });
        report("busy", p.await.unwrap());
        println!("{}", w.await.unwrap());

        let w = tokio::spawn(async move {
            let (k, n, size) = (100 * s as i64, 1000 * s as i64, 10000);
            let mut blocks: VecDeque<Vec<i64>> = (0..k).map(|j| (j..j + size).collect()).collect();
            let (me, mail) = unbounded_channel();
            let p = tokio::spawn(pinger(me.clone(), mail, 1_000_000_000));
            for i in 0..n {
                blocks.pop_back();
                blocks.push_front((1_000_000 + i..1_000_000 + i + size).collect());
                yield_now().await;
            }
            let _ = me.send(Msg::Done(Instant::now()));
            (p.await.unwrap(), blocks.iter().map(|b| b[0]).sum::<i64>())
        });
        let (samples, sum) = w.await.unwrap();
        report("heap", samples);
        println!("{sum}");
    });
}
