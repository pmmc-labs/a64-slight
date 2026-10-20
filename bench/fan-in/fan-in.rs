// fan-in: 100 tasks each send their number n/100 times to one mailbox,
// which main sums. It's unbounded, as slight's and Erlang's are, so
// nothing slows the senders and most of the messages wait at once (D201,
// D224); Go's channel, which holds 1,024, blocks them.
use tokio::sync::mpsc;

fn main() {
    let n = rt::arg();
    rt::runtime().block_on(async {
        let (tx, mut rx) = mpsc::unbounded_channel();
        for s in 1..=100u64 {
            let tx = tx.clone();
            tokio::spawn(async move {
                for _ in 0..n / 100 {
                    tx.send(s).unwrap();
                }
            });
        }
        let mut sum = 0;
        for _ in 0..n {
            sum += rx.recv().await.unwrap();
        }
        println!("{sum}");
    });
}
