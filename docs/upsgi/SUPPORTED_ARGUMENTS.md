# upsgi supported arguments reference

This document is the operator-facing argument reference for upsgi v1.

It intentionally documents the curated, supported public surface of the fork,
not every inherited parser token from the original upstream codebase. For raw
implementation breadth, inspect the option definitions in `core/upsgi.c` and
the relevant plugin option tables under `plugins/`.

upsgi still inherits a much larger parser table from shared core code. That raw
breadth is useful for implementation review and compatibility analysis, but it
is not the best way to present the supported public operator surface.

## How to read this reference

Each option is grouped into one of these categories:

- **Baseline**: first-class upsgi v1 operator surface
- **Advanced**: supported, but not part of the default quickstart story
- **Compatibility-only**: accepted to ease migration, but not part of the
  recommended public shape
- **No-op compatibility**: parsed for migration compatibility and intentionally
  has no runtime effect in the PSGI-only fork

## Runtime config mapping

For the shipped YAML examples and templates, config keys map to the long option
name without the leading `--`.

Examples:

- `--http-socket` -> `http-socket:`
- `--workers` -> `workers:`
- `--static-map` -> `static-map:`
- `--log-exceptions` -> `log-exceptions:`

Canonical runtime entrypoint:

```sh
./upsgi --config path/to/file.yaml
```

Retained compatibility paths still supported by the inherited runtime loader:

```sh
./upsgi --ini legacy.ini
./upsgi --yaml config.yaml
./upsgi --xml config.xml
./upsgi --json config.json
```

## Baseline supported arguments

## Listener and frontend

### `--http-socket`
Primary listener for new deployments. This is the first-class frontend socket
shape for the fork.

### `--http11-socket`
HTTP/1.1 oriented listener variant. Useful when the deployment explicitly wants
clear HTTP/1.1 listener semantics.

### `--http-path-info-no-decode-slashes`
Keep encoded slashes from being decoded into `PATH_INFO`. Useful behind
frontends or apps that rely on preserving encoded path separators.

### `--socket`
Generic listener socket inherited from shared core. Supported, but new operator
examples should prefer `--http-socket` unless a different protocol flow is
intentional.

### `--upsgi-socket`
Internal protocol listener retained from shared core. Supported as part of the
inherited stack, but not part of the primary public deployment story.

### `--protocol`
Explicitly set the protocol for generic socket usage.

### `--socket-protocol`
Set the protocol for a specific socket declaration. More common in advanced or
multi-socket inherited deployments.

## Process model and worker lifecycle

### `--master`
Enable the master process. Recommended for normal production deployments.

### `--workers`
Set worker count. This is one of the primary tuning knobs for CPU and workload
shape.

### `--processes`
Alias-style inherited worker count control retained from shared core.

### `--thunder-lock`
Enable serialized accept coordination for inherited shared listeners. This is a
recommended baseline setting for common prefork deployments.

### `--thunder-lock-watchdog`
Additional watchdog behavior around thunder-lock usage.

### `--need-app`
Fail startup if the PSGI app does not load. Recommended baseline safety option.

### `--strict`
Reject unknown or malformed options more aggressively. Recommended baseline
safety option.

### `--vacuum`
Clean up sockets and pidfiles on exit. Recommended baseline option.

### `--die-on-term`
Treat TERM as a normal shutdown path. Recommended baseline option and especially
useful under service managers.

### `--touch-chain-reload`
Trigger paced worker reloads when the watched file is touched. Preferred
low-disruption graceful reload path for preloaded PSGI apps.

### `--chain-reload-delay`
Delay between worker restarts during chain reload. Useful for smoothing reload
bursts on small hosts.

### `--reload-mercy`
Grace period before harder reload handling.

### `--worker-reload-mercy`
Worker-specific reload grace period.

### `--max-requests`
Recycle workers after a request count threshold. Useful for bounding long-lived
fragmentation or leaks.

### `--max-requests-delta`
Stagger max-requests recycling between workers.

