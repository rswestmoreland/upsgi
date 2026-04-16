#include "upsgi.h"

extern struct upsgi_server upsgi;

static int connect_to_unix(char *, int, int);
static int connect_to_tcp(char *, int, int, int);
static int connect_to_udp(char *, int);

void upsgi_socket_setup_protocol(struct upsgi_socket *upsgi_sock, char *protocol) {
        if (!protocol) protocol = "upsgi";
        struct upsgi_protocol *up = upsgi.protocols;
        while(up) {
                if (!strcmp(protocol, up->name)) {
                        up->func(upsgi_sock);
                        return;
                }
                up = up->next;
        }

        upsgi_log("unable to find protocol %s\n", protocol);
        exit(1);
}


static int upsgi_socket_strcmp(char *sock1, char *sock2) {
	size_t sock1_len = strlen(sock1);
	size_t sock2_len = strlen(sock2);

	if (!upsgi_starts_with(sock1, sock1_len, "0.0.0.0:", 8)) {
		sock1 += 7;
		sock1_len = strlen(sock1);
	}

	if (!upsgi_starts_with(sock2, sock2_len, "0.0.0.0:", 8)) {
                sock2 += 7;
                sock2_len = strlen(sock2);
        }

	return upsgi_strncmp(sock1, sock1_len, sock2, sock2_len);
}

char *upsgi_getsockname(int fd) {

	socklen_t socket_type_len = sizeof(struct sockaddr_un);
	union upsgi_sockaddr usa;
	union upsgi_sockaddr_ptr gsa;
	char computed_port[6];
	char ipv4a[INET_ADDRSTRLEN + 1];

	gsa.sa = (struct sockaddr *) &usa;

	if (!getsockname(fd, gsa.sa, &socket_type_len)) {
		if (gsa.sa->sa_family == AF_UNIX) {
			// unnamed socket ?
			if (socket_type_len == sizeof(sa_family_t)) return "";
			if (usa.sa_un.sun_path[0] == 0) {
				return upsgi_concat2("@", usa.sa_un.sun_path + 1);
			}
			else {
				return upsgi_str(usa.sa_un.sun_path);
			}
		}
		else {
			memset(ipv4a, 0, INET_ADDRSTRLEN + 1);
			memset(computed_port, 0, 6);
			if (snprintf(computed_port, 6, "%d", ntohs(gsa.sa_in->sin_port)) > 0) {
				if (inet_ntop(AF_INET, (const void *) &gsa.sa_in->sin_addr.s_addr, ipv4a, INET_ADDRSTRLEN)) {
					if (!strcmp("0.0.0.0", ipv4a)) {
						return upsgi_concat2(":", computed_port);
					}
					else {
						return upsgi_concat3(ipv4a, ":", computed_port);
					}
				}
			}
		}
	}
	return NULL;
}

static int create_server_socket(int domain, int type) {
	int serverfd = socket(domain, type, 0);
	if (serverfd < 0) {
		upsgi_error("socket()");
		upsgi_nuclear_blast();
		return -1;
	}

	if (upsgi.close_on_exec2 && fcntl(serverfd, F_SETFD, FD_CLOEXEC) < 0)
		upsgi_error("fcntl()");

	if (domain != AF_UNIX) {
		int reuse = 1;
		if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &reuse, sizeof(int)) < 0) {
			upsgi_error("SO_REUSEADDR setsockopt()");
			upsgi_nuclear_blast();
			return -1;
		}
	}

	if (type == SOCK_STREAM || type == SOCK_DGRAM) {
		if (upsgi.so_sndbuf) {
			socklen_t sndbuf = (socklen_t) upsgi.so_sndbuf;
			if (setsockopt(serverfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(socklen_t)) < 0) {
				upsgi_error("SO_SNDBUF setsockopt()");
				upsgi_nuclear_blast();
				return -1;
			}
			else {
				upsgi_log("successfully set SO_SNDBUF to %u for socket fd %d\n", sndbuf, serverfd);
			}
		}

		if (upsgi.so_rcvbuf) {
			socklen_t rcvbuf = (socklen_t) upsgi.so_rcvbuf;
			if (setsockopt(serverfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(socklen_t)) < 0) {
				upsgi_error("SO_RCVBUF setsockopt()");
				upsgi_nuclear_blast();
				return -1;
			}
			else {
				upsgi_log("successfully set SO_RCVBUF to %u for socket fd %d\n", rcvbuf, serverfd);
			}
		}

#ifdef __linux__
		long somaxconn = upsgi_num_from_file("/proc/sys/net/core/somaxconn", 1);
		if (somaxconn > 0 && upsgi.listen_queue > somaxconn) {
			upsgi_log("Listen queue size is greater than the system max net.core.somaxconn (%li).\n", somaxconn);
			upsgi_nuclear_blast();
			return -1;
		}
#endif
	}

	return serverfd;
}

