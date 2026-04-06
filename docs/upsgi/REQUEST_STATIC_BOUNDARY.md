# upsgi request/static boundary

## Goal
Keep the request path and static-serving path easy to audit.

## Boundary split

### Request parsing and interception
Owned by `core/protocol.c`.

Responsibilities:
- parse request vars and request metadata
- normalize request identity fields used by logging
- decide whether baseline static features should intercept the request
- handle baseline static entry points:
  - `check_static`
  - `static-map`
  - `static-map2`
  - `check_static_docroot`
- fall through to PSGI when no static rule serves the request

### Filesystem validation and static response emission
Owned by `core/static.c`.

Responsibilities:
- join docroot and request path into a filesystem target
- resolve the real path
- enforce containment within the selected docroot or explicit safe paths
- perform stat/index checks
- emit headers and file bodies
- return control when the target is missing or not eligible for direct serving

### Route-action static features
Owned by `plugins/router_static/router_static.c`.

Responsibilities:
- explicit routing-driven file and static actions
- broader routing use cases outside the baseline upsgi product story

This is intentionally separate from baseline `--static-map`.

## v1 interpretation
- `--static-map` remains baseline and first-class
- baseline static interception stays in the core request path
- actual static file serving stays in `core/static.c`
- `router_static` is not required solely for baseline static-map support
