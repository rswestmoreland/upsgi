/* async upsgi protocol parser */

#include "upsgi.h"

extern struct upsgi_server upsgi;

static int upsgi_proto_upsgi_parser(struct wsgi_request *wsgi_req) {
	char *ptr = (char *) wsgi_req->uh;
	ssize_t len = read(wsgi_req->fd, ptr + wsgi_req->proto_parser_pos, (upsgi.buffer_size + 4) - wsgi_req->proto_parser_pos);
	if (len > 0) {
		wsgi_req->proto_parser_pos += len;
		if (wsgi_req->proto_parser_pos >= 4) {
#ifdef __BIG_ENDIAN__
			wsgi_req->uh->_pktsize = upsgi_swap16(wsgi_req->uh->_pktsize);
#endif
			wsgi_req->len = wsgi_req->uh->_pktsize;
			if ((wsgi_req->proto_parser_pos - 4) == wsgi_req->uh->_pktsize) {
				return UPSGI_OK;
			}
			if ((wsgi_req->proto_parser_pos - 4) > wsgi_req->uh->_pktsize) {
				wsgi_req->proto_parser_remains = wsgi_req->proto_parser_pos - (4 + wsgi_req->uh->_pktsize);
				wsgi_req->proto_parser_remains_buf = wsgi_req->buffer + wsgi_req->uh->_pktsize;
				return UPSGI_OK;
			}
			if (wsgi_req->uh->_pktsize > upsgi.buffer_size) {
				upsgi_log("invalid request block size: %u (max %u)...skip\n", wsgi_req->uh->_pktsize, upsgi.buffer_size);
				return -1;
			}
		}
		return UPSGI_AGAIN;
	}
	if (len < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
			return UPSGI_AGAIN;
		}
		upsgi_error("upsgi_proto_upsgi_parser()");
		return -1;
	}
	// 0 len
	if (wsgi_req->proto_parser_pos > 0) {
		upsgi_error("upsgi_proto_upsgi_parser()");
	}
	return -1;
}

#ifdef UPSGI_SSL
static int upsgi_proto_supsgi_parser(struct wsgi_request *wsgi_req) {
        char *ptr = (char *) wsgi_req->uh;
	int len = -1;
retry:
        len = SSL_read(wsgi_req->ssl, ptr + wsgi_req->proto_parser_pos, (upsgi.buffer_size + 4) - wsgi_req->proto_parser_pos);
        if (len > 0) {
                wsgi_req->proto_parser_pos += len;
                if (wsgi_req->proto_parser_pos >= 4) {
#ifdef __BIG_ENDIAN__
                        wsgi_req->uh->_pktsize = upsgi_swap16(wsgi_req->uh->_pktsize);
#endif
                        if ((wsgi_req->proto_parser_pos - 4) == wsgi_req->uh->_pktsize) {
                                return UPSGI_OK;
                        }
                        if ((wsgi_req->proto_parser_pos - 4) > wsgi_req->uh->_pktsize) {
                                wsgi_req->proto_parser_remains = wsgi_req->proto_parser_pos - (4 + wsgi_req->uh->_pktsize);
                                wsgi_req->proto_parser_remains_buf = wsgi_req->buffer + wsgi_req->uh->_pktsize;
                                return UPSGI_OK;
                        }
                        if (wsgi_req->uh->_pktsize > upsgi.buffer_size) {
                                upsgi_log("invalid request block size: %u (max %u)...skip\n", wsgi_req->uh->_pktsize, upsgi.buffer_size);
                                return -1;
                        }
                }
                return UPSGI_AGAIN;
        }
	else if (len == 0) goto empty;
	int err = SSL_get_error(wsgi_req->ssl, len);

	if (err == SSL_ERROR_WANT_READ) {
                return UPSGI_AGAIN;
        }

	else if (err == SSL_ERROR_WANT_WRITE) {
                int ret = upsgi_wait_write_req(wsgi_req);
                if (ret <= 0) return -1;
                goto retry;
        }

        else if (err == SSL_ERROR_SYSCALL) {
		if (errno != 0)
                	upsgi_error("upsgi_proto_supsgi_parser()/SSL_read()");
        }

        return -1;
empty:
        // 0 len
        if (wsgi_req->proto_parser_pos > 0) {
                upsgi_error("upsgi_proto_upsgi_parser()");
        }
        return -1;
}

#endif

