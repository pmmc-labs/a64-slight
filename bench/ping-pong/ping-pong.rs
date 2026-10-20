// ping-pong: n round trips between two tasks. A mailbox is an unbounded
// channel, as slight's and Erlang's are (D224).
use tokio::sync::mpsc;

fn main() {
    let n = rt::arg();
    rt::runtime().block_on(async {
        let (ping, mut pings) = mpsc::unbounded_channel();
        let (pong, mut pongs) = mpsc::unbounded_channel();
        tokio::spawn(async move {
            while let Some(i) = pings.recv().await {
                pong.send(i).unwrap();
            }
        });
        for i in 0..n {
            ping.send(i).unwrap();
            pongs.recv().await;
        }
        println!("{n}");
    });
}
