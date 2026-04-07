# upsgi Release Readiness

This document summarizes the current release/readiness posture after the optimization, hardening, and compatibility sweep through Checkpoint 69.

## Current assessment

The fork is in a strong release-candidate state for the PSGI-first scope that was locked for this project cycle.

The following areas are now covered by maintained code, docs, and tests:

- strict request-header hardening and duplicate handling
- strict PSGI env, input, error, logger, and response validation
- HTTP/1.1 keepalive, `Connection: close`, HEAD, and chunked-upload correctness for PSGI hosting
- lower-risk request-body memory improvements
- maintained fork regressions for env surface, body types, cleanup, harakiri, streaming, delayed responses, informational responses, logging backpressure, worker lifecycle controls, and accept-path behavior
- worker lifecycle pacing and documented PSGI-first restart guidance
- bounded logging-path draining with counters for backpressure and stalls
- selective handler-inspired support for `psgix.informational` / `103 Early Hints`

## What looks release-ready

### Protocol and PSGI boundary

- the maintained harness now covers both writer-based delayed streaming and the non-writer delayed responder callback path
- normal PSGI responses are validated strictly
- malformed or suspicious request-header duplication is rejected early

### Operability

- chain reload now has an explicit paced mode suitable for constrained PSGI hosts
- runtime tuning nits that could silently degrade behavior are now clamped and logged
- logging remains memory-first and bounded rather than growing an unbounded retry path

### Performance and memory posture

- the benchmark review showed modest wins on common GET and POST paths against the closest local pre-optimization baseline
- the optimized fork did not show a major common-path regression in the local review pass
- the request-body buffering cleanup lowers the risk of oversized fixed buffers on many-worker hosts

## Remaining non-blocking follow-ups

These items are still useful, but they do not look like release blockers for the current PSGI-first target.

### External measurement

- run a quieter dedicated benchmark package outside the sandbox
- compare against a byte-identical upstream/original upsgi + PSGI-plugin build
- add persistent-connection and tail-latency measurements

### Broader cleanup

- continue public-surface naming cleanup where it improves operator clarity
- review additional path-normalization and percent-decoding copy opportunities
- review whether any retained Perl extras can be documented as optional higher-cost features in future packaging guidance

### Packaging / release mechanics

- do a final source-release packaging pass
- verify docs/index cross-links and top-level release notes one more time
- decide the release number and release notes cut for the public repo

## Recommendation

The fork is ready for public repo upload and release artifact generation for 0.1.2.

For the next step, the best use of effort is not more core-path feature work. The best next step is:

1. upload the cleaned 0.1.2 source tree to the public repo
2. generate the 0.1.2 source release artifact and checksums
3. run the maintained regression sweep in CI on the public repo
4. cut the public 0.1.2 release tag and notes
