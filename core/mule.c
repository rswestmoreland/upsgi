/*

upsgi mules are very simple workers only managing signals or running custom code in background.

By default they born in signal-only mode, but if you patch them (passing the script/code to run) they will became fully customized daemons.

*/

#include "upsgi.h"

extern struct upsgi_server upsgi;

void upsgi_mule_handler(void);

int mule_send_msg(int fd, char *message, size_t len) {

	socklen_t so_bufsize_len = sizeof(int);
	int so_bufsize = 0;

	if (write(fd, message, len) != (ssize_t) len) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &so_bufsize, &so_bufsize_len)) {
				upsgi_error("getsockopt()");
			}
			upsgi_log("*** MULE MSG QUEUE IS FULL: buffer size %d bytes (you can tune it with --mule-msg-size) ***\n", so_bufsize);
		}
		else {
			upsgi_error("mule_send_msg()");
		}
		return -1;
	}
	return 0;
}

void upsgi_mule(int id) {

	int i;

	pid_t pid = upsgi_fork(upsgi.mules[id - 1].name);
	if (pid == 0) {
#ifdef __linux__
		if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) {
			upsgi_error("prctl()");
		}
#endif

		signal(SIGALRM, SIG_IGN);
                signal(SIGHUP, end_me);
                signal(SIGINT, end_me);
                signal(SIGTERM, end_me);
                signal(SIGUSR1, SIG_IGN);
                signal(SIGUSR2, SIG_IGN);
                signal(SIGPIPE, SIG_IGN);
                signal(SIGSTOP, SIG_IGN);
                signal(SIGTSTP, SIG_IGN);

		upsgi.muleid = id;
		// avoid race conditions
		upsgi.mules[id - 1].id = id;
		upsgi.mules[id - 1].pid = getpid();
		upsgi.mypid = upsgi.mules[id - 1].pid;

		upsgi.mule_msg_recv_buf = upsgi_malloc(upsgi.mule_msg_recv_size);

		upsgi_fixup_fds(0, id, NULL);

		upsgi.my_signal_socket = upsgi.mules[id - 1].signal_pipe[1];
		upsgi.signal_socket = upsgi.shared->mule_signal_pipe[1];

		upsgi_close_all_sockets();

		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->master_fixup) {
				upsgi.p[i]->master_fixup(1);
			}
		}

		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->post_fork) {
				upsgi.p[i]->post_fork();
			}
		}

		upsgi_hooks_run(upsgi.hook_as_mule, "as-mule", 1);
		upsgi_mule_run();

	}
	else if (pid > 0) {
		upsgi.mules[id - 1].id = id;
		upsgi.mules[id - 1].pid = pid;
		upsgi_log("spawned upsgi mule %d (pid: %d)\n", id, (int) pid);
	}
}

void upsgi_mule_run() {
	int id = upsgi.muleid;
	int i;
	if (upsgi.mules[id - 1].patch) {
                        for (i = 0; i < 256; i++) {
                                if (upsgi.p[i]->mule) {
                                        if (upsgi.p[i]->mule(upsgi.mules[id - 1].patch) == 1) {
                                                // never here ?
                                                end_me(1);
                                        }
                                }
                        }
                }

                upsgi_mule_handler();
}

int upsgi_farm_has_mule(struct upsgi_farm *farm, int muleid) {

	struct upsgi_mule_farm *umf = farm->mules;

	while (umf) {
		if (umf->mule->id == muleid) {
			return 1;
		}
		umf = umf->next;
	}

	return 0;
}

int farm_has_signaled(int fd) {

	int i;
	for (i = 0; i < upsgi.farms_cnt; i++) {
		struct upsgi_mule_farm *umf = upsgi.farms[i].mules;
		while (umf) {
			if (umf->mule->id == upsgi.muleid && upsgi.farms[i].signal_pipe[1] == fd) {
				return 1;
			}
			umf = umf->next;
		}
	}

	return 0;
}

