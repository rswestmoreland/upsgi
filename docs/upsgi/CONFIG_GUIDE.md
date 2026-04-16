# Configuration guide

upsgi keeps the inherited multi-format runtime config parser, but the curated
operator baseline uses YAML examples for new deployments.

## Minimal baseline keys

```yaml
upsgi:
  master: true
  workers: 2
  need-app: true
  strict: true
  vacuum: true
  die-on-term: true
  http-socket: 127.0.0.1:9090
  psgi: /srv/app/app.psgi
  static-map: /static=/srv/app/static
  logto: /var/log/upsgi/app.log
  log-x-forwarded-for: true
```

## Reverse-proxy aware options

- `log-x-forwarded-for` controls whether request logging prefers `X-Forwarded-For` over `REMOTE_ADDR`
- `enable-proxy-protocol` turns on HAProxy PROXY v1 parsing for HTTP listeners

## Example profiles

- `examples/upsgi/baseline.yaml`
- `examples/upsgi/debug_exceptions.yaml`
- `examples/upsgi/body_scheduler.yaml`
- `examples/upsgi/logging_small_host.yaml`
- `examples/upsgi/logging_chatty_sink.yaml`
- `examples/upsgi/upsgi.example.yaml`
