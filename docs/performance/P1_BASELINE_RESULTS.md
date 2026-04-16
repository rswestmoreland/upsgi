# P1 baseline results

## A1 warm keepalive UI mix
First measured baseline run:
- 50 clients
- 10 keepalive sequences per client
- 6 requests per sequence

Observed results:
- total successful requests: 3000
- request errors: 0
- reconnects: 1000
- throughput: 491.20 requests/sec
- request latency: p95 119.59 ms, p99 175.61 ms, max 194.83 ms
- sequence latency: p95 687.22 ms, p99 702.64 ms, max 756.26 ms

Sampled stats observations:
- final worker requests: 3001
- final worker avg_rt: 210
- final static_realpath_calls: 1000
- final static_stat_calls: 1000
- log_backpressure_events: 0
- log_dropped_messages: 0

## R1 steady small ingest
First measured baseline run:
- payload sizes: 8 KiB, 32 KiB, 64 KiB
- concurrency: 50
- requests: 100 per payload size
- traffic driver: ApacheBench

Observed results:

### 8 KiB payload
- requests/sec: 130.69
- mean request time: 382.59 ms
- p95: 383 ms
- failures: 0

### 32 KiB payload
- requests/sec: 36.02
- mean request time: 1387.98 ms
- p95: 1402 ms
- failures: 0

### 64 KiB payload
- requests/sec: 17.98
- mean request time: 2780.65 ms
- p95: 2806 ms
- failures: 0

Sampled stats observations:
- final worker requests: 301
- final worker avg_rt: 54498
- final static_realpath_calls: 0
- final static_stat_calls: 0
- log_backpressure_events: 0
- log_dropped_messages: 0

## Environment limits affecting collectors
- strace unavailable in this sandbox
- perf unavailable in this sandbox
- vmstat present but unusable in this sandbox
- /proc/<pid>/smaps_rollup was not usable for the server pid shape exposed here

## Early interpretation
- A1 is functionally stable under the first measured baseline, but connection reuse is not clean. Frequent reconnects remain the biggest notable signal.
- R1 shows a clear payload-size throughput cliff even at only 50 concurrency, which supports moving compressed/body-heavy scenarios up next.