int farm_has_msg(int fd) {

	int i;
	for (i = 0; i < upsgi.farms_cnt; i++) {
		struct upsgi_mule_farm *umf = upsgi.farms[i].mules;
		while (umf) {
			if (umf->mule->id == upsgi.muleid && upsgi.farms[i].queue_pipe[1] == fd) {
				return 1;
			}
			umf = umf->next;
		}
	}

	return 0;
}


void upsgi_mule_add_farm_to_queue(int queue) {

	int i;
	for (i = 0; i < upsgi.farms_cnt; i++) {
		if (upsgi_farm_has_mule(&upsgi.farms[i], upsgi.muleid)) {
			event_queue_add_fd_read(queue, upsgi.farms[i].signal_pipe[1]);
			event_queue_add_fd_read(queue, upsgi.farms[i].queue_pipe[1]);
		}
	}
}

void upsgi_mule_handler() {

	ssize_t len;
	uint8_t upsgi_signal;
	int rlen;
	int interesting_fd;

	char *message = upsgi.mule_msg_recv_buf;

	int mule_queue = event_queue_init();

	event_queue_add_fd_read(mule_queue, upsgi.signal_socket);
	event_queue_add_fd_read(mule_queue, upsgi.my_signal_socket);
	event_queue_add_fd_read(mule_queue, upsgi.mules[upsgi.muleid - 1].queue_pipe[1]);
	event_queue_add_fd_read(mule_queue, upsgi.shared->mule_queue_pipe[1]);

	upsgi_mule_add_farm_to_queue(mule_queue);

	int fatal_errors_counter = 0;

	for (;;) {
		rlen = event_queue_wait(mule_queue, -1, &interesting_fd);
		if (rlen == 0) {
			continue;
		}

		if (rlen < 0) {
			if (errno == EINVAL) {
				fatal_errors_counter++;
				if (fatal_errors_counter >= 3) {
					upsgi_log_verbose("invalid internal state, restarting mule %d...\n", upsgi.muleid);
					end_me(0);
				}
			}
			continue;
		}

		fatal_errors_counter = 0;

		if (interesting_fd == upsgi.signal_socket || interesting_fd == upsgi.my_signal_socket || farm_has_signaled(interesting_fd)) {
			len = read(interesting_fd, &upsgi_signal, 1);
			if (len <= 0) {
				if (len < 0 && (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK)) continue;
				upsgi_log_verbose("upsgi mule %d braying: my master died, i will follow him...\n", upsgi.muleid);
				end_me(0);
			}
#ifdef UPSGI_DEBUG
			upsgi_log_verbose("master sent signal %d to mule %d\n", upsgi_signal, upsgi.muleid);
#endif
			if (upsgi_signal_handler(NULL, upsgi_signal)) {
				upsgi_log_verbose("error managing signal %d on mule %d\n", upsgi_signal, upsgi.muleid);
			}
		}
		else if (interesting_fd == upsgi.mules[upsgi.muleid - 1].queue_pipe[1] || interesting_fd == upsgi.shared->mule_queue_pipe[1] || farm_has_msg(interesting_fd)) {
			if(!message) {
				upsgi_log("*** MULE %d MESSAGE BUFFER IS NOT INITIALIZED ***\n", upsgi.muleid);
				continue;
			}
			len = read(interesting_fd, message, upsgi.mule_msg_recv_size);
			if (len < 0) {
				if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK) {
					upsgi_error("upsgi_mule_handler/read()");
				}
			}
			else {
				int i, found = 0;
				for (i = 0; i < 256; i++) {
					if (upsgi.p[i]->mule_msg) {
						if (upsgi.p[i]->mule_msg(message, len)) {
							found = 1;
							break;
						}
					}
				}
				if (!found)
					upsgi_log("*** mule %d received a %ld bytes message ***\n", upsgi.muleid, (long) len);
			}
		}
	}

}

struct upsgi_mule *get_mule_by_id(int id) {

	int i;

	for (i = 0; i < upsgi.mules_cnt; i++) {
		if (upsgi.mules[i].id == id) {
			return &upsgi.mules[i];
		}
	}

