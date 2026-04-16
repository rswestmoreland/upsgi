# Lossless logger queue

The current source tree now includes the first active logger-side queue behavior:

- blocks the logger-side drain path instead of dropping log records
- batch flushes for stream-style sinks

- `log-queue-enabled`
- `disable-log-queue`
- `log-queue-records`
- `log-queue-bytes`

- 512 records per queue
- 512 KiB per queue
