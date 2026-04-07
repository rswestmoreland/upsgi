#include "upsgi.h"

/*

        upsgi websockets functions

	sponsored by 20Tab S.r.l. <info@20tab.com>

*/

extern struct upsgi_server upsgi;

#define REQ_DATA wsgi_req->method_len, wsgi_req->method, wsgi_req->uri_len, wsgi_req->uri, wsgi_req->remote_addr_len, wsgi_req->remote_addr 

static struct upsgi_buffer *upsgi_websocket_message(struct wsgi_request *wsgi_req, char *msg, size_t len, uint8_t opcode) {
	struct upsgi_buffer *ub = wsgi_req->websocket_send_buf;
	if (!ub) {
		wsgi_req->websocket_send_buf = upsgi_buffer_new(10 + len);
		ub = wsgi_req->websocket_send_buf;
	}
	else {
		// reset the buffer
		ub->pos = 0;
	}
	if (upsgi_buffer_u8(ub, opcode)) goto error;
	if (len < 126) {
		if (upsgi_buffer_u8(ub, len)) goto error;
	}
	else if (len <= (uint16_t) 0xffff) {
		if (upsgi_buffer_u8(ub, 126)) goto error;
		if (upsgi_buffer_u16be(ub, len)) goto error;
	}
	else {
		if (upsgi_buffer_u8(ub, 127)) goto error;
                if (upsgi_buffer_u64be(ub, len)) goto error;
	}

	if (upsgi_buffer_append(ub, msg, len)) goto error;
	return ub;

error:
	return NULL;
}

static int upsgi_websockets_ping(struct wsgi_request *wsgi_req) {
        if (upsgi_response_write_body_do(wsgi_req, upsgi.websockets_ping->buf, upsgi.websockets_ping->pos)) {
		return -1;
	}
	wsgi_req->websocket_last_ping = upsgi_now();
        return 0;
}

static int upsgi_websockets_pong(struct wsgi_request *wsgi_req) {
        return upsgi_response_write_body_do(wsgi_req, upsgi.websockets_pong->buf, upsgi.websockets_pong->pos);
}

static int upsgi_websockets_close(struct wsgi_request *wsgi_req) {
	return upsgi_response_write_body_do(wsgi_req, upsgi.websockets_close->buf, upsgi.websockets_close->pos);
}

static int upsgi_websockets_check_pingpong(struct wsgi_request *wsgi_req) {
	time_t now = upsgi_now();
	// first round
	if (wsgi_req->websocket_last_ping == 0) {
		return upsgi_websockets_ping(wsgi_req);
	}
	// pong not received ?
	if (wsgi_req->websocket_last_pong < wsgi_req->websocket_last_ping) {
		if (now - wsgi_req->websocket_last_ping > upsgi.websockets_pong_tolerance) {
                                upsgi_log("[upsgi-websocket] \"%.*s %.*s\" (%.*s) no PONG received in %d seconds !!!\n", REQ_DATA, upsgi.websockets_pong_tolerance);
				return -1;
		}
		return 0;
	}
	// pong received, send another ping
        if (now - wsgi_req->websocket_last_ping >= upsgi.websockets_ping_freq) {
                return upsgi_websockets_ping(wsgi_req);
	}
	return 0;
}

static int upsgi_websocket_send_do(struct wsgi_request *wsgi_req, char *msg, size_t len, uint8_t opcode) {
	struct upsgi_buffer *ub = upsgi_websocket_message(wsgi_req, msg, len, opcode);
	if (!ub) return -1;

	return upsgi_response_write_body_do(wsgi_req, ub->buf, ub->pos);
}

static int upsgi_websocket_send_from_sharedarea_do(struct wsgi_request *wsgi_req, int id, uint64_t pos, uint64_t len, uint8_t opcode) {
	struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
	if (!sa) return -1;
	if (!len) {
		len = sa->honour_used ? sa->used-pos : ((sa->max_pos+1)-pos);
	}
	upsgi_rlock(sa->lock);
	sa->hits++;
        struct upsgi_buffer *ub = upsgi_websocket_message(wsgi_req, sa->area, len, opcode);
	upsgi_rwunlock(sa->lock);
        if (!ub) return -1;

        return upsgi_response_write_body_do(wsgi_req, ub->buf, ub->pos);
}

int upsgi_websocket_send(struct wsgi_request *wsgi_req, char *msg, size_t len) {
	if (wsgi_req->websocket_closed) {
                return -1;
        }
	ssize_t ret = upsgi_websocket_send_do(wsgi_req, msg, len, 0x81);
	if (ret < 0) {
		wsgi_req->websocket_closed = 1;
	}
	return ret;
}