	return NULL;
}

struct upsgi_farm *get_farm_by_name(char *name) {

	int i;

	for (i = 0; i < upsgi.farms_cnt; i++) {
		if (!strcmp(upsgi.farms[i].name, name)) {
			return &upsgi.farms[i];
		}
	}

	return NULL;
}


struct upsgi_mule_farm *upsgi_mule_farm_new(struct upsgi_mule_farm **umf, struct upsgi_mule *um) {

	struct upsgi_mule_farm *upsgi_mf = *umf, *old_umf;

	if (!upsgi_mf) {
		*umf = upsgi_malloc(sizeof(struct upsgi_mule_farm));
		upsgi_mf = *umf;
	}
	else {
		while (upsgi_mf) {
			old_umf = upsgi_mf;
			upsgi_mf = upsgi_mf->next;
		}

		upsgi_mf = upsgi_malloc(sizeof(struct upsgi_mule_farm));
		old_umf->next = upsgi_mf;
	}

	upsgi_mf->mule = um;
	upsgi_mf->next = NULL;

	return upsgi_mf;
}

ssize_t upsgi_mule_get_msg(int manage_signals, int manage_farms, char *message, size_t buffer_size, int timeout) {

	ssize_t len = 0;
	struct pollfd *mulepoll;
	int count = 4;
	int farms_count = 0;
	uint8_t upsgi_signal;
	int i;

	if (upsgi.muleid == 0)
		return -1;

	if (manage_signals)
		count = 2;

	if (!manage_farms)
		goto next;

	for (i = 0; i < upsgi.farms_cnt; i++) {
		if (upsgi_farm_has_mule(&upsgi.farms[i], upsgi.muleid))
			farms_count++;
	}
next:

	if (timeout > -1)
		timeout = timeout * 1000;

	mulepoll = upsgi_malloc(sizeof(struct pollfd) * (count + farms_count));

	mulepoll[0].fd = upsgi.mules[upsgi.muleid - 1].queue_pipe[1];
	mulepoll[0].events = POLLIN;
	mulepoll[1].fd = upsgi.shared->mule_queue_pipe[1];
	mulepoll[1].events = POLLIN;
	if (count > 2) {
		mulepoll[2].fd = upsgi.signal_socket;
		mulepoll[2].events = POLLIN;
		mulepoll[3].fd = upsgi.my_signal_socket;
		mulepoll[3].events = POLLIN;
	}

	if (farms_count > 0) {
		int tmp_cnt = 0;
		for (i = 0; i < upsgi.farms_cnt; i++) {
			if (upsgi_farm_has_mule(&upsgi.farms[i], upsgi.muleid)) {
				mulepoll[count + tmp_cnt].fd = upsgi.farms[i].queue_pipe[1];
				mulepoll[count + tmp_cnt].events = POLLIN;
				tmp_cnt++;
			}
		}
	}

	int ret = -1;
retry:
	ret = poll(mulepoll, count + farms_count, timeout);
	if (ret < 0) {
		upsgi_error("upsgi_mule_get_msg()/poll()");
	}
	else if (ret > 0 ) {
		if (mulepoll[0].revents & POLLIN) {
			len = read(upsgi.mules[upsgi.muleid - 1].queue_pipe[1], message, buffer_size);
		}
		else if (mulepoll[1].revents & POLLIN) {
			len = read(upsgi.shared->mule_queue_pipe[1], message, buffer_size);
		}
		else {
			if (count > 2) {
				int interesting_fd = -1;
				if (mulepoll[2].revents & POLLIN) {
					interesting_fd = mulepoll[2].fd;
				}
				else if (mulepoll[3].revents & POLLIN) {
					interesting_fd = mulepoll[3].fd;
				}

				if (interesting_fd > -1) {
					len = read(interesting_fd, &upsgi_signal, 1);
					if (len <= 0) {
						if (upsgi_is_again()) goto retry;
						upsgi_log_verbose("upsgi mule %d braying: my master died, i will follow him...\n", upsgi.muleid);
						end_me(0);
					}
#ifdef UPSGI_DEBUG
					upsgi_log_verbose("master sent signal %d to mule %d\n", upsgi_signal, upsgi.muleid);
#endif
					if (upsgi_signal_handler(NULL, upsgi_signal)) {
						upsgi_log_verbose("error managing signal %d on mule %d\n", upsgi_signal, upsgi.muleid);
					}
					// set the error condition
					len = -1;
					goto clear;
				}
			}

			// read messages in the farm
			for (i = 0; i < farms_count; i++) {
				if (mulepoll[count + i].revents & POLLIN) {
					len = read(mulepoll[count + i].fd, message, buffer_size);
					break;
				}
			}
		}
	}

	if (len < 0) {
		if (upsgi_is_again()) goto retry;
		upsgi_error("read()");
		goto clear;
	}

clear:
	free(mulepoll);
	return len;
}


