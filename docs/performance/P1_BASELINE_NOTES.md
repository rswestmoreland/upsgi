# P1 baseline notes

## Completed measured runs
- A1 first measured baseline run
  - 50 clients
  - 10 keepalive sequences per client
  - total successful requests: 3000
- R1 first measured baseline run
  - 8 KiB, 32 KiB, and 64 KiB payload sizes
  - 50 concurrency
  - 100 requests per payload size

## Environment notes
- `strace` is not available in this sandbox, so the P2 collector records an availability note instead of syscall profile data.
- `perf` is not available in this sandbox, so the P3 collector records an availability note instead of CPU profile data.
- `vmstat` exists but is not usable in this sandbox; the P5 collector records a host note instead of disk profile data.
- `/proc/<pid>/smaps_rollup` is not usable against the server PID shape exposed here, so the P4 collector records an availability note.

## Key early observations
- A1 completed without request errors, but connection reuse still required frequent reconnects after static responses.
- R1 showed predictable throughput decay as payload size increased from 8 KiB to 64 KiB.
- Neither run showed logging backpressure or dropped-log counters in the sampled stats.
