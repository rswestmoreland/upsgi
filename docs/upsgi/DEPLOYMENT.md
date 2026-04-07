# upsgi deployment guidance

This note narrows the practical deployment story for the PSGI-first fork.

## Reverse-proxy-first recommendation

For most internet-facing deployments, run upsgi behind a trusted front end.

Why:
- TLS termination, HTTP/2, and broader client-facing protocol concerns stay at the edge
- upsgi can stay focused on lean HTTP/1.1 PSGI hosting
- trusted-front-end options such as `--log-x-forwarded-for` and `--enable-proxy-protocol` remain easier to reason about

Typical shape:
- reverse proxy on the public edge
- `http-socket` or `http11-socket` bound on loopback, private network, or service mesh address
- `static-map` only for baseline local assets that truly belong in the app server path
- runtime config launched with `./upsgi --config /etc/upsgi/app.yaml`

## Runtime config guidance

For new deployments, prefer YAML runtime configs loaded through `--config`.
Examples in this repo are YAML-first, while legacy INI, XML, and JSON configs
remain supported through the inherited multi-format runtime path.

## Current keepalive story

upsgi now has a clearer practical HTTP/1.1 story:
- HTTP/1.1 request reuse works on the `http11-socket` path
- explicit `Connection: close` is honored
- HEAD requests keep headers but suppress body bytes
- chunked request bodies are delivered through `psgi.input`

Important scope note:
- `--so-keepalive` is TCP keepalive, not an HTTP keepalive policy knob
- the narrowed fork does not add a Starman-style family of dedicated HTTP keepalive timeout controls in this milestone
- native HTTP/2 and edge-grade TLS policy belong at the trusted front end, not inside the default app-server story

## Logging posture

The default fork story keeps logging first-class:
- logfile sink retained
- logsocket sink retained
- rsyslog sink retained
- forwarded-for logging supported
- explicit PSGI exception visibility only when `log-exceptions` is turned on

## Static content guidance

`--static-map` remains a baseline feature, but use it deliberately:
- small app-owned static assets are fine
- edge caches or the front-end web server should still own larger static delivery in most production layouts
- `router_static` is not required solely for the baseline `static-map` path

## Suggested production baseline

- `master: true`
- `workers: N` sized to the workload and memory budget
- `need-app: true`
- `strict: true`
- `vacuum: true`
- `die-on-term: true`
- `http-socket` or `http11-socket` bound behind a trusted front end
- `log-x-forwarded-for: true` only when the trusted front-end chain is controlled
- `log-exceptions: true` only for targeted debugging windows

## Service management

Systemd is now the primary documented Linux service model for upsgi. See `docs/upsgi/SYSTEMD.md` and the shipped examples in `examples/systemd/`.
