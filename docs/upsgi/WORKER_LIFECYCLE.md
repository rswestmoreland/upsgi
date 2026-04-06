# upsgi worker lifecycle and graceful restart guidance

This document narrows the worker lifecycle story for PSGI hosting and highlights the restart paths that best preserve availability and copy-on-write sharing.

## Preferred graceful restart pattern for PSGI apps

For preloaded PSGI apps, prefer **chain reload** over full worker fan-out reload when you want the master to replace workers one at a time.

Recommended baseline:
- `--master`
- `--workers <n>`
- preload in master (do not use `--lazy` or `--lazy-apps` unless you explicitly need per-worker app loading)
- `--touch-chain-reload <path>` or master FIFO command `c`
- `--reload-mercy <seconds>`
- `--worker-reload-mercy <seconds>`

Why this is preferred:
- only one worker is turned over at a time
- the next worker is not reloaded until the previous replacement is accepting requests
- copy-on-write sharing is disturbed more gradually than a broad worker reload
- RSS spikes are easier to control on constrained hosts

## Paced chain reload

`--chain-reload-delay <seconds>` adds a minimum delay between chain-reload worker turnovers.

This is useful when:
- workers are large and warm-up allocates noticeable memory
- PSGI app startup is heavy enough that replacing workers back-to-back creates RSS spikes
- you want a slower and more predictable rollout after a deploy or code touch

A paced chain reload still waits for the previous replacement worker to become accepting before moving to the next worker.

## When to use full worker reload instead

Use a broader worker reload when you intentionally want all workers to reload as quickly as possible and temporary extra memory pressure is acceptable.

For PSGI-first deployments on smaller hosts, chain reload is usually the safer default.

## Memory-friendly preloaded PSGI guidance

To preserve copy-on-write benefits:
- load the PSGI app in the master when practical
- avoid `--lazy` and `--lazy-apps` for the common preloaded deployment path
- prefer chain reload to broad worker reloads
- add `--chain-reload-delay` when worker warm-up causes noticeable RSS spikes
- keep `--post-buffering-bufsize` modest even when `--post-buffering` is larger
- use `--cheaper-rss-limit-soft` and `--cheaper-rss-limit-hard` deliberately if you rely on cheaper mode

## Recycling controls already supported

upsgi already supports these recycling and lifecycle controls:
- `--max-requests`
- `--max-requests-delta`
- `--min-worker-lifetime`
- `--max-worker-lifetime`
- `--reload-mercy`
- `--worker-reload-mercy`
- `--touch-reload`
- `--touch-workers-reload`
- `--touch-chain-reload`
- `--fs-reload`
- cheaper mode controls such as `--cheaper`, `--cheaper-initial`, `--cheaper-step`, `--cheaper-overload`, `--cheaper-idle`, and the RSS guardrails

## Live worker adjustment operator story

For adaptive worker control, the current low-level operator surface is the master FIFO.

Useful FIFO commands:
- `c` - start chain reload
- `r` - graceful reload all workers
- `w` - reload only workers
- `+` - request one more worker in cheaper mode
- `-` - request one fewer worker in cheaper mode
- `q` - graceful stop
- `Q` - brutal stop

The `+` and `-` controls are only meaningful when cheaper mode is enabled.

Example:
- `--master-fifo /run/upsgi/master.fifo`
- `printf 'c' > /run/upsgi/master.fifo`

## Practical recommendation

For a production PSGI deployment on a smaller host:
1. preload in master
2. use chain reload for routine code/config refresh
3. add `--chain-reload-delay` if worker warm-up briefly spikes RSS
4. reserve broad worker reloads for cases where speed matters more than smooth turnover
