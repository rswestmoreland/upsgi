#include "upsgi.h"

extern struct upsgi_server upsgi;

struct wsgi_request *threaded_current_wsgi_req() {
	return pthread_getspecific(upsgi.tur_key);
}
struct wsgi_request *simple_current_wsgi_req() {
	return upsgi.wsgi_req;
}


void upsgi_register_loop(char *name, void (*func) (void)) {

	struct upsgi_loop *old_loop = NULL, *loop = upsgi.loops;

	while (loop) {
		// check if the loop engine is already registered
		if (!strcmp(name, loop->name))
			return;
		old_loop = loop;
		loop = loop->next;
	}

	loop = upsgi_calloc(sizeof(struct upsgi_loop));
	loop->name = name;
	loop->loop = func;

	if (old_loop) {
		old_loop->next = loop;
	}
	else {
		upsgi.loops = loop;
	}
}

void *upsgi_get_loop(char *name) {

	struct upsgi_loop *loop = upsgi.loops;

	while (loop) {
		if (!strcmp(name, loop->name)) {
			return loop->loop;
		}
		loop = loop->next;
	}
	return NULL;
}

/*

	this is the default (simple) loop.

	it will run simple_loop_run function for each spawned thread

	simple_loop_run monitors sockets and signals descriptors
	and manages them.

*/

void simple_loop() {
	upsgi_loop_cores_run(simple_loop_run);
	// Other threads may still run. Make sure they will stop.
	upsgi.workers[upsgi.mywid].manage_next_request = 0;

	if (upsgi.workers[upsgi.mywid].shutdown_sockets)
		upsgi_shutdown_all_sockets();
}

void upsgi_loop_cores_run(void *(*func) (void *)) {
	int i;
	for (i = 1; i < upsgi.threads; i++) {
		long j = i;
		pthread_create(&upsgi.workers[upsgi.mywid].cores[i].thread_id, &upsgi.threads_attr, func, (void *) j);
	}
	long y = 0;
	func((void *) y);
}

void upsgi_setup_thread_req(long core_id, struct wsgi_request *wsgi_req) {
	int i;
	sigset_t smask;

	pthread_setspecific(upsgi.tur_key, (void *) wsgi_req);

	if (core_id > 0) {
		// block all signals on new threads
		sigfillset(&smask);
#ifdef UPSGI_DEBUG
		sigdelset(&smask, SIGSEGV);
#endif
		pthread_sigmask(SIG_BLOCK, &smask, NULL);

		// run per-thread socket hook
		struct upsgi_socket *upsgi_sock = upsgi.sockets;
		while (upsgi_sock) {
			if (upsgi_sock->proto_thread_fixup) {
				upsgi_sock->proto_thread_fixup(upsgi_sock, core_id);
			}
			upsgi_sock = upsgi_sock->next;
		}

		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->init_thread) {
				upsgi.p[i]->init_thread(core_id);
			}
		}
	}

}

void simple_loop_run_int(int core_id) {
	long y = core_id;
	simple_loop_run((void *) y);
}

void *simple_loop_run(void *arg1) {

	long core_id = (long) arg1;

	struct wsgi_request *wsgi_req = &upsgi.workers[upsgi.mywid].cores[core_id].req;

	if (upsgi.threads > 1) {
		upsgi_setup_thread_req(core_id, wsgi_req);
	}
	// initialize the main event queue to monitor sockets
	int main_queue = event_queue_init();

	upsgi_add_sockets_to_queue(main_queue, core_id);
	event_queue_add_fd_read(main_queue, upsgi.loop_stop_pipe[0]);

	if (upsgi.signal_socket > -1) {
		event_queue_add_fd_read(main_queue, upsgi.signal_socket);
		event_queue_add_fd_read(main_queue, upsgi.my_signal_socket);
	}


	// ok we are ready, let's start managing requests and signals
	while (upsgi.workers[upsgi.mywid].manage_next_request) {

		wsgi_req_setup(wsgi_req, core_id, NULL);

		if (wsgi_req_accept(main_queue, wsgi_req)) {
			continue;
		}

		if (wsgi_req_recv(main_queue, wsgi_req)) {
			upsgi_destroy_request(wsgi_req);
			continue;
		}

		upsgi_close_request(wsgi_req);
	}

	// end of the loop
	if (upsgi.workers[upsgi.mywid].destroy && upsgi.workers[0].pid > 0) {
#ifdef __APPLE__
		kill(upsgi.workers[0].pid, SIGTERM);
#else
		if (upsgi.propagate_touch) {
			kill(upsgi.workers[0].pid, SIGHUP);
		}
		else {
			gracefully_kill(0);
		}
#endif
	}
	return NULL;
}
