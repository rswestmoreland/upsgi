# upsgi logging boundary

## Goal
Keep logging first-class while making subsystem ownership obvious.

## Boundary split

### Generic logging core
Owned by `core/logging.c`.

Responsibilities:
- worker/master log pipe setup
- logger registration table
- logger selection
- request logger selection
- encoder application
- optional advanced log routing

### Request identity preprocessing
Owned by `core/protocol.c`.

Responsibilities:
- parse request vars and headers
- apply `--log-x-forwarded-for` by rewriting the request identity fields used by request logging
- hand the normalized request onward to the rest of the request path

This stays outside the generic logging core on purpose.

### Default sink backend registration
Owned by plugin files:
- `plugins/logfile/logfile.c`
- `plugins/logsocket/logsocket_plugin.c`
- `plugins/rsyslog/rsyslog_plugin.c`

Responsibilities:
- register concrete sink backends
- parse sink-local arguments
- emit log records to their destination

### PSGI exception visibility policy
Owned by the PSGI layer, not by the generic logging core.

Responsibilities:
- keep PSGI die/stack visibility off by default
- enable explicit debugging visibility only when `--log-exceptions` is set

## v1 interpretation
- logging remains baseline and first-class
- the default embedded logging bundle is `logfile`, `logsocket`, and `rsyslog`
- advanced logger routing and selection remain available, but they are not the center of the upsgi product story