int bind_to_unix_dgram(char *socket_name) {

	int serverfd;
	struct sockaddr_un *uws_addr;
	socklen_t len;

	serverfd = create_server_socket(AF_UNIX, SOCK_DGRAM);
	if (serverfd < 0) return -1;

	if (unlink(socket_name) != 0 && errno != ENOENT) {
		upsgi_error("error removing unix socket, unlink()");
	}

	uws_addr = upsgi_calloc(sizeof(struct sockaddr_un));
	uws_addr->sun_family = AF_UNIX;

	memcpy(uws_addr->sun_path, socket_name, UMIN(strlen(socket_name), 102));
	len = strlen(socket_name);

#ifdef __HAIKU__
	if (bind(serverfd, (struct sockaddr *) uws_addr, sizeof(struct sockaddr_un))) {
#else
	if (bind(serverfd, (struct sockaddr *) uws_addr, len + ((void *) uws_addr->sun_path - (void *) uws_addr)) != 0) {
#endif
		upsgi_error("bind()");
		upsgi_nuclear_blast();
		return -1;
	}

	return serverfd;
}

int bind_to_unix(char *socket_name, int listen_queue, int chmod_socket, int abstract_socket) {

	int serverfd;
	struct sockaddr_un *uws_addr;
	socklen_t len;

	// leave 1 byte for abstract namespace (108 linux -> 104 bsd/mac)
	if (strlen(socket_name) > 102) {
		upsgi_log("invalid socket name\n");
		upsgi_nuclear_blast();
		return -1;
	}

	if (socket_name[0] == '@') {
		abstract_socket = 1;
	}
	else if (strlen(socket_name) > 1 && socket_name[0] == '\\' && socket_name[1] == '0') {
		abstract_socket = 1;
	}

	uws_addr = malloc(sizeof(struct sockaddr_un));
	if (uws_addr == NULL) {
		upsgi_error("malloc()");
		upsgi_nuclear_blast();
		return -1;
	}

	memset(uws_addr, 0, sizeof(struct sockaddr_un));
	serverfd = create_server_socket(AF_UNIX, SOCK_STREAM);
	if (serverfd < 0) {
		free(uws_addr);
		return -1;
	}
	if (abstract_socket == 0) {
		if (unlink(socket_name) != 0 && errno != ENOENT) {
			upsgi_error("error removing unix socket, unlink()");
		}
	}

	if (abstract_socket == 1) {
		upsgi_log("setting abstract socket mode (warning: only Linux supports this)\n");
	}

	uws_addr->sun_family = AF_UNIX;
	if (socket_name[0] == '@') {
		memcpy(uws_addr->sun_path + abstract_socket, socket_name + 1, UMIN(strlen(socket_name + 1), 101));
		len = strlen(socket_name) + 1;
	}
	else if (strlen(socket_name) > 1 && socket_name[0] == '\\' && socket_name[1] == '0') {
		memcpy(uws_addr->sun_path + abstract_socket, socket_name + 2, UMIN(strlen(socket_name + 2), 101));
		len = strlen(socket_name + 1) + 1;

	}
	else if (abstract_socket) {
		memcpy(uws_addr->sun_path + 1, socket_name, UMIN(strlen(socket_name), 101));
		len = strlen(socket_name) + 1;
	}
	else {
		memcpy(uws_addr->sun_path + abstract_socket, socket_name, UMIN(strlen(socket_name), 102));
		len = strlen(socket_name);
	}

#ifdef __HAIKU__
	if (bind(serverfd, (struct sockaddr *) uws_addr, sizeof(struct sockaddr_un))) {
#else
	if (bind(serverfd, (struct sockaddr *) uws_addr, len + ((void *) uws_addr->sun_path - (void *) uws_addr)) != 0) {
#endif
		upsgi_error("bind()");
		upsgi_nuclear_blast();
		return -1;
	}

	if (listen(serverfd, listen_queue) != 0) {
		upsgi_error("listen()");
		upsgi_nuclear_blast();
		return -1;
	}

	// chmod unix socket for lazy users
	if (chmod_socket == 1 && abstract_socket == 0) {
		if (upsgi.chmod_socket_value) {
			if (chmod(socket_name, upsgi.chmod_socket_value) != 0) {
				upsgi_error("chmod()");
			}
		}
		else {
			upsgi_log("chmod() socket to 666 for lazy and brave users\n");
			if (chmod(socket_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) != 0) {
				upsgi_error("chmod()");
			}
		}
	}

	free(uws_addr);

	return serverfd;
}

int bind_to_udp(char *socket_name, int multicast, int broadcast) {
	int serverfd;
	struct sockaddr_in uws_addr;
	char *udp_port;
	int bcast = 1;

	struct ip_mreq mc;

	udp_port = strchr(socket_name, ':');
	if (udp_port == NULL) {
		return -1;
	}

	udp_port[0] = 0;

	if (socket_name[0] == 0 && multicast) {
		upsgi_log("invalid multicast address\n");
		return -1;
	}
	memset(&uws_addr, 0, sizeof(struct sockaddr_in));
	uws_addr.sin_family = AF_INET;
	uws_addr.sin_port = htons(atoi(udp_port + 1));

	if (!broadcast && !multicast) {
		char quad[4];
		char *first_part = strchr(socket_name, '.');
		if (first_part && first_part - socket_name < 4) {
			memset(quad, 0, 4);
			memcpy(quad, socket_name, first_part - socket_name);
			if (atoi(quad) >= 224 && atoi(quad) <= 239) {
				multicast = 1;
			}
		}
		if (!strcmp(socket_name, "255.255.255.255")) {
			broadcast = 1;
		}
	}

	if (broadcast) {
		uws_addr.sin_addr.s_addr = INADDR_BROADCAST;
	}
	else if (socket_name[0] != 0) {
		uws_addr.sin_addr.s_addr = inet_addr(socket_name);
	}
	else {
		uws_addr.sin_addr.s_addr = INADDR_ANY;
	}


	serverfd = create_server_socket(AF_INET, SOCK_DGRAM);
	if (serverfd < 0) return -1;

	if (multicast) {
		// if multicast is enabled remember to bind to INADDR_ANY
		uws_addr.sin_addr.s_addr = INADDR_ANY;
		mc.imr_multiaddr.s_addr = inet_addr(socket_name);
		mc.imr_interface.s_addr = INADDR_ANY;
	}

	if (broadcast) {
		if (setsockopt(serverfd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast))) {
			perror("setsockopt");
			close(serverfd);
			return -1;
		}
	}

	if (bind(serverfd, (struct sockaddr *) &uws_addr, sizeof(uws_addr)) != 0) {
		upsgi_error("bind()");
		close(serverfd);
		return -1;
	}

	if (multicast) {
		upsgi_log("[upsgi-mcast] joining multicast group: %s:%d\n", socket_name, ntohs(uws_addr.sin_port));
		if (setsockopt(serverfd, IPPROTO_IP, IP_MULTICAST_LOOP, &upsgi.multicast_loop, sizeof(upsgi.multicast_loop))) {
			upsgi_error("setsockopt()");
		}

		if (setsockopt(serverfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc, sizeof(mc))) {
			upsgi_error("setsockopt()");
		}

		if (setsockopt(serverfd, IPPROTO_IP, IP_MULTICAST_TTL, &upsgi.multicast_ttl, sizeof(upsgi.multicast_ttl))) {
			upsgi_error("setsockopt()");
		}

	}

	udp_port[0] = ':';
	return serverfd;

}

static int upsgi_connect_do(char *socket_name, int timeout, int async) {
        char *tcp_port = strchr(socket_name, ':');

        if (tcp_port) {
                tcp_port[0] = 0;
                tcp_port++;
                return connect_to_tcp(socket_name, atoi(tcp_port), timeout, async);
        }

        return connect_to_unix(socket_name, timeout, async);
}

int upsgi_connectn(char *socket_name, uint16_t len, int timeout, int async) {
	char *zeroed_socket_name = upsgi_concat2n(socket_name, len, "", 0);
	int fd = upsgi_connect_do(zeroed_socket_name, timeout, async);
	free(zeroed_socket_name);
	return fd;
}

int upsgi_connect(char *socket_name, int timeout, int async) {
	char *zeroed_socket_name = upsgi_str(socket_name);
	int fd = upsgi_connect_do(zeroed_socket_name, timeout, async);
	free(zeroed_socket_name);
	return fd;
}

int upsgi_connect_udp(char *socket_name) {
	int fd = -1;
	char *zeroed_socket_name = upsgi_str(socket_name);
	char *udp_port = strchr(zeroed_socket_name, ':');
	if (!udp_port) goto end;
	*udp_port = 0;
	udp_port++;
        fd = connect_to_udp(zeroed_socket_name, atoi(udp_port));
end:
        free(zeroed_socket_name);
        return fd;
}

static int connect_to_unix(char *socket_name, int timeout, int async) {

	struct pollfd upsgi_poll;
	struct sockaddr_un uws_addr;
	socklen_t un_size = sizeof(struct sockaddr_un);

	memset(&uws_addr, 0, sizeof(struct sockaddr_un));

	uws_addr.sun_family = AF_UNIX;

	if (socket_name[0] == '@') {
		un_size = sizeof(uws_addr.sun_family) + strlen(socket_name) + 1;
		memcpy(uws_addr.sun_path + 1, socket_name + 1, UMIN(strlen(socket_name + 1), 101));
	}
	else if (strlen(socket_name) > 1 && socket_name[0] == '\\' && socket_name[1] == '0') {
		un_size = sizeof(uws_addr.sun_family) + strlen(socket_name + 1) + 1;
		memcpy(uws_addr.sun_path + 1, socket_name + 2, UMIN(strlen(socket_name + 2), 101));
	}
	else {
		memcpy(uws_addr.sun_path, socket_name, UMIN(strlen(socket_name), 102));
	}

#if defined(__linux__) && defined(SOCK_NONBLOCK) && !defined(OBSOLETE_LINUX_KERNEL)
	upsgi_poll.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
#else
	upsgi_poll.fd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
	if (upsgi_poll.fd < 0) {
		upsgi_error("socket()");
		return -1;
	}

	upsgi_poll.events = POLLIN;

	if (timed_connect(&upsgi_poll, (const struct sockaddr *) &uws_addr, un_size, timeout, async)) {
		// avoid error storm
		//upsgi_error("connect()");
		close(upsgi_poll.fd);
		return -1;
	}

	return upsgi_poll.fd;

}

static int connect_to_tcp(char *socket_name, int port, int timeout, int async) {

	struct pollfd upsgi_poll;
	struct sockaddr_in uws_addr;

	memset(&uws_addr, 0, sizeof(struct sockaddr_in));

	uws_addr.sin_family = AF_INET;
	uws_addr.sin_port = htons(port);

	if (socket_name[0] == 0) {
		uws_addr.sin_addr.s_addr = INADDR_ANY;
	}
	else {
		uws_addr.sin_addr.s_addr = inet_addr(socket_name);
	}

#if defined(__linux__) && defined(SOCK_NONBLOCK) && !defined(OBSOLETE_LINUX_KERNEL)
	upsgi_poll.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
#else
	upsgi_poll.fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
	if (upsgi_poll.fd < 0) {
		upsgi_error("connect_to_tcp()/socket()");
		return -1;
	}

	upsgi_poll.events = POLLIN;

	if (timed_connect(&upsgi_poll, (const struct sockaddr *) &uws_addr, sizeof(struct sockaddr_in), timeout, async)) {
		//upsgi_error("connect()");
		close(upsgi_poll.fd);
		return -1;
	}

	return upsgi_poll.fd;

}

static int connect_to_udp(char *socket_name, int port) {

        struct sockaddr_in uws_addr;
        memset(&uws_addr, 0, sizeof(struct sockaddr_in));

        uws_addr.sin_family = AF_INET;
        uws_addr.sin_port = htons(port);

        if (socket_name[0] == 0) {
                uws_addr.sin_addr.s_addr = INADDR_ANY;
        }
        else {
                uws_addr.sin_addr.s_addr = inet_addr(socket_name);
        }

        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
                upsgi_error("connect_to_udp()/socket()");
                return -1;
        }

	if (connect(fd, (const struct sockaddr *) &uws_addr, sizeof(struct sockaddr_in))) {
		close(fd);
		return -1;
	}

        return fd;

}



