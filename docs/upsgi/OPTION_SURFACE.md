# upsgi option surface

This document is the high-level map of the upsgi v1 operator surface.

For the fuller categorized argument reference, see:

- `docs/upsgi/SUPPORTED_ARGUMENTS.md`

For raw implementation breadth and the wider inherited parser table, inspect the
option definitions in:

- `core/upsgi.c`
- `plugins/psgi/psgi_plugin.c`
- `plugins/rsyslog/rsyslog_plugin.c`

## Public operator model

upsgi documents its option surface in layers:

- **Baseline-supported**: first-class options that belong in quickstart,
  examples, the canonical YAML template, and normal production deployment docs
- **Advanced-supported**: options that remain supported, but belong in deeper
  tuning or specialist documentation
- **Compatibility-only**: options kept to reduce migration friction
- **No-op compatibility**: accepted parser surface that intentionally has no
  runtime meaning in the PSGI-only fork

This avoids presenting every inherited parser token as equally recommended.

## Baseline-supported families

### Runtime entry and app loading
- `--config`
- `--psgi`
- `--chdir`
- `--env`
- `--need-app`
- `--strict`

### Listener and frontend
- `--http-socket`
- `--http11-socket`
- `--socket`
- `--protocol`
- `--socket-protocol`
- `--http-path-info-no-decode-slashes`

### Workers and lifecycle
- `--master`
- `--workers`
- `--processes`
- `--thunder-lock`
- `--touch-chain-reload`
- `--chain-reload-delay`
- `--reload-mercy`
- `--worker-reload-mercy`
- `--max-requests`
- `--harakiri`
- `--die-on-term`
- `--vacuum`

### Identity and service controls
- `--uid`
- `--gid`
- `--umask`
- `--pidfile`
- `--safe-pidfile`
- `--master-fifo`
- `--daemonize`
- `--daemonize2`

### Static serving
- `--static-map`
- `--static-map2`
- `--check-static`
- `--check-static-docroot`
- `--static-index`
- `--static-safe`
- gzip-related static controls

### Logging
- `--logto`
- `--logto2`
- `--log-format`
- `--logger`
- `--req-logger`
- `--worker-logger`
- `--log-master`
- `--log-drain-burst`
- `--log-syslog`
- `--log-socket`
- `--log-x-forwarded-for`
- `--log-4xx`
- `--log-5xx`
- `--touch-logreopen`

### Stats and observability
- `--stats`
- `--stats-server`
- `--stats-http`
- `--stats-minified`
- `--stats-no-cores`
- `--stats-no-metrics`
- `--stats-push`

### PSGI-specific
- `--log-exceptions`
- `--psgi-enable-psgix-io`
- `--perl-local-lib`
- `--perl-version`
- `--perl-exec`
- `--perl-auto-reload`
- `--early-perl`
- `--early-psgi`

## Advanced-supported families

### Adaptive workers
- `--cheaper`
- `--cheaper-*`

### Memory and buffering
- `--buffer-size`
- `--post-buffering`
- `--post-buffering-bufsize`
- `--body-read-warning`
- `--limit-as`
- `--max-fd`
- `--listen`
- `--listen-queue-alarm`
- `--offload-threads`
- `--memory-report`
- `--never-swap`
- `--ksm`

### Socket specialization and reload thresholds
- `--https-socket`
- `--shared-socket`
- `--undeferred-shared-socket`
- `--so-keepalive`
- `--socket-timeout`
- `--reuse-port`
- `--freebind`
- `--map-socket`
- `--reload-on-rss`
- `--reload-on-as`
- `--reload-on-uss`
- `--reload-on-pss`
- `--reload-on-fd`
- `--touch-reload`
- `--touch-workers-reload`
- `--touch-mules-reload`
- `--touch-spoolers-reload`

## Compatibility-only surface

These options are retained to reduce migration friction:

- explicit legacy config loaders such as `--ini`, `--yaml`, `--xml`, and `--json`
- inherited deployment forms that are still parsed but no longer lead the public
  operator story

## No-op compatibility surface

These options parse but intentionally do not affect runtime behavior in the
PSGI-only fork:

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

The default build story does not center the broader upstream router, gateway,
async, or multi-language ecosystems.

## Deferred / non-baseline

These areas may still exist in inherited shared code, but they are not part of
upsgi's primary public operator model:

- non-Perl runtimes
- broader router and gateway families
- Emperor backend ecosystem as the primary service model
- alternate async or event-loop families
- non-baseline logging sinks
- broader inherited parser breadth not documented as part of the supported
  public operator surface
- `router_static` unless explicit route-action usage is needed

## Related documents

- `docs/upsgi/SUPPORTED_ARGUMENTS.md`
- `docs/upsgi/RUNTIME_CONFIG_POLICY.md`
- `docs/upsgi/COMPATIBILITY.md`
- `docs/upsgi/DEPLOYMENT.md`
- `docs/upsgi/WORKER_LIFECYCLE.md`
- `docs/upsgi/ACCEPT_PATH.md`
