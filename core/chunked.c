#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	Chunked input implementation

	--chunked-input-limit <bytes> (default 1MB)

	--chunked-input-timeout <s> (default --socket-timeout)

	chunk = upsgi.chunked_read([timeout])

	timeout = -1 (wait forever)
	timeout = 0 (default)

*/

static ssize_t upsgi_chunked_input_recv(struct wsgi_request *wsgi_req, int timeout, int nb) {

	if (timeout == 0) timeout = upsgi.chunked_input_timeout;
	if (timeout == 0) timeout = upsgi.socket_timeout;

	int ret = -1;

	for(;;) {
		size_t read_budget = upsgi_body_sched_read_budget(wsgi_req, wsgi_req->chunked_input_buf->len - wsgi_req->chunked_input_buf->pos);
		ssize_t rlen = wsgi_req->socket->proto_read_body(wsgi_req, wsgi_req->chunked_input_buf->buf + wsgi_req->chunked_input_buf->pos, read_budget);
		if (rlen > 0) {
			upsgi_body_sched_note_bytes(wsgi_req, (size_t) rlen);
			return rlen;
		}
		if (rlen == 0) {
			upsgi_body_sched_note_empty_read(wsgi_req);
			return -1;
		}
		if (rlen < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
				upsgi_body_sched_note_eagain(wsgi_req);
				if (nb) return -1; 
                                goto wait;
                        }
                        upsgi_error("upsgi_chunked_input_recv()");
                        return -1;
                }

wait:
                ret = upsgi.wait_read_hook(wsgi_req->fd, timeout);
                if (ret > 0) {
			read_budget = upsgi_body_sched_read_budget(wsgi_req, wsgi_req->chunked_input_buf->len - wsgi_req->chunked_input_buf->pos);
			rlen = wsgi_req->socket->proto_read_body(wsgi_req, wsgi_req->chunked_input_buf->buf + wsgi_req->chunked_input_buf->pos, read_budget);
			if (rlen > 0) {
				upsgi_body_sched_note_bytes(wsgi_req, (size_t) rlen);
				return rlen;
			}
			if (rlen == 0) {
				upsgi_body_sched_note_empty_read(wsgi_req);
				return -1;
			}
			if (rlen < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS)) {
				upsgi_body_sched_note_eagain(wsgi_req);
				if (nb) return -1;
				goto wait;
			}
			if (rlen <= 0) return -1;
		}
                if (ret < 0) {
			upsgi_error("upsgi_chunked_input_recv()");
                }
		return -1;
	}

        return -1;
}

static int upsgi_chunked_require_crlf(char *buf, size_t chunk_len) {
	if (buf[chunk_len] != '\r' || buf[chunk_len + 1] != '\n') {
		upsgi_log("Invalid chunked body: missing chunk data terminator.\n");
		return -1;
	}
	return 0;
}

static int upsgi_chunked_require_final_crlf(struct wsgi_request *wsgi_req) {
	if (wsgi_req->chunked_input_buf->pos < 2) {
		wsgi_req->chunked_input_need = 2 - wsgi_req->chunked_input_buf->pos;
		wsgi_req->chunked_input_parser_status = 2;
		return 1;
	}
	if (wsgi_req->chunked_input_buf->buf[0] != '\r' || wsgi_req->chunked_input_buf->buf[1] != '\n') {
		upsgi_log("Invalid chunked body: trailers are unsupported or the terminating CRLF is missing.\n");
		return -1;
	}
	if (upsgi_buffer_decapitate(wsgi_req->chunked_input_buf, 2)) return -1;
	wsgi_req->chunked_input_need = 0;
	wsgi_req->chunked_input_parser_status = 0;
	wsgi_req->chunked_input_complete = 1;
	return 0;
}