char *generate_socket_name(char *socket_name) {

	char *asterisk = strchr(socket_name, '*');

	char *tcp_port;
	int i;
	char *ptr = socket_name;

	// ltrim spaces

	for (i = 0; i < (int) strlen(socket_name); i++) {
		if (isspace((int) socket_name[i])) {
			ptr++;
		}
		else {
			break;
		}
	}

	socket_name = ptr;

	if (socket_name[0] == 0) {
		upsgi_log("invalid/empty upsgi socket name\n");
		exit(1);
	}

	tcp_port = strchr(socket_name, ':');
	if (!tcp_port)
		return socket_name;

	if (asterisk) {

#ifndef UPSGI_HAS_IFADDRS
		upsgi_log("your system does not support ifaddrs subsystem\n");
#else
		char *new_socket;

#ifdef UPSGI_DEBUG
		upsgi_log("generate_socket_name(%s)\n", socket_name);
#endif
		// get all the AF_INET addresses available
		struct ifaddrs *ifap = NULL, *ifa, *ifaf;
		if (getifaddrs(&ifap)) {
			upsgi_error("getifaddrs()");
			upsgi_nuclear_blast();
		}

		// here socket_name will be truncated
		asterisk[0] = 0;

#ifdef UPSGI_DEBUG
		upsgi_log("asterisk found\n");
#endif

		char new_addr[16];
		struct sockaddr_in *sin;
		ifa = ifap;
		while (ifa) {
			memset(new_addr, 0, 16);
			if (!ifa->ifa_addr) goto next;
			sin = (struct sockaddr_in *) ifa->ifa_addr;
			if (inet_ntop(AF_INET, (void *) &sin->sin_addr.s_addr, new_addr, 16)) {
				if (!strncmp(socket_name, new_addr, strlen(socket_name))) {
					asterisk[0] = '*';
					new_socket = upsgi_concat3(new_addr, ":", tcp_port + 1);
					upsgi_log("[upsgi-autoip] found %s for %s on interface %s\n", new_socket, socket_name, ifa->ifa_name);
					freeifaddrs(ifap);
					return new_socket;
				}

			}

next:
			ifaf = ifa;
			ifa = ifaf->ifa_next;

		}

		upsgi_log("unable to find a valid socket address\n");
#endif
		upsgi_nuclear_blast();
	}
	return socket_name;
}

socklen_t socket_to_un_addr(char *socket_name, struct sockaddr_un * sun_addr) {

	size_t len = strlen(socket_name);

	if (len > 102) {
		upsgi_log("invalid UNIX socket address: %s\n", socket_name);
		upsgi_nuclear_blast();
	}

	memset(sun_addr, 0, sizeof(struct sockaddr_un));

	sun_addr->sun_family = AF_UNIX;

	// abstract socket
	if (socket_name[0] == '@') {
		memcpy(sun_addr->sun_path + 1, socket_name + 1, UMIN(len - 1, 101));
		len = strlen(socket_name) + 1;
	}
	else if (len > 1 && socket_name[0] == '\\' && socket_name[1] == '0') {
		memcpy(sun_addr->sun_path + 1, socket_name + 2, UMIN(len - 2, 101));
		len = strlen(socket_name + 1) + 1;
	}
	else {
		memcpy(sun_addr->sun_path, socket_name, UMIN(len, 102));
	}

	return sizeof(sun_addr->sun_family) + len;
}

socklen_t socket_to_in_addr(char *socket_name, char *port, int portn, struct sockaddr_in *sin_addr) {

	memset(sin_addr, 0, sizeof(struct sockaddr_in));

	sin_addr->sin_family = AF_INET;
	if (port) {
		*port = 0;
		sin_addr->sin_port = htons(atoi(port + 1));
	}
	else {
		sin_addr->sin_port = htons(portn);
	}

	if (socket_name[0] == 0) {
		sin_addr->sin_addr.s_addr = INADDR_ANY;
	}
	else {
		char *resolved = upsgi_resolve_ip(socket_name);
		if (resolved) {
			sin_addr->sin_addr.s_addr = inet_addr(resolved);
		}
		else {
			sin_addr->sin_addr.s_addr = inet_addr(socket_name);
		}
	}

	if (port) {
		*port = ':';
	}

	return sizeof(struct sockaddr_in);

}

int bind_to_tcp(char *socket_name, int listen_queue, char *tcp_port) {

	int serverfd;
#ifdef AF_INET6
	struct sockaddr_in6 uws_addr;
#else
	struct sockaddr_in uws_addr;
#endif
	int family = AF_INET;
	socklen_t addr_len = sizeof(struct sockaddr_in);

#ifdef AF_INET6
	if (socket_name[0] == '[' && tcp_port[-1] == ']') {
		family = AF_INET6;
		socket_to_in_addr6(socket_name, tcp_port, 0, &uws_addr);
		addr_len = sizeof(struct sockaddr_in6);
	}
	else {	
#endif
		socket_to_in_addr(socket_name, tcp_port, 0, (struct sockaddr_in *) &uws_addr);
#ifdef AF_INET6
	}
#endif


	serverfd = create_server_socket(family, SOCK_STREAM);
	if (serverfd < 0) return -1;
	
#ifdef __linux__
#ifndef IP_FREEBIND
#define IP_FREEBIND 15
#endif
	if (upsgi.freebind) {
		if (setsockopt(serverfd, SOL_IP, IP_FREEBIND, (const void *) &upsgi.freebind, sizeof(int)) < 0) {
			upsgi_error("IP_FREEBIND setsockopt()");
			upsgi_nuclear_blast();
			return -1;
		}
	}
#endif

	if (upsgi.reuse_port) {
#ifdef SO_REUSEPORT
		if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEPORT, (const void *) &upsgi.reuse_port, sizeof(int)) < 0) {
			upsgi_error("SO_REUSEPORT setsockopt()");
			upsgi_nuclear_blast();
			return -1;
		}
#else
		upsgi_log("!!! your system does not support SO_REUSEPORT !!!\n");
#endif
	}

	if (upsgi.tcp_fast_open) {
#ifdef TCP_FASTOPEN

    #ifndef SOL_TCP
    #define SOL_TCP IPPROTO_TCP
    #endif

		if (setsockopt(serverfd, SOL_TCP, TCP_FASTOPEN, (const void *) &upsgi.tcp_fast_open, sizeof(int)) < 0) {
			upsgi_error("TCP_FASTOPEN setsockopt()");
		}
		else {
			upsgi_log("TCP_FASTOPEN enabled on %s\n", socket_name);
		}
#else
		upsgi_log("!!! your system does not support TCP_FASTOPEN !!!\n");
#endif
	}

	if (upsgi.so_send_timeout) {
		struct timeval tv;
		tv.tv_sec = upsgi.so_send_timeout;
		tv.tv_usec = 0;
		if (setsockopt(serverfd, SOL_SOCKET, SO_SNDTIMEO, (const void *) &tv, sizeof(struct timeval)) < 0) {
			upsgi_error("SO_SNDTIMEO setsockopt()");
			upsgi_nuclear_blast();
			return -1;
		}
	}

	if (!upsgi.no_defer_accept) {

#ifdef __linux__
		if (setsockopt(serverfd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &upsgi.socket_timeout, sizeof(int))) {
			upsgi_error("TCP_DEFER_ACCEPT setsockopt()");
		}
		// OSX has no SO_ACCEPTFILTER !!!
#elif defined(__freebsd__)
		struct accept_filter_arg afa;
		strcpy(afa.af_name, "dataready");
		afa.af_arg[0] = 0;
		if (setsockopt(serverfd, SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(struct accept_filter_arg))) {
			upsgi_error("SO_ACCEPTFILTER setsockopt()");
		}
#endif

	}

	if (upsgi.so_keepalive) {
		if (setsockopt(serverfd, SOL_SOCKET, SO_KEEPALIVE, &upsgi.so_keepalive, sizeof(int))) {
			upsgi_error("SO_KEEPALIVE setsockopt()");
		}
	}


	if (bind(serverfd, (struct sockaddr *) &uws_addr, addr_len) != 0) {
		if (errno == EADDRINUSE) {
			upsgi_log("probably another instance of upsgi is running on the same address (%s).\n", socket_name);
		}
		upsgi_error("bind()");
		upsgi_nuclear_blast();
		return -1;
	}

	if (listen(serverfd, listen_queue) != 0) {
		upsgi_error("listen()");
		upsgi_nuclear_blast();
		return -1;
	}


	if (tcp_port)
		tcp_port[0] = ':';

	return serverfd;
}

