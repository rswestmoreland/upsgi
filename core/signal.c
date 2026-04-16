#include "upsgi.h"

extern struct upsgi_server upsgi;

int upsgi_signal_handler(struct wsgi_request *wsgi_req, uint8_t sig) {

	struct upsgi_signal_entry *use = NULL;

	int pos = (upsgi.mywid * 256) + sig;

	use = &upsgi.shared->signal_table[pos];

	if (!use->handler)
		return -1;

	if (!upsgi.p[use->modifier1]->signal_handler) {
		return -1;
	}

	// check for COW
	if (upsgi.master_process) {
		if (use->wid != 0 && use->wid != upsgi.mywid) {
			upsgi_log("[upsgi-signal] you have registered this signal in worker %d memory area, only that process will be able to run it\n", use->wid);
			return -1;
		}
	}
	// in lazy mode (without a master), only the same worker will be able to run handlers
	else if (upsgi.lazy) {
		if (use->wid != upsgi.mywid) {
			upsgi_log("[upsgi-signal] you have registered this signal in worker %d memory area, only that process will be able to run it\n", use->wid);
			return -1;
		}
	}
	else {
		// when master is not active, worker1 is the COW-leader
		if (use->wid != 1 && use->wid != upsgi.mywid) {
			upsgi_log("[upsgi-signal] you have registered this signal in worker %d memory area, only that process will be able to run it\n", use->wid);
			return -1;
		}
	}

	// set harakiri here (if required and if i am a worker)

	if (upsgi.mywid > 0 && wsgi_req) {
		upsgi.workers[upsgi.mywid].sig = 1;
		upsgi.workers[upsgi.mywid].signum = sig;
		upsgi.workers[upsgi.mywid].signals++;
		if (upsgi.harakiri_options.workers > 0) {
			set_harakiri(wsgi_req, upsgi.harakiri_options.workers);
		}
	}

	int ret = upsgi.p[use->modifier1]->signal_handler(sig, use->handler);

	if (upsgi.mywid > 0 && wsgi_req) {
		upsgi.workers[upsgi.mywid].sig = 0;
		if (upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri > 0) {
			set_harakiri(wsgi_req, 0);
		}
	}

	return ret;
}

int upsgi_signal_registered(uint8_t sig) {

	int pos = (upsgi.mywid * 256) + sig;
	if (upsgi.shared->signal_table[pos].handler != NULL)
		return 1;

	return 0;
}

int upsgi_register_signal(uint8_t sig, char *receiver, void *handler, uint8_t modifier1) {

	struct upsgi_signal_entry *use = NULL;
	size_t receiver_len;

	if (!upsgi.master_process) {
		upsgi_log("you cannot register signals without a master\n");
		return -1;
	}

	if (upsgi.mywid == 0 && upsgi.workers[0].pid != upsgi.mypid) {
                upsgi_log("only the master and the workers can register signal handlers\n");
                return -1;
        }

	receiver_len = strlen(receiver);
	if (receiver_len > 63)
		return -1;

	upsgi_lock(upsgi.signal_table_lock);

	int pos = (upsgi.mywid * 256) + sig;
	use = &upsgi.shared->signal_table[pos];

	if (use->handler && upsgi.mywid == 0) {
		upsgi_log("[upsgi-signal] you cannot re-register a signal as the master !!!\n");
		upsgi_unlock(upsgi.signal_table_lock);
		return -1;
	}

	strncpy(use->receiver, receiver, receiver_len + 1);
	use->handler = handler;
	use->modifier1 = modifier1;
	use->wid = upsgi.mywid;

	if (use->receiver[0] == 0) {
		upsgi_log("[upsgi-signal] signum %d registered (wid: %d modifier1: %d target: default, any worker)\n", sig, upsgi.mywid, modifier1);
	}
	else {
		upsgi_log("[upsgi-signal] signum %d registered (wid: %d modifier1: %d target: %s)\n", sig, upsgi.mywid, modifier1, receiver);
	}

	// check for cow
	if (upsgi.mywid == 0) {
		int i;
                for(i=1;i<=upsgi.numproc;i++) {
                        int pos = (i * 256);
                        memcpy(&upsgi.shared->signal_table[pos], &upsgi.shared->signal_table[0], sizeof(struct upsgi_signal_entry) * 256);
                }
	}

	upsgi_unlock(upsgi.signal_table_lock);

	return 0;
}


