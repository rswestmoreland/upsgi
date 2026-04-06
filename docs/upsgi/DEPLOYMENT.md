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

## Current keepalive story

upsgi now has a clearer practical HTTP/1.1 story:
- HTTP/1.1 request reuse works on the `http11-socket` path
- explicit `Connection: close` is honored
- HEAD requests keep headers but suppress body bytes
- chunked request bodies are delivered through `psgi.input`

Important scope note:
- `--so-keepalive` is TCP keepalive, not an HTTP keepalive policy knob
- the narrowed fork does not add a Starman-style family of dedicated HTTP keepalive timeout controls in this milestone
- native HTTP/2 and HTTP/3 remain out of scope for the current C fork

## PSGI informational responses

The fork now exposes `psgix.informational` for HTTP/1.1 requests.

This callback accepts:
- informational status code
- arrayref of headers

Current intended use:
- `103 Early Hints` for preload/preconnect style hints before the final response

Current constraints:
- available only on HTTP/1.1 request paths
- must be called before the final response headers are sent
- intended for ordinary HTTP informational responses, not protocol switching

## Preload and graceful reload guidance

For PSGI apps that preload cleanly:
- preload in master when practical
- prefer chain reload to broad worker reload
- use `--chain-reload-delay` on smaller hosts if worker warm-up causes RSS spikes

This keeps the deployment story closer to the fork's goals:
- lean memory usage
- predictable worker turnover
- minimal operational surprises

## Scope discipline

upsgi intentionally does not try to become a CLI clone of Gazelle or Starman.

What it borrows instead:
- useful handler behavior such as `psgix.informational`
- clearer operational guidance around keepalive and graceful restart behavior
- stronger documentation around reverse-proxy-first deployment
