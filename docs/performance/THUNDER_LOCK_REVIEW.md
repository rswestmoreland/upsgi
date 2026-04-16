# Thunder lock review and post-TL2 decision

## Current implementation shape

The live thunder-lock path is the classic global shared-listener serialization
model, now with explicit backend reporting:
- `core/lock.c` initializes one process-shared thunder lock when `--thunder-lock`
  is enabled.
- `core/socket.c` bypasses the thunder lock when the worker socket map is
  worker-exclusive.
- `wsgi_req_accept()` in `core/utils.c` acquires the thunder lock before the
  blocking readiness wait and releases it after the accept path completes.

This remains conservative, but it is the safest validated design in the current
project state.

Thunder lock is now enabled by default. Disable it only for controlled
comparison or troubleshooting.

## What is implemented and kept

### TL1. Observability first
TL1 status: implemented and kept.

The live tree also reports the selected thunder-lock backend at startup, whether
owner-death recovery is active, whether the watchdog compatibility thread is
enabled, and why a degraded backend was selected.

The tree records per-worker thunder-lock counters in the stats and metrics
surfaces:
- `thunder_lock_acquires`
- `thunder_lock_contention_events`
- `thunder_lock_wait_us`
- `thunder_lock_hold_us`
- `thunder_lock_bypass_count`

Current semantics:
- acquisitions, wait time, and hold time are recorded only when the global
  process-shared thunder lock is actually taken
- `thunder_lock_contention_events` is a measured positive-wait acquisition
- `thunder_lock_bypass_count` increments when worker-local mapped sockets avoid
  the process-shared thunder-lock path
- the threaded in-process mutex path is preserved but is not counted as a
  thunder-lock acquisition

### Safety hardening
Safety hardening status: implemented and kept.

### Worker-death / restart validation
Worker-death / restart validation status: complete.

Validated retained backends:
- robust-pthread backend: worker SIGKILL followed by master respawn completed and the listener remained healthy
- fd-lock backend: worker SIGKILL followed by master respawn completed and the listener remained healthy
- stats remained readable after respawn in both cases

This gives the current backend posture direct runtime coverage under a basic crash/restart path.

The lock path now fails fast when pthread mutex operations return unexpected
errors:
- `upsgi_mutex_fatal()` is the hard-stop path for mutex failures
- robust-mutex owner death still recovers through `pthread_mutex_consistent()`
- `ENOTRECOVERABLE` and unexpected mutex failures stop the process with a
  specific lock id in the log

## TL2 result

### TL2. Per-socket accept locks
TL2 status: measured and removed from the live tree.

The TL2 prototype narrowed the accept lock window, but the measurements showed
that it traded shorter lock holds for a high rate of empty accepts and did not
stay neutral across listener shapes. That does not meet the project bar for a
default-on shared hot-path design.

Decision:
- do not keep per-socket accept locks in the live tree
- keep TL1 observability
- keep the mutex safety hardening

## What still looks expensive or fragile
- one global contention domain covers all shared listeners
- lock hold scope includes the blocking readiness wait
- the robust-mutex watchdog is a compatibility workaround for fragile older
  pthread environments and should stay optional

## Safer next steps
Further thunder-lock work should focus on safety, observability, and operator
clarity rather than new lock-model rewrites. The most reasonable next checks are:
- keep clear startup diagnostics about the active backend and whether watchdog diagnostics are enabled
- add a dedicated watchdog heartbeat metric or log message only if operators need
  proof that the compatibility path is alive
- verify that robust-mutex recovery and fail-fast behavior remain clean under
  worker death and restart scenarios

## Success criteria
Any future thunder-lock change is worth keeping only if it:
- improves shared-listener mixed-mode throughput or tail latency meaningfully
- stays neutral in worker-exclusive or already-healthy cases
- does not regress accepted work such as W4, W5, B2c, or the shared-deferred
  baseline
- remains supportable in the shared hot path with understandable stats and
  failure behavior