int upsgi_websocket_send_from_sharedarea(struct wsgi_request *wsgi_req, int id, uint64_t pos, uint64_t len) {
        if (wsgi_req->websocket_closed) {
                return -1;
        }
        ssize_t ret = upsgi_websocket_send_from_sharedarea_do(wsgi_req, id, pos, len, 0x81);
        if (ret < 0) {
                wsgi_req->websocket_closed = 1;
        }
        return ret;
}

int upsgi_websocket_send_binary(struct wsgi_request *wsgi_req, char *msg, size_t len) {
        if (wsgi_req->websocket_closed) {
                return -1;
        }
        ssize_t ret = upsgi_websocket_send_do(wsgi_req, msg, len, 0x82);
        if (ret < 0) {
                wsgi_req->websocket_closed = 1;
        }
        return ret;
}

int upsgi_websocket_send_binary_from_sharedarea(struct wsgi_request *wsgi_req, int id, uint64_t pos, uint64_t len) {
        if (wsgi_req->websocket_closed) {
                return -1;
        }
        ssize_t ret = upsgi_websocket_send_from_sharedarea_do(wsgi_req, id, pos, len, 0x82);
        if (ret < 0) {
                wsgi_req->websocket_closed = 1;
        }
        return ret;
}

static void upsgi_websocket_parse_header(struct wsgi_request *wsgi_req) {
	uint8_t byte1 = wsgi_req->websocket_buf->buf[0];
	uint8_t byte2 = wsgi_req->websocket_buf->buf[1];
	wsgi_req->websocket_is_fin = byte1 >> 7;
	wsgi_req->websocket_opcode = byte1 & 0xf;
	wsgi_req->websocket_has_mask = byte2 >> 7;
	wsgi_req->websocket_size = byte2 & 0x7f;
}

static struct upsgi_buffer *upsgi_websockets_parse(struct wsgi_request *wsgi_req) {
	// de-mask buffer
	uint8_t *ptr = (uint8_t *) (wsgi_req->websocket_buf->buf + (wsgi_req->websocket_pktsize - wsgi_req->websocket_size));
	size_t i;

	if (wsgi_req->websocket_has_mask) {
		uint8_t *mask = ptr-4;
		for(i=0;i<wsgi_req->websocket_size;i++) {
			ptr[i] = ptr[i] ^ mask[i%4];	
		}
	}

	struct upsgi_buffer *ub = NULL;
	if (wsgi_req->websocket_opcode == 0) {
		if (upsgi.websockets_continuation_buffer == NULL) {
			upsgi_log("Error continuation with empty previous buffer");
			goto error;
		}
		ub = upsgi.websockets_continuation_buffer;
	}
	else {
		ub = upsgi_buffer_new(wsgi_req->websocket_size);
	}
	if (upsgi_buffer_append(ub, (char *) ptr, wsgi_req->websocket_size)) goto error;	
	if (upsgi_buffer_decapitate(wsgi_req->websocket_buf, wsgi_req->websocket_pktsize)) goto error;
	wsgi_req->websocket_phase = 0;
	wsgi_req->websocket_need = 2;

	if (wsgi_req->websocket_is_fin) {
		upsgi.websockets_continuation_buffer = NULL;
		/// Freeing websockets_continuation_buffer is done by the caller
		return ub;
	}
	upsgi.websockets_continuation_buffer = ub;
	/// Message is not complete, send empty dummy buffer to signal waiting for full message
	ub = upsgi_buffer_new(1);
	upsgi_buffer_append(ub, "\0", 1);
	return ub;
error:
	upsgi_buffer_destroy(ub);
	if (upsgi.websockets_continuation_buffer != NULL && ub != upsgi.websockets_continuation_buffer) {
		upsgi_buffer_destroy(upsgi.websockets_continuation_buffer);
	}
	upsgi.websockets_continuation_buffer = NULL;
	return NULL;
}


static ssize_t upsgi_websockets_recv_pkt(struct wsgi_request *wsgi_req, int nb) {

	int ret = -1;

	for(;;) {
		ssize_t rlen = wsgi_req->socket->proto_read_body(wsgi_req, wsgi_req->websocket_buf->buf + wsgi_req->websocket_buf->pos, wsgi_req->websocket_buf->len - wsgi_req->websocket_buf->pos);
		if (rlen > 0) return rlen;
		if (rlen == 0) return -1;
		if (rlen < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
				if (nb) {
					if (upsgi_websockets_check_pingpong(wsgi_req)) {
						return -1;
					}
					return 0;
				}
                                goto wait;
                        }
                        upsgi_req_error("upsgi_websockets_recv_pkt()");
                        return -1;
                }

wait:
                ret = upsgi.wait_read_hook(wsgi_req->fd, upsgi.websockets_ping_freq);
                if (ret > 0) {
			rlen = wsgi_req->socket->proto_read_body(wsgi_req, wsgi_req->websocket_buf->buf + wsgi_req->websocket_buf->pos, wsgi_req->websocket_buf->len - wsgi_req->websocket_buf->pos);
			if (rlen > 0) return rlen;
			if (rlen <= 0) return -1;
		}
                if (ret < 0) {
                        upsgi_req_error("upsgi_websockets_recv_pkt()");
			return -1;
                }
		// send unsolicited pong
		if (upsgi_websockets_check_pingpong(wsgi_req)) {
			return -1;
		}
	}

        return -1;
}


