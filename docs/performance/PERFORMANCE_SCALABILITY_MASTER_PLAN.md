# upsgi Performance and Scalability Master Plan

## Objective
Validate how upsgi behaves on a resource-constrained host while preserving PSGI
correctness, HTTP compatibility, and resilience under noisy or hostile traffic.

This document covers the completed performance/scalability exploration and the
current runtime-baseline conclusion that came out of it.

## Accepted groundwork

### W4: PSGI slow-reader / async response behavior
Status: accepted.
- Generic writer fairness was explored and rejected as unsafe.
- The accepted result is the narrow PSGI-specific redesign for the async
  object/getline path.
- Slow-reader and mixed-load validation completed.

### W5: static-serving performance / metadata work
Status: accepted.
- Narrow static hot-path improvements completed.
- Static-focused validation completed.

## Measured conclusions before scheduler work

The measured evidence before the scheduler line converged on four points:
- response-side slow readers were comparatively well-contained after W4
- request-body pressure was the larger problem
- sink pressure was real, but not the primary explanation for control-lane pain
- mixed-host degradation tracked most strongly with a few larger, longer-lived
  bodies consuming too much shared service time

That made ingress/body scheduling the first long-term performance direction, with
sink isolation remaining the second lever.

## Scheduler workstream conclusion

### What was pursued
A QoS-inspired request-body scheduler meant to reduce the damage caused by a few
larger or longer-lived uploads occupying too much shared service time.

### What was proved
- B2c was the strongest validated scheduler checkpoint.
- B2c showed modest A5 improvement, neutral representative R4 behavior, and the
  best mixed-host mini-X1 improvement.
- B2d, B2e, and B2f did not clearly beat B2c.

### Current baseline posture
- B2c is the only active request-body scheduler candidate.
- B2d, B2e, and B2f remain historical measurement branches only.
- In the maintained runtime posture, the scheduler is enabled by default.
- Disable it only for controlled comparison through `--disable-body-scheduler`
  or `disable-body-scheduler: true`.

## Execution progress summary

### P0 Harness and manifest packaging
Status: complete.

### P1 Clean baselines
Status: partial.
- A1 completed the first measured baseline and a 200-client expansion.
- A1 500-client behavior completed only as a reduced sandbox pilot.
- R1 completed the first measured baseline.
- R1 200-concurrency expansion hit early listen-queue saturation before the full
  size sweep completed cleanly.

### P2 Early chokepoint identification
Status: partial.
- R2 focused measured compressed-ingest cases completed.
- R6 focused hostile-ingest run completed.
- A2 completed as a scaled measured screening pass and is reflected in the
  reconciled project checklist outside this file.

### P3 Static and body breakpoint evidence
Status: complete.
- A3 completed, including the static hot-path follow-up.
- R3 completed.

### P4 Slow-client pressure
Status: complete.
- A5 completed.
- R4 completed.
- A6 completed.

### P5 Sink and backpressure evidence
Status: complete.
- R5 completed.

### P6 Mixed-workload fairness
Status: complete through X1g.
- X1a through X1g completed well enough for the keep-or-discard scheduler
  decision.

### P7 Logging pressure and soak
Status: complete as focused measured passes.
- X2 completed.
- X3 completed as a focused measured pass rather than a long certification soak.

### P8 Download fairness and head-of-line effects
Status: complete as focused measured passes.
- A4 completed.
- X4 completed.

## What the focused performance program says now

- Keep W4.
- Keep W5.
- Keep B2c as the only active scheduler candidate.
- Do not reopen B2d, B2e, or B2f.
- Do not treat response-send fairness as a front-of-queue item based on current
  evidence.
- Treat sink isolation / bounded backpressure as the next performance lever only
  if later evidence makes it necessary.

## Narrowed deferred follow-up after the first performance round

The remaining borrowed-idea exploration is now narrowed to three lines only:
- thunder-lock review and redesign, with TL1 observability now implemented
- narrow response-send fairness around offload and download paths only
- narrow sink-isolation follow-up for server-owned sinks or an explicit opt-in
  facility only

These are tracked in:
- `docs/performance/DEFERRED_FOLLOWUP_EXPERIMENT_MATRIX.md`
- `docs/performance/THUNDER_LOCK_REVIEW.md`
- `docs/performance/RESPONSE_SEND_FAIRNESS_REVIEW.md`
- `docs/performance/SINK_ISOLATION_REVIEW.md`

## Immediate roadmap consequence

The focused performance/scalability exploration is complete enough to stop being
the front-of-queue workstream.

The broader roadmap should now proceed with:
- N1 stabilize the accepted runtime baseline
- N2 config and operator-surface cleanup
- N3 CLI and documentation completion
- N4 namespace and repo cleanup completion
- N5 hardening
- N6 observability and profiling surface cleanup
- N7 packaging, migration, and release preparation
- N8 deferred performance follow-up only if later evidence justifies it

Current deferred follow-up status:
- TL1 thunder-lock observability is now implemented
- TL2 per-socket accept-lock prototype was measured and ruled out for default-on use
- response-send fairness remains narrowed to offload and download paths only
- sink isolation remains narrowed to server-owned sinks or explicit opt-in facilities

## Thunder-lock backend posture
- startup reporting now exposes the active thunder-lock backend, owner-death recovery status, watchdog state, and degraded-backend notes
- robust process-shared pthread mutex remains the preferred default backend
- fd-lock fallback remains the next compatibility backend to implement

- fd-lock compatibility backend is now implemented for thunder lock; the next validation step is worker-death/restart coverage across robust-pthread and fd-lock modes
