// idle-memory: n tasks, all waiting at once, each on a mailbox of its own
// (a channel, not a oneshot, which would leave out what a mailbox costs:
// D224), then each asked for 1; the answers summed. They're asked from a
// task started after them, since on one core main asking them itself
// would find none of them started: the queue is first in, first out, so
// that task runs once each of them is waiting.
use tokio::sync::mpsc;

fn main() {
    let n = rt::arg();
    rt::runtime().block_on(async {
        let (reply, mut replies) = mpsc::unbounded_channel();
        let mut asks = Vec::new();
        for _ in 0..n {
            let (ask, mut inbox) = mpsc::unbounded_channel::<()>();
            let reply = reply.clone();
            tokio::spawn(async move {
                inbox.recv().await;
                reply.send(1u64).unwrap();
            });
            asks.push(ask);
        }
        tokio::spawn(async move {
            for ask in &asks {
                ask.send(()).unwrap();
            }
        });
        let mut sum = 0;
        for _ in 0..n {
            sum += replies.recv().await.unwrap();
        }
        println!("{sum}");
    });
}