### `--harakiri`
Request timeout kill threshold. One of the main production safety controls.

### `--harakiri-verbose`
Log more detail when harakiri actions trigger.

### `--harakiri-graceful-timeout`
Grace period before escalating from graceful handling to harder termination.

### `--harakiri-graceful-signal`
Signal to use for graceful timeout handling.

### `--harakiri-queue-threshold`
Only apply harakiri when queue pressure crosses a threshold.

### `--harakiri-no-arh`
Inherited harakiri behavior toggle retained from shared core.

## App loading and deployment identity

### `--psgi`
Path to the PSGI app entrypoint. Core application loading option for the fork.

### `--chdir`
Change working directory before app load.

### `--module`
Set module loading behavior from inherited shared core.

### `--env`
Set environment variables for the runtime.

### `--uid`
Drop privileges to the specified user ID after privileged setup.

### `--gid`
Drop privileges to the specified group ID.

### `--umask`
Set process umask.

### `--pidfile`
Write a pidfile. Still supported, though systemd-style service management should
be preferred where available.

### `--safe-pidfile`
Safer pidfile handling variant.

### `--daemonize`
Detach and log to a file. Still supported, but not the preferred service model
for modern Linux deployments.

### `--daemonize2`
Alternate daemonize path with related inherited behavior.

### `--master-fifo`
Create a FIFO control path for maintenance and manual runtime control.

## Static serving

### `--static-map`
Canonical static mapping feature kept first-class in upsgi.

### `--static-map2`
Additional static mapping form retained from shared core.

### `--check-static`
Check the listed path or docroot for static file handling.

### `--check-static-docroot`
Use an alternate docroot for static existence checks.

### `--static-index`
Index filename for directory static serving.

### `--static-safe`
Restrict static serving to configured safe paths.

### `--static-gzip`
Serve precompressed gzip assets when present.

### `--static-gzip-all`
Broader static gzip behavior.

### `--static-gzip-dir`
Directory prefix used for gzip asset lookup.

### `--static-gzip-prefix`
Prefix used in gzip asset resolution.

### `--static-gzip-ext`
Extension used for gzip asset resolution.

### `--static-gzip-suffix`
Suffix used for gzip asset resolution.

### `--file-serve-mode`
File serving mode toggle retained from shared core.

## Logging

### `--logto`
Write logs to a file path. Baseline logging option.

### `--logto2`
Secondary file log target.

### `--log-format` and `--logformat`
Set request log formatting.

### `--logformat-strftime` and `--log-format-strftime`
Enable strftime expansion in log formatting.

### `--logger`
Configure a named logger route or sink.

### `--logger-list` and `--loggers-list`
List configured loggers.

### `--req-logger` and `--logger-req`
Configure request logging routes.

### `--worker-logger` and `--worker-logger-req`
Configure worker-side logging routes.

### `--log-master`
Route logging through the master-managed path.

### `--log-master-bufsize`
Size the master log buffer.

### `--log-master-stream`
Stream master-managed logs more directly.

### `--log-drain-burst`
Bound how many queued records are drained per wake. Useful for smoothing bursty
logging without creating an unbounded drain loop.

### `--log-syslog`
Enable syslog-style logging through the inherited logging surface.

### `--log-socket`
Send logs to a socket target.

### `--log-x-forwarded-for`
Include `X-Forwarded-For` in request logs. Recommended behind trusted reverse
proxies when you want proxy-aware client identity in logs.

### `--log-4xx`
Enable logging for 4xx responses.

### `--log-5xx`
Enable logging for 5xx responses.

### `--disable-logging`
Disable request logging.

### `--touch-logreopen`
Watch a file trigger to reopen logs.

## Stats and observability

### `--stats`
Expose a stats socket.

### `--stats-server`
Enable the stats server.

### `--stats-http`
Expose stats over HTTP.

### `--stats-minified` and `--stats-min`
Emit smaller stats payloads.