int upsgi_add_file_monitor(uint8_t sig, char *filename) {

	if (strlen(filename) > (0xff - 1)) {
		upsgi_log("upsgi_add_file_monitor: invalid filename length\n");
		return -1;
	}

	upsgi_lock(upsgi.fmon_table_lock);

	if (ushared->files_monitored_cnt < 64) {

		// fill the fmon table, the master will use it to add items to the event queue
		memcpy(ushared->files_monitored[ushared->files_monitored_cnt].filename, filename, strlen(filename));
		ushared->files_monitored[ushared->files_monitored_cnt].registered = 0;
		ushared->files_monitored[ushared->files_monitored_cnt].sig = sig;

		ushared->files_monitored_cnt++;
	}
	else {
		upsgi_log("you can register max 64 file monitors !!!\n");
		upsgi_unlock(upsgi.fmon_table_lock);
		return -1;
	}

	upsgi_unlock(upsgi.fmon_table_lock);

	return 0;

}

int upsgi_add_timer_hr(uint8_t sig, int secs, long nsecs) {

	if (!upsgi.master_process) return -1;

	upsgi_lock(upsgi.timer_table_lock);

	if (ushared->timers_cnt < 64) {

		// fill the timer table, the master will use it to add items to the event queue
		ushared->timers[ushared->timers_cnt].value = secs;
		ushared->timers[ushared->timers_cnt].registered = 0;
		ushared->timers[ushared->timers_cnt].sig = sig;
		ushared->timers[ushared->timers_cnt].nsvalue = nsecs;
		ushared->timers_cnt++;
	}
	else {
		upsgi_log("you can register max 64 timers !!!\n");
		upsgi_unlock(upsgi.timer_table_lock);
		return -1;
	}

	upsgi_unlock(upsgi.timer_table_lock);

	return 0;

}

int upsgi_add_timer(uint8_t sig, int secs) {
	return upsgi_add_timer_hr(sig, secs, 0);
}

int upsgi_signal_add_rb_timer(uint8_t sig, int secs, int iterations) {

	if (!upsgi.master_process)
		return -1;

	upsgi_lock(upsgi.rb_timer_table_lock);

	if (ushared->rb_timers_cnt < 64) {

		// fill the timer table, the master will use it to add items to the event queue
		ushared->rb_timers[ushared->rb_timers_cnt].value = secs;
		ushared->rb_timers[ushared->rb_timers_cnt].registered = 0;
		ushared->rb_timers[ushared->rb_timers_cnt].iterations = iterations;
		ushared->rb_timers[ushared->rb_timers_cnt].iterations_done = 0;
		ushared->rb_timers[ushared->rb_timers_cnt].sig = sig;
		ushared->rb_timers_cnt++;
	}
	else {
		upsgi_log("you can register max 64 rb_timers !!!\n");
		upsgi_unlock(upsgi.rb_timer_table_lock);
		return -1;
	}

	upsgi_unlock(upsgi.rb_timer_table_lock);

	return 0;

}

void create_signal_pipe(int *sigpipe) {

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sigpipe)) {
		upsgi_error("socketpair()\n");
		exit(1);
	}
	upsgi_socket_nb(sigpipe[0]);
	upsgi_socket_nb(sigpipe[1]);

	if (upsgi.signal_bufsize) {
		if (setsockopt(sigpipe[0], SOL_SOCKET, SO_SNDBUF, &upsgi.signal_bufsize, sizeof(int))) {
			upsgi_error("setsockopt()");
		}
		if (setsockopt(sigpipe[0], SOL_SOCKET, SO_RCVBUF, &upsgi.signal_bufsize, sizeof(int))) {
			upsgi_error("setsockopt()");
		}

		if (setsockopt(sigpipe[1], SOL_SOCKET, SO_SNDBUF, &upsgi.signal_bufsize, sizeof(int))) {
			upsgi_error("setsockopt()");
		}
		if (setsockopt(sigpipe[1], SOL_SOCKET, SO_RCVBUF, &upsgi.signal_bufsize, sizeof(int))) {
			upsgi_error("setsockopt()");
		}
	}
}

int upsgi_remote_signal_send(char *addr, uint8_t sig) {

	struct upsgi_header uh;

	uh.modifier1 = 110;
	uh._pktsize = 0;
	uh.modifier2 = sig;

        int fd = upsgi_connect(addr, 0, 1);
        if (fd < 0) return -1;

        // wait for connection
        if (upsgi.wait_write_hook(fd, upsgi.socket_timeout) <= 0) goto end;

        if (upsgi_write_true_nb(fd, (char *) &uh, 4, upsgi.socket_timeout)) goto end;

	if (upsgi_read_whole_true_nb(fd, (char *) &uh, 4, upsgi.socket_timeout)) goto end;
	close(fd);

	return uh.modifier2;

end:
	close(fd);
	return -1;

}

