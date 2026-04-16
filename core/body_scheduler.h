#ifndef UPSGI_BODY_SCHEDULER_H
#define UPSGI_BODY_SCHEDULER_H

#include "upsgi.h"

#define UPSGI_BODY_SCHED_LANE_NONE 0
#define UPSGI_BODY_SCHED_LANE_INTERACTIVE 1
#define UPSGI_BODY_SCHED_LANE_BULK 2

int upsgi_body_sched_enabled(void);
void upsgi_body_sched_note_request(struct wsgi_request *wsgi_req);
size_t upsgi_body_sched_read_budget(struct wsgi_request *wsgi_req, size_t requested_bytes);
void upsgi_body_sched_note_bytes(struct wsgi_request *wsgi_req, size_t observed_bytes);
void upsgi_body_sched_note_empty_read(struct wsgi_request *wsgi_req);
void upsgi_body_sched_note_eagain(struct wsgi_request *wsgi_req);
void upsgi_body_sched_note_chunked_complete(struct wsgi_request *wsgi_req);
void upsgi_body_sched_finish(struct wsgi_request *wsgi_req);

#endif