static struct upsgi_buffer *upsgi_websocket_recv_do(struct wsgi_request *wsgi_req, int nb) {
	if (!wsgi_req->websocket_buf) {
		// this buffer will be destroyed on connection close
		wsgi_req->websocket_buf = upsgi_buffer_new(upsgi.page_size);
		// need 2 byte header
		wsgi_req->websocket_need = 2;
	}

	for(;;) {
		size_t remains = wsgi_req->websocket_buf->pos;
		// i have data;
		if (remains >= wsgi_req->websocket_need) {
			switch(wsgi_req->websocket_phase) {
				// header
				case 0:
					upsgi_websocket_parse_header(wsgi_req);
					wsgi_req->websocket_pktsize = 2 + (wsgi_req->websocket_has_mask*4);
					if (wsgi_req->websocket_size == 126) {
						wsgi_req->websocket_need += 2;
						wsgi_req->websocket_phase = 1;
						wsgi_req->websocket_pktsize += 2;
					}
					else if (wsgi_req->websocket_size == 127) {
						wsgi_req->websocket_need += 8;
						wsgi_req->websocket_phase = 1;
						wsgi_req->websocket_pktsize += 8;
					}
					else {
						wsgi_req->websocket_phase = 2;
					}
					break;
				// size
				case 1:
					if (wsgi_req->websocket_size == 126) {
						wsgi_req->websocket_size = upsgi_be16(wsgi_req->websocket_buf->buf+2);
					}
					else if (wsgi_req->websocket_size == 127) {
						wsgi_req->websocket_size = upsgi_be64(wsgi_req->websocket_buf->buf+2);
					}
					else {
						upsgi_log("[upsgi-websocket] \"%.*s %.*s\" (%.*s) BUG error in websocket parser\n", REQ_DATA);
						return NULL;
					}
					if (wsgi_req->websocket_size > (upsgi.websockets_max_size*1024)) {
						upsgi_log("[upsgi-websocket] \"%.*s %.*s\" (%.*s) invalid packet size received: %llu, max allowed: %llu\n", REQ_DATA, wsgi_req->websocket_size, upsgi.websockets_max_size * 1024);
						return NULL;
					}
					wsgi_req->websocket_phase = 2;
					break;
				// mask check
				case 2:
					if (wsgi_req->websocket_has_mask) {
						wsgi_req->websocket_need += 4;
						wsgi_req->websocket_phase = 3;
					}
					else {
						wsgi_req->websocket_need += wsgi_req->websocket_size;
						wsgi_req->websocket_pktsize += wsgi_req->websocket_size;
						wsgi_req->websocket_phase = 4;
					}
					break;
				// mask
				case 3:
					wsgi_req->websocket_pktsize += wsgi_req->websocket_size;
					wsgi_req->websocket_need += wsgi_req->websocket_size;
                                        wsgi_req->websocket_phase = 4;
					break;
				// message
				case 4:
					switch (wsgi_req->websocket_opcode) {
						// message
						case 0:
						case 1:
						case 2:
							return upsgi_websockets_parse(wsgi_req);
						// close
						case 0x8:
							upsgi_websockets_close(wsgi_req);
							return NULL;
						// ping
						case 0x9:
							if (upsgi_websockets_pong(wsgi_req)) {
								return NULL;
							}
							break;
						// pong
						case 0xA:
							wsgi_req->websocket_last_pong = upsgi_now();
							break;
						default:
							break;	
					}
					// reset the status
					wsgi_req->websocket_phase = 0;	
					wsgi_req->websocket_need = 2;	
					// decapitate the buffer
					if (upsgi_buffer_decapitate(wsgi_req->websocket_buf, wsgi_req->websocket_pktsize)) return NULL;
					break;
				// oops
				default:
					upsgi_log("[upsgi-websocket] \"%.*s %.*s\" (%.*s) BUG error in websocket parser\n", REQ_DATA);
					return NULL;
			}
		}
		// need more data
		else {
			if (upsgi_buffer_ensure(wsgi_req->websocket_buf, upsgi.page_size)) return NULL;
			ssize_t len = upsgi_websockets_recv_pkt(wsgi_req, nb);
			if (len <= 0) {
				if (nb == 1 && len == 0) {
					// return an empty buffer to signal blocking event
					return upsgi_buffer_new(0);
				}
				return NULL;	
			}
			// update buffer size
			wsgi_req->websocket_buf->pos+=len;
		}
	}

