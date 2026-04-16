# Changelog

## Unreleased

## 0.1.3 - 2026-04-15
### Added
- curated operator docs under `docs/upsgi/`, including quickstart, configuration, logging, hardening, runtime defaults, argument reference, packaging, migration, and support-boundary guides
- shipped YAML examples under `examples/upsgi/` for baseline startup, exception debugging, body-scheduler notes, and logging queue sizing
- explicit regression coverage for `psgix.io` keepalive safety and core `IO::Socket` method behavior

### Changed
- pruned unconditional Perl startup loads so `IO::Socket`, `IO::Handle`, and `IO::File` are no longer paid for in the default PSGI path when they are not needed
- fixed `psgix.io` to wrap a duplicated client fd instead of the live server-owned fd, preserving normal keepalive behavior when the feature is enabled but unused
- forced `autoflush(1)` on the wrapped `psgix.io` handle and replaced Perl-level `IO::Socket->new_from_fd(...)` dispatch with direct Perl core handle construction
- refreshed top-level README, install guidance, examples, and public repo packaging for the official `0.1.3` release

### Removed
- stale release artifacts, internal summary files, and generated build leftovers from the public source tree

## 0.1.2 - 2026-04-06
### Added
- maintained delayed-responder callback coverage for the non-writer PSGI path
- maintained startup validation coverage for release-facing runtime tuning clamps
- release-readiness summary and minor-nits sweep notes for the post-optimization fork state

### Changed
- clamp invalid `--log-drain-burst` values into a safe bounded runtime range
- retain a defensive startup clamp for negative `--chain-reload-delay` values, even though normal option parsing already rejects them
- refresh milestone tracking to mark the parser benchmark review and maintained test migration work complete

## 0.1.1 - 2026-04-05
### Added
- maintained Y1 benchmark artifacts for the post-maturation performance baseline and Y7 rerun under `tests/fork/artifacts/`
- retained duplicate `Transfer-Encoding` hardening coverage in the maintained fault harness
- operator-facing Y-phase review and implementation docs under the top-level `docs/` tree

### Changed
- reduced common request-time overhead in the retained HTTP socket path with a normalized `PATH_INFO` fast path and cached `SERVER_PORT` slice reuse
- reduced common PSGI bridge overhead by trimming `%env` construction work and constructing `psgi.input` and `psgi.errors` directly from cached stashes when available
- reduced simple request logging overhead with a second-level timestamp cache in `upsgi_logit_simple()` for single-threaded workers
- reduced common static candidate-path construction overhead with a stack-buffer fast path in `upsgi_file_serve()`
- refreshed the public source release artifacts and release decision/docs for `0.1.1` after the hardening and optimization cycle

## 0.1.0 - 2026-04-04
### Added
- PSGI-first default build profile and default embedded logging bundle
- explicit `--log-exceptions` PSGI debug flag
- fork-owned regression, fault, and initial soak harness under `tests/fork/`
- upsgi-specific quickstart, option-surface, packaging, migration, and deferred scope docs
- upsgi example configs and example PSGI app under `examples/upsgi/`

### Changed
- public binary name is now `upsgi`
- public version string is now `0.1.0`
- `--http-modifier*`, `--http-socket-modifier*`, and `--https-socket-modifier*` are now compatibility-only parse shims in the fork
- `--perl-no-die-catch` is now a compatibility-only parse shim in the fork
- default PSGI exception catch/logging is off; explicit exception visibility is opt-in via `--log-exceptions`
- default repo story is now centered on PSGI hosting rather than upstream multi-runtime breadth
