# upsgi runtime stats sampler design

## Goal
Define a low-overhead, operator-facing periodic sampler that emits a compact runtime summary useful for production tuning.

This is a design and documentation step. It does not add a new runtime option yet.

## Why this should exist
upsgi already exposes meaningful runtime state through the stats server and stats push infrastructure. That surface is useful for tooling, but it is not ideal for operators who want a lightweight periodic summary in normal logs while tuning a deployment.

A periodic sampler would make it easier to answer practical questions such as:
- do I have enough workers?
- is the listen queue backing up?
- are logs stalling or being dropped?
- are harakiri and worker recycling thresholds too aggressive?
- do adaptive worker settings need adjustment?

## Existing runtime stats surface
The current inherited stats infrastructure already provides most of the raw material needed for a low-overhead sampler.

Key runtime entrypoints and implementation areas:
- `--stats`
- `--stats-server`
- `--stats-http`
- `--stats-minified` / `--stats-min`
- `--stats-no-cores`
- `--stats-no-metrics`
- `--stats-push`
- `--stats-pusher-default-freq` / `--stats-pushers-default-freq`

Relevant implementation paths:
- `core/master.c`
- `core/master_events.c`
- `core/master_utils.c`
- `core/stats.c`
- `core/metrics.c`
- `core/logging.c`

Observed values already exposed or assembled by the master-side stats generator include:
- `listen_queue`
- `listen_queue_errors`
- `signal_queue`
- `load`
- `log_records`
- `req_log_records`
- `log_backpressure_events`
- `req_log_backpressure_events`
- `log_sink_stall_events`
- `req_log_sink_stall_events`
- `log_dropped_messages`
- `req_log_dropped_messages`
- worker data
- socket data
- cache data
- metrics data when metrics are enabled

## Recommended feature shape
The feature should be framed as a **runtime stats sampler** or **periodic stats logger**, not as a benchmark mode.

Recommended characteristics:
- master-side only
- disabled by default
- periodic, with a clear interval in seconds
- one compact summary line per interval
- derived from already tracked counters and cheap gauges
- no per-request heavy instrumentation
- no requirement for an external collector

## Recommended first-pass interface
Document this as the intended future direction:
- `runtime-stats-sampler: true`
- `runtime-stats-sampler-interval: 60`
- `runtime-stats-sampler-format: kv`

Equivalent CLI spelling, if added later:
- `--runtime-stats-sampler`
- `--runtime-stats-sampler-interval 60`
- `--runtime-stats-sampler-format kv`

These names are deliberately explicit and do not collide with the inherited stats socket features.

## Recommended emitted fields
A first-pass sampler should stay compact and focus on production tuning.

Recommended fields:
- timestamp
- pid
- workers configured
- workers active
- workers busy or running requests, if cheaply available
- listen queue depth
- listen queue errors
- aggregate load
- harakiri count, if cheaply available
- worker respawns since start or since last sample
- reload count or reload pressure indicator, if cheaply available
- log backpressure events delta
- request-log backpressure events delta
- log sink stall events delta
- request-log sink stall events delta
- dropped log messages delta
- dropped request-log messages delta

Optional later fields:
- RSS or reload-on-rss pressure summary
- post-buffering spill indicators
- adaptive worker adjustments
- accept contention indicators when they are already tracked cheaply

## Output format recommendation
The recommended default is a single compact key-value log line.

Example shape:
`upsgi runtime-stats pid=1234 workers=4 active=4 listen_queue=0 listen_queue_errors=0 load=2 log_backpressure_delta=0 req_log_backpressure_delta=0 log_dropped_delta=0 req_log_dropped_delta=0`

Why key-value first:
- easy to read in journald and plain files
- easy to grep
- easy to parse later
- avoids forcing a JSON logging contract for a lightweight feature

A JSON mode can be considered later if there is strong operator demand.

## Safety and performance constraints
The sampler should not materially change runtime behavior.

Guardrails:
- no per-request allocations or formatting work for the sampler path
- read already maintained counters where possible
- compute deltas from prior sample state in the master process only
- bounded formatting work once per interval
- default-off
- coarse intervals, with 60 seconds as the likely documentation default

## Operator tuning use cases
The sampler should help answer concrete tuning questions.

### Worker count
Persistent queue depth, rising load, and sustained busy workers suggest more workers may be needed.

### Listen backlog
Non-zero queue growth or queue errors suggest the listen queue or accept path needs tuning.

### Logging path pressure
Backpressure, stall, or dropped-message deltas suggest sink pressure, routing problems, or insufficient drain settings.

### Harakiri and recycling
Repeated harakiri or respawn churn suggests timeout or worker lifecycle settings need review.

### Reload thresholds and memory pressure
Repeated worker recycling without a deployment reason suggests reload thresholds may be too low or the app needs profiling.

## Relationship to existing stats features
This sampler is not a replacement for:
- the stats socket
- HTTP stats exposure
- stats pushers
- external observability tooling

It is a small operational summary layer built from the same underlying state.

## Recommendation for this repo cycle
For this cycle:
- document the design
- keep the feature unimplemented
- continue using the existing stats socket for detailed inspection

Implementation can follow after the public docs, runtime config examples, and repo cleanup work are finished.
