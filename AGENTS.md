# AGENTS.md

Guidance for automated coding agents working on this repository.

## Project scope

`ramflow` simulates sustained real-world RAM data movement. It is designed to observe system behavior under controlled RAM activity, not to certify memory correctness or replace dedicated memory test tools.

Keep the project technically honest:

- Do not describe ramflow as a RAM stress test.
- Do not describe ramflow as a benchmark.
- Do not describe ramflow as a replacement for MemTest86, memtest86+ or vendor validation tools.
- Prefer clear, boring, maintainable code over clever code. Humanity already has enough clever disasters.
- Keep Rust as the main cross-platform implementation.
- Keep C as the low-level Linux-friendly implementation.
- Avoid unsafe Rust unless there is a strong and documented reason.
- Avoid large dependencies unless they clearly simplify maintenance.

## Expected checks

After Rust changes, run when available:

```bash
cd rust
cargo fmt
cargo clippy
cargo build --release
```

After C changes, run when available:

```bash
cd c
make
```

If a check cannot be run in the current environment, mention that clearly in the PR or commit notes.
