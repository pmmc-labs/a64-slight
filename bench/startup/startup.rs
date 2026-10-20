// startup: prints 0, from inside the runtime, which every other program
// starts too.
fn main() {
    rt::runtime().block_on(async { println!("0") });
}
