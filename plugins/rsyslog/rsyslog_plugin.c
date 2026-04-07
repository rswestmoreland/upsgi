#include "../../upsgi.h"

/*
 * RFC3164-style rsyslog sink kept first-class in the default upsgi logging
 * bundle. Advanced message routing still belongs to core/logging.c.
 */
extern struct upsgi_server upsgi;


struct upsgi_rsyslog {
	int packet_size;
	int msg_size;
	int split_msg;
} u_rsyslog;


struct upsgi_option rsyslog_options[] = {
	{"rsyslog-packet-size", required_argument, 0, "set maximum packet size for syslog messages (default 1024) WARNING! using packets > 1024 breaks RFC 3164 (#4.1)", upsgi_opt_set_int, &u_rsyslog.packet_size, 0},
	{"rsyslog-split-messages", no_argument, 0, "split big messages into multiple chunks if they are bigger than allowed packet size (default is false)", upsgi_opt_true, &u_rsyslog.split_msg, 0},
	{0, 0, 0, 0, 0, 0, 0},
};


ssize_t upsgi_rsyslog_logger(struct upsgi_logger *ul, char *message, size_t len) {

	char ctime_storage[26];
	time_t current_time;
	int portn = 514;
	int rlen;

	if (!ul->configured) {

                if (!ul->arg) {
			upsgi_log_safe("invalid rsyslog syntax\n");
			exit(1);
		}

		if (ul->arg[0] == '/') {
                	ul->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
		}
		else {
                	ul->fd = socket(AF_INET, SOCK_DGRAM, 0);
		}
                if (ul->fd < 0) {
			upsgi_error_safe("socket()");
			exit(1);
		}

		upsgi_socket_nb(ul->fd);

		ul->count = 29;

                char *comma = strchr(ul->arg, ',');
		if (comma) {
			ul->data = comma+1;
                	*comma = 0;
			char *prisev = strchr(ul->data, ',');
			if (prisev) {
				*prisev = 0;
				ul->count = atoi(prisev+1);
			}
		}
		else {
			ul->data = upsgi_concat2(upsgi.hostname," upsgi");
		}


                char *port = strchr(ul->arg, ':');
                if (port) {
			portn = atoi(port+1);
			*port = 0;
		}

		if (ul->arg[0] == '/') {
			ul->addr_len = socket_to_un_addr(ul->arg, &ul->addr.sa_un);
		}
		else {
			ul->addr_len = socket_to_in_addr(ul->arg, NULL, portn, &ul->addr.sa_in);
		}

		if (port) *port = ':';
		if (comma) *comma = ',';

		if (!u_rsyslog.packet_size) u_rsyslog.packet_size = 1024;
		if (!u_rsyslog.msg_size) u_rsyslog.msg_size = u_rsyslog.packet_size - 30;

		ul->buf = upsgi_malloc(upsgi.log_master_bufsize);

                ul->configured = 1;
        }


	current_time = upsgi_now();

	// drop newline
	if (message[len-1] == '\n') len--;
#if defined(__sun__) && !defined(__clang__)
	ctime_r(&current_time, ctime_storage, 26);
#else
	ctime_r(&current_time, ctime_storage);
#endif

	int pos, msg_len, ret;
	for (pos=0 ; pos < (int) len ;) {
		if (pos > 0 && !u_rsyslog.split_msg) return pos;
		msg_len = ( ((int)len)-pos > u_rsyslog.msg_size ? u_rsyslog.msg_size : ((int)len)-pos);
		rlen = snprintf(ul->buf, u_rsyslog.packet_size, "<%d>%.*s %s: %.*s", ul->count, 15, ctime_storage+4, (char *) ul->data, msg_len, &message[pos]);
		if (rlen > 0 && rlen < u_rsyslog.packet_size) {
			ret = sendto(ul->fd, ul->buf, rlen, 0, (const struct sockaddr *) &ul->addr, ul->addr_len);
			if (ret <= 0) return ret;
			pos += msg_len;
		} else {
			return -1;
		}
	}
	return pos;

}

/* Register the rsyslog sink backend and its plugin-local options. */
void upsgi_rsyslog_register() {
	upsgi_register_logger("rsyslog", upsgi_rsyslog_logger);
}

struct upsgi_plugin rsyslog_plugin = {

        .name = "rsyslog",
        .on_load = upsgi_rsyslog_register,
        .options = rsyslog_options,

};

