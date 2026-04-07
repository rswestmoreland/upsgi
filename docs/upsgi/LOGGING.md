# upsgi logging

## Baseline logging bundle
The default embedded logging bundle is:
- `logfile`
- `logsocket`
- `rsyslog`

These sinks remain first-class in the fork's default build.

## Baseline logging behavior
- normal request and error logging stays enabled unless explicitly disabled
- `--log-x-forwarded-for` is supported for forwarded client-IP logging
- PSGI exception stack logging is off by default
- `--log-exceptions` enables PSGI exception visibility for debugging

## Advanced logging
Advanced logger routing and encoding options remain available for operators who need them, including logger selection, request logger routing, worker logger routing, encoders, and log routes.

## Scope
The fork keeps logging first-class while narrowing the default build identity to the baseline logging bundle above.

## Boundary summary
- generic logging core: `core/logging.c`
- request identity preprocessing such as `--log-x-forwarded-for`: `core/protocol.c`
- sink backend registration: `plugins/logfile/logfile.c`, `plugins/logsocket/logsocket_plugin.c`, `plugins/rsyslog/rsyslog_plugin.c`
- PSGI exception visibility policy: PSGI layer only, behind `--log-exceptions`


## Master-side smoothing and counters
When `--log-master` is enabled, the master-side logger now drains a bounded number of records per wake and then yields back to the event loop. Use `--log-drain-burst` to control that bound.

The logging path stays memory-first: it relies on the existing socketpair buffering and does not introduce an unbounded retry queue. This keeps memory use predictable under load while still smoothing bursts.

The stats surface now exposes logging counters for both generic logs and request logs:
- `log_records` and `req_log_records`
- `log_backpressure_events` and `req_log_backpressure_events`
- `log_sink_stall_events` and `req_log_sink_stall_events`
- `log_dropped_messages` and `req_log_dropped_messages`

These counters are intended to make slow sinks and burst pressure visible without changing the default logging durability model.

## Runtime stats sampler design
The repo now includes a design document for a future low-overhead periodic runtime sampler:
- `docs/upsgi/RUNTIME_STATS_SAMPLER_DESIGN.md`

This is intended to complement the existing stats socket and stats-push surfaces with a compact operator-facing summary line suitable for normal service logs while tuning production settings.
