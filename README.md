# upsgi

upsgi is a PSGI-first fork of the original upstream codebase.

It keeps the proven C prefork server model for Perl/PSGI hosting while reducing
the default product surface to what matters for real PSGI deployment. The fork
keeps compatibility where that materially helps adoption, but it now presents a
narrower, clearer public operator surface.

## Baseline fork direction

- PSGI-first
- HTTP-socket-first
- logging-enabled by default
- thunder lock enabled by default
- body scheduler enabled by default
- static-map kept as a baseline feature
- compatibility-minded for existing PSGI deployments
- YAML-first runtime configuration for new deployments
- TOML build profiles for the fork build system

## Default embedded plugins

- `psgi`
- `logfile`
- `logsocket`
- `rsyslog`

## Recommended quickstart path

Build and verify:

```sh
CPUCOUNT=1 python3 upsgiconfig.py --build default
./upsgi --version
```

Alternative local build path when your environment already matches the default
profile closely:

```sh
make
./upsgi --version
```

Run the shipped baseline example:

```sh
./upsgi --config examples/upsgi/baseline.yaml
```

Run the explicit exception-debugging example:

```sh
./upsgi --config examples/upsgi/debug_exceptions.yaml
```

Start from the commented template:

```sh
cp examples/upsgi/upsgi.example.yaml /etc/upsgi/app.yaml
./upsgi --config /etc/upsgi/app.yaml
```

## Default runtime posture

The current default runtime posture keeps the accepted baseline enabled without
extra tuning flags:

- thunder lock is enabled by default
- body scheduler is enabled by default
- logging queues remain enabled by default
- W4 slow-reader handling remains protected
- W5 static-serving improvements remain protected

Disable knobs are available when direct comparison or troubleshooting is needed:

- `--disable-thunder-lock`
- `--disable-body-scheduler`
- `--disable-log-queue`

Thunder lock is the shared-listener accept-serialization mechanism that mitigates
the thundering-herd problem. Body scheduler is the retained request-body
fairness path that reduces the damage caused by heavier upload readers in mixed
workloads.

## Current removal boundary

- Routing remains deferred from removal pending more use-case review.
- The default validated build profile keeps `routing = false` and `pcre = false`,
  so Routing remains a source-level deferred area rather than part of the
  recommended default runtime baseline.
- Legion support has been removed from the supported parser and runtime surface.
- Queue / sharedarea / subscription has been removed from the supported parser
  and runtime surface.
- Generic cache management and cache-backed response lookup have been removed.
- A reduced local cache subset remains only for static-path caching and SSL
  session cache support.
- Spooler / mule / farm has been removed from the supported parser and runtime surface.
- The conservative family-removal cleanup workstream is complete.
- Treat this reduced public surface as the current baseline unless Routing is
  re-approved for separate review.

## Documentation starting points

- `docs/upsgi/INDEX.md` - current curated entrypoint
- `docs/upsgi/QUICKSTART.md` - shortest supported path from source tree to a running instance
- `docs/upsgi/CONFIG_GUIDE.md` - curated runtime configuration model and example profiles
- `docs/upsgi/LOGGING.md` - logging defaults, stats posture, and queue guidance
- `docs/upsgi/RUNTIME_DEFAULTS.md` - what is on by default and how to disable it
- `docs/upsgi/THUNDER_LOCK.md` - what thunder lock is, how it works, and backend posture
- `docs/upsgi/BODY_SCHEDULER.md` - what the retained body scheduler does
- `docs/upsgi/HARDENING.md` - reverse-proxy posture, parser cautions, and deployment guardrails
- `docs/upsgi/SUPPORT_BOUNDARY.md` - current supported, deferred, removed, and compatibility-only surface
- `docs/upsgi/ARGUMENT_REFERENCE.md` - curated command-line and YAML key reference
- `docs/upsgi/REMOVAL_SCOPE_SUMMARY.md` - final family-removal scope boundary
- `docs/performance/PERFORMANCE_SCALABILITY_MASTER_PLAN.md` - measured performance roadmap and historical findings
- `docs/performance/THUNDER_LOCK_REVIEW.md` - thunder-lock design and validation history
- `docs/performance/THUNDER_LOCK_BACKENDS.md` - backend selection and recovery posture
- `docs/performance/N1_ACCEPTED_RUNTIME_BASELINE.md` - protected runtime baseline summary

## Compatibility notes

- `--http-modifier*` and related socket modifier flags remain accepted for
  migration compatibility only and have no runtime effect in the PSGI-only fork.
- `--perl-no-die-catch` remains accepted for migration compatibility only.
- `--log-exceptions` is the canonical flag for explicit PSGI exception logging
  during debugging.
- `--psgi-enable-psgix-io` enables the optional raw-socket escape hatch for
  PSGI apps that explicitly need it.
- New runtime examples should use `./upsgi --config path/to/file.yaml`.
- Older INI, YAML, XML, and JSON runtime configs still load through the
  inherited runtime config system.

## Public surface notes

The inherited parser surface is still broader than the curated upsgi operator
model. Public entry docs intentionally focus on the supported baseline.
Performance history remains available under `docs/performance/`, but the main
operator path is the curated `docs/upsgi/` set and the shipped example
configurations under `examples/upsgi/`.