### `--stats-no-cores`
Reduce per-core details in stats output.

### `--stats-no-metrics`
Reduce metrics detail in stats output.

### `--stats-push`
Push stats to a configured destination.

### `--stats-pusher-default-freq` and `--stats-pushers-default-freq`
Control default stats push frequency.

### Runtime stats sampler direction
The repo now documents a planned low-overhead periodic runtime sampler in `docs/upsgi/RUNTIME_STATS_SAMPLER_DESIGN.md`.

That planned feature is intentionally separate from the inherited stats socket and stats-push paths. The goal is a compact operator-facing summary line for production tuning, not a replacement for the detailed stats surface.

## PSGI-specific arguments

### `--log-exceptions`
Canonical debug flag for explicit PSGI exception visibility.

### `--psgi-enable-psgix-io`
Enable PSGIX IO support in the PSGI plugin.

### `--perl-local-lib`
Set a local Perl library path.

### `--perl-version`
Select a Perl runtime version in environments where multiple versions are
available.

### `--perl-args` and `--perl-arg`
Pass Perl-specific arguments.

### `--perl-exec`
Execute Perl code before normal runtime flow.

### `--perl-exec-post-fork`
Execute Perl code after worker fork.

### `--perl-auto-reload`
Enable Perl-side autoreload behavior.

### `--perl-auto-reload-ignore`
Exclude paths from Perl autoreload behavior.

### `--plshell`
Enter the Perl shell mode retained from the inherited plugin.

### `--plshell-oneshot`
Run the Perl shell in one-shot mode.

### `--perl-no-plack`
Disable Plack integration path where inherited behavior still allows it.

### `--early-perl`
Initialize Perl early in startup.

### `--early-psgi`
Initialize PSGI early in startup.

### `--early-perl-exec`
Run Perl execution hooks earlier in startup.

## Advanced supported arguments

## Adaptive worker controls

### `--cheaper`
Enable adaptive worker scaling.

### `--cheaper-initial`
Initial worker count when cheaper mode is enabled.

### `--cheaper-step`
Worker adjustment step size in cheaper mode.

### `--cheaper-overload`
Backlog or overload threshold used by cheaper logic.

### `--cheaper-algo`
Select the cheaper algorithm.

### `--cheaper-count`
Worker count constraint for cheaper mode.

### `--cheaper-idle`
Idle threshold for cheaper worker decisions.

### `--cheaper-busyness-max`
Upper busyness threshold.

### `--cheaper-busyness-min`
Lower busyness threshold.

### `--cheaper-busyness-multiplier`
Multiplier used by busyness-driven cheaper behavior.

### `--cheaper-busyness-verbose`
Verbose logging for busyness-driven cheaper decisions.

## Memory, buffering, and resource limits

### `--buffer-size`
Request header and buffer sizing control.

### `--post-buffering`
Body spill threshold. Useful for controlling when larger request bodies spill out
of memory-first handling.

### `--post-buffering-bufsize`
Chunk size used while post-buffering request bodies.

### `--body-read-warning`
Warn when request bodies cross a configured threshold.

### `--limit-as`
Address-space limit.

### `--max-fd`
Raise or constrain open file descriptor count.

### `--listen`
Listener backlog size.

### `--listen-queue-alarm`
Trigger logging or alert behavior when queue pressure crosses a threshold.

### `--offload-threads`
Enable offload helper threads for supported paths.

### `--memory-report`
Emit memory reporting information.

### `--never-swap`
Try to avoid swap usage.

### `--ksm`
Kernel same-page merging control retained from shared core.

## Advanced socket controls

### `--https-socket`
HTTPS listener retained from shared core.

### `--shared-socket`
Create a shared socket declaration.

### `--undeferred-shared-socket`
Undeferred shared socket creation variant.

### `--so-keepalive`
Enable socket keepalive.

### `--socket-timeout`
Socket timeout control.

### `--reuse-port`
Use `SO_REUSEPORT` where applicable. Useful, but not a substitute for the
fork's documented accept-path guidance.

