# Sink isolation and bounded backpressure review

## What the measured work already says
The append-saturation evidence shows that sink service time can dominate the
receiver lane. The mixed-host discrimination work then showed that append delay
was real, but did not explain most of the control-lane pain as strongly as
request-body pressure did.

That means sink isolation remains interesting, but it should be treated as the
second lever, not the first one.

## What the current tree already has
For server-owned logging sinks, the current tree already includes a real bounded
backpressure design:
- bounded generic and request logger queues
- queue byte and record limits
- backpressure counters
- blocking flush behavior rather than silent drop when the queue is full
- `--threaded-logger` as the existing logger-drain isolation surface

That is already a serious implementation of lossless bounded backpressure for
server-owned logging sinks.

## What is supportable next

### SI1. Improve server-owned sink observability first
Before changing behavior, add or tighten measurements for:
- queue occupancy high-water marks
- flush batch sizes
- time blocked waiting for queue room
- time blocked during drain/flush
- per-sink drain lag where practical

Goal:
- prove whether server-owned sink drain behavior is a meaningful source of
  request-side pain in realistic configurations

### SI2. Refine logger-drain isolation only if SI1 justifies it
Candidate follow-up areas:
- tighter high-water and low-water policy review
- batch sizing review for stream-style sinks
- wake/drain policy review for the threaded logger path

This should stay inside server-owned logging sinks first.

### SI3. Treat app sink offload as explicit, not hidden
If receiver-style append offload is ever revisited, it should be explicit and
opt-in rather than a hidden generic write-behind queue inside upsgi.

That keeps the integrity and failure semantics visible to the operator and does
not quietly change PSGI request completion behavior.

## What should stay out
- hidden generic write-behind queues for arbitrary PSGI app sinks
- default-on internal handoff that can acknowledge success before the effective
  sink write semantics are clear
- broad server-core complexity added on behalf of one app pattern

## Success criteria
A sink-isolation follow-up is worth keeping only if it:
- helps a server-owned sink or an explicit opt-in sink facility
- improves mixed-mode behavior without weakening durability semantics
- remains understandable to operators through clear stats and backpressure
  behavior

## Stop conditions
Stop the line if:
- SI1 shows the logger-side sinks are not materially involved in the target hot
  path cases
- the proposed fix starts to look like a hidden internal queue for app work
- the semantics become harder to explain than the value they add
