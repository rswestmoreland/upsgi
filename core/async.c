#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	This is a general-purpose async loop engine (it expects a coroutine-based approach)

	You can see it as an hub holding the following structures:

	1) the runqueue, cores ready to be run are appended to this list

	2) the fd list, this is a list of monitored file descriptors, a core can wait for all the file descriptors it needs

	3) the timeout value, if set, the current core will timeout after the specified number of seconds (unless an event cancels it)


	IMPORTANT: this is not a callback-based engine !!!

*/

// this is called whenever a new connection is ready, but there are no cores to handle it
void upsgi_async_queue_is_full(time_t now) {
	if (now > upsgi.async_queue_is_full && upsgi.async_warn_if_queue_full) {
		upsgi_log_verbose("[DANGER] async queue is full !!!\n");
		upsgi.async_queue_is_full = now;
	}
}

void upsgi_async_init() {

	upsgi.async_queue = event_queue_init();

	if (upsgi.async_queue < 0) {
		exit(1);
	}

	upsgi_add_sockets_to_queue(upsgi.async_queue, -1);

	upsgi.rb_async_timeouts = upsgi_init_rb_timer();

	// optimization, this array maps file descriptor to requests
        upsgi.async_waiting_fd_table = upsgi_calloc(sizeof(struct wsgi_request *) * upsgi.max_fd);
        upsgi.async_proto_fd_table = upsgi_calloc(sizeof(struct wsgi_request *) * upsgi.max_fd);

}

struct wsgi_request *find_wsgi_req_proto_by_fd(int fd) {
	return upsgi.async_proto_fd_table[fd];
}

struct wsgi_request *find_wsgi_req_by_fd(int fd) {
	return upsgi.async_waiting_fd_table[fd];
}

static void runqueue_remove(struct upsgi_async_request *u_request) {

	struct upsgi_async_request *parent = u_request->prev;
	struct upsgi_async_request *child = u_request->next;

	if (parent) {
		parent->next = child;
	}
	if (child) {
		child->prev = parent;
	}

	if (u_request == upsgi.async_runqueue) {
		upsgi.async_runqueue = child;
	}

	if (u_request == upsgi.async_runqueue_last) {
		upsgi.async_runqueue_last = parent;
	}

	free(u_request);
}

static void runqueue_push(struct wsgi_request *wsgi_req) {

	// do not push the same request in the runqueue
	struct upsgi_async_request *uar = upsgi.async_runqueue;
	while(uar) {
		if (uar->wsgi_req == wsgi_req) return;
		uar = uar->next;
	}

	uar = upsgi_malloc(sizeof(struct upsgi_async_request));
	uar->prev = NULL;
	uar->next = NULL;
	uar->wsgi_req = wsgi_req;

	if (upsgi.async_runqueue == NULL) {
		upsgi.async_runqueue = uar;
	}
	else {
		uar->prev = upsgi.async_runqueue_last;	
	}

	if (upsgi.async_runqueue_last) {
		upsgi.async_runqueue_last->next = uar;
	}
	upsgi.async_runqueue_last = uar;
}

struct wsgi_request *find_first_available_wsgi_req() {

	struct wsgi_request *wsgi_req;

	if (upsgi.async_queue_unused_ptr < 0) {
		return NULL;
	}

	wsgi_req = upsgi.async_queue_unused[upsgi.async_queue_unused_ptr];
	upsgi.async_queue_unused_ptr--;
	return wsgi_req;
}

void async_reset_request(struct wsgi_request *wsgi_req) {
	if (wsgi_req->async_timeout) {
		upsgi_del_rb_timer(upsgi.rb_async_timeouts, wsgi_req->async_timeout);
		free(wsgi_req->async_timeout);
		wsgi_req->async_timeout = NULL;
	}
	
	struct upsgi_async_fd *uaf = wsgi_req->waiting_fds;
	while (uaf) {
        	event_queue_del_fd(upsgi.async_queue, uaf->fd, uaf->event);
                upsgi.async_waiting_fd_table[uaf->fd] = NULL;
                struct upsgi_async_fd *current_uaf = uaf;
                uaf = current_uaf->next;
                free(current_uaf);
	}

	wsgi_req->waiting_fds = NULL;
}

