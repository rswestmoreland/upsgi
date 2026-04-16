#include <sched.h>

#include "body_scheduler.h"

extern struct upsgi_server upsgi;

#define UPSGI_BODY_SCHED_INTERACTIVE_BYTES_THRESHOLD (256ULL * 1024ULL)
#define UPSGI_BODY_SCHED_INTERACTIVE_ROUNDS_THRESHOLD 4ULL
#define UPSGI_BODY_SCHED_INTERACTIVE_RESIDENCY_US_THRESHOLD (250ULL * 1000ULL)
#define UPSGI_BODY_SCHED_NEAR_COMPLETE_BYTES_THRESHOLD (64ULL * 1024ULL)
#define UPSGI_BODY_SCHED_PROMOTION_MIN_BYTES (64ULL * 1024ULL)
#define UPSGI_BODY_SCHED_FULL_BUDGET_PROMOTION_THRESHOLD 2ULL
#define UPSGI_BODY_SCHED_FULL_BUDGET_RATIO_NUM 7ULL
#define UPSGI_BODY_SCHED_FULL_BUDGET_RATIO_DEN 8ULL
#define UPSGI_BODY_SCHED_INTERACTIVE_QUANTUM_BYTES (64ULL * 1024ULL)
#define UPSGI_BODY_SCHED_BULK_QUANTUM_BYTES (32ULL * 1024ULL)
#define UPSGI_BODY_SCHED_DEFICIT_CAP_MULTIPLIER 4ULL

static const uint64_t upsgi_body_sched_residency_bucket_limits_us[] = {
	1000ULL,
	2000ULL,
	4000ULL,
	8000ULL,
	16000ULL,
	32000ULL,
	64000ULL,
	128000ULL,
	256000ULL,
	512000ULL,
	1000000ULL,
	2000000ULL,
	4000000ULL,
	8000000ULL,
	16000000ULL,
	UINT64_MAX,
};

static inline struct upsgi_core *upsgi_body_sched_core(struct wsgi_request *wsgi_req) {
	return &upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id];
}

static inline uint64_t upsgi_body_sched_now_us(void) {
	return upsgi_micros();
}

int upsgi_body_sched_enabled(void) {
	return upsgi.body_scheduler;
}

static uint64_t upsgi_body_sched_lane_quantum(const struct wsgi_request *wsgi_req) {
	if (wsgi_req && wsgi_req->body_sched_lane_class == UPSGI_BODY_SCHED_LANE_BULK) {
		return UPSGI_BODY_SCHED_BULK_QUANTUM_BYTES;
	}
	return UPSGI_BODY_SCHED_INTERACTIVE_QUANTUM_BYTES;
}

static int upsgi_body_sched_is_full_budget_turn(const struct wsgi_request *wsgi_req, uint64_t observed_bytes) {
	if (!wsgi_req || wsgi_req->body_sched_last_budget_bytes == 0) {
		return 0;
	}
	if (observed_bytes >= wsgi_req->body_sched_last_budget_bytes) {
		return 1;
	}
	return (observed_bytes * UPSGI_BODY_SCHED_FULL_BUDGET_RATIO_DEN) >=
		(wsgi_req->body_sched_last_budget_bytes * UPSGI_BODY_SCHED_FULL_BUDGET_RATIO_NUM);
}

static void upsgi_body_sched_update_depth_max(struct upsgi_core *uc) {
	if (uc->body_sched_interactive_depth_current > uc->body_sched_interactive_depth_max) {
		uc->body_sched_interactive_depth_max = uc->body_sched_interactive_depth_current;
	}
	if (uc->body_sched_bulk_depth_current > uc->body_sched_bulk_depth_max) {
		uc->body_sched_bulk_depth_max = uc->body_sched_bulk_depth_current;
	}
}

static void upsgi_body_sched_promote_to_bulk(struct wsgi_request *wsgi_req, struct upsgi_core *uc) {
	if (wsgi_req->body_sched_lane_class == UPSGI_BODY_SCHED_LANE_BULK) {
		return;
	}
	if (uc->body_sched_interactive_depth_current > 0) {
		uc->body_sched_interactive_depth_current--;
	}
	uc->body_sched_bulk_depth_current++;
	upsgi_body_sched_update_depth_max(uc);
	uc->body_sched_promotions_to_bulk++;
	wsgi_req->body_sched_lane_class = UPSGI_BODY_SCHED_LANE_BULK;
	if (wsgi_req->body_sched_deficit_bytes > UPSGI_BODY_SCHED_BULK_QUANTUM_BYTES) {
		wsgi_req->body_sched_deficit_bytes = UPSGI_BODY_SCHED_BULK_QUANTUM_BYTES;
	}
}

