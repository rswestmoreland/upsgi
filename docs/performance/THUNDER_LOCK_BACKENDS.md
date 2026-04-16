Thunder lock is enabled by default.

# Thunder-lock backends

## Current implementation status
- robust-pthread: active default backend when robust process-shared pthread mutexes are available
- plain-pthread: degraded compatibility fallback when robust mutex support is unavailable in the current build or runtime environment
- ipcsem: inherited only when the generic `--lock-engine=ipcsem` override is selected
- fd-lock: implemented as the thunder-lock compatibility backend and selectable with `--thunder-lock-backend=fdlock`

## Startup diagnostics
When thunder lock is enabled, startup logs now report:
- selected thunder-lock backend
- whether robust owner-death recovery is active
- whether the watchdog compatibility thread is enabled
- a backend note describing why the active backend was selected

## Current guidance
- prefer the default robust-pthread backend
- use fd-lock as the compatibility backend on fragile environments or for explicit testing
- plain-pthread should no longer be the preferred compatibility story for thunder lock
- the watchdog is diagnostic-only and does not spawn a recovery thread
- prefer worker-exclusive socket mapping whenever possible so thunder lock can be bypassed entirely

## Worker-death / restart validation
Validated in the sandbox against both retained backends:
- robust-pthread backend: worker SIGKILL followed by master respawn completed and the server remained reachable
- fd-lock backend: worker SIGKILL followed by master respawn completed and the server remained reachable
- stats remained readable after respawn and continued to expose thunder-lock accounting

Reading:
- both retained backends now have direct crash/respawn runtime coverage
- robust-pthread remains the preferred fast path
- fd-lock is now validated as the safer compatibility backend