int upsgi_signal_send(int fd, uint8_t sig) {

	socklen_t so_bufsize_len = sizeof(int);
	int so_bufsize = 0;

	if (write(fd, &sig, 1) != 1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &so_bufsize, &so_bufsize_len)) {
				upsgi_error("getsockopt()");
			}
			upsgi_log("*** SIGNAL QUEUE IS FULL: buffer size %d bytes (you can tune it with --signal-bufsize) ***\n", so_bufsize);
		}
		else {
			upsgi_error("upsgi_signal_send()");
		}
		upsgi.shared->unrouted_signals++;
		return -1;
	}
	upsgi.shared->routed_signals++;
	return 0;

}

void upsgi_route_signal(uint8_t sig) {

	int pos = (upsgi.mywid * 256) + sig;
	struct upsgi_signal_entry *use = &ushared->signal_table[pos];
	int i;

	// send to first available worker
	if (use->receiver[0] == 0 || !strcmp(use->receiver, "worker") || !strcmp(use->receiver, "worker0")) {
		if (upsgi_signal_send(ushared->worker_signal_pipe[0], sig)) {
			upsgi_log("could not deliver signal %d to workers pool\n", sig);
		}
	}
	// send to all workers
	else if (!strcmp(use->receiver, "workers")) {
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi_signal_send(upsgi.workers[i].signal_pipe[0], sig)) {
				upsgi_log("could not deliver signal %d to worker %d\n", sig, i);
			}
		}
	}
	// send to all active workers
	else if (!strcmp(use->receiver, "active-workers")) {
                for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].pid > 0 && !upsgi.workers[i].cheaped && !upsgi.workers[i].suspended) {
                        	if (upsgi_signal_send(upsgi.workers[i].signal_pipe[0], sig)) {
                                	upsgi_log("could not deliver signal %d to worker %d\n", sig, i);
                        	}
			}
                }
        }
	// route to specific worker
	else if (!strncmp(use->receiver, "worker", 6)) {
		i = atoi(use->receiver + 6);
		if (i > upsgi.numproc) {
			upsgi_log("invalid signal target: %s\n", use->receiver);
		}
		if (upsgi_signal_send(upsgi.workers[i].signal_pipe[0], sig)) {
			upsgi_log("could not deliver signal %d to worker %d\n", sig, i);
		}
	}
	// route to subscribed
	else if (!strcmp(use->receiver, "subscribed")) {
	}

	else {
		// unregistered signal, sending it to all the workers
		upsgi_log("^^^ UNSUPPORTED SIGNAL TARGET: %s ^^^\n", use->receiver);
	}

}

int upsgi_signal_wait(struct wsgi_request *wsgi_req, int signum) {

	int wait_for_specific_signal = 0;
	uint8_t upsgi_signal = 0;
	int received_signal = -1;
	int ret;
	struct pollfd pfd[2];

	if (signum > -1) {
		wait_for_specific_signal = 1;
	}

	pfd[0].fd = upsgi.signal_socket;
	pfd[0].events = POLLIN;
	pfd[1].fd = upsgi.my_signal_socket;
	pfd[1].events = POLLIN;

cycle:
	ret = poll(pfd, 2, -1);
	if (ret > 0) {
		if (pfd[0].revents == POLLIN) {
			if (read(upsgi.signal_socket, &upsgi_signal, 1) != 1) {
				upsgi_error("read()");
			}
			else {
				(void) upsgi_signal_handler(wsgi_req, upsgi_signal);
				if (wait_for_specific_signal) {
					if (signum != upsgi_signal)
						goto cycle;
				}
				received_signal = upsgi_signal;
			}
		}
		if (pfd[1].revents == POLLIN) {
			if (read(upsgi.my_signal_socket, &upsgi_signal, 1) != 1) {
				upsgi_error("read()");
			}
			else {
				(void) upsgi_signal_handler(wsgi_req, upsgi_signal);
				if (wait_for_specific_signal) {
					if (signum != upsgi_signal)
						goto cycle;
				}
			}
			received_signal = upsgi_signal;
		}

	}

	return received_signal;
}

int upsgi_receive_signal(struct wsgi_request *wsgi_req, int fd, char *name, int id) {
	uint8_t upsgi_signal;

	ssize_t ret = read(fd, &upsgi_signal, 1);

	if (ret == 0) {
		goto destroy;
	}
	else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
		upsgi_error("[upsgi-signal] read()");
		goto destroy;
	}
	else if (ret > 0) {
#ifdef UPSGI_DEBUG
		upsgi_log_verbose("master sent signal %d to %s %d\n", upsgi_signal, name, id);
#endif
		if (upsgi_signal_handler(wsgi_req, upsgi_signal)) {
			upsgi_log_verbose("error managing signal %d on %s %d\n", upsgi_signal, name, id);
		}
		return 1;
	}

	return 0;

destroy:
	// better to kill the whole worker...
	upsgi_log_verbose("upsgi %s %d error: the master disconnected from this worker. Shutting down the worker.\n", name, id);
	end_me(0);
	// never here
	return 0;
}
