# upsgi quickstart

## Baseline launch

```ini
[uwsgi]
master = true
workers = 2
need-app = true
strict = true
vacuum = true
die-on-term = true
http-socket = :9090
psgi = app.psgi
static-map = /assets=./public
logto = ./upsgi.log
log-x-forwarded-for = true
```

Run the default build:

```sh
make
./upsgi --ini baseline.ini
```

## Baseline-supported options
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

## Advanced but supported
- `--psgi-enable-psgix-io`
- `--reload-on-rss`
- `--limit-as`
- `--post-buffering`
- `--buffer-size`
- `--so-keepalive`
- `--cpu-affinity`
- advanced logger routing options

## Notes
- New baseline examples should not include compatibility-only flags.
- For internet-facing deployments, prefer a trusted reverse proxy in front of the HTTP socket. See `DEPLOYMENT.md`.
- `--log-exceptions` is opt-in and intended for debugging.
- `--static-map` is a baseline core-path feature.
- request/static ownership is split between `core/protocol.c` and `core/static.c`; `router_static` is separate and not required solely for baseline static-map.

## Shipped release examples
- `examples/upsgi/app.psgi`
- `examples/upsgi/baseline.ini`
- `examples/upsgi/debug_exceptions.ini`
- `examples/upsgi/migration_legacy.ini`

## Docs map
- For the full maintained docs map, start at `docs/upsgi/INDEX.md`.