static void upsgi_body_sched_refresh_samples(struct upsgi_core *uc) {
	uint64_t total = 0;
	uint64_t target50;
	uint64_t target95;
	uint64_t seen = 0;
	uint64_t p50 = 0;
	uint64_t p95 = 0;
	size_t i;

	for (i = 0; i < (sizeof(upsgi_body_sched_residency_bucket_limits_us) / sizeof(upsgi_body_sched_residency_bucket_limits_us[0])); i++) {
		total += uc->body_sched_residency_hist[i];
	}

	if (total == 0) {
		uc->body_sched_residency_us_p50_sample = 0;
		uc->body_sched_residency_us_p95_sample = 0;
		return;
	}

	target50 = (total + 1) / 2;
	target95 = ((total * 95ULL) + 99ULL) / 100ULL;
	if (target95 == 0) {
		target95 = 1;
	}

	for (i = 0; i < (sizeof(upsgi_body_sched_residency_bucket_limits_us) / sizeof(upsgi_body_sched_residency_bucket_limits_us[0])); i++) {
		seen += uc->body_sched_residency_hist[i];
		if (p50 == 0 && seen >= target50) {
			p50 = upsgi_body_sched_residency_bucket_limits_us[i];
		}
		if (p95 == 0 && seen >= target95) {
			p95 = upsgi_body_sched_residency_bucket_limits_us[i];
			break;
		}
	}

	uc->body_sched_residency_us_p50_sample = p50;
	uc->body_sched_residency_us_p95_sample = p95;
}

static void upsgi_body_sched_record_residency(struct upsgi_core *uc, uint64_t residency_us) {
	size_t i;

	if (residency_us > uc->body_sched_residency_us_max) {
		uc->body_sched_residency_us_max = residency_us;
	}

	for (i = 0; i < (sizeof(upsgi_body_sched_residency_bucket_limits_us) / sizeof(upsgi_body_sched_residency_bucket_limits_us[0])); i++) {
		if (residency_us <= upsgi_body_sched_residency_bucket_limits_us[i]) {
			uc->body_sched_residency_hist[i]++;
			break;
		}
	}

	upsgi_body_sched_refresh_samples(uc);
}

static void upsgi_body_sched_update_promotions(struct wsgi_request *wsgi_req, struct upsgi_core *uc) {
	uint64_t residency_us;

	if (!wsgi_req->body_sched_registered) {
		return;
	}

	residency_us = wsgi_req->body_sched_last_service_us - wsgi_req->body_sched_started_at_us;
	if (!wsgi_req->body_sched_promoted_by_bytes && wsgi_req->body_sched_bytes_observed >= UPSGI_BODY_SCHED_INTERACTIVE_BYTES_THRESHOLD) {
		wsgi_req->body_sched_promoted_by_bytes = 1;
		uc->body_sched_items_promoted_by_bytes++;
		upsgi_body_sched_promote_to_bulk(wsgi_req, uc);
	}
	if (!wsgi_req->body_sched_promoted_by_rounds &&
		wsgi_req->body_sched_rounds_observed >= UPSGI_BODY_SCHED_INTERACTIVE_ROUNDS_THRESHOLD &&
		wsgi_req->body_sched_bytes_observed >= UPSGI_BODY_SCHED_PROMOTION_MIN_BYTES &&
		wsgi_req->body_sched_full_budget_turns >= UPSGI_BODY_SCHED_FULL_BUDGET_PROMOTION_THRESHOLD) {
		wsgi_req->body_sched_promoted_by_rounds = 1;
		uc->body_sched_items_promoted_by_rounds++;
		upsgi_body_sched_promote_to_bulk(wsgi_req, uc);
	}
	if (!wsgi_req->body_sched_promoted_by_residency &&
		residency_us >= UPSGI_BODY_SCHED_INTERACTIVE_RESIDENCY_US_THRESHOLD &&
		wsgi_req->body_sched_bytes_observed >= UPSGI_BODY_SCHED_PROMOTION_MIN_BYTES &&
		wsgi_req->body_sched_full_budget_turns > 0) {
		wsgi_req->body_sched_promoted_by_residency = 1;
		uc->body_sched_items_promoted_by_residency++;
		upsgi_body_sched_promote_to_bulk(wsgi_req, uc);
	}
}