	return NULL;
}

static void clear_continuation_buffer() {
	if (upsgi.websockets_continuation_buffer != NULL) {
		upsgi_buffer_destroy(upsgi.websockets_continuation_buffer);
		upsgi.websockets_continuation_buffer = NULL;
	}
}

struct upsgi_buffer *upsgi_websocket_recv(struct wsgi_request *wsgi_req) {
	if (wsgi_req->websocket_closed) {
		return NULL;
	}
	struct upsgi_buffer *ub = upsgi_websocket_recv_do(wsgi_req, 0);
	if (!ub) {
		clear_continuation_buffer();
		wsgi_req->websocket_closed = 1;
	}
	return ub;
}

struct upsgi_buffer *upsgi_websocket_recv_nb(struct wsgi_request *wsgi_req) {
        if (wsgi_req->websocket_closed) {
                return NULL;
        }
        struct upsgi_buffer *ub = upsgi_websocket_recv_do(wsgi_req, 1);
        if (!ub) {
		clear_continuation_buffer();
                wsgi_req->websocket_closed = 1;
        }
        return ub;
}



ssize_t upsgi_websockets_simple_send(struct wsgi_request *wsgi_req, struct upsgi_buffer *ub) {
	ssize_t len = wsgi_req->socket->proto_write(wsgi_req, ub->buf, ub->pos);
	if (wsgi_req->write_errors > 0) {
		return -1;
	}
	return len;
}

int upsgi_websocket_handshake(struct wsgi_request *wsgi_req, char *key, uint16_t key_len, char *origin, uint16_t origin_len, char *proto, uint16_t proto_len) {
#ifdef UPSGI_SSL
	if (!key_len) {
		key = wsgi_req->http_sec_websocket_key;
		key_len = wsgi_req->http_sec_websocket_key_len;
	}
	if (key_len == 0) return -1;

	char sha1[20];
	if (upsgi_response_prepare_headers(wsgi_req, "101 Web Socket Protocol Handshake", 33)) return -1;
	if (upsgi_response_add_header(wsgi_req, "Upgrade", 7, "WebSocket", 9)) return -1;
	if (upsgi_response_add_header(wsgi_req, "Connection", 10, "Upgrade", 7)) return -1;

	// if origin was requested or proto_len is specified, send it back
        if (wsgi_req->http_origin_len > 0 || origin_len > 0) {
		if (!origin_len) {
			origin = wsgi_req->http_origin;
			origin_len = wsgi_req->http_origin_len;
		}
		if (upsgi_response_add_header(wsgi_req, "Sec-WebSocket-Origin", 20, origin, origin_len)) return -1;
        }
        else {
		if (upsgi_response_add_header(wsgi_req, "Sec-WebSocket-Origin", 20, "*", 1)) return -1;
        }
	
	// if protocol was requested or proto_len is specified, send it back
	if (wsgi_req->http_sec_websocket_protocol_len > 0 || proto_len > 0) {
		if (!proto_len) {
			proto = wsgi_req->http_sec_websocket_protocol;
			proto_len = wsgi_req->http_sec_websocket_protocol_len;
		}
		if (upsgi_response_add_header(wsgi_req, "Sec-WebSocket-Protocol", 22, proto, proto_len)) return -1;
	}
	// generate websockets sha1 and encode it to base64
        if (!upsgi_sha1_2n(key, key_len, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36, sha1)) return -1;
	size_t b64_len = 0;
        char *b64 = upsgi_base64_encode(sha1, 20, &b64_len);
	if (!b64) return -1;

	if (upsgi_response_add_header(wsgi_req, "Sec-WebSocket-Accept", 20, b64, b64_len)) {
		free(b64);
		return -1;
	}
	free(b64);

	wsgi_req->websocket_last_pong = upsgi_now();

	return upsgi_response_write_headers_do(wsgi_req);
#else
	upsgi_log("you need to build upsgi with SSL support to use the websocket handshake api function !!!\n");
	return -1;
#endif
}

void upsgi_websockets_init() {
        upsgi.websockets_pong = upsgi_buffer_new(2);
        upsgi_buffer_append(upsgi.websockets_pong, "\x8A\0", 2);
        upsgi.websockets_ping = upsgi_buffer_new(2);
        upsgi_buffer_append(upsgi.websockets_ping, "\x89\0", 2);
        upsgi.websockets_close = upsgi_buffer_new(2);
        upsgi_buffer_append(upsgi.websockets_close, "\x88\0", 2);
	upsgi.websockets_ping_freq = 30;
	upsgi.websockets_pong_tolerance = 3;
	upsgi.websockets_max_size = 1024;
	upsgi.websockets_continuation_buffer = NULL;
}
