# Philosophy

`ramflow` is built around one simple idea: create controlled, repeatable RAM data movement without pretending to be something it is not.

The goal is observability, not synthetic scoring.

## Not the hammer approach

ramflow is not designed to hit a system with maximum possible pressure.

It should not behave like a sledgehammer. The point is not to force the system into failure as fast as possible, but to create a shaped workload that resembles sustained real software behavior.

A classic stress tool often asks:

```text
How hard can this system be pushed?
```

ramflow asks:

```text
How does this system behave under controlled, sustained memory data movement?
```

That distinction matters. Real systems often fail under ordinary, repeated, long-running activity rather than under one dramatic synthetic peak. Naturally, that is less theatrical, so people ignore it until production starts making clicking noises.

## Controlled memory activity

ramflow allocates a configurable amount of memory and repeatedly moves data between buffers. This creates sustained RAM activity that can be adjusted through allocation size, worker count, copy block size and pause intervals.

The pause interval is part of the design, not an afterthought. It allows the workload to be shaped away from constant maximum pressure and toward more realistic activity patterns.

## Real-world-like data movement

Many real applications do not run dedicated memory test patterns. They allocate memory, copy data, transform buffers, move blocks, cache data, flush data and keep doing that for a long time.

ramflow tries to model that kind of boring but useful activity.

Examples of behavior ramflow tries to approximate:

- repeated buffer copies
- long-running data movement
- sustained working sets
- moderate memory activity over time
- configurable pauses between work cycles

## No benchmark claims

ramflow does not report scores and should not be used to compare systems by throughput. Runtime status output exists only to show that work is still happening.

## No memory correctness claims

ramflow does not prove that memory is good or bad. It does not replace MemTest86, memtest86+ or any vendor-approved validation procedure.

If the goal is memory correctness testing, use dedicated memory test tools.

## Honest scope boundaries

ramflow can help observe system behavior under controlled RAM activity, especially during thermal observation, long-duration checks and plausibility testing.

It cannot certify hardware, validate a platform officially or replace professional qualification processes.