void upsgi_body_sched_note_request(struct wsgi_request *wsgi_req) {
	struct upsgi_core *uc;

	if (!wsgi_req || wsgi_req->body_sched_registered) {
		return;
	}
	if (!wsgi_req->post_cl && !wsgi_req->body_is_chunked) {
		return;
	}

	uc = upsgi_body_sched_core(wsgi_req);
	wsgi_req->body_sched_registered = 1;
	wsgi_req->body_sched_lane_class = UPSGI_BODY_SCHED_LANE_INTERACTIVE;
	wsgi_req->body_sched_started_at_us = upsgi_body_sched_now_us();
	wsgi_req->body_sched_last_service_us = wsgi_req->body_sched_started_at_us;
	uc->body_sched_active_items++;
	uc->body_sched_interactive_depth_current++;
	upsgi_body_sched_update_depth_max(uc);

	if (!upsgi_body_sched_enabled() && !wsgi_req->body_sched_disabled_fallback_noted) {
		uc->body_sched_disabled_fallbacks++;
		wsgi_req->body_sched_disabled_fallback_noted = 1;
	}
}

size_t upsgi_body_sched_read_budget(struct wsgi_request *wsgi_req, size_t requested_bytes) {
	struct upsgi_core *uc;
	uint64_t quantum;
	uint64_t deficit_cap;
	size_t grant;

	if (!wsgi_req || requested_bytes == 0) {
		return requested_bytes;
	}
	upsgi_body_sched_note_request(wsgi_req);
	if (!wsgi_req->body_sched_registered) {
		return requested_bytes;
	}
	uc = upsgi_body_sched_core(wsgi_req);
	wsgi_req->body_sched_last_budget_capped = 0;
	wsgi_req->body_sched_last_budget_bytes = 0;

	if (!upsgi_body_sched_enabled()) {
		uc->body_sched_credit_granted_bytes += requested_bytes;
		wsgi_req->body_sched_last_budget_bytes = requested_bytes;
		return requested_bytes;
	}

	upsgi_body_sched_update_promotions(wsgi_req, uc);
	quantum = upsgi_body_sched_lane_quantum(wsgi_req);
	if (wsgi_req->body_sched_recent_wait_event) {
		grant = (size_t) UMIN((uint64_t) requested_bytes, quantum);
		uc->body_sched_wait_relief_events++;
		uc->body_sched_credit_granted_bytes += grant;
		wsgi_req->body_sched_last_budget_bytes = grant;
		return grant;
	}
	deficit_cap = quantum * UPSGI_BODY_SCHED_DEFICIT_CAP_MULTIPLIER;
	if (wsgi_req->body_sched_deficit_bytes < deficit_cap) {
		wsgi_req->body_sched_deficit_bytes += quantum;
		if (wsgi_req->body_sched_deficit_bytes > deficit_cap) {
			wsgi_req->body_sched_deficit_bytes = deficit_cap;
			uc->body_sched_overflow_protection_hits++;
		}
	}
	else {
		uc->body_sched_overflow_protection_hits++;
	}

	grant = (size_t) UMIN((uint64_t) requested_bytes, wsgi_req->body_sched_deficit_bytes);
	if (grant == 0) {
		uc->body_sched_no_credit_skips++;
		grant = (size_t) UMIN((uint64_t) requested_bytes, quantum);
		wsgi_req->body_sched_deficit_bytes = 0;
	}
	if ((uint64_t) grant < (uint64_t) requested_bytes) {
		uc->body_sched_no_credit_skips++;
		wsgi_req->body_sched_last_budget_capped = 1;
	}

	wsgi_req->body_sched_last_budget_bytes = grant;
	uc->body_sched_credit_granted_bytes += grant;
	return grant;
}

