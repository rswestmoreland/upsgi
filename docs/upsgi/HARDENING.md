# upsgi hardening notes

This document captures the current operator-facing hardening guidance for the retained PSGI-first fork.

## Current front-door assumptions
upsgi is designed around a retained HTTP socket front door with PSGI request handling, baseline static-map support, and first-class logging.

The current retained parser already has some useful protections:
- total request-line plus header-block growth is bounded by `--buffer-size`
- baseline static serving resolves the final target with `realpath()` and rejects paths outside the configured docroot unless they are under an explicit safe path
- static serving is limited to `GET` and `HEAD`

## Trusted-proxy-only options
These should be treated as trusted-front-end options, not internet-facing trust anchors.

### `--log-x-forwarded-for`
When enabled, the request logging path will use `X-Forwarded-For` instead of the direct peer address. That is correct only when the socket is behind a trusted reverse proxy. If the socket is directly reachable by clients, the logged address can be spoofed.

### `--enable-proxy-protocol`
This enables PROXY1 parsing on the HTTP socket path. It should only be used when the socket is reachable exclusively by a trusted front-end that is expected to send PROXY protocol frames.

## Recommended baseline knobs
For general deployments, the retained fork should continue to emphasize these options:
- `--strict`
- `--need-app`
- `--master`
- `--workers`
- `--vacuum`
- `--die-on-term`
- `--harakiri`
- `--buffer-size`
- `--limit-post`
- `--post-buffering`
- `--post-buffering-bufsize`

## Historical audit note
The earlier request-header audit identified ambiguous duplicate `Content-Length`, duplicate `Host`, and mixed `Transfer-Encoding` plus `Content-Length` handling as the highest-value retained front-door issues. Those parser hardening items are now fixed in the retained fork and are summarized below in the current retained parser hardening state section.

## Historical memory-safety audit note
The earlier memory-safety audit did not identify an immediate externally-triggerable buffer overflow in the retained HTTP socket parser path reviewed for the fork. It did identify several low-risk hardening opportunities, including unsigned-char ctype usage, safer `http_status_code()` bounds checks, explicit empty static-base rejection, and stricter `psgi.input->read()` offset validation. Those follow-up fixes are now part of the retained fork state summarized below.


## Current DoS/resource-control audit note
The third hardening audit confirmed that the retained fork already has useful knobs for request-time resource control:
- `--socket-timeout`
- `--limit-post`
- `--post-buffering`
- `--harakiri`
- `--max-requests`
- `--reload-on-rss`

It also identified several important limits in the current default story:
- `--limit-post` is enforced for `Content-Length` bodies, but the retained audit did not find an equivalent aggregate chunked-body ceiling in the HTTP socket path
- `--post-buffering` is useful, but it should be paired with `--limit-post` so disk-backed buffering does not simply move exhaustion from memory to `/tmp`
- `--post-buffering-bufsize` now controls only the read chunk buffer used while post buffering; it no longer silently expands to match the spill threshold, which keeps per-core resident memory lower on constrained hosts
- `body-read-warning` is advisory only; it does not cap application-driven request-body allocation

For internet-facing deployments, the maintained recommendation should now be treated as:
- explicitly set `--limit-post`
- explicitly set `--post-buffering`
- explicitly set `--harakiri`
- keep `--buffer-size` and `--socket-timeout` deliberate rather than relying on accidental defaults

These resource-control notes remain relevant for future flow-control review and release guidance.


## Historical PSGI/XS boundary audit note
The fourth hardening audit focused on the retained PSGI and XS bridge.

Main conclusions:
- the bridge usually catches Perl exceptions with `G_EVAL` and explicit `ERRSV` checks, which is a good baseline
- malformed-response handling and several exported XS helper validation paths were the highest-value fix area and are now reflected in the current retained PSGI/XS hardening state below
- `--psgi-enable-psgix-io` and retained helpers such as reload/signal should be treated as trusted-app-only capabilities, not sandbox-safe features

Operational guidance for now:
- leave `--psgi-enable-psgix-io` off unless an application explicitly requires it
- treat retained Perl helpers and bootstrap options (`perl-exec`, early exec/load paths, local::lib wiring) as trusted operator/application code surfaces
- do not represent upsgi as a safe isolation boundary for hosting untrusted PSGI applications from different tenants in the same process model


## Current retained parser hardening state
The retained fork now rejects several ambiguous request shapes at the HTTP socket front door:
- duplicate `Host`
- duplicate `Content-Length`
- duplicate `Transfer-Encoding`
- chunked `Transfer-Encoding` combined with `Content-Length`

`Content-Length` parsing is also now strict and overflow-aware, so malformed comma-joined or mixed-value forms are not accepted as numeric prefixes.

## Current PSGI/XS hardening state
The retained PSGI bridge now fails closed on several malformed app-controlled inputs that previously trusted value shape too early:
- `psgix.logger` requires a real hashref with defined `level` and `message`
- `upsgi::stream` requires an arrayref responder payload
- malformed PSGI headers/body/path objects now hit a safe invalid-response path instead of being dereferenced blindly
- `psgi.input->read()` now rejects negative or extreme read lengths/offsets


## Reload and worker-turnover guidance
For PSGI-first deployments on smaller hosts, prefer master-preloaded apps plus chain reload over broad worker fan-out reloads. When worker warm-up temporarily increases RSS, use `--chain-reload-delay` to pace turnovers and preserve copy-on-write sharing more smoothly.

## Accept-path guidance
For multi-worker PSGI deployments on constrained hosts, keep the accept topology deliberate:
- shared inherited listeners plus `--thunder-lock` remain the safe default
- worker-local mapped sockets now bypass the inter-process thunder lock automatically
- `--reuse-port` alone should not be treated as a sharding mechanism in the current inherited-listener model

This prevents operators from assuming a more distributed accept path than the current architecture actually provides.


## Logging-path guidance
The fork keeps the logging path memory-first. It does not add a disk-backed retry queue by default. For bursty workloads, `--log-master` plus `--log-drain-burst` smooths log draining while bounding time spent in the master logger per wake. Stats now expose logging backpressure, sink-stall, and dropped-message counters so operators can detect slow sinks before they become silent failures.
