#include "upsgi.h"

extern struct upsgi_server upsgi;

struct upsgi_gateway *register_gateway(char *name, void (*loop) (int, void *), void *data) {

	struct upsgi_gateway *ug;
	int num = 1, i;

	if (ushared->gateways_cnt >= MAX_GATEWAYS) {
		upsgi_log("you can register max %d gateways\n", MAX_GATEWAYS);
		return NULL;
	}

	for (i = 0; i < ushared->gateways_cnt; i++) {
		if (!strcmp(name, ushared->gateways[i].name)) {
			num++;
		}
	}

	char *str = upsgi_num2str(num);
	char *fullname = upsgi_concat3(name, " ", str);
	free(str);

	ug = &ushared->gateways[ushared->gateways_cnt];
	ug->pid = 0;
	ug->name = name;
	ug->loop = loop;
	ug->num = num;
	ug->fullname = fullname;
	ug->data = data;
	ug->uid = 0;
	ug->gid = 0;

#if defined(SOCK_SEQPACKET) && defined(__linux__)
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ug->internal_subscription_pipe)) {
#else
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, ug->internal_subscription_pipe)) {
#endif
		upsgi_error("socketpair()");
	}

	upsgi_socket_nb(ug->internal_subscription_pipe[0]);
	upsgi_socket_nb(ug->internal_subscription_pipe[1]);

	if (!upsgi.master_process && !upsgi.force_gateway)
		gateway_respawn(ushared->gateways_cnt);

	ushared->gateways_cnt++;


	return ug;
}

static void gateway_brutal_end() {
        _exit(UPSGI_END_CODE);
}

void gateway_respawn(int id) {

	pid_t gw_pid;
	struct upsgi_gateway *ug = &ushared->gateways[id];

	if (upsgi.master_process)
		upsgi.shared->gateways_harakiri[id] = 0;

	gw_pid = upsgi_fork(ug->fullname);
	if (gw_pid < 0) {
		upsgi_error("fork()");
		return;
	}

	if (gw_pid == 0) {
		upsgi_fixup_fds(0, 0, ug);
		upsgi_close_all_unshared_sockets();
		if (upsgi.master_as_root)
			upsgi_as_root();
#ifdef __linux__
		if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) {
			upsgi_error("prctl()");
		}
#endif
		upsgi.mypid = getpid();
		atexit(gateway_brutal_end);
		signal(SIGALRM, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
		signal(SIGINT, end_me);
		signal(SIGTERM, end_me);
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);
		signal(SIGPIPE, SIG_IGN);
		signal(SIGSTOP, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);

		upsgi_hooks_run(upsgi.hook_as_gateway, "as-gateway", 1);

		if (ug->gid) {
			upsgi_log("%s %d setgid() to %d\n", ug->name, ug->num, (int) ug->gid);
			if (setgid(ug->gid)) {
				upsgi_error("gateway_respawn()/setgid()");
				exit(1);
			}
		}

		if (ug->uid) {
			upsgi_log("%s %d setuid() to %d\n", ug->name, ug->num, (int) ug->uid);
			if (setuid(ug->uid)) {
				upsgi_error("gateway_respawn()/setuid()");
				exit(1);
			}
		}

		ug->loop(id, ug->data);
		// never here !!! (i hope)
		exit(1);
	}

	ug->pid = gw_pid;
	ug->respawns++;
	if (ug->respawns == 1) {
		upsgi_log("spawned %s %d (pid: %d)\n", ug->name, ug->num, (int) gw_pid);
	}
	else {
		upsgi_log("respawned %s %d (pid: %d)\n", ug->name, ug->num, (int) gw_pid);
	}

}

struct upsgi_gateway_socket *upsgi_new_gateway_socket(char *name, char *owner) {

	struct upsgi_gateway_socket *upsgi_sock = upsgi.gateway_sockets, *old_upsgi_sock;

	if (!upsgi_sock) {
		upsgi.gateway_sockets = upsgi_malloc(sizeof(struct upsgi_gateway_socket));
		upsgi_sock = upsgi.gateway_sockets;
	}
	else {
		while (upsgi_sock) {
			old_upsgi_sock = upsgi_sock;
			upsgi_sock = upsgi_sock->next;
		}

		upsgi_sock = upsgi_malloc(sizeof(struct upsgi_gateway_socket));
		old_upsgi_sock->next = upsgi_sock;
	}

	memset(upsgi_sock, 0, sizeof(struct upsgi_gateway_socket));
	upsgi_sock->fd = -1;
	upsgi_sock->shared = 0;
	upsgi_sock->name = name;
	upsgi_sock->name_len = strlen(name);
	upsgi_sock->owner = owner;

	return upsgi_sock;
}

struct upsgi_gateway_socket *upsgi_new_gateway_socket_from_fd(int fd, char *owner) {

	struct upsgi_gateway_socket *upsgi_sock = upsgi.gateway_sockets, *old_upsgi_sock;

	if (!upsgi_sock) {
		upsgi.gateway_sockets = upsgi_malloc(sizeof(struct upsgi_gateway_socket));
		upsgi_sock = upsgi.gateway_sockets;
	}
	else {
		while (upsgi_sock) {
			old_upsgi_sock = upsgi_sock;
			upsgi_sock = upsgi_sock->next;
		}

		upsgi_sock = upsgi_malloc(sizeof(struct upsgi_gateway_socket));
		old_upsgi_sock->next = upsgi_sock;
	}

	memset(upsgi_sock, 0, sizeof(struct upsgi_gateway_socket));
	upsgi_sock->fd = fd;
	upsgi_sock->name = upsgi_getsockname(fd);
	upsgi_sock->name_len = strlen(upsgi_sock->name);
	upsgi_sock->owner = owner;

	return upsgi_sock;
}


void upsgi_gateway_go_cheap(char *gw_id, int queue, int *i_am_cheap) {

	upsgi_log("[%s pid %d] no more nodes available. Going cheap...\n", gw_id, (int) upsgi.mypid);
	struct upsgi_gateway_socket *ugs = upsgi.gateway_sockets;
	while (ugs) {
		if (!strcmp(ugs->owner, gw_id) && !ugs->subscription) {
			event_queue_del_fd(queue, ugs->fd, event_queue_read());
		}
		ugs = ugs->next;
	}
	*i_am_cheap = 1;
}