// set non-blocking socket
void upsgi_socket_nb(int fd) {
	int arg;

	arg = fcntl(fd, F_GETFL, NULL);
	if (arg < 0) {
		upsgi_error("fcntl()");
		return;
	}
	arg |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, arg) < 0) {
		upsgi_error("fcntl()");
		return;
	}

}

// set blocking socket
void upsgi_socket_b(int fd) {
	int arg;

	arg = fcntl(fd, F_GETFL, NULL);
	if (arg < 0) {
		upsgi_error("fcntl()");
		return;
	}
	arg &= (~O_NONBLOCK);
	if (fcntl(fd, F_SETFL, arg) < 0) {
		upsgi_error("fcntl()");
		return;
	}

}


int timed_connect(struct pollfd *fdpoll, const struct sockaddr *addr, int addr_size, int timeout, int async) {

	int ret;
	int soopt = 0;
	socklen_t solen = sizeof(int);
	int cnt;
	/* set non-blocking socket */

#if defined(__linux__) && defined(SOCK_NONBLOCK) && !defined(OBSOLETE_LINUX_KERNEL)
	// hmm, nothing to do, as we are already non-blocking
#else
	int arg = fcntl(fdpoll->fd, F_GETFL, NULL);
	if (arg < 0) {
		upsgi_error("fcntl()");
		return -1;
	}
	arg |= O_NONBLOCK;
	if (fcntl(fdpoll->fd, F_SETFL, arg) < 0) {
		upsgi_error("fcntl()");
		return -1;
	}
#endif

#ifdef MSG_FASTOPEN
	if (addr->sa_family == AF_INET && upsgi.tcp_fast_open_client) {
		ret = sendto(fdpoll->fd, "", 0, MSG_FASTOPEN, addr, addr_size);
	}
	else {
#endif
		ret = connect(fdpoll->fd, addr, addr_size);
#ifdef MSG_FASTOPEN
	}
#endif

	if (async) {
		if (ret < 0 && errno != EINPROGRESS) {
			return -1;
		}
		return 0;
	}


#if defined(__linux__) && defined(SOCK_NONBLOCK) && !defined(OBSOLETE_LINUX_KERNEL)
	upsgi_socket_b(fdpoll->fd);
#else
	/* re-set blocking socket */
	arg &= (~O_NONBLOCK);
	if (fcntl(fdpoll->fd, F_SETFL, arg) < 0) {
		upsgi_error("fcntl()");
		return -1;
	}
#endif

	if (ret < 0) {
		/* check what happened */

		// in progress ?
		if (errno == EINPROGRESS) {
			if (timeout < 1)
				timeout = 3;
			fdpoll->events = POLLOUT;
			cnt = poll(fdpoll, 1, timeout * 1000);
			/* check for errors */
			if (cnt < 0 && errno != EINTR) {
				upsgi_error("poll()");
				return -1;
			}
			/* something happened on the socket ... */
			else if (cnt > 0) {
				if (getsockopt(fdpoll->fd, SOL_SOCKET, SO_ERROR, (void *) (&soopt), &solen) < 0) {
					upsgi_error("getsockopt()");
					return -1;
				}
				/* is something bad ? */
				if (soopt) {
					return -1;
				}
			}
			/* timeout */
			else {
				return -1;
			}
		}
		else {
			return -1;
		}
	}


	return 0;

}

int upsgi_count_sockets(struct upsgi_socket *upsgi_sock) {

	int count = 0;
	while (upsgi_sock) {
		count++;
		upsgi_sock = upsgi_sock->next;
	}

	return count;
}

int upsgi_get_socket_num(struct upsgi_socket *upsgi_sock) {

	int count = 0;
	struct upsgi_socket *current_sock = upsgi.sockets;

	while (current_sock) {
		if (upsgi_sock == current_sock) {
			return count;
		}
		count++;
		current_sock = current_sock->next;
	}

	return -1;
}

int upsgi_get_shared_socket_num(struct upsgi_socket *upsgi_sock) {

	int count = 0;
	struct upsgi_socket *current_sock = upsgi.shared_sockets;

	while (current_sock) {
		if (upsgi_sock == current_sock) {
			return count;
		}
		count++;
		current_sock = current_sock->next;
	}

	return -1;
}


struct upsgi_socket *upsgi_new_shared_socket(char *name) {

	struct upsgi_socket *upsgi_sock = upsgi.shared_sockets, *old_upsgi_sock;

	if (!upsgi_sock) {
		upsgi.shared_sockets = upsgi_malloc(sizeof(struct upsgi_socket));
		upsgi_sock = upsgi.shared_sockets;
	}
	else {
		while (upsgi_sock) {
			old_upsgi_sock = upsgi_sock;
			upsgi_sock = upsgi_sock->next;
		}

		upsgi_sock = upsgi_malloc(sizeof(struct upsgi_socket));
		old_upsgi_sock->next = upsgi_sock;
	}

	memset(upsgi_sock, 0, sizeof(struct upsgi_socket));
	upsgi_sock->name = name;
	upsgi_sock->fd = -1;
	upsgi_sock->configured_socket_num = upsgi_get_socket_num(upsgi_sock);

	return upsgi_sock;
}


struct upsgi_socket *upsgi_new_socket(char *name) {

	struct upsgi_socket *upsgi_sock = upsgi.sockets, *old_upsgi_sock;
	struct sockaddr_in sin;
	socklen_t socket_type_len;

	if (!upsgi_sock) {
		upsgi.sockets = upsgi_malloc(sizeof(struct upsgi_socket));
		upsgi_sock = upsgi.sockets;
	}
	else {
		while (upsgi_sock) {
			old_upsgi_sock = upsgi_sock;
			upsgi_sock = upsgi_sock->next;
		}

		upsgi_sock = upsgi_malloc(sizeof(struct upsgi_socket));
		old_upsgi_sock->next = upsgi_sock;
	}

	memset(upsgi_sock, 0, sizeof(struct upsgi_socket));
	upsgi_sock->name = name;
	upsgi_sock->fd = -1;

	if (!name)
		return upsgi_sock;

	if (name[0] == '=') {
		int shared_socket = atoi(upsgi_sock->name + 1);
		if (shared_socket >= 0) {
			struct upsgi_socket *uss = upsgi_get_shared_socket_by_num(shared_socket);
			if (!uss) {
				upsgi_log("unable to use shared socket %d\n", shared_socket);
				exit(1);
			}
			upsgi_sock->bound = 1;
			upsgi_sock->shared = 1;
			upsgi_sock->from_shared = shared_socket;
			return upsgi_sock;
		}
	}

	if (!upsgi_startswith(name, "fd://", 5)) {
		upsgi_add_socket_from_fd(upsgi_sock, atoi(name + 5));
		return upsgi_sock;
	}

	char *tcp_port = strrchr(name, ':');
	if (tcp_port) {
		// INET socket, check for 0 port
		if (tcp_port[1] == 0 || tcp_port[1] == '0') {
			upsgi_sock->fd = bind_to_tcp(name, upsgi.listen_queue, tcp_port);
			upsgi_sock->family = AF_INET;
			upsgi_sock->bound = 1;

			upsgi_sock->auto_port = 1;

			socket_type_len = sizeof(struct sockaddr_in);

			if (getsockname(upsgi_sock->fd, (struct sockaddr *) &sin, &socket_type_len)) {
				upsgi_error("getsockname()");
				exit(1);
			}


			char *auto_port = upsgi_num2str(ntohs(sin.sin_port));
			upsgi_sock->name = upsgi_concat3n(name, tcp_port - name, ":", 1, auto_port, strlen(auto_port));
		}
		// is it fd 0 ?
		else if (tcp_port[1] == ':') {
			upsgi_sock->fd = 0;
			upsgi_sock->family = AF_INET;
			upsgi_sock->bound = 1;

			socket_type_len = sizeof(struct sockaddr_in);

			if (getsockname(0, (struct sockaddr *) &sin, &socket_type_len)) {
				upsgi_error("getsockname()");
				exit(1);
			}


			char *auto_port = upsgi_num2str(ntohs(sin.sin_port));
			char *auto_ip = inet_ntoa(sin.sin_addr);
			upsgi_sock->name = upsgi_concat3n(auto_ip, strlen(auto_ip), ":", 1, auto_port, strlen(auto_port));
			free(auto_port);
		}
	}

