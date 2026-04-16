# Deferred borrowed-idea follow-up experiment matrix

This document narrows the remaining borrowed-idea exploration to the lines that
still look supportable after the first performance and scalability round.

No code changes are implied by this document. This is the scope lock for the
next review and prototype pass.

## Active follow-up lines

### 1. Thunder-lock review and redesign
Status: worth reviewing first.

Reason:
- true shared hot path
- existing implementation is conservative and may serialize more than necessary
- best chance of a default-on win if the measurements support it

Primary next step:
- runtime measurement pass using the implemented TL1 counters
- review watchdog/failure-state cleanup and any low-risk thunder-lock safety polish

Reference:
- `docs/performance/THUNDER_LOCK_REVIEW.md`

### 2. Narrow response-send fairness
Status: worth reviewing second.

Reason:
- broad generic fairness was already rejected
- narrow offload-path or app-download-path work may still help without touching
  the ordinary writer path

Primary next step:
- compare app-memory downloads, static/sendfile downloads, and offload-thread
  settings before proposing any response-path prototype

Reference:
- `docs/performance/RESPONSE_SEND_FAIRNESS_REVIEW.md`

### 3. Narrow sink isolation / bounded backpressure follow-up
Status: worth reviewing third.

Reason:
- the measured work says sink pressure is real
- the measured work also says it is not the first explanation for the mixed-mode
  control-lane pain
- the current tree already has meaningful bounded backpressure for server-owned
  logging sinks

Primary next step:
- observability-first review of server-owned sink drain behavior

Reference:
- `docs/performance/SINK_ISOLATION_REVIEW.md`

## Recommended order
1. thunder-lock safety and maintainability review on the classic global design
2. response-send fairness review around offload/download paths
3. sink isolation follow-up only for server-owned sinks or an explicit opt-in
   sink facility

## Default-on support rule
A prototype from this follow-up set is only eligible for mainline retention if
it:
- improves best-case mixed-mode scenarios meaningfully
- stays neutral, or at least not materially worse, in worst-case scenarios
- is supportable in a hot shared path
- does not regress accepted work

## Explicitly not front-of-queue
- broad generic writer fairness
- hidden internal app handoff queues
- Linux-specific accept rewrites as the first next step
- reviving the removed rotating socket-pool line