static void async_expire_timeouts(uint64_t now) {

	struct wsgi_request *wsgi_req;
	struct upsgi_rb_timer *urbt;

	for (;;) {

		urbt = upsgi_min_rb_timer(upsgi.rb_async_timeouts, NULL);

		if (urbt == NULL)
			return;

		if (urbt->value <= now) {
			wsgi_req = (struct wsgi_request *) urbt->data;
			// timeout expired
			wsgi_req->async_timed_out = 1;
			// reset the request
			async_reset_request(wsgi_req);
			// push it in the runqueue
			runqueue_push(wsgi_req);
			continue;
		}

		break;
	}

}

int async_add_fd_read(struct wsgi_request *wsgi_req, int fd, int timeout) {

	if (upsgi.async < 1 || !upsgi.async_waiting_fd_table){ 
		upsgi_log_verbose("ASYNC call without async mode !!!\n");
		return -1;
	}

	struct upsgi_async_fd *last_uad = NULL, *uad = wsgi_req->waiting_fds;

	if (fd < 0)
		return -1;

	// find last slot
	while (uad) {
		last_uad = uad;
		uad = uad->next;
	}

	uad = upsgi_malloc(sizeof(struct upsgi_async_fd));
	uad->fd = fd;
	uad->event = event_queue_read();
	uad->prev = last_uad;
	uad->next = NULL;

	if (last_uad) {
		last_uad->next = uad;
	}
	else {
		wsgi_req->waiting_fds = uad;
	}

	if (timeout > 0) {
		async_add_timeout(wsgi_req, timeout);
	}
	upsgi.async_waiting_fd_table[fd] = wsgi_req;
	wsgi_req->async_force_again = 1;
	return event_queue_add_fd_read(upsgi.async_queue, fd);
}

static int async_wait_fd_read(int fd, int timeout) {

	struct wsgi_request *wsgi_req = current_wsgi_req();

	wsgi_req->async_ready_fd = 0;

	if (async_add_fd_read(wsgi_req, fd, timeout)) {
		return -1;
	}
	if (upsgi.schedule_to_main) {
		upsgi.schedule_to_main(wsgi_req);
	}
	if (wsgi_req->async_timed_out) {
		wsgi_req->async_timed_out = 0;
		return 0;
	}
	return 1;
}

static int async_wait_fd_read2(int fd0, int fd1, int timeout, int *fd) {

        struct wsgi_request *wsgi_req = current_wsgi_req();

        wsgi_req->async_ready_fd = 0;

        if (async_add_fd_read(wsgi_req, fd0, timeout)) {
                return -1;
        }

        if (async_add_fd_read(wsgi_req, fd1, timeout)) {
		// reset already registered fd
		async_reset_request(wsgi_req);
                return -1;
        }

        if (upsgi.schedule_to_main) {
                upsgi.schedule_to_main(wsgi_req);
        }

        if (wsgi_req->async_timed_out) {
                wsgi_req->async_timed_out = 0;
                return 0;
        }

	if (wsgi_req->async_ready_fd) {
		*fd = wsgi_req->async_last_ready_fd;
		return 1;
	}

        return -1;
}


void async_add_timeout(struct wsgi_request *wsgi_req, int timeout) {

	if (upsgi.async < 1 || !upsgi.rb_async_timeouts) {
		upsgi_log_verbose("ASYNC call without async mode !!!\n");
		return;
	}

	wsgi_req->async_ready_fd = 0;

	if (timeout > 0 && wsgi_req->async_timeout == NULL) {
		wsgi_req->async_timeout = upsgi_add_rb_timer(upsgi.rb_async_timeouts, upsgi_now() + timeout, wsgi_req);
	}

}