	return upsgi_sock;
}

void upsgi_add_socket_from_fd(struct upsgi_socket *upsgi_sock, int fd) {

	socklen_t socket_type_len;
	union upsgi_sockaddr_ptr gsa, isa;
	union upsgi_sockaddr usa;
	int abstract = 0;

	memset(&usa, 0, sizeof(usa));
	socket_type_len = sizeof(struct sockaddr_un);
	gsa.sa = &usa.sa;
	if (!getsockname(fd, gsa.sa, &socket_type_len)) {
		if (socket_type_len <= 2) {
			// unbound socket
			return;
		}
		if (gsa.sa->sa_family == AF_UNIX) {
			if (usa.sa_un.sun_path[0] == 0)
				abstract = 1;
			// is it a zerg ?
			if (upsgi_sock->name == NULL) {
				upsgi_sock->fd = fd;
				upsgi_sock->family = AF_UNIX;
				upsgi_sock->bound = 1;
				upsgi_sock->name = upsgi_concat2(usa.sa_un.sun_path + abstract, "");
				if (upsgi.zerg) {
					upsgi_log("upsgi zerg socket %d attached to UNIX address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), usa.sa_un.sun_path + abstract, upsgi_sock->fd);
				}
				else {
					upsgi_log("upsgi socket %d attached to UNIX address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), usa.sa_un.sun_path + abstract, upsgi_sock->fd);
				}
				return;
			}
			if (!upsgi_startswith(upsgi_sock->name, "fd://", 5)) {
				if (atoi(upsgi_sock->name + 5) == fd) {
					upsgi_sock->fd = fd;
					upsgi_sock->family = AF_UNIX;
					upsgi_sock->bound = 1;
					upsgi_sock->name = upsgi_str(usa.sa_un.sun_path + abstract);
					upsgi_log("upsgi socket %d inherited UNIX address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
				}
			}
			else if (!strcmp(usa.sa_un.sun_path + abstract, upsgi_sock->name + abstract)) {
				upsgi_sock->fd = fd;
				upsgi_sock->family = AF_UNIX;
				upsgi_sock->bound = 1;
				upsgi_log("upsgi socket %d inherited UNIX address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
			}
		}
		else if (gsa.sa->sa_family == AF_INET) {
			char *computed_addr;
			char computed_port[6];
			isa.sa_in = (struct sockaddr_in *) &usa;
			char ipv4a[INET_ADDRSTRLEN + 1];
			memset(ipv4a, 0, INET_ADDRSTRLEN + 1);
			memset(computed_port, 0, 6);


			if (snprintf(computed_port, 6, "%d", ntohs(isa.sa_in->sin_port)) > 0) {
				if (inet_ntop(AF_INET, (const void *) &isa.sa_in->sin_addr.s_addr, ipv4a, INET_ADDRSTRLEN)) {

					if (!strcmp("0.0.0.0", ipv4a)) {
						computed_addr = upsgi_concat2(":", computed_port);
					}
					else {
						computed_addr = upsgi_concat3(ipv4a, ":", computed_port);
					}

					// is it a zerg ?
					if (upsgi_sock->name == NULL) {
						upsgi_sock->fd = fd;
						upsgi_sock->family = AF_INET;
						upsgi_sock->bound = 1;
						upsgi_sock->name = upsgi_concat2(computed_addr, "");
						if (upsgi.zerg) {
							upsgi_log("upsgi zerg socket %d attached to INET address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), computed_addr, upsgi_sock->fd);
						}
						else {
							upsgi_log("upsgi socket %d attached to INET address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), computed_addr, upsgi_sock->fd);
						}
						free(computed_addr);
						return;
					}
					char *asterisk = strchr(upsgi_sock->name, '*');
					int match = 1;
					if (asterisk) {
						asterisk[0] = 0;
						match = strncmp(computed_addr, upsgi_sock->name, strlen(upsgi_sock->name));
						asterisk[0] = '*';
					}
					else {
						if (!upsgi_startswith(upsgi_sock->name, "fd://", 5)) {
							if (atoi(upsgi_sock->name + 5) == fd) {
								upsgi_sock->fd = fd;
								upsgi_sock->family = AF_INET;
								upsgi_sock->bound = 1;
								upsgi_sock->name = upsgi_str(computed_addr);
								upsgi_log("upsgi socket %d inherited INET address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
								match = 1;
							}
						}
						else {
							match = upsgi_socket_strcmp(computed_addr, upsgi_sock->name);
						}
					}
					if (!match) {
						upsgi_sock->fd = fd;
						upsgi_sock->family = AF_INET;
						upsgi_sock->bound = 1;
						upsgi_log("upsgi socket %d inherited INET address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
					}
					free(computed_addr);
				}
			}
		}
#ifdef AF_INET6
		else if (gsa.sa->sa_family == AF_INET6) {
			char *computed_addr;
			char computed_port[6];
			isa.sa_in6 = (struct sockaddr_in6 *) &usa;
			char ipv6a[INET6_ADDRSTRLEN + 1];
			memset(ipv6a, 0, INET_ADDRSTRLEN + 1);
			memset(computed_port, 0, 6);
			int match = 0;


			if (snprintf(computed_port, 6, "%d", ntohs(isa.sa_in6->sin6_port)) > 0) {
				if (inet_ntop(AF_INET6, (const void *) &isa.sa_in6->sin6_addr.s6_addr, ipv6a, INET6_ADDRSTRLEN)) {
					upsgi_log("ipv6a = %s\n", ipv6a);
					if (!strcmp("::", ipv6a)) {
						computed_addr = upsgi_concat2("[::]:", computed_port);
					}
					else {
						computed_addr = upsgi_concat4("[", ipv6a, "]:", computed_port);
					}
					// is it a zerg ?
					if (upsgi_sock->name == NULL) {
						upsgi_sock->fd = fd;
						upsgi_sock->family = AF_INET6;
						upsgi_sock->bound = 1;
						upsgi_sock->name = upsgi_concat2(computed_addr, "");
						if (upsgi.zerg) {
							upsgi_log("upsgi zerg socket %d attached to INET6 address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), computed_addr, upsgi_sock->fd);
						}
						else {
							upsgi_log("upsgi socket %d attached to INET6 address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), computed_addr, upsgi_sock->fd);
						}
						free(computed_addr);
						return;
					}

					if (!upsgi_startswith(upsgi_sock->name, "fd://", 5)) {
						if (atoi(upsgi_sock->name + 5) == fd) {
							upsgi_sock->fd = fd;
							upsgi_sock->family = AF_INET6;
							upsgi_sock->bound = 1;
							upsgi_sock->name = upsgi_str(computed_addr);
							upsgi_log("upsgi socket %d inherited INET address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
							match = 1;
						}
					}
					else {
						match = strcmp(computed_addr, upsgi_sock->name);
					}

					if (!match) {
						upsgi_sock->fd = fd;
						upsgi_sock->family = AF_INET;
						upsgi_sock->bound = 1;
						upsgi_log("upsgi socket %d inherited INET6 address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
					}
					free(computed_addr);
				}
			}
		}

#endif
	}

}

void upsgi_close_all_sockets() {
        struct upsgi_socket *upsgi_sock = upsgi.sockets;

        while (upsgi_sock) {
                if (upsgi_sock->bound) {
                        close(upsgi_sock->fd);
                }
                upsgi_sock = upsgi_sock->next;
        }
}

void upsgi_shutdown_all_sockets() {
        struct upsgi_socket *upsgi_sock = upsgi.sockets;

        while (upsgi_sock) {
                if (upsgi_sock->bound) {
                        shutdown(upsgi_sock->fd, SHUT_RDWR);
                        close(upsgi_sock->fd);
                }
                upsgi_sock = upsgi_sock->next;
        }
}

void upsgi_close_all_unshared_sockets() {
	struct upsgi_socket *upsgi_sock = upsgi.sockets;

	while (upsgi_sock) {
		if (upsgi_sock->bound && !upsgi_sock->shared)
			close(upsgi_sock->fd);
		upsgi_sock = upsgi_sock->next;
	}
}

struct upsgi_socket *upsgi_del_socket(struct upsgi_socket *upsgi_sock) {

	struct upsgi_socket *upsgi_current_sock = upsgi.sockets, *old_sock = NULL;

	while (upsgi_current_sock) {
		if (upsgi_current_sock == upsgi_sock) {
			// parent instance ?
			if (old_sock == NULL) {
				upsgi.sockets = upsgi_current_sock->next;
				free(upsgi_current_sock);
				return upsgi.sockets;
			}
			else {
				old_sock->next = upsgi_current_sock->next;
				free(upsgi_current_sock);
				return old_sock->next;
			}

		}

		old_sock = upsgi_current_sock;
		upsgi_current_sock = upsgi_current_sock->next;
	}

	return NULL;
}


int upsgi_get_shared_socket_fd_by_num(int num) {

	int counter = 0;

	struct upsgi_socket *found_sock = NULL, *upsgi_sock = upsgi.shared_sockets;

	while (upsgi_sock) {
		if (counter == num) {
			found_sock = upsgi_sock;
			break;
		}
		counter++;
		upsgi_sock = upsgi_sock->next;
	}

	if (found_sock) {
		return found_sock->fd;
	}

	return -1;
}

struct upsgi_socket *upsgi_get_shared_socket_by_num(int num) {

	int counter = 0;

	struct upsgi_socket *found_sock = NULL, *upsgi_sock = upsgi.shared_sockets;

	while (upsgi_sock) {
		if (counter == num) {
			found_sock = upsgi_sock;
			break;
		}
		counter++;
		upsgi_sock = upsgi_sock->next;
	}

	if (found_sock) {
		return found_sock;
	}

	return NULL;
}

struct upsgi_socket *upsgi_get_socket_by_num(int num) {

	int counter = 0;

	struct upsgi_socket *found_sock = NULL, *upsgi_sock = upsgi.sockets;

	while (upsgi_sock) {
		if (counter == num) {
			found_sock = upsgi_sock;
			break;
		}
		counter++;
		upsgi_sock = upsgi_sock->next;
	}

	if (found_sock) {
		return found_sock;
	}

	return NULL;
}



void upsgi_add_sockets_to_queue(int queue, int async_id) {

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_sock->fd_threads && async_id > -1 && upsgi_sock->fd_threads[async_id] > -1) {
			event_queue_add_fd_read(queue, upsgi_sock->fd_threads[async_id]);
		}
		else if (upsgi_sock->fd > -1) {
			event_queue_add_fd_read(queue, upsgi_sock->fd);
		}
		upsgi_sock = upsgi_sock->next;
	}

}

void upsgi_del_sockets_from_queue(int queue) {

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_sock->fd == -1)
			goto nextsock;
		event_queue_del_fd(queue, upsgi_sock->fd, event_queue_read());
nextsock:
		upsgi_sock = upsgi_sock->next;
	}

}

int upsgi_is_bad_connection(int fd) {

	int soopt = 0;
	socklen_t solen = sizeof(int);

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *) (&soopt), &solen) < 0) {
		return -1;
	}

	// will be 0 if all ok
	return soopt;
}

int upsgi_socket_is_already_bound(char *name) {
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_sock->name && !strcmp(upsgi_sock->name, name) && upsgi_sock->bound) {
			return 1;
		}
		upsgi_sock = upsgi_sock->next;
	}
	return 0;
}

int upsgi_socket_uniq(struct upsgi_socket *list, struct upsgi_socket *item) {
	int found = 0;

	if (list == item)
		return 0;
	struct upsgi_socket *upsgi_sock = list;
	while (upsgi_sock && upsgi_sock != item) {
		if (upsgi_sock->fd == -1)
			goto nextsock;
		if (!strcmp(upsgi_sock->name, item->name)) {
			found = 1;
			break;
		}
nextsock:
		upsgi_sock = upsgi_sock->next;
	}
	return found;
}

void upsgi_manage_zerg(int fd, int num_sockets, int *sockets) {
	struct sockaddr_un zsun;
	socklen_t zsun_len = sizeof(struct sockaddr_un);

	int zerg_client = accept(fd, (struct sockaddr *) &zsun, &zsun_len);
	if (zerg_client < 0) {
		upsgi_error("zerg: accept()");
		return;
	}

	if (!num_sockets) {
		num_sockets = upsgi_count_sockets(upsgi.sockets);
	}

	struct msghdr zerg_msg;
	void *zerg_msg_control = upsgi_malloc(CMSG_SPACE(sizeof(int) * num_sockets));
	struct iovec zerg_iov[2];
	struct cmsghdr *cmsg;

	zerg_iov[0].iov_base = "upsgi-zerg";
	zerg_iov[0].iov_len = 10;
	zerg_iov[1].iov_base = &num_sockets;
	zerg_iov[1].iov_len = sizeof(int);

	zerg_msg.msg_name = NULL;
	zerg_msg.msg_namelen = 0;

	zerg_msg.msg_iov = zerg_iov;
	zerg_msg.msg_iovlen = 2;

	zerg_msg.msg_flags = 0;
	zerg_msg.msg_control = zerg_msg_control;
	zerg_msg.msg_controllen = CMSG_SPACE(sizeof(int) * num_sockets);

	cmsg = CMSG_FIRSTHDR(&zerg_msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_sockets);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	unsigned char *zerg_fd_ptr = CMSG_DATA(cmsg);

	if (!sockets) {
		struct upsgi_socket *upsgi_sock = upsgi.sockets;
		int uniq_count = 0;
		while (upsgi_sock) {
			if (upsgi_sock->fd == -1)
				goto nextsock;
			if (!upsgi_socket_uniq(upsgi.sockets, upsgi_sock)) {
				memcpy(zerg_fd_ptr, &upsgi_sock->fd, sizeof(int));
				zerg_fd_ptr += sizeof(int);
				uniq_count++;
			}
nextsock:
			upsgi_sock = upsgi_sock->next;
		}
		zerg_iov[1].iov_base = &uniq_count;
		cmsg->cmsg_len = CMSG_LEN(sizeof(int) * uniq_count);
	}
	else {
		memcpy(zerg_fd_ptr, sockets, sizeof(int) * num_sockets);
	}

	if (sendmsg(zerg_client, &zerg_msg, 0) < 0) {
		upsgi_error("sendmsg()");
	}

	free(zerg_msg_control);

	close(zerg_client);

}


#ifdef AF_INET6
socklen_t socket_to_in_addr6(char *socket_name, char *port, int portn, struct sockaddr_in6 * sin_addr) {

	memset(sin_addr, 0, sizeof(struct sockaddr_in6));

	sin_addr->sin6_family = AF_INET6;
	if (port) {
		*port = 0;
		sin_addr->sin6_port = htons(atoi(port + 1));
	}
	else {
		sin_addr->sin6_port = htons(portn);
	}

	if (!strcmp(socket_name, "[::]")) {
		sin_addr->sin6_addr = in6addr_any;
	}
	else {
		char *sanitized_sn = upsgi_concat2n(socket_name + 1, strlen(socket_name + 1) - 1, "", 0);
		char *resolved = upsgi_resolve_ip(sanitized_sn);
		if (resolved) {
			inet_pton(AF_INET6, resolved, sin_addr->sin6_addr.s6_addr);
		}
		else {
			inet_pton(AF_INET6, sanitized_sn, sin_addr->sin6_addr.s6_addr);
		}
		free(sanitized_sn);
	}

	if (port) {
		*port = ':';
	}

	return sizeof(struct sockaddr_in6);

}


#endif

void upsgi_setup_shared_sockets() {
	int i;
	struct upsgi_socket *shared_sock = upsgi.shared_sockets;
	while (shared_sock) {
		if (!upsgi.is_a_reload) {
			char *tcp_port = strrchr(shared_sock->name, ':');
			int current_defer_accept = upsgi.no_defer_accept;
                        if (shared_sock->no_defer) {
                        	upsgi.no_defer_accept = 1;
                        }
			if (tcp_port == NULL) {
				shared_sock->fd = bind_to_unix(shared_sock->name, upsgi.listen_queue, upsgi.chmod_socket, upsgi.abstract_socket);
				shared_sock->family = AF_UNIX;
				upsgi_log("upsgi shared socket %d bound to UNIX address %s fd %d\n", upsgi_get_shared_socket_num(shared_sock), shared_sock->name, shared_sock->fd);
				if (upsgi.chown_socket) {
                                        upsgi_chown(shared_sock->name, upsgi.chown_socket);
                                }
			}
			else {
#ifdef AF_INET6
				if (shared_sock->name[0] == '[' && tcp_port[-1] == ']') {
					shared_sock->fd = bind_to_tcp(shared_sock->name, upsgi.listen_queue, tcp_port);
					shared_sock->family = AF_INET6;
					// fix socket name
					shared_sock->name = upsgi_getsockname(shared_sock->fd);
					upsgi_log("upsgi shared socket %d bound to TCP6 address %s fd %d\n", upsgi_get_shared_socket_num(shared_sock), shared_sock->name, shared_sock->fd);
				}
				else {
#endif
					shared_sock->fd = bind_to_tcp(shared_sock->name, upsgi.listen_queue, tcp_port);
					shared_sock->family = AF_INET;
					// fix socket name
					shared_sock->name = upsgi_getsockname(shared_sock->fd);
					upsgi_log("upsgi shared socket %d bound to TCP address %s fd %d\n", upsgi_get_shared_socket_num(shared_sock), shared_sock->name, shared_sock->fd);
#ifdef AF_INET6
				}
#endif
			}

			if (shared_sock->fd < 0) {
				upsgi_log("unable to create shared socket on: %s\n", shared_sock->name);
				exit(1);
			}
 
			if (shared_sock->no_defer) {
                                upsgi.no_defer_accept = current_defer_accept;
                        }

		}
		else {
			for (i = 3; i < (int) upsgi.max_fd; i++) {
				char *sock = upsgi_getsockname(i);
				if (sock) {
					if (!upsgi_socket_strcmp(sock, shared_sock->name)) {
						if (strchr(sock, ':')) {
							upsgi_log("upsgi shared socket %d inherited TCP address %s fd %d\n", upsgi_get_shared_socket_num(shared_sock), sock, i);
							shared_sock->family = AF_INET;
						}
						else {
							upsgi_log("upsgi shared socket %d inherited UNIX address %s fd %d\n", upsgi_get_shared_socket_num(shared_sock), sock, i);
							shared_sock->family = AF_UNIX;
						}
						shared_sock->fd = i;
					}
					free(sock);
				}
			}
		}
		shared_sock->bound = 1;
		shared_sock = shared_sock->next;
	}

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {

		if (upsgi_sock->shared) {
			shared_sock = upsgi_get_shared_socket_by_num(upsgi_sock->from_shared);
			if (!shared_sock) {
				upsgi_log("unable to find shared socket %d\n", upsgi_sock->from_shared);
				exit(1);
			}
			upsgi_sock->fd = shared_sock->fd;
			upsgi_sock->family = shared_sock->family;
			upsgi_sock->name = shared_sock->name;
			upsgi_log("upsgi socket %d mapped to shared socket %d (%s) fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_get_shared_socket_num(shared_sock), shared_sock->name, upsgi_sock->fd);
		}

		upsgi_sock = upsgi_sock->next;
	}


}

void upsgi_map_sockets() {
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		struct upsgi_string_list *usl = upsgi.map_socket;
		int enabled = 1;
		while (usl) {

			char *colon = strchr(usl->value, ':');
			if (!colon) {
				upsgi_log("invalid socket mapping, must be socket:worker[,worker...]\n");
				exit(1);
			}
			if ((int) upsgi_str_num(usl->value, colon - usl->value) == upsgi_sock->configured_socket_num) {
				enabled = 0;
				char *p, *ctx = NULL;
				upsgi_foreach_token(colon + 1, ",", p, ctx) {
					int w = atoi(p);
					if (w < 1 || w > upsgi.numproc) {
						upsgi_log("invalid worker num: %d\n", w);
						exit(1);
					}
					if (w == upsgi.mywid) {
						enabled = 1;
						upsgi_log("mapped socket %d (%s) to worker %d\n", upsgi_sock->configured_socket_num, upsgi_sock->name, upsgi.mywid);
						break;
					}
				}
			}

			usl = usl->next;
		}

		if (!enabled) {
			close(upsgi_sock->fd);
			upsgi_remap_fd(upsgi_sock->fd, "/dev/null");
			upsgi_sock->disabled = 1;
		}


		upsgi_sock = upsgi_sock->next;

	}

	upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_sock->disabled) {
			upsgi_sock = upsgi_del_socket(upsgi_sock);
		}
		else {
			upsgi_sock = upsgi_sock->next;
		}
	}

}

int upsgi_socket_mapping_is_worker_exclusive(struct upsgi_socket *upsgi_sock) {
	struct upsgi_string_list *usl = upsgi.map_socket;
	int found = 0;
	int my_worker = 0;
	int other_workers = 0;
	int sock_num = upsgi_sock->configured_socket_num;

	while (usl) {
		char *colon = strchr(usl->value, ':');
		if (!colon) {
			upsgi_log("invalid socket mapping, must be socket:worker[,worker...]\n");
			exit(1);
		}

		if ((int) upsgi_str_num(usl->value, colon - usl->value) == sock_num) {
			char *map_copy = upsgi_str(colon + 1);
			char *p, *ctx = NULL;
			found = 1;
			upsgi_foreach_token(map_copy, ",", p, ctx) {
				int w = atoi(p);
				if (w < 1 || w > upsgi.numproc) {
					upsgi_log("invalid worker num: %d\n", w);
					free(map_copy);
					exit(1);
				}
				if (w == upsgi.mywid) {
					my_worker = 1;
				}
				else {
					other_workers = 1;
				}
			}
			free(map_copy);
		}

		usl = usl->next;
	}

	return found && my_worker && !other_workers;
}

void upsgi_configure_worker_accept_mode() {
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	int enabled_sockets = 0;
	int shared_accept_sockets = 0;

	upsgi.skip_thunder_lock = 0;

	while (upsgi_sock) {
		if (!upsgi_sock->disabled) {
			enabled_sockets++;
			if (!upsgi_socket_mapping_is_worker_exclusive(upsgi_sock)) {
				shared_accept_sockets = 1;
			}
		}
		upsgi_sock = upsgi_sock->next;
	}

	if (upsgi.use_thunder_lock && enabled_sockets > 0 && !shared_accept_sockets) {
		upsgi.skip_thunder_lock = 1;
	}

	if (enabled_sockets == 0) {
		upsgi_log("accept mode: no enabled request sockets for worker %d\n", upsgi.mywid);
		return;
	}

	if (upsgi.skip_thunder_lock) {
		upsgi_log("accept mode: worker-local mapped sockets (thunder lock bypassed)\n");
		return;
	}

	if (upsgi.use_thunder_lock) {
		if (upsgi.reuse_port && shared_accept_sockets) {
			upsgi_log("accept mode: shared listeners serialized by thunder lock (SO_REUSEPORT on inherited listeners does not shard accepts)\n");
		}
		else {
			upsgi_log("accept mode: shared listeners serialized by thunder lock\n");
		}
	}
	else {
		if (upsgi.reuse_port && shared_accept_sockets) {
			upsgi_log("accept mode: shared listeners without thunder lock (SO_REUSEPORT on inherited listeners does not shard accepts)\n");
		}
		else {
			upsgi_log("accept mode: shared listeners without thunder lock\n");
		}
	}
}

void upsgi_bind_sockets() {
	socklen_t socket_type_len;
	union upsgi_sockaddr usa;
	union upsgi_sockaddr_ptr gsa;

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (!upsgi_sock->bound && !upsgi_socket_is_already_bound(upsgi_sock->name)) {
			char *tcp_port = strrchr(upsgi_sock->name, ':');
			int current_defer_accept = upsgi.no_defer_accept;
                        if (upsgi_sock->no_defer) {
                                upsgi.no_defer_accept = 1;
                        }
			if (tcp_port == NULL) {
				upsgi_sock->fd = bind_to_unix(upsgi_sock->name, upsgi.listen_queue, upsgi.chmod_socket, upsgi.abstract_socket);
				upsgi_sock->family = AF_UNIX;
				if (upsgi.chown_socket) {
					upsgi_chown(upsgi_sock->name, upsgi.chown_socket);
				}
				upsgi_log("upsgi socket %d bound to UNIX address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
				struct stat st;
				if (upsgi_sock->name[0] != '@' && !stat(upsgi_sock->name, &st)) {
					upsgi_sock->inode = st.st_ino;
				}
			}
			else {
#ifdef AF_INET6
				if (upsgi_sock->name[0] == '[' && tcp_port[-1] == ']') {
					upsgi_sock->fd = bind_to_tcp(upsgi_sock->name, upsgi.listen_queue, tcp_port);
					upsgi_log("upsgi socket %d bound to TCP6 address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
					upsgi_sock->family = AF_INET6;
				}
				else {
#endif
					upsgi_sock->fd = bind_to_tcp(upsgi_sock->name, upsgi.listen_queue, tcp_port);
					upsgi_log("upsgi socket %d bound to TCP address %s fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
					upsgi_sock->family = AF_INET;
#ifdef AF_INET6
				}
#endif
			}

			if (upsgi_sock->fd < 0 && !upsgi_sock->per_core) {
				upsgi_log("unable to create server socket on: %s\n", upsgi_sock->name);
				exit(1);
			}
			upsgi.no_defer_accept = current_defer_accept;
		}
		upsgi_sock->bound = 1;
		upsgi_sock = upsgi_sock->next;
	}

	int zero_used = 0;
	upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_sock->bound && upsgi_sock->fd == 0) {
			zero_used = 1;
			break;
		}
		upsgi_sock = upsgi_sock->next;
	}

	if (!zero_used) {
		socket_type_len = sizeof(struct sockaddr_un);
		gsa.sa = (struct sockaddr *) &usa;
		if (!upsgi.skip_zero && !getsockname(0, gsa.sa, &socket_type_len)) {
			if (gsa.sa->sa_family == AF_UNIX) {
				upsgi_sock = upsgi_new_socket(upsgi_getsockname(0));
				upsgi_sock->family = AF_UNIX;
				upsgi_sock->fd = 0;
				upsgi_sock->bound = 1;
				upsgi_log("upsgi socket %d inherited UNIX address %s fd 0\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name);
				if (!upsgi.is_a_reload) {
					if (upsgi.chown_socket) {
                                        	upsgi_chown(upsgi_sock->name, upsgi.chown_socket);
                                	}
					if (upsgi.chmod_socket) {
                				if (upsgi.chmod_socket_value) {
                        				if (chmod(upsgi_sock->name, upsgi.chmod_socket_value) != 0) {
                                				upsgi_error("inherit fd0: chmod()");
                        				}
                				}
                				else {
                        				upsgi_log("chmod() fd0 socket to 666 for lazy and brave users\n");
                        				if (chmod(upsgi_sock->name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) != 0) {
                                				upsgi_error("inherit fd0: chmod()");
                        				}
						}
                			}
				}
			}
			else {
				upsgi_sock = upsgi_new_socket(upsgi_getsockname(0));
				upsgi_sock->family = AF_INET;
				upsgi_sock->fd = 0;
				upsgi_sock->bound = 1;
				upsgi_log("upsgi socket %d inherited INET address %s fd 0\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name);
			}
		}
		else if (!upsgi.honour_stdin) {
			int fd = open("/dev/null", O_RDONLY);
			if (fd < 0) {
				upsgi_error_open("/dev/null");
				upsgi_log("WARNING: unable to remap stdin, /dev/null not available\n");
				goto stdin_done;
			}
			if (fd != 0) {
				if (dup2(fd, 0) < 0) {
					upsgi_error("dup2()");
					exit(1);
				}
				close(fd);
			}
		}
		else if (upsgi.honour_stdin) {
			if (!tcgetattr(0, &upsgi.termios)) {
				upsgi.restore_tc = 1;
			}
		}

	}

stdin_done:

	// check for auto_port socket
	upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_sock->auto_port) {
#ifdef AF_INET6
			if (upsgi_sock->family == AF_INET6) {
				upsgi_log("upsgi socket %d bound to TCP6 address %s (port auto-assigned) fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
			}
			else {
#endif
				upsgi_log("upsgi socket %d bound to TCP address %s (port auto-assigned) fd %d\n", upsgi_get_socket_num(upsgi_sock), upsgi_sock->name, upsgi_sock->fd);
#ifdef AF_INET6
			}
#endif
		}
		upsgi_sock = upsgi_sock->next;
	}


}

void upsgi_set_sockets_protocols() {

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		char *requested_protocol = upsgi_sock->proto_name;

		if (upsgi_sock->lazy)
			goto setup_proto;
		if (!upsgi_sock->bound || upsgi_sock->fd == -1)
			goto nextsock;
		if (!upsgi_sock->per_core) {
			upsgi_sock->arg = fcntl(upsgi_sock->fd, F_GETFL, NULL);
			if (upsgi_sock->arg < 0) {
				upsgi_error("fcntl()");
				exit(1);
			}
			upsgi_sock->arg |= O_NONBLOCK;
			if (fcntl(upsgi_sock->fd, F_SETFL, upsgi_sock->arg) < 0) {
				upsgi_error("fcntl()");
				exit(1);
			}
		}


setup_proto:
		if (!requested_protocol) requested_protocol = upsgi.protocol;
		upsgi_socket_setup_protocol(upsgi_sock, requested_protocol);
nextsock:
		upsgi_sock = upsgi_sock->next;
	}


}

void upsgi_tcp_nodelay(int fd) {
#ifdef TCP_NODELAY
        int flag = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int))) {
                upsgi_error("upsgi_tcp_nodelay()/setsockopt()");
        }
