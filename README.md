# ramflow

`ramflow` simulates sustained real-world memory data movement. It is designed to observe system behavior under controlled RAM activity, not to certify memory correctness or replace dedicated memory test tools.

## What ramflow is

- A configurable memory activity simulator
- A tool for observing system behavior under sustained RAM data movement
- Useful for thermal observation, long-running behavior checks and reproducible workload scenarios
- Implementation-first, not benchmark-first
- Built around C, with Go and Rust variants for portability and comparison of implementation styles

## What ramflow is not

- Not a RAM stress test
- Not a benchmark
- Not a replacement for MemTest86 or memtest86+
- Not a memory correctness certification tool
- Not a vendor validation procedure

For actual memory correctness testing, use MemTest86 or memtest86+.
For official platform validation, follow the vendor-approved validation process.

## Implementation roles

```text
c/         Main implementation. Linux-first, low-level, boring in the good way.
go/        Portable implementation. Easy Windows/Linux builds and simple distribution.
rust/      Additional implementation. Included because someone will ask "why not Rust?" and the internet must be fed occasionally.
docs/      Project documentation.
examples/  Example commands and scripts.
```

The C implementation is the main implementation because ramflow is fundamentally about simple, explicit memory movement. Go exists for portability and easy standalone binaries. Rust exists as a modern implementation variant, not as the center of the project.

## Build C version

```bash
cd c
make
```

Binary:

```text
c/ramflow-c
```

Example:

```bash
./ramflow-c --size 8G --workers 1 --block 4M --pause-ms 500
```

## Build Go version

```bash
cd go
go build -o ramflow-go
```

Windows cross-build from Linux:

```bash
cd go
GOOS=windows GOARCH=amd64 go build -o ramflow-go.exe
```

Examples:

```bash
./ramflow-go --size 8G --workers 1 --block 4M --pause-ms 500
./ramflow-go --size 16G --workers 1 --block 4M --pause-ms 250 --log ramflow.log
```

## Build Rust version

```bash
cd rust
cargo build --release
```

Windows binary:

```text
rust/target/release/ramflow.exe
```

Linux binary:

```text
rust/target/release/ramflow
```

Example:

```bash
./ramflow --size 8G --workers 1 --block 4M --pause-ms 500
```

## Suggested profiles

Light:

```bash
--size 8G --workers 1 --block 4M --pause-ms 500
```

Moderate:

```bash
--size 16G --workers 1 --block 4M --pause-ms 250
```

High:

```bash
--size 24G --workers 2 --block 4M --pause-ms 100
```

Adapt the allocation size to the installed RAM. Do not allocate 100% of physical memory. If the OS starts paging or swapping heavily, the workload no longer represents clean RAM activity. Watch system temperatures.

## Common options

The implementations aim to expose the same basic controls:

```text
--size       Total memory to allocate, for example 512M, 4G, 16G
--workers    Number of workers
--block      Copy block size, for example 1M, 4M, 64M
--pause-ms   Pause after each copy round
--duration   Optional runtime limit
--status-sec Status output interval
--touch-only Touch memory pages without copying between buffers
```

The Go and Rust implementations also support:

```text
--log        Optional log file
```

## License

Dual licensed under MIT OR Apache-2.0.
