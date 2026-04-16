# R-lane receiver assets

The R-lane is now packaged from repo-local fixtures.

Structure:
- `common/` shared receiver app, corpus generator, clients, and seed corpora
- `R1/` through `R6/` scenario-specific matrices, config templates, and smoke runners

These assets model the earlier receiver example behavior:
- POST to `/push`
- read full request body
- optionally inflate `application/octet-stream` payloads in memory
- validate JSON
- append one record per accepted request to a queue file
