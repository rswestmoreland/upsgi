# upsgi quickstart

## Baseline launch

```yaml
upsgi:
  master: true
  workers: 2
  need-app: true
  strict: true
  vacuum: true
  die-on-term: true
  http-socket: :9090
  psgi: examples/upsgi/app.psgi
  static-map: /assets=examples/upsgi/public
  logto: ./upsgi.log
  log-x-forwarded-for: true
```

Run the default build and start the baseline example:

```sh
make
./upsgi --config examples/upsgi/baseline.yaml

For a fuller starting point, copy and edit:

```
./upsgi --config examples/upsgi/upsgi.example.yaml
```
```

Existing INI, XML, JSON, and YAML configs still load through the retained
multi-format runtime path. YAML is simply the preferred format for new runtime
configs and examples.

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
- `examples/upsgi/baseline.yaml`
- `examples/upsgi/debug_exceptions.yaml`
- `examples/upsgi/migration_legacy.yaml`
- `examples/upsgi/upsgi.example.yaml`

## Docs map
- For the full maintained docs map, start at `docs/upsgi/INDEX.md`.
