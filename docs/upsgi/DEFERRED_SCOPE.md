# upsgi deferred and non-baseline scope

This document exists so the first public release does not accidentally read like
full upstream parity.

## Baseline product story for v1
- PSGI-first
- HTTP-socket-first
- logging-enabled by default
- `static-map` retained as a baseline feature
- compatibility-aware for existing PSGI deployments

## Intentionally deferred or non-baseline
- non-Perl runtimes
- broader router and gateway families as part of the supported product story
- Emperor ecosystem breadth
- alternate async or event-loop families as part of the supported product story
- non-baseline logging sinks beyond the default bundled set:
  - `logfile`
  - `logsocket`
  - `rsyslog`
- broader operator-facing stats and metrics surface beyond the documented sampler design remains deferred
- `router_static` as a required dependency for baseline static-map

## Retained but not centered in the release story
Some upstream subsystems remain in the tree because this is a fork, but they are
not part of the primary release message for the first public version. Retained
source presence should not be mistaken for first-class support claims.

## Why this matters
Release docs, examples, and migration notes must describe what the fork is:
- a production-oriented PSGI server fork
- not a promise of full upstream multi-runtime breadth

## Tree cleanup already applied
- unsupported upstream standalone plugin directories are no longer carried in the maintained fork tree
- the old external installer helper path was removed
- generated build/test/release artifacts are intentionally kept out of the source tree
