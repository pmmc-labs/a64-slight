// parallel-fib: 64 tasks each work out fib(k); the answers summed.
use tokio::sync::mpsc;

fn fib(n: u64) -> u64 {
    if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

fn main() {
    let k = rt::arg();
    rt::runtime().block_on(async {
        let (tx, mut rx) = mpsc::unbounded_channel();
        for _ in 0..64 {
            let tx = tx.clone();
            tokio::spawn(async move { tx.send(fib(k)).unwrap() });
        }
        let mut sum = 0;
        for _ in 0..64 {
            sum += rx.recv().await.unwrap();
        }
        println!("{sum}");
    });
}
