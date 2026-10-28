# How-to

Short recipes for common jobs, each a program that runs as it stands.
They assume the [tour](../tour.md); the [concepts](../concepts/README.md)
explain why things are the way they are.

**Files and data**

- [Read a file](read-a-file.md): whole, a line at a time from a task, or
  from an actor
- [Read and write JSON](json.md): from strings, files and sockets
- [Read arguments and the environment](arguments.md): `@ARGV` and
  `%ENV`

**Other programs**

- [Run a command](run-a-command.md): to its end, or with input, a line
  at a time
- [Serve and call HTTP](http.md): a server with routes, and a client
- [Ship a program in a container](containers.md): `FROM scratch`

**Getting it right**

- [Test a program](testing.md): TAP with `(@include :test)`
- [Find a deadlock](deadlocks.md): reading the message, and looking
  inside a running program with `:system`
- [Find a race](races.md): other orders with `SLIGHT_SCHED`, and
  recording and replaying a run

Calls between slight programs, with `(@include :rpc)`, are in tutorial
[chapter 8](../tutorial/08-serving.md).
