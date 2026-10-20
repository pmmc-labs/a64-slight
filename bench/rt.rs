// The runtime every Rust benchmark starts (D224): tokio's current-thread
// one, every task on main's thread, one core, as slight runs; or, with
// BENCH_MC set, its multi-thread one, a worker per core, as #[tokio::main]
// makes it. One worker of that isn't one core: main's future has a thread
// of its own, so every message to it would cross threads.
use tokio::runtime::{Builder, Runtime};

pub fn runtime() -> Runtime {
    let mut b = if std::env::var_os("BENCH_MC").is_some() {
        Builder::new_multi_thread()
    } else {
        Builder::new_current_thread()
    };
    b.build().unwrap()
}

pub fn arg() -> u64 {
    std::env::args().nth(1).unwrap().parse().unwrap()
}
