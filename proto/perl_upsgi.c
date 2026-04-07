/* async upsgi protocol parser */

#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

perfect framing is required in persistent mode, so we need at least 2 syscall to assemble
a upsgi header: 4 bytes header + payload

increase write_errors on error to force socket close

*/

int upsgi_proto_pupsgi_parser(struct wsgi_request *wsgi_req) {
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
				wsgi_req->write_errors++;
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
		wsgi_req->write_errors++;		
		return -1;
	}
	// 0 len
	if (wsgi_req->proto_parser_pos > 0) {
		upsgi_error("upsgi_proto_upsgi_parser()");	
	}
	wsgi_req->write_errors++;		
	return -1;
}

/*
close the connection on errors, otherwise force edge triggering
*/
void upsgi_proto_pupsgi_close(struct wsgi_request *wsgi_req) {
	// check for errors or incomplete packets
	if (wsgi_req->write_errors || (size_t) (wsgi_req->len + 4) != wsgi_req->proto_parser_pos) {
		close(wsgi_req->fd);
		wsgi_req->socket->retry[wsgi_req->async_id] = 0;
		wsgi_req->socket->fd_threads[wsgi_req->async_id] = -1;
	}
	else {
		wsgi_req->socket->retry[wsgi_req->async_id] = 1;
		wsgi_req->socket->fd_threads[wsgi_req->async_id] = wsgi_req->fd;
	}
}

int upsgi_proto_pupsgi_accept(struct wsgi_request *wsgi_req, int fd) {
	if (wsgi_req->socket->retry[wsgi_req->async_id]) {
		wsgi_req->fd = wsgi_req->socket->fd_threads[wsgi_req->async_id];
		int ret = upsgi_wait_read_req(wsgi_req);
                if (ret <= 0) {
			close(wsgi_req->fd);
			wsgi_req->socket->retry[wsgi_req->async_id] = 0;
			wsgi_req->socket->fd_threads[wsgi_req->async_id] = -1;
                	return -1;
		}
		return wsgi_req->socket->fd_threads[wsgi_req->async_id];	
	}
	return upsgi_proto_base_accept(wsgi_req, fd);
}

void upsgi_proto_pupsgi_setup(struct upsgi_socket *upsgi_sock) {
                        upsgi_sock->proto = upsgi_proto_pupsgi_parser;
                        upsgi_sock->proto_accept = upsgi_proto_pupsgi_accept;
                        upsgi_sock->proto_prepare_headers = upsgi_proto_base_prepare_headers;
                        upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
                        upsgi_sock->proto_fix_headers = upsgi_proto_base_fix_headers;
                        upsgi_sock->proto_read_body = upsgi_proto_noop_read_body;
                        upsgi_sock->proto_write = upsgi_proto_base_write;
                        upsgi_sock->proto_writev = upsgi_proto_base_writev;
                        upsgi_sock->proto_write_headers = upsgi_proto_base_write;
                        upsgi_sock->proto_sendfile = upsgi_proto_base_sendfile;
                        upsgi_sock->proto_close = upsgi_proto_pupsgi_close;
                        upsgi_sock->fd_threads = upsgi_malloc(sizeof(int) * upsgi.cores);
                        memset(upsgi_sock->fd_threads, -1, sizeof(int) * upsgi.cores);
                        upsgi_sock->retry = upsgi_calloc(sizeof(int) * upsgi.cores);
                        upsgi.is_et = 1;
                }
