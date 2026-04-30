# AGENTS.md

Guidance for automated coding agents working on this repository.

## Project scope

`ramflow` simulates sustained real-world RAM data movement. It is designed to observe system behavior under controlled RAM activity, not to certify memory correctness or replace dedicated memory test tools.

Keep the project technically honest:

- Do not describe ramflow as a RAM stress test.
- Do not describe ramflow as a benchmark.
- Do not describe ramflow as a replacement for MemTest86, memtest86+ or vendor validation tools.
- Prefer clear, boring, maintainable code over clever code. Humanity already has enough clever disasters.
- Keep C as the main implementation.
- Keep Go as the portable implementation for easy Windows/Linux builds.
- Keep Rust as an additional implementation variant, not as the project center.
- Avoid unsafe Rust unless there is a strong and documented reason.
- Avoid large dependencies unless they clearly simplify maintenance.
- Keep CLI behavior aligned across C, Go and Rust where practical.

## Implementation roles

```text
c/     Main implementation. Linux-first, low-level and explicit.
go/    Portable implementation. Simple standalone binaries for Windows and Linux.
rust/  Additional implementation variant. Useful, but not the main project identity.
```

## Expected checks

After C changes, run when available:

```bash
cd c
make
```

After Go changes, run when available:

```bash
cd go
gofmt -w main.go
go build
```

After Rust changes, run when available:

```bash
cd rust
cargo fmt
cargo clippy
cargo build --release
```

If a check cannot be run in the current environment, mention that clearly in the PR or commit notes.