int async_add_fd_write(struct wsgi_request *wsgi_req, int fd, int timeout) {

	if (upsgi.async < 1 || !upsgi.async_waiting_fd_table) {
		upsgi_log_verbose("ASYNC call without async mode !!!\n");
		return -1;
	}

	struct upsgi_async_fd *last_uad = NULL, *uad = wsgi_req->waiting_fds;

	if (fd < 0)
		return -1;

	// find last slot
	while (uad) {
		last_uad = uad;
		uad = uad->next;
	}

	uad = upsgi_malloc(sizeof(struct upsgi_async_fd));
	uad->fd = fd;
	uad->event = event_queue_write();
	uad->prev = last_uad;
	uad->next = NULL;

	if (last_uad) {
		last_uad->next = uad;
	}
	else {
		wsgi_req->waiting_fds = uad;
	}

	if (timeout > 0) {
		async_add_timeout(wsgi_req, timeout);
	}

	upsgi.async_waiting_fd_table[fd] = wsgi_req;
	wsgi_req->async_force_again = 1;
	return event_queue_add_fd_write(upsgi.async_queue, fd);
}

static int async_wait_fd_write(int fd, int timeout) {
	struct wsgi_request *wsgi_req = current_wsgi_req();

	wsgi_req->async_ready_fd = 0;

	if (async_add_fd_write(wsgi_req, fd, timeout)) {
		return -1;
	}
	if (upsgi.schedule_to_main) {
		upsgi.schedule_to_main(wsgi_req);
	}
	if (wsgi_req->async_timed_out) {
		wsgi_req->async_timed_out = 0;
		return 0;
	}
	return 1;
}

void async_schedule_to_req(void) {
#ifdef UPSGI_ROUTING
        if (upsgi_apply_routes(upsgi.wsgi_req) == UPSGI_ROUTE_BREAK) {
		goto end;
        }
	// a trick to avoid calling routes again
	upsgi.wsgi_req->is_routing = 1;
#endif
	upsgi.wsgi_req->async_status = upsgi.p[upsgi.wsgi_req->uh->modifier1]->request(upsgi.wsgi_req);
        if (upsgi.wsgi_req->async_status <= UPSGI_OK) goto end;

	if (upsgi.schedule_to_main) {
        	upsgi.schedule_to_main(upsgi.wsgi_req);
	}
	return;

end:
	async_reset_request(upsgi.wsgi_req);
	upsgi_close_request(upsgi.wsgi_req);
	upsgi.wsgi_req->async_status = UPSGI_OK;	
	upsgi.async_queue_unused_ptr++;
        upsgi.async_queue_unused[upsgi.async_queue_unused_ptr] = upsgi.wsgi_req;
}

void async_schedule_to_req_green(void) {
	struct wsgi_request *wsgi_req = upsgi.wsgi_req;
#ifdef UPSGI_ROUTING
        if (upsgi_apply_routes(wsgi_req) == UPSGI_ROUTE_BREAK) {
                goto end;
        }
#endif
        for(;;) {
		wsgi_req->async_status = upsgi.p[wsgi_req->uh->modifier1]->request(wsgi_req);
                if (wsgi_req->async_status <= UPSGI_OK) {
                        break;
                }
                wsgi_req->switches++;
		if (upsgi.schedule_fix) {
			upsgi.schedule_fix(wsgi_req);
		}
                // switch after each yield
		if (upsgi.schedule_to_main)
                	upsgi.schedule_to_main(wsgi_req);
        }

#ifdef UPSGI_ROUTING
end:
#endif
	// re-set the global state
	upsgi.wsgi_req = wsgi_req;
        async_reset_request(wsgi_req);
        upsgi_close_request(wsgi_req);
	// re-set the global state (routing could have changed it)
	upsgi.wsgi_req = wsgi_req;
        wsgi_req->async_status = UPSGI_OK;
	upsgi.async_queue_unused_ptr++;
        upsgi.async_queue_unused[upsgi.async_queue_unused_ptr] = wsgi_req;
}