static ssize_t upsgi_chunked_readline(struct wsgi_request *wsgi_req) {
	size_t i;
	int found = 0;
	for(i=0;i<wsgi_req->chunked_input_buf->pos;i++) {
		char c = wsgi_req->chunked_input_buf->buf[i];
		if (found) {
			if (c == '\n') {
				// strtoul will stop at \r
				size_t num =  strtoul(wsgi_req->chunked_input_buf->buf, NULL, 16);
				if (upsgi_buffer_decapitate(wsgi_req->chunked_input_buf, i+1)) return -1;
				return num;
			}
			upsgi_log("Invalid chunked body: malformed chunk line terminator.\n");
			return -1;
		}

		if ((c >= '0' && c <= '9') ||
			(c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F')) continue;
		if (c == '\r') { found = 1; continue; }
		upsgi_log("Invalid chunked body: non-hex chunk size digit 0x%02x.\n", (unsigned char) c);
		return -1;
	}

	return -2;
}

/*

	0 -> waiting for \r\n
	1 -> waiting for whole body

*/

struct upsgi_buffer *upsgi_chunked_read_smart(struct wsgi_request *wsgi_req, size_t len, int timeout) {
	// check for buffer
	if (!wsgi_req->body_chunked_buf)
		wsgi_req->body_chunked_buf = upsgi_buffer_new(upsgi.page_size);	
	// first case: asking for all
	if (!len) {
		for(;;) {
			size_t chunked_len = 0;
			char *buf = upsgi_chunked_read(wsgi_req, &chunked_len, timeout, 0);
			if (!buf) {
				return NULL;
			}
			if (chunked_len == 0) {
				struct upsgi_buffer *ret = upsgi_buffer_new(wsgi_req->body_chunked_buf->pos);
				if (upsgi_buffer_append(ret, wsgi_req->body_chunked_buf->buf, wsgi_req->body_chunked_buf->pos)) {
					upsgi_buffer_destroy(ret);
					return NULL;
				}
				upsgi_buffer_decapitate(wsgi_req->body_chunked_buf, wsgi_req->body_chunked_buf->pos);
				return ret;
			}
			if (upsgi_buffer_append(wsgi_req->body_chunked_buf, buf, chunked_len)) {
				return NULL;
			}
		}
	} 

	// asking for littler part
	if (len <= wsgi_req->body_chunked_buf->pos) {
		struct upsgi_buffer *ret = upsgi_buffer_new(len);
		if (upsgi_buffer_append(ret, wsgi_req->body_chunked_buf->buf, len)) {
			upsgi_buffer_destroy(ret);
			return NULL;
		}
		upsgi_buffer_decapitate(wsgi_req->body_chunked_buf, len);
		return ret;
	}

	// more data required
	size_t remains = len;
	struct upsgi_buffer *ret = upsgi_buffer_new(remains);
	if (wsgi_req->body_chunked_buf->pos > 0) {
		if (upsgi_buffer_append(ret, wsgi_req->body_chunked_buf->buf, wsgi_req->body_chunked_buf->pos)) {
			upsgi_buffer_destroy(ret);
			return NULL;
		}
		remains -= wsgi_req->body_chunked_buf->pos;
		upsgi_buffer_decapitate(wsgi_req->body_chunked_buf, wsgi_req->body_chunked_buf->pos);
	}

	while(remains) {
		size_t chunked_len = 0;
                char *buf = upsgi_chunked_read(wsgi_req, &chunked_len, timeout, 0);
                if (!buf) {
			upsgi_buffer_destroy(ret);
			return NULL;
		}
                if (chunked_len == 0) {
			break;
		}
		if (upsgi_buffer_append(wsgi_req->body_chunked_buf, buf, chunked_len)) {
			upsgi_buffer_destroy(ret);
			return NULL;
		}

		if (chunked_len > remains) {
			if (upsgi_buffer_append(ret, wsgi_req->body_chunked_buf->buf, wsgi_req->body_chunked_buf->pos - (chunked_len - remains))) {
                                upsgi_buffer_destroy(ret);
                                return NULL;
                        }       
                        upsgi_buffer_decapitate(wsgi_req->body_chunked_buf, wsgi_req->body_chunked_buf->pos - (chunked_len - remains));
                        return ret;
		}
		remains -= chunked_len;
	}

	if (upsgi_buffer_append(ret, wsgi_req->body_chunked_buf->buf, wsgi_req->body_chunked_buf->pos)) {
		upsgi_buffer_destroy(ret);
		return NULL;
	}
	upsgi_buffer_decapitate(wsgi_req->body_chunked_buf, wsgi_req->body_chunked_buf->pos);
	return ret;
}

char *upsgi_chunked_read(struct wsgi_request *wsgi_req, size_t *len, int timeout, int nb) {

	if (!wsgi_req->chunked_input_buf) {
		wsgi_req->chunked_input_buf = upsgi_buffer_new(upsgi.page_size);
		wsgi_req->chunked_input_buf->limit = upsgi.chunked_input_limit;
	}

	// the whole chunk stream has been consumed
	if (wsgi_req->chunked_input_complete) {
		*len = 0;
		return wsgi_req->chunked_input_buf->buf;
	}

	if (wsgi_req->chunked_input_decapitate > 0) {
		if (upsgi_buffer_decapitate(wsgi_req->chunked_input_buf, wsgi_req->chunked_input_decapitate)) return NULL;
		wsgi_req->chunked_input_decapitate = 0;
	}

	for(;;) {
		if (wsgi_req->chunked_input_need > 0 || wsgi_req->chunked_input_buf->pos == 0) {
			if (upsgi_buffer_ensure(wsgi_req->chunked_input_buf, UMAX((uint64_t)upsgi.page_size, wsgi_req->chunked_input_need))) return NULL;
			ssize_t rlen = upsgi_chunked_input_recv(wsgi_req, timeout, nb);	
			if (rlen <= 0) return NULL;
			// update buffer position
			wsgi_req->chunked_input_buf->pos += rlen;
			if (wsgi_req->chunked_input_need > 0) {
				if ((size_t)rlen > wsgi_req->chunked_input_need) {
					wsgi_req->chunked_input_need = 0;
				}
				else {
					wsgi_req->chunked_input_need -= rlen;
				}
			}
		}

		if (wsgi_req->chunked_input_need > 0) continue;

		// ok we have a frame, let's parse it
		if (wsgi_req->chunked_input_buf->pos > 0) {
			switch(wsgi_req->chunked_input_parser_status) {
				case 0:
					wsgi_req->chunked_input_chunk_len = upsgi_chunked_readline(wsgi_req);
					if (wsgi_req->chunked_input_chunk_len == -2) {
						wsgi_req->chunked_input_need++;
						break;
					}
					else if (wsgi_req->chunked_input_chunk_len < 0) {
						return NULL;
					}
					else if (wsgi_req->chunked_input_chunk_len == 0) {
						int final_status = upsgi_chunked_require_final_crlf(wsgi_req);
						if (final_status > 0) {
							break;
						}
						if (final_status < 0) {
							return NULL;
						}
						*len = 0;
						return wsgi_req->chunked_input_buf->buf;
					}
					// if here the buffer has been already decapitated
					if ((size_t)(wsgi_req->chunked_input_chunk_len+2) > wsgi_req->chunked_input_buf->pos) {
						wsgi_req->chunked_input_need = (wsgi_req->chunked_input_chunk_len+2) - wsgi_req->chunked_input_buf->pos;
						wsgi_req->chunked_input_parser_status = 1;
						break;
					}
					if (upsgi_chunked_require_crlf(wsgi_req->chunked_input_buf->buf, (size_t) wsgi_req->chunked_input_chunk_len)) {
						return NULL;
					}
					*len = wsgi_req->chunked_input_chunk_len;
					wsgi_req->chunked_input_decapitate = wsgi_req->chunked_input_chunk_len+2;
					return wsgi_req->chunked_input_buf->buf;
				case 1:
					if ((size_t)(wsgi_req->chunked_input_chunk_len+2) > wsgi_req->chunked_input_buf->pos) {
	                                                wsgi_req->chunked_input_need = (wsgi_req->chunked_input_chunk_len+2) - wsgi_req->chunked_input_buf->pos;
						break;
	                                        }	
					if (upsgi_chunked_require_crlf(wsgi_req->chunked_input_buf->buf, (size_t) wsgi_req->chunked_input_chunk_len)) {
						return NULL;
					}
					*len = wsgi_req->chunked_input_chunk_len;
					wsgi_req->chunked_input_decapitate = wsgi_req->chunked_input_chunk_len+2;
					wsgi_req->chunked_input_parser_status = 0;
	                                        return wsgi_req->chunked_input_buf->buf;
				case 2: {
					int final_status = upsgi_chunked_require_final_crlf(wsgi_req);
					if (final_status > 0) {
						break;
					}
					if (final_status < 0) {
						return NULL;
					}
					*len = 0;
					return wsgi_req->chunked_input_buf->buf;
				}
			}
		}
	}
}
