# ramflow

`ramflow` simulates sustained real-world memory data movement. It is designed to observe system behavior under controlled RAM activity, not to certify memory correctness or replace dedicated memory test tools.

## What ramflow is

- A configurable memory activity simulator
- A tool for observing system behavior under sustained RAM data movement
- Useful for thermal observation, long-running behavior checks and reproducible workload scenarios
- Cross-platform, with Rust as the main implementation
- Also available as a C implementation for Linux-focused low-level usage

## What ramflow is not

- Not a RAM stress test
- Not a benchmark
- Not a replacement for MemTest86 or memtest86+
- Not a memory correctness certification tool
- Not a vendor validation procedure

For actual memory correctness testing, use MemTest86 or memtest86+.
For official platform validation, follow the vendor-approved validation process.

## Repository layout

```text
rust/      Cross-platform Rust implementation
c/         Linux-friendly C implementation
docs/      Project documentation
examples/  Example commands and scripts
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

## Build C version

```bash
cd c
make
```

Binary:

```text
c/ramflow-c
```

## Examples

Windows:

```powershell
.\ramflow.exe --size 8G --workers 1 --block 4M --pause-ms 500
.\ramflow.exe --size 16G --workers 1 --block 4M --pause-ms 250 --log ramflow.log
```

Linux:

```bash
./ramflow --size 8G --workers 1 --block 4M --pause-ms 500
./ramflow-c --size 8G --workers 1 --block 4M --pause-ms 500
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

## License

Dual licensed under MIT OR Apache-2.0.
