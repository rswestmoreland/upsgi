#include "../../upsgi.h"

/*
 * Datagram socket logging sink kept in the default upsgi logging bundle.
 * Core logger routing chooses when this backend receives records.
 */
extern struct upsgi_server upsgi;

ssize_t upsgi_socket_logger(struct upsgi_logger *ul, char *message, size_t len) {

	int family = AF_UNIX;

	if (!ul->configured) {

		char *comma = strchr(ul->arg, ',');
		if (comma) {
			ul->data = comma+1;
			*comma = 0;
		}

        	char *colon = strchr(ul->arg, ':');
        	if (colon) {
                	family = AF_INET;
                	ul->addr_len = socket_to_in_addr(ul->arg, colon, 0, &ul->addr.sa_in);
        	}
        	else {
                	ul->addr_len = socket_to_un_addr(ul->arg, &ul->addr.sa_un);
        	}

        	ul->fd = socket(family, SOCK_DGRAM, 0);
        	if (ul->fd < 0) {
                	upsgi_error_safe("socket()");
			exit(1);
        	}

		memset(&ul->msg, 0, sizeof(struct msghdr));

		ul->msg.msg_name = &ul->addr;
		ul->msg.msg_namelen = ul->addr_len;
		if (ul->data) {
			ul->msg.msg_iov = upsgi_malloc(sizeof(struct iovec) * 2);
			ul->msg.msg_iov[0].iov_base = ul->data;
			ul->msg.msg_iov[0].iov_len = strlen(ul->data);
			ul->msg.msg_iovlen = 2;
			ul->count = 1;
		}
		else {
			ul->msg.msg_iov = upsgi_malloc(sizeof(struct iovec));
			ul->msg.msg_iovlen = 1;
			ul->count = 0;
		}

		if (comma) {
			*comma = ',' ;
		}

		ul->configured = 1;
	}

	
	ul->msg.msg_iov[ul->count].iov_base = message;
	ul->msg.msg_iov[ul->count].iov_len = len;

	return sendmsg(ul->fd, &ul->msg, 0);

}

/* Register the socket logger backend used by --logger socket:... */
void upsgi_logsocket_register() {
	upsgi_register_logger("socket", upsgi_socket_logger);
}

struct upsgi_plugin logsocket_plugin = {

        .name = "logsocket",
        .on_load = upsgi_logsocket_register,

};