#endif
}

int upsgi_accept(int server_fd) {
	struct sockaddr_un client_src;
        memset(&client_src, 0, sizeof(struct sockaddr_un));
        socklen_t client_src_len = 0;
#if defined(__linux__) && defined(SOCK_NONBLOCK) && !defined(OBSOLETE_LINUX_KERNEL)
        return accept4(server_fd, (struct sockaddr *) &client_src, &client_src_len, SOCK_NONBLOCK);
#elif defined(__linux__)
        int client_fd = accept(server_fd, (struct sockaddr *) &client_src, &client_src_len);
        if (client_fd >= 0) {
                upsgi_socket_nb(client_fd);
        }
        return client_fd;
#else
 	return accept(server_fd, (struct sockaddr *) &client_src, &client_src_len);
#endif


}

struct upsgi_protocol *upsgi_register_protocol(char *name, void (*func)(struct upsgi_socket *)) {
	struct upsgi_protocol *old_up = NULL, *up = upsgi.protocols;
	while(up) {
		if (!strcmp(name, up->name)) {
			goto found;
		}
		old_up = up;
		up = up->next;
	}
	up = upsgi_calloc(sizeof(struct upsgi_protocol));
	up->name = name;
	if (old_up) {
		old_up->next = up;
	}
	else {
		upsgi.protocols = up;
	}
found:
	up->func = func;
	return up;
}

