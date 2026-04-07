#include "upsgi.h"

extern struct upsgi_server upsgi;


void upsgi_setup_systemd() {
	struct upsgi_socket *upsgi_sock = NULL;
	int i;

	char *listen_pid = getenv("LISTEN_PID");
	if (listen_pid) {
		if (atoi(listen_pid) == (int) getpid()) {
			char *listen_fds = getenv("LISTEN_FDS");
			if (listen_fds) {
				int systemd_fds = atoi(listen_fds);
				if (systemd_fds > 0) {
					upsgi_log("- SystemD socket activation detected -\n");
					for (i = 3; i < 3 + systemd_fds; i++) {
						upsgi_sock = upsgi_new_socket(NULL);
						upsgi_add_socket_from_fd(upsgi_sock, i);
					}
					upsgi.skip_zero = 1;
				}
				unsetenv("LISTEN_PID");
				unsetenv("LISTEN_FDS");
			}
		}
	}

}

void upsgi_setup_upstart() {

	struct upsgi_socket *upsgi_sock = NULL;

	char *upstart_events = getenv("UPSTART_EVENTS");
	if (upstart_events && !strcmp(upstart_events, "socket")) {
		char *upstart_fds = getenv("UPSTART_FDS");
		if (upstart_fds) {
			upsgi_log("- Upstart socket bridge detected (job: %s) -\n", getenv("UPSTART_JOB"));
			upsgi_sock = upsgi_new_socket(NULL);
			upsgi_add_socket_from_fd(upsgi_sock, atoi(upstart_fds));
			upsgi.skip_zero = 1;
		}
		unsetenv("UPSTART_EVENTS");
		unsetenv("UPSTART_FDS");
	}

}


void upsgi_setup_zerg() {

	struct upsgi_socket *upsgi_sock = NULL;
	int i;

	struct upsgi_string_list *zn = upsgi.zerg_node;
	while (zn) {
		if (upsgi_zerg_attach(zn->value)) {
			if (!upsgi.zerg_fallback) {
				exit(1);
			}
		}
		zn = zn->next;
	}



	if (upsgi.zerg) {
#ifdef UPSGI_DEBUG
		upsgi_log("attaching zerg sockets...\n");
#endif
		int zerg_fd;
		i = 0;
		for (;;) {
			zerg_fd = upsgi.zerg[i];
			if (zerg_fd == -1) {
				break;
			}
			upsgi_sock = upsgi_new_socket(NULL);
			upsgi_add_socket_from_fd(upsgi_sock, zerg_fd);
			i++;
		}

		upsgi_log("zerg sockets attached\n");
	}
}

void upsgi_setup_inherited_sockets() {

	int j;
	union upsgi_sockaddr usa;
	union upsgi_sockaddr_ptr gsa;

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		//a bit overengineering
		if (upsgi_sock->name[0] != 0 && !upsgi_sock->bound) {
			for (j = 3; j < (int) upsgi.max_fd; j++) {
				upsgi_add_socket_from_fd(upsgi_sock, j);
			}
		}
		upsgi_sock = upsgi_sock->next;
	}

	//now close all the unbound fd
	for (j = 3; j < (int) upsgi.max_fd; j++) {
		int useless = 1;

		if (upsgi_fd_is_safe(j)) continue;

		if (upsgi.has_emperor) {
			if (j == upsgi.emperor_fd)
				continue;
			if (j == upsgi.emperor_fd_config)
				continue;
		}

		if (upsgi.shared->worker_log_pipe[0] > -1) {
			if (j == upsgi.shared->worker_log_pipe[0])
				continue;
		}

		if (upsgi.shared->worker_log_pipe[1] > -1) {
			if (j == upsgi.shared->worker_log_pipe[1])
				continue;
		}

		if (upsgi.shared->worker_req_log_pipe[0] > -1) {
			if (j == upsgi.shared->worker_req_log_pipe[0])
				continue;
		}

		if (upsgi.shared->worker_req_log_pipe[1] > -1) {
			if (j == upsgi.shared->worker_req_log_pipe[1])
				continue;
		}

		if (upsgi.original_log_fd > -1) {
			if (j == upsgi.original_log_fd)
				continue;
		}

		struct upsgi_gateway_socket *ugs = upsgi.gateway_sockets;
		int found = 0;
		while (ugs) {
			if (ugs->fd == j) {
				found = 1;
				break;
			}
			ugs = ugs->next;
		}
		if (found)
			continue;


		int y;
		found = 0;
		for (y = 0; y < ushared->gateways_cnt; y++) {
			if (ushared->gateways[y].internal_subscription_pipe[0] == j) {
				found = 1;
				break;
			}
			if (ushared->gateways[y].internal_subscription_pipe[1] == j) {
				found = 1;
				break;
			}
		}

		if (found)
			continue;


		socklen_t socket_type_len = sizeof(struct sockaddr_un);
		gsa.sa = (struct sockaddr *) &usa;
		if (!getsockname(j, gsa.sa, &socket_type_len)) {
			upsgi_sock = upsgi.sockets;
			while (upsgi_sock) {
				if (upsgi_sock->fd == j && upsgi_sock->bound) {
					useless = 0;
					break;
				}
				upsgi_sock = upsgi_sock->next;
			}

			if (useless) {
				upsgi_sock = upsgi.shared_sockets;
				while (upsgi_sock) {
					if (upsgi_sock->fd == j && upsgi_sock->bound) {
						useless = 0;
						break;
					}
					upsgi_sock = upsgi_sock->next;
				}
			}
		}

		if (useless) {
			close(j);
		}
	}

}
