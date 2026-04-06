# upsgi option surface

## Baseline-supported
- `--master`
- `--workers`
- `--http-socket`
- `--psgi`
- `--thunder-lock`
- `--max-requests`
- `--harakiri`
- `--need-app`
- `--strict`
- `--pidfile`
- `--vacuum`
- `--die-on-term`
- `--uid`
- `--static-map`
- `--log-x-forwarded-for`
- `--log-exceptions`

## Advanced/reference supported
- `--psgi-enable-psgix-io`
- `--reload-on-rss`
- `--limit-as`
- `--post-buffering`
- `--post-buffering-bufsize`
- `--buffer-size`
- `--so-keepalive`
- `--cpu-affinity`
- `--reload-mercy`
- `--worker-reload-mercy`
- `--touch-chain-reload`
- `--chain-reload-delay`
- `--master-fifo`
- `--cheaper` and related cheaper controls
- advanced logger selection and routing
- `--log-drain-burst`

## Compatibility-only
These options still parse for migration compatibility but do not change runtime behavior in the PSGI-only fork. In source, they are funneled through a dedicated parse-only compatibility shim callback so the behavior stays centralized and easy to audit.

- `--http-modifier1`
- `--http-modifier2`
- `--http-socket-modifier1`
- `--http-socket-modifier2`
- `--https-socket-modifier1`
- `--https-socket-modifier2`
- `--perl-no-die-catch`

## Default build story
The default build is intentionally small:
- main plugin: `psgi`
- embedded logging bundle: `logfile`, `logsocket`, `rsyslog`

The default build story does not center the broader upstream router, gateway, async, or multi-language ecosystems.

## Deferred / non-baseline
- non-Perl runtimes
- broader router and gateway families
- Emperor backend ecosystem
- alternate async or event-loop families
- non-baseline logging sinks
- stats and metrics operator surface pending fuller review
- `router_static` unless route-action usage is explicitly needed

## Post-buffering memory note
`--post-buffering` is the spill threshold. `--post-buffering-bufsize` is the read chunk size used during post buffering. These are intentionally independent in the fork so operators can keep per-core memory small while still buffering moderately sized request bodies in memory when appropriate.

## Worker lifecycle note
For preloaded PSGI apps, `--touch-chain-reload` or FIFO command `c` is the preferred graceful restart path. `--chain-reload-delay` can pace worker turnover to smooth RSS spikes on smaller hosts. See `WORKER_LIFECYCLE.md`.

## Accept-path note
`--thunder-lock` remains the preferred baseline for shared inherited listeners. When `map-socket` leaves a worker with only worker-local request sockets, the fork now bypasses the inter-process thunder lock automatically for that worker. `--reuse-port` alone does not imply per-worker accept sharding in the current inherited-listener model. See `ACCEPT_PATH.md`.

## Logging-path note
Master logging remains memory-first. `--log-drain-burst` bounds how many log records are drained per wake before yielding back to the event loop, which smooths bursts without introducing an unbounded in-memory queue. Stats now expose processed-record, backpressure, stall, and dropped-message counters for both generic logs and request logs.

## Handler-inspired additions note
The fork now supports `psgix.informational` for HTTP/1.1 requests without broadening the CLI into a Gazelle or Starman clone. Keepalive behavior is documented more clearly, but dedicated Starman-style keepalive tuning knobs are still deferred. See `DEPLOYMENT.md`.
