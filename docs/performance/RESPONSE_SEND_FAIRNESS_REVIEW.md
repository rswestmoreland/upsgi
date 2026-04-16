# Response-send fairness review and narrowed experiment plan

## Current posture
The broad generic writer-fairness idea was explored earlier and rejected as too
risky. W4 remains accepted and protected.

That does not mean all response-path follow-up is off the table. It means the
remaining exploration space needs to stay narrow and focused on paths that can
benefit without disturbing the ordinary small-response writer path.

## Current implementation shape
- sendfile offload is already supported through the offload subsystem when the
  socket can offload
- the offload subsystem also has transfer, pipe, and memory engines
- the current tree exposes `--offload-threads`, but the normal app response path
  is still primarily the worker write loop unless the response is using a path
  that explicitly goes through offload
- the A4 app download fixture returns large response bodies from PSGI memory,
  which makes it a useful probe for app-generated download head-of-line effects

One useful implementation note from the review: the memory offload engine exists
in `core/offload.c`, but the normal response path does not currently appear to
wire typical app-generated in-memory bodies into it.

## What is worth testing now

### RF1. Reconfirm the download/head-of-line signal with offload context
Use A4 and X4 style mixes to compare:
- app-generated in-memory downloads
- sendfile/static downloads
- `offload-threads = 0` versus a small nonzero value

Goal:
- separate worker residency from socket-send residency
- confirm whether the remaining pain is really on the response-send path or in
  app body generation before the server can offload anything

### RF2. Narrow app-memory offload prototype
Only if RF1 shows response-send residency is still meaningful.

Candidate shape:
- large contiguous app-generated body path only
- opt-in or threshold-gated first
- offload only after headers are complete and the response body shape is simple
  enough to hand off safely

Why this is better than generic fairness:
- it targets a narrow response class
- it avoids changing the ordinary writer loop for all responses
- it fits the existing offload-thread model better than a global fairness layer

### RF3. Bounded send quantum only inside offload ownership
If RF2 is promising, consider a bounded send quantum inside the offload thread
only, not in the worker writer path.

Goal:
- reduce offload-path monopolization by a few large transfers
- keep the fairness logic outside the ordinary worker response writer path

## What should stay out
- generic write slicing for all responses
- fairness logic in the ordinary worker write path
- changes that disturb accepted W4 behavior or static/sendfile paths already
  performing well enough

## Success criteria
A response-path fairness follow-up is worth keeping only if it:
- improves tiny-request tail latency meaningfully in mixed download cases
- does not regress ordinary small responses
- does not complicate PSGI correctness or offload ownership rules
- remains supportable as a narrow path rather than a broad writer scheduler

## Stop conditions
Stop the line if:
- RF1 shows the main cost is app body creation rather than socket send
- the benefit only appears for synthetic large-body mixes and disappears on the
  more realistic app download path
- the required changes start to resemble a generic writer fairness layer again