static int upsgi_async_wait_milliseconds_hook(int timeout) {
	struct wsgi_request *wsgi_req = current_wsgi_req();
	timeout = timeout / 1000;
	if (!timeout) timeout = 1;
	async_add_timeout(wsgi_req, timeout);
	wsgi_req->async_force_again = 1;
	if (upsgi.schedule_to_main) {
                upsgi.schedule_to_main(wsgi_req);
        }
        if (wsgi_req->async_timed_out) {
                wsgi_req->async_timed_out = 0;
                return 0;
        }

	return -1;
}

void async_loop() {

	if (upsgi.async < 1) {
		upsgi_log("the async loop engine requires async mode (--async <n>)\n");
		exit(1);
	}

	int interesting_fd, i;
	struct upsgi_rb_timer *min_timeout;
	int timeout;
	int is_a_new_connection;
	int proto_parser_status;

	uint64_t now;

	struct upsgi_async_request *current_request = NULL;

	void *events = event_queue_alloc(64);
	struct upsgi_socket *upsgi_sock;

	upsgi_async_init();

	upsgi.async_runqueue = NULL;

	upsgi.wait_write_hook = async_wait_fd_write;
        upsgi.wait_read_hook = async_wait_fd_read;
        upsgi.wait_read2_hook = async_wait_fd_read2;
	upsgi.wait_milliseconds_hook = upsgi_async_wait_milliseconds_hook;

	if (upsgi.signal_socket > -1) {
		event_queue_add_fd_read(upsgi.async_queue, upsgi.signal_socket);
		event_queue_add_fd_read(upsgi.async_queue, upsgi.my_signal_socket);
	}

	// set a default request manager
	if (!upsgi.schedule_to_req)
		upsgi.schedule_to_req = async_schedule_to_req;

	if (!upsgi.schedule_to_main) {
		upsgi_log("*** DANGER *** async mode without coroutine/greenthread engine loaded !!!\n");
	}

	while (upsgi.workers[upsgi.mywid].manage_next_request) {

		now = (uint64_t) upsgi_now();
		if (upsgi.async_runqueue) {
			timeout = 0;
		}
		else {
			min_timeout = upsgi_min_rb_timer(upsgi.rb_async_timeouts, NULL);
			if (min_timeout) {
				timeout = min_timeout->value - now;
				if (timeout <= 0) {
					async_expire_timeouts(now);
					timeout = 0;
				}
			}
			else {
				timeout = -1;
			}
		}

		upsgi.async_nevents = event_queue_wait_multi(upsgi.async_queue, timeout, events, 64);

		now = (uint64_t) upsgi_now();
		// timeout ???
		if (upsgi.async_nevents == 0) {
			async_expire_timeouts(now);
		}


		for (i = 0; i < upsgi.async_nevents; i++) {
			// manage events
			interesting_fd = event_queue_interesting_fd(events, i);

			// signals are executed in the main stack... in the future we could have dedicated stacks for them
			if (upsgi.signal_socket > -1 && (interesting_fd == upsgi.signal_socket || interesting_fd == upsgi.my_signal_socket)) {
				upsgi.wsgi_req = find_first_available_wsgi_req();
                                if (upsgi.wsgi_req == NULL) {
                                	upsgi_async_queue_is_full((time_t)now);
                                        continue; 
                                }
				upsgi_receive_signal(upsgi.wsgi_req, interesting_fd, "worker", upsgi.mywid);
				continue;
			}

			is_a_new_connection = 0;

			// new request coming in ?
			upsgi_sock = upsgi.sockets;
			while (upsgi_sock) {

				if (interesting_fd == upsgi_sock->fd) {

					is_a_new_connection = 1;

					upsgi.wsgi_req = find_first_available_wsgi_req();
					if (upsgi.wsgi_req == NULL) {
						upsgi_async_queue_is_full((time_t)now);
						break;
					}

					// on error re-insert the request in the queue
					wsgi_req_setup(upsgi.wsgi_req, upsgi.wsgi_req->async_id, upsgi_sock);
					if (wsgi_req_simple_accept(upsgi.wsgi_req, interesting_fd)) {
						upsgi.async_queue_unused_ptr++;
						upsgi.async_queue_unused[upsgi.async_queue_unused_ptr] = upsgi.wsgi_req;
						break;
					}

					if (wsgi_req_async_recv(upsgi.wsgi_req)) {
						upsgi.async_queue_unused_ptr++;
						upsgi.async_queue_unused[upsgi.async_queue_unused_ptr] = upsgi.wsgi_req;
						break;
					}

					// by default the core is in UPSGI_AGAIN mode
					upsgi.wsgi_req->async_status = UPSGI_AGAIN;
					// some protocol (like zeromq) do not need additional parsing, just push it in the runqueue
					if (upsgi.wsgi_req->do_not_add_to_async_queue) {
						runqueue_push(upsgi.wsgi_req);
					}

					break;
				}

				upsgi_sock = upsgi_sock->next;
			}

			if (!is_a_new_connection) {
				// proto event
				upsgi.wsgi_req = find_wsgi_req_proto_by_fd(interesting_fd);
				if (upsgi.wsgi_req) {
					proto_parser_status = upsgi.wsgi_req->socket->proto(upsgi.wsgi_req);
					// reset timeout
					async_reset_request(upsgi.wsgi_req);
					// parsing complete
					if (!proto_parser_status) {
						// remove fd from event poll and fd proto table 
						upsgi.async_proto_fd_table[interesting_fd] = NULL;
						event_queue_del_fd(upsgi.async_queue, interesting_fd, event_queue_read());
						// put request in the runqueue (set it as UPSGI_OK to signal the first run)
						upsgi.wsgi_req->async_status = UPSGI_OK;
						runqueue_push(upsgi.wsgi_req);
						continue;
					}
					else if (proto_parser_status < 0) {
						upsgi.async_proto_fd_table[interesting_fd] = NULL;
						close(interesting_fd);
						upsgi.async_queue_unused_ptr++;
						upsgi.async_queue_unused[upsgi.async_queue_unused_ptr] = upsgi.wsgi_req;
						continue;
					}
					// re-add timer
					async_add_timeout(upsgi.wsgi_req, upsgi.socket_timeout);
					continue;
				}

				// app-registered event
				upsgi.wsgi_req = find_wsgi_req_by_fd(interesting_fd);
				// unknown fd, remove it (for safety)
				if (upsgi.wsgi_req == NULL) {
					close(interesting_fd);
					continue;
				}

				// remove all the fd monitors and timeout
				async_reset_request(upsgi.wsgi_req);
				upsgi.wsgi_req->async_ready_fd = 1;
				upsgi.wsgi_req->async_last_ready_fd = interesting_fd;

				// put the request in the runqueue again
				runqueue_push(upsgi.wsgi_req);
			}
		}


		// event queue managed, give cpu to runqueue
		current_request = upsgi.async_runqueue;

		while(current_request) {

			// current_request could be nulled on error/end of request
			struct upsgi_async_request *next_request = current_request->next;

			upsgi.wsgi_req = current_request->wsgi_req;
			upsgi.schedule_to_req();
			upsgi.wsgi_req->switches++;

			// request ended ?
			if (upsgi.wsgi_req->async_status <= UPSGI_OK ||
				upsgi.wsgi_req->waiting_fds || upsgi.wsgi_req->async_timeout) {
				// remove from the runqueue
				runqueue_remove(current_request);
			}
			current_request = next_request;
		}

	}

}