void upsgi_opt_add_mule(char *opt, char *value, void *foobar) {

	upsgi.mules_cnt++;
	upsgi_string_new_list(&upsgi.mules_patches, value);
}

void upsgi_opt_add_mules(char *opt, char *value, void *foobar) {
	int i;

	for (i = 0; i < atoi(value); i++) {
		upsgi.mules_cnt++;
		upsgi_string_new_list(&upsgi.mules_patches, NULL);
	}
}

void upsgi_opt_add_farm(char *opt, char *value, void *foobar) {
	upsgi.farms_cnt++;
	upsgi_string_new_list(&upsgi.farms_list, value);

}

void upsgi_setup_mules_and_farms() {
	int i;
	if (upsgi.mules_cnt > 0) {
		upsgi.mules = (struct upsgi_mule *) upsgi_calloc_shared(sizeof(struct upsgi_mule) * upsgi.mules_cnt);

		create_signal_pipe(upsgi.shared->mule_signal_pipe);
		create_msg_pipe(upsgi.shared->mule_queue_pipe, upsgi.mule_msg_size);

		for (i = 0; i < upsgi.mules_cnt; i++) {
			// create the socket pipe
			create_signal_pipe(upsgi.mules[i].signal_pipe);
			create_msg_pipe(upsgi.mules[i].queue_pipe, upsgi.mule_msg_size);

			upsgi.mules[i].id = i + 1;

			snprintf(upsgi.mules[i].name, 0xff, "upsgi mule %d", i + 1);
		}
	}

	if (upsgi.farms_cnt > 0) {
		upsgi.farms = (struct upsgi_farm *) upsgi_calloc_shared(sizeof(struct upsgi_farm) * upsgi.farms_cnt);

		struct upsgi_string_list *farm_name = upsgi.farms_list;
		for (i = 0; i < upsgi.farms_cnt; i++) {

			char *farm_value = upsgi_str(farm_name->value);

			char *mules_list = strchr(farm_value, ':');
			if (!mules_list) {
				upsgi_log("invalid farm value (%s) must be in the form name:mule[,muleN].\n", farm_value);
				exit(1);
			}

			mules_list[0] = 0;
			mules_list++;

			snprintf(upsgi.farms[i].name, 0xff, "%s", farm_value);

			// create the socket pipe
			create_signal_pipe(upsgi.farms[i].signal_pipe);
			create_msg_pipe(upsgi.farms[i].queue_pipe, upsgi.mule_msg_size);

			char *p, *ctx = NULL;
			upsgi_foreach_token(mules_list, ",", p, ctx) {
				struct upsgi_mule *um = get_mule_by_id(atoi(p));
				if (!um) {
					upsgi_log("invalid mule id: %s\n", p);
					exit(1);
				}

				upsgi_mule_farm_new(&upsgi.farms[i].mules, um);
			}
			upsgi_log("created farm %d name: %s mules:%s\n", i + 1, upsgi.farms[i].name, strchr(farm_name->value, ':') + 1);

			farm_name = farm_name->next;
			free(farm_value);
		}

	}

}
