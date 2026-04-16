# R common receiver assets

Shared fixture layer for the R-lane ingest scenarios.

Contents:
- `receiver_app.psgi` - queue-appending JSON receiver modeled on the earlier ingest example behavior.
- `generate_payloads.py` - generates valid, invalid, compressed, and broken payloads.
- `receiver_client.py` - simple POST client for valid and compressed payloads.
- `slow_uploader_client.py` - sends a request body slowly for R4.
- `raw_abuse_client.py` - emits wrong-method, wrong-path, header-abuse, and disconnect cases for R6.
- `corpus/` - small seed payloads for smoke and dry-run references.