int upsgi_socket_passcred(int fd) {
#ifdef SO_PASSCRED
	int optval = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) < 0) {
                upsgi_error("upsgi_socket_passcred()/setsockopt()");
		return -1;
	}
	return 0;
#else
	return -1;
#endif
}

int upsgi_socket_from_addr(union upsgi_sockaddr *addr, socklen_t *len, char *addrtxt, int sock_type) {
	int fd = -1;
	int type;
	char *colon = strchr(addrtxt, ':');

#ifdef AF_INET6
	if (*addrtxt == '[' && colon && colon[-1] == ']') {
		type = AF_INET6;
	}
	else
#endif
	if (colon) {
		type = AF_INET;
	}
	else {
		type = AF_UNIX;
	}

	if (type != AF_UNIX) {
		if (!colon) {
			upsgi_log("invalid address %s\n", addrtxt);
			return fd;
		}
	}

	switch (type) {
		case AF_INET:
			*len = socket_to_in_addr(addrtxt, colon, 0, &addr->sa_in);
			break;
#ifdef AF_INET6
		case AF_INET6:
			*len = socket_to_in_addr6(addrtxt, colon, 0, &addr->sa_in6);
			break;
#endif
		case AF_UNIX:
			*len = socket_to_un_addr(addrtxt, &addr->sa_un);
			break;
		default:
			upsgi_log("unsupported socket type: %d\n", type);
			return -1;
	}

	fd = socket(type, sock_type, 0);
	if (fd < 0) {
		upsgi_error("upsgi_socket_from_addr()");
		return fd;
	}

	return fd;
}

void upsgi_protocols_register() {
	upsgi_register_protocol("upsgi", upsgi_proto_upsgi_setup);
	upsgi_register_protocol("pupsgi", upsgi_proto_pupsgi_setup);

	upsgi_register_protocol("http", upsgi_proto_http_setup);
	upsgi_register_protocol("http11", upsgi_proto_http11_setup);

#ifdef UPSGI_SSL
	upsgi_register_protocol("supsgi", upsgi_proto_supsgi_setup);
	upsgi_register_protocol("https", upsgi_proto_https_setup);
#endif
	upsgi_register_protocol("fastcgi", upsgi_proto_fastcgi_setup);
	upsgi_register_protocol("fastcgi-nph", upsgi_proto_fastcgi_nph_setup);

	upsgi_register_protocol("scgi", upsgi_proto_scgi_setup);
	upsgi_register_protocol("scgi-nph", upsgi_proto_scgi_nph_setup);

	upsgi_register_protocol("raw", upsgi_proto_raw_setup);
}