### `--freebind`
Allow binding to nonlocal addresses where supported.

### `--map-socket`
Map sockets to specific workers or components.

## Advanced reload and recycling controls

### `--min-worker-lifetime`
Minimum worker lifetime before recycling.

### `--max-worker-lifetime`
Maximum worker lifetime.

### `--max-worker-lifetime-delta`
Delta or staggering control for maximum worker lifetime.

### `--reload-on-as`
Reload workers when address-space usage exceeds a threshold.

### `--reload-on-rss`
Reload workers when RSS exceeds a threshold.

### `--reload-on-uss`
Reload workers when USS exceeds a threshold.

### `--reload-on-pss`
Reload workers when PSS exceeds a threshold.

### `--evil-reload-on-as`
More aggressive address-space reload behavior retained from shared core.

### `--evil-reload-on-rss`
More aggressive RSS reload behavior retained from shared core.

### `--reload-on-fd`
Reload based on file descriptor usage.

### `--brutal-reload-on-fd`
Harder file-descriptor-based reload behavior.

### `--touch-reload`
Reload on touch file trigger.

### `--touch-workers-reload`
Reload workers on touch file trigger.

### `--touch-mules-reload`
Reload mules on touch file trigger.

### `--touch-spoolers-reload`
Reload spoolers on touch file trigger.

## Compatibility-only arguments

These options are still accepted so older PSGI-oriented configs and service
scripts can continue to parse.

### `--ini`
Explicit legacy INI loader. Retained for migration compatibility; new examples
should prefer `--config file.yaml`.

### `--yaml`
Explicit YAML loader retained from the inherited loader family.

### `--xml`
Explicit XML loader retained from the inherited loader family.

### `--json`
Explicit JSON loader retained from the inherited loader family.

## No-op compatibility arguments

These options parse successfully for migration compatibility but intentionally do
not affect runtime behavior in the PSGI-only fork.

### `--http-modifier1`
Compatibility-only parse/no-op.

### `--http-modifier2`
Compatibility-only parse/no-op.

### `--http-socket-modifier1`
Compatibility-only parse/no-op.

### `--http-socket-modifier2`
Compatibility-only parse/no-op.

### `--https-socket-modifier1`
Compatibility-only parse/no-op.

### `--https-socket-modifier2`
Compatibility-only parse/no-op.

### `--perl-no-die-catch`
Compatibility-only parse/no-op. Default PSGI exception catch logging is already
not enabled by default in the fork. Use `--log-exceptions` when you want
explicit debug visibility.

## Logging sink notes

The default build embeds these logging plugins:

- `logfile`
- `logsocket`
- `rsyslog`

The rsyslog plugin adds:

### `--rsyslog-packet-size`
Packet size for rsyslog output.

### `--rsyslog-hostname`
Hostname override for rsyslog output.

## Common deployment examples

## Minimal local baseline

```sh
./upsgi --http-socket 127.0.0.1:9090 --master --workers 2 --need-app --strict \
  --vacuum --die-on-term --psgi examples/upsgi/app.psgi
```

## YAML-first runtime launch

```sh
./upsgi --config examples/upsgi/baseline.yaml
```

## Full commented YAML template

```sh
cp examples/upsgi/upsgi.example.yaml /etc/upsgi/app.yaml
./upsgi --config /etc/upsgi/app.yaml
```

## Debug explicit PSGI exceptions

```sh
./upsgi --config examples/upsgi/debug_exceptions.yaml
```

## Legacy config compatibility launch

```sh
./upsgi --config existing.ini
./upsgi --ini existing.ini
```

## Related documents

- `docs/upsgi/OPTION_SURFACE.md`
- `docs/upsgi/RUNTIME_CONFIG_POLICY.md`
- `docs/upsgi/COMPATIBILITY.md`
- `docs/upsgi/DEPLOYMENT.md`
- `docs/upsgi/QUICKSTART.md`
