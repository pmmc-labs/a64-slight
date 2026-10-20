// spawn: n tasks started one after another, each sending its number back
// and ending; the numbers summed.
use tokio::sync::mpsc;

fn main() {
    let n = rt::arg();
    rt::runtime().block_on(async {
        let (tx, mut rx) = mpsc::unbounded_channel();
        for i in 0..n {
            let tx = tx.clone();
            tokio::spawn(async move { tx.send(i).unwrap() });
        }
        let mut sum = 0;
        for _ in 0..n {
            sum += rx.recv().await.unwrap();
        }
        println!("{sum}");
    });
}
