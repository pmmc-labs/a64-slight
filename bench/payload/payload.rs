// payload: ping-pong with something in each message (D237): n round trips
// between two tasks, each message the same Vec of 100 integers, which a
// channel moves without copying. A mailbox is an unbounded channel, as
// slight's and Erlang's are (D224).
use tokio::sync::mpsc;

fn main() {
    let n = rt::arg();
    rt::runtime().block_on(async {
        let (ping, mut pings) = mpsc::unbounded_channel::<Vec<i64>>();
        let (pong, mut pongs) = mpsc::unbounded_channel();
        tokio::spawn(async move {
            while let Some(data) = pings.recv().await {
                pong.send(data).unwrap();
            }
        });
        let mut data: Vec<i64> = (1..=100).collect();
        for _ in 0..n {
            ping.send(data).unwrap();
            data = pongs.recv().await.unwrap();
        }
        println!("{n}");
        println!("{}", data.iter().sum::<i64>());
    });
}