void upsgi_body_sched_note_bytes(struct wsgi_request *wsgi_req, size_t observed_bytes) {
	struct upsgi_core *uc;
	uint64_t bytes_before;
	uint64_t observed64;
	uint64_t unused_credit;
	int full_budget_turn;

	if (!wsgi_req || observed_bytes == 0) {
		return;
	}
	upsgi_body_sched_note_request(wsgi_req);
	if (!wsgi_req->body_sched_registered) {
		return;
	}
	uc = upsgi_body_sched_core(wsgi_req);
	bytes_before = wsgi_req->body_sched_bytes_observed;
	observed64 = (uint64_t) observed_bytes;
	wsgi_req->body_sched_rounds_observed++;
	wsgi_req->body_sched_bytes_observed += observed_bytes;
	wsgi_req->body_sched_last_service_us = upsgi_body_sched_now_us();
	uc->body_sched_rounds++;
	if (wsgi_req->body_sched_lane_class == UPSGI_BODY_SCHED_LANE_BULK) {
		uc->body_sched_bulk_turns++;
		uc->body_sched_bytes_bulk += observed_bytes;
	}
	else {
		uc->body_sched_interactive_turns++;
		uc->body_sched_bytes_interactive += observed_bytes;
	}
	uc->body_sched_bytes_total += observed_bytes;
	if (!wsgi_req->body_sched_marked_complete) {
		uc->body_sched_requeues++;
	}

	if (wsgi_req->body_sched_last_budget_bytes > observed64) {
		unused_credit = wsgi_req->body_sched_last_budget_bytes - observed64;
		uc->body_sched_credit_unused_bytes += unused_credit;
	}
	if (upsgi_body_sched_enabled()) {
		if (wsgi_req->body_sched_deficit_bytes >= observed64) {
			wsgi_req->body_sched_deficit_bytes -= observed64;
		}
		else {
			wsgi_req->body_sched_deficit_bytes = 0;
		}
	}

	full_budget_turn = upsgi_body_sched_is_full_budget_turn(wsgi_req, observed64);
	if (full_budget_turn) {
		wsgi_req->body_sched_full_budget_turns++;
		uc->body_sched_full_budget_turns++;
	}
	else {
		wsgi_req->body_sched_full_budget_turns = 0;
	}

	upsgi_body_sched_update_promotions(wsgi_req, uc);
	if (!wsgi_req->body_is_chunked && wsgi_req->post_cl > 0 && wsgi_req->body_sched_bytes_observed >= wsgi_req->post_cl) {
		wsgi_req->body_sched_marked_complete = 1;
		if ((wsgi_req->post_cl - UMIN(bytes_before, wsgi_req->post_cl)) <= UPSGI_BODY_SCHED_NEAR_COMPLETE_BYTES_THRESHOLD) {
			uc->body_sched_near_complete_fastfinishes++;
		}
	}

	if (upsgi_body_sched_enabled() &&
		wsgi_req->body_sched_last_budget_capped &&
		wsgi_req->body_sched_lane_class == UPSGI_BODY_SCHED_LANE_BULK &&
		!wsgi_req->body_sched_marked_complete) {
		if (full_budget_turn && !wsgi_req->body_sched_recent_wait_event) {
			uc->body_sched_yield_hints++;
			sched_yield();
		}
	}

	wsgi_req->body_sched_recent_wait_event = 0;
}

void upsgi_body_sched_note_empty_read(struct wsgi_request *wsgi_req) {
	struct upsgi_core *uc;
	if (!wsgi_req) return;
	upsgi_body_sched_note_request(wsgi_req);
	if (!wsgi_req->body_sched_registered) return;
	uc = upsgi_body_sched_core(wsgi_req);
	uc->body_sched_empty_read_events++;
	wsgi_req->body_sched_recent_wait_event = 1;
	wsgi_req->body_sched_full_budget_turns = 0;
}

void upsgi_body_sched_note_eagain(struct wsgi_request *wsgi_req) {
	struct upsgi_core *uc;
	if (!wsgi_req) return;
	upsgi_body_sched_note_request(wsgi_req);
	if (!wsgi_req->body_sched_registered) return;
	uc = upsgi_body_sched_core(wsgi_req);
	uc->body_sched_eagain_events++;
	wsgi_req->body_sched_recent_wait_event = 1;
	wsgi_req->body_sched_full_budget_turns = 0;
}

void upsgi_body_sched_note_chunked_complete(struct wsgi_request *wsgi_req) {
	if (!wsgi_req) return;
	upsgi_body_sched_note_request(wsgi_req);
	wsgi_req->body_sched_marked_complete = 1;
}

void upsgi_body_sched_finish(struct wsgi_request *wsgi_req) {
	struct upsgi_core *uc;
	uint64_t residency_us;

	if (!wsgi_req || !wsgi_req->body_sched_registered) {
		return;
	}
	uc = upsgi_body_sched_core(wsgi_req);
	if (uc->body_sched_active_items > 0) {
		uc->body_sched_active_items--;
	}
	if (wsgi_req->body_sched_lane_class == UPSGI_BODY_SCHED_LANE_BULK) {
		if (uc->body_sched_bulk_depth_current > 0) uc->body_sched_bulk_depth_current--;
	}
	else {
		if (uc->body_sched_interactive_depth_current > 0) uc->body_sched_interactive_depth_current--;
	}
	residency_us = upsgi_body_sched_now_us() - wsgi_req->body_sched_started_at_us;
	upsgi_body_sched_record_residency(uc, residency_us);
	if (wsgi_req->body_sched_marked_complete) {
		uc->body_sched_completed_items++;
	}
	wsgi_req->body_sched_registered = 0;
	wsgi_req->body_sched_deficit_bytes = 0;
	wsgi_req->body_sched_last_budget_bytes = 0;
	wsgi_req->body_sched_last_budget_capped = 0;
	wsgi_req->body_sched_disabled_fallback_noted = 0;
	wsgi_req->body_sched_recent_wait_event = 0;
	wsgi_req->body_sched_full_budget_turns = 0;
}
