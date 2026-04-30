# Philosophy

`ramflow` is built around one simple idea: create controlled, repeatable RAM data movement without pretending to be something it is not.

The goal is observability, not synthetic scoring.

## Controlled memory activity

ramflow allocates a configurable amount of memory and repeatedly moves data between buffers. This creates sustained RAM activity that can be adjusted through allocation size, worker count, copy block size and pause intervals.

## Real-world-like data movement

Many real applications do not run dedicated memory test patterns. They allocate memory, copy data, transform buffers, move blocks and keep doing that for a long time because apparently computers needed cardio too.

ramflow tries to model that kind of boring but useful activity.

## No benchmark claims

ramflow does not report scores and should not be used to compare systems by throughput. Runtime status output exists only to show that work is still happening.

## No memory correctness claims

ramflow does not prove that memory is good or bad. It does not replace MemTest86, memtest86+ or any vendor-approved validation procedure.

If the goal is memory correctness testing, use dedicated memory test tools.

## Honest scope boundaries

ramflow can help observe system behavior under controlled RAM activity, especially during thermal observation, long-duration checks and plausibility testing.

It cannot certify hardware, validate a platform officially or replace professional qualification processes.
