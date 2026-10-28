# Ship a program in a container

On Linux, `slightc` links programs statically, so a program runs in an
empty image, with nothing but itself (D227).

```
node bin/slightc.ts --target x86_64 -o store server.slight
```

```
FROM scratch
COPY store /store
ENTRYPOINT ["/store"]
```

```
docker build -t store .
docker run --rm -p 7317:7317 store
```

The image is the binary, about 2 MB. Build on Linux, for the machine
the container runs on: `--target x86_64` for x86-64, or the default,
AArch64, for ARM. macOS has no static binaries, so a Mac builds the
program inside a Linux container, or on a Linux machine.

## What the runtime does for you

- **Stopping.** In a container the program is PID 1, which the kernel
  shields from signals it hasn't asked for. slight's runtime asks for
  SIGTERM, SIGINT and SIGHUP, so `docker stop` ends it at once, with
  status 143, and its commands with it. As PID 1 it also waits for
  anything its commands leave behind, as an init would.
- **Output.** What `pprint` writes goes out whenever the program waits,
  so `docker logs` has it as it happens, and a stopped program loses
  none of it.
- **Memory.** Each process's heap is capped at a quarter of the
  container's memory limit (`--memory`), so a runaway process faults on
  its own rather than the kernel killing the whole program
  (see [Memory](../concepts/memory.md)).
- **Names.** The runtime looks up host names itself, in the container's
  `/etc/hosts` and from the name servers in its `/etc/resolv.conf`, so
  two containers on a Docker network find each other by name.

## Listening

A server in a container listens on all interfaces, a port number rather
than `"127.0.0.1:port"`, so that Docker's published port reaches it:
`(rpc/listen 7317 handlers)` or `(http/listen 8080 handler)`. Neither
`rpc` nor `http` has authentication, so publish the port only where
that's safe.

The other way is a Unix socket in a directory shared with the host,
which needs no port, and whose file permissions say who may connect:

```
docker run --rm -v /run/store:/run/store --user $(id -u):$(id -g) store
```

with the program listening at `"/run/store/store.sock"`. `docker stop`
removes the socket; after `docker kill`, the next start replaces it.

[How-to](README.md)