/*
int upsgi_proto_upsgi_parser_unix(struct wsgi_request *wsgi_req) {

	uint8_t *hdr_buf = (uint8_t *) & wsgi_req->uh;
	ssize_t len;

	struct iovec iov[1];
	struct cmsghdr *cmsg;

	if (wsgi_req->proto_parser_status == PROTO_STATUS_RECV_HDR) {


		if (wsgi_req->proto_parser_pos > 0) {
			len = read(wsgi_req->fd, hdr_buf + wsgi_req->proto_parser_pos, 4 - wsgi_req->proto_parser_pos);
		}
		else {
			iov[0].iov_base = hdr_buf;
			iov[0].iov_len = 4;

			wsgi_req->msg.msg_name = NULL;
			wsgi_req->msg.msg_namelen = 0;
			wsgi_req->msg.msg_iov = iov;
			wsgi_req->msg.msg_iovlen = 1;
			wsgi_req->msg.msg_control = &wsgi_req->msg_control;
			wsgi_req->msg.msg_controllen = sizeof(wsgi_req->msg_control);
			wsgi_req->msg.msg_flags = 0;

			len = recvmsg(wsgi_req->fd, &wsgi_req->msg, 0);
		}

		if (len <= 0) {
			if (len < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
					return UPSGI_AGAIN;
				}
			}
			// ignore empty packets
			if (len == 0 && wsgi_req->proto_parser_pos == 0) return -3;
			upsgi_error(wsgi_req->proto_parser_pos > 0 ? "read()" : "recvmsg()");
			return -1;
		}
		wsgi_req->proto_parser_pos += len;
		// header ready ?
		if (wsgi_req->proto_parser_pos == 4) {
			wsgi_req->proto_parser_status = PROTO_STATUS_RECV_VARS;
			wsgi_req->proto_parser_pos = 0;
#ifdef __BIG_ENDIAN__
			wsgi_req->uh->_pktsize = upsgi_swap16(wsgi_req->uh->_pktsize);
#endif

#ifdef UPSGI_DEBUG
			upsgi_debug("upsgi payload size: %d (0x%X) modifier1: %d modifier2: %d\n", wsgi_req->uh._pktsize, wsgi_req->uh._pktsize, wsgi_req->uh.modifier1, wsgi_req->uh.modifier2);
#endif

			if (wsgi_req->uh->_pktsize > upsgi.buffer_size) {
				return -1;
			}

			if (!wsgi_req->uh->_pktsize)
				return UPSGI_OK;

		}
		return UPSGI_AGAIN;
	}

	else if (wsgi_req->proto_parser_status == PROTO_STATUS_RECV_VARS) {
		len = read(wsgi_req->fd, wsgi_req->buffer + wsgi_req->proto_parser_pos, wsgi_req->uh->_pktsize - wsgi_req->proto_parser_pos);
		if (len <= 0) {
			upsgi_error("read()");
			return -1;
		}
		wsgi_req->proto_parser_pos += len;

		// body ready ?
		if (wsgi_req->proto_parser_pos >= wsgi_req->uh->_pktsize) {

			// older OSX versions make mess with CMSG_FIRSTHDR
#ifdef __APPLE__
			if (!wsgi_req->msg.msg_controllen)
				return UPSGI_OK;
#endif

			if (upsgi.no_fd_passing)
				return UPSGI_OK;

			cmsg = CMSG_FIRSTHDR(&wsgi_req->msg);
			while (cmsg != NULL) {
				if (cmsg->cmsg_len == CMSG_LEN(sizeof(int)) && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type && SCM_RIGHTS) {

					// upgrade connection to the new socket
#ifdef UPSGI_DEBUG
					upsgi_log("upgrading fd %d to ", wsgi_req->fd);
#endif
					close(wsgi_req->fd);
					memcpy(&wsgi_req->fd, CMSG_DATA(cmsg), sizeof(int));
#ifdef UPSGI_DEBUG
					upsgi_log("%d\n", wsgi_req->fd);
#endif
				}
				cmsg = CMSG_NXTHDR(&wsgi_req->msg, cmsg);
			}

			return UPSGI_OK;
		}
		return UPSGI_AGAIN;
	}

	// never here

	return -1;
}

*/

void upsgi_proto_upsgi_setup(struct upsgi_socket *upsgi_sock) {
	upsgi_sock->proto = upsgi_proto_upsgi_parser;
	upsgi_sock->proto_accept = upsgi_proto_base_accept;
	upsgi_sock->proto_prepare_headers = upsgi_proto_base_prepare_headers;
	upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
	upsgi_sock->proto_fix_headers = upsgi_proto_base_fix_headers;
	upsgi_sock->proto_read_body = upsgi_proto_base_read_body;
	upsgi_sock->proto_write = upsgi_proto_base_write;
	upsgi_sock->proto_writev = upsgi_proto_base_writev;
	upsgi_sock->proto_write_headers = upsgi_proto_base_write;
	upsgi_sock->proto_sendfile = upsgi_proto_base_sendfile;
	upsgi_sock->proto_close = upsgi_proto_base_close;
	if (upsgi.offload_threads > 0)
		upsgi_sock->can_offload = 1;
}

#ifdef UPSGI_SSL
void upsgi_proto_supsgi_setup(struct upsgi_socket *upsgi_sock) {
        upsgi_sock->proto = upsgi_proto_supsgi_parser;
        upsgi_sock->proto_accept = upsgi_proto_ssl_accept;
        upsgi_sock->proto_prepare_headers = upsgi_proto_base_prepare_headers;
        upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
        upsgi_sock->proto_fix_headers = upsgi_proto_base_fix_headers;
        upsgi_sock->proto_read_body = upsgi_proto_ssl_read_body;
        upsgi_sock->proto_write = upsgi_proto_ssl_write;
        upsgi_sock->proto_write_headers = upsgi_proto_ssl_write;
        upsgi_sock->proto_sendfile = upsgi_proto_ssl_sendfile;
        upsgi_sock->proto_close = upsgi_proto_ssl_close;
        if (upsgi.offload_threads > 0)
                upsgi_sock->can_offload = 1;
}
#endif
