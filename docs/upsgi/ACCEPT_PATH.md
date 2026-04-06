# upsgi accept-path guidance

This document captures the current accept-side policy for the PSGI-first fork.

## Current topology model
upsgi still uses the inherited-listener model from the upstream codebase for the normal socket path:
- the listener is created before workers fork
- workers inherit that listener fd
- request acceptance happens from the inherited socket set inside the worker loop

That means not every accept-side knob has the same effect that operators might expect from evented load balancers or servers that create one listener per worker.

## Preferred policy in the current fork

### Shared listener topology
When workers share the same inherited listener, the preferred safety/performance policy remains:
- use `--thunder-lock` for multi-worker deployments that contend on the same accept socket
- treat this as the baseline serialized shared-listener mode

The startup log now reports this explicitly as one of:
- `accept mode: shared listeners serialized by thunder lock`
- `accept mode: shared listeners without thunder lock`

### Worker-local mapped sockets
When non-overlapping `map-socket` rules leave a worker with only worker-local request sockets, the fork now automatically bypasses the inter-process thunder lock for that worker.

The startup log reports:
- `accept mode: worker-local mapped sockets (thunder lock bypassed)`

This keeps the existing shared-listener protection for the normal case, but avoids paying for a global accept lock when the worker no longer shares request sockets with other workers.

### `--reuse-port` in the inherited-listener model
`--reuse-port` is still parsed and applied at socket bind time, but in the current inherited-listener architecture it does not by itself create one accept listener per worker.

For that reason, the fork now logs explicit guidance when `--reuse-port` is combined with shared inherited listeners:
- `SO_REUSEPORT on inherited listeners does not shard accepts`

This is intentionally explicit so operators do not assume HAProxy-style or per-worker listener sharding from the current model.

## High-worker-count guidance
For practical PSGI hosting today:
- shared TCP or HTTP listeners plus `--thunder-lock` remain the safe default
- if you intentionally build a worker-local socket topology with `map-socket`, the fork will bypass the inter-process thunder lock automatically for those workers
- do not treat `--reuse-port` alone as a distribution strategy in the current inherited-listener model

## Deferred scope
True per-worker listener sharding using `SO_REUSEPORT` would require a broader listener-lifecycle change than this checkpoint. That remains a possible future optimization, but it is not represented as complete in the current fork state.
