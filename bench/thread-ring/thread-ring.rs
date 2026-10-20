// thread-ring: 503 tasks in a ring, each with a mailbox of its own, pass
// a token on n times; the one that takes it at 0 says who it is.
use tokio::sync::mpsc;

const RING: usize = 503;

fn main() {
    let n = rt::arg();
    rt::runtime().block_on(async {
        let (txs, rxs): (Vec<_>, Vec<_>) = (0..RING).map(|_| mpsc::unbounded_channel::<u64>()).unzip();
        let (done, mut dones) = mpsc::unbounded_channel();
        for (i, mut inbox) in rxs.into_iter().enumerate() {
            let (out, done) = (txs[(i + 1) % RING].clone(), done.clone());
            tokio::spawn(async move {
                while let Some(t) = inbox.recv().await {
                    if t == 0 {
                        done.send(i + 1).unwrap();
                        return;
                    }
                    out.send(t - 1).unwrap();
                }
            });
        }
        txs[0].send(n).unwrap();
        println!("{}", dones.recv().await.unwrap());
    });
}
