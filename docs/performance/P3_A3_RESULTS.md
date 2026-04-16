# P3 A3 static fanout results

## Cold page-load sequence
- sequences: 1
- requests: 14
- sequence latency: p95 38.60 ms
- document latency: p95 4.21 ms
- asset latency: p95 3.05 ms

Cold static counter deltas:
- requests: 14
- static_requests: 12
- static_path_cache_hits: 0
- static_path_cache_misses: 12
- static_realpath_calls: 12
- static_stat_calls: 12
- static_index_checks: 0

## Warm fanout load
- clients: 20
- iterations per client: 10
- sequences: 200
- successful requests: 2800
- throughput: 417.31 req/s
- sequence latency: p95 765.03 ms
- document latency: p95 89.94 ms
- api latency: p95 90.02 ms
- asset latency: p95 85.46 ms

Warm static counter deltas:
- requests: 2800
- static_requests: 2400
- static_path_cache_hits: 2400
- static_path_cache_misses: 0
- static_realpath_calls: 0
- static_stat_calls: 2400
- static_index_checks: 0

Sampled queue observations:
- max listen_queue: 10
- max listen_queue_errors: 0

## Interpretation
- The first cold page load paid exactly 12 static path misses, 12 realpath calls, and 12 stat calls for the 12 asset files.
- The warm phase added 2400 static path cache hits and 0 additional realpath calls, which is direct evidence that the W5 static path-cache work is effective under repeated fanout traffic.
- The warm phase still added 2400 static stat calls, so path caching removed path-resolution cost but did not remove per-request metadata cost. That keeps broader metadata caching as a possible future optimization, but outside the narrow W5 scope.
- No index checks were involved because this fixture maps direct asset files rather than directory index fallbacks.
- Queue pressure remained low in this focused run.
