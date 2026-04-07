#include "upsgi.h"

extern struct upsgi_server upsgi;

int upsgi_simple_wait_milliseconds_hook(int timeout) {
        return poll(NULL, 0, timeout);
}


// in the future we will need to use the best clock source for each os/system
time_t upsgi_now() {
	return upsgi.clock->seconds();
}

uint64_t upsgi_micros() {
	return upsgi.clock->microseconds();
}

uint64_t upsgi_millis() {
	return upsgi.clock->microseconds() / 1000;
}


void upsgi_register_clock(struct upsgi_clock *clock) {
	struct upsgi_clock *clocks = upsgi.clocks;

	clock->next = NULL;

	if (!clocks) {
		upsgi.clocks = clock;
		return;
	}

	while (clocks) {
		if (!clocks->next) {
			clocks->next = clock;
			return;
		}
		clocks = clocks->next;
	}
}

void upsgi_set_clock(char *name) {
	struct upsgi_clock *clocks = upsgi.clocks;
	while (clocks) {
		if (!strcmp(name, clocks->name)) {
			upsgi.clock = clocks;
			return;
		}
		clocks = clocks->next;
	}

	upsgi_log("unable to set \"%s\" clock\n", name);
	exit(1);
}
