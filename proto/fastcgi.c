/* async fastcgi protocol parser */

#include "upsgi.h"

extern struct upsgi_server upsgi;

#define FCGI_END_REQUEST_BYTES "\1\x06\0\1\0\0\0\0\1\3\0\1\0\x08\0\0\0\0\0\0\0\0\0\0"

struct fcgi_record {
	uint8_t version;
	uint8_t type;
	uint8_t req1;
	uint8_t req0;
	uint8_t cl1;
	uint8_t cl0;
	uint8_t pad;
	uint8_t reserved;
} __attribute__ ((__packed__));


// convert fastcgi params to upsgi key/val
int fastcgi_to_upsgi(struct wsgi_request *wsgi_req, char *buf, size_t len) {
	size_t j;
	uint8_t octet;
	uint32_t keylen, vallen;
	for (j = 0; j < len; j++) {
		octet = (uint8_t) buf[j];
		if (octet > 127) {
			if (j + 4 >= len)
				return -1;
			keylen = upsgi_be32(&buf[j]) ^ 0x80000000;
			j += 4;
		}
		else {
			if (j + 1 >= len)
				return -1;
			keylen = octet;
			j++;
		}
		octet = (uint8_t) buf[j];
		if (octet > 127) {
			if (j + 4 >= len)
				return -1;
			vallen = upsgi_be32(&buf[j]) ^ 0x80000000;
			j += 4;
		}
		else {
			if (j + 1 >= len)
				return -1;
			vallen = octet;
			j++;
		}

		if (j + (keylen + vallen) > len) {
			return -1;
		}

		if (keylen > 0xffff || vallen > 0xffff)
			return -1;
		uint16_t pktsize = proto_base_add_upsgi_var(wsgi_req, buf + j, keylen, buf + j + keylen, vallen);
		if (pktsize == 0)
			return -1;
		wsgi_req->len += pktsize;
		// -1 here as the for() will increment j again
		j += (keylen + vallen) - 1;
	}

	return 0;
}

static int buf_append_fcgi_u32(struct upsgi_buffer *ub, uint32_t n) {
	unsigned char c[4];

	c[0] = (n & 0xFF000000) >> 24;
	c[1] = (n & 0x00FF0000) >> 16;
	c[2] = (n & 0x0000FF00) >> 8;
	c[3] = (n & 0x000000FF);

	return upsgi_buffer_append(ub, (char *)c, 4);
}

static int buf_append_fcgi_u16(struct upsgi_buffer *ub, uint16_t n) {
	unsigned char c[2];

	c[0] = (n & 0xFF00) >> 8;
	c[1] = (n & 0x00FF);

	return upsgi_buffer_append(ub, (char *)c, 2);
}

static int buf_append_fcgi_u8(struct upsgi_buffer *ub, uint8_t n) {
	unsigned char c;

	c = (n & 0xFF);

	return upsgi_buffer_append(ub, (char *)&c, 1);
}

static int buf_append_fcgi_rec(struct upsgi_buffer *ub, uint8_t type, uint16_t reqid, uint16_t dlen, char *data) {
	static const unsigned char version = 1;
	if (upsgi_buffer_append(ub, (char *) &version, 1)) return -1;
	if (upsgi_buffer_append(ub, (char *) &type, 1)) return -1;
	if (buf_append_fcgi_u16(ub, reqid)) return -1;
	if (buf_append_fcgi_u16(ub, dlen)) return -1;
	if (buf_append_fcgi_u8(ub, 0)) return -1; /* padding length */
	if (buf_append_fcgi_u8(ub, 0)) return -1; /* reserved */
	if (data)
		if (upsgi_buffer_append(ub, data, dlen)) return -1;
	return 0;
}

static int buf_append_fcgi_nameval(struct upsgi_buffer *ub, char *name, size_t namelen, char *value, size_t vlen) {
	int i;

	if (namelen < 128)
		i = buf_append_fcgi_u8(ub, namelen);
	else
		i = buf_append_fcgi_u32(ub, namelen);
	if (i) return -1;
	
	if (vlen < 128)
		i = buf_append_fcgi_u8(ub, vlen);
	else
		i = buf_append_fcgi_u32(ub, vlen);
	if (i) return -1;

	if (upsgi_buffer_append(ub, name, namelen)) return -1;
	if (upsgi_buffer_append(ub, value, vlen)) return -1;
	return 0;
}

static int fastcgi_buf_begin_request(struct upsgi_buffer *ub, int reqid, int role, int flags) {
	if (buf_append_fcgi_rec(ub, FCGI_BEGIN_REQUEST, reqid, 8, NULL)) return -1;
	if (buf_append_fcgi_u16(ub, role)) return -1;
	if (buf_append_fcgi_u8(ub, flags)) return -1;
	if (upsgi_buffer_append(ub, "     ", 5)) return -1; /* reserved data */
	return 0;
}

/* convert a upsgi request into a fastcgi request */
struct upsgi_buffer *upsgi_to_fastcgi(struct wsgi_request *wsgi_req, int role) {
	struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	int i;
	size_t rlen = 0;

	fastcgi_buf_begin_request(ub, 1, role, 0);

#define FCGI_LEN_LEN(x) ((x) < 128 ? 1 : 4)
        for(i = 0; i < wsgi_req->var_cnt; i += 2)
		rlen += wsgi_req->hvec[i].iov_len + wsgi_req->hvec[i+1].iov_len
			+ FCGI_LEN_LEN(wsgi_req->hvec[i].iov_len)
			+ FCGI_LEN_LEN(wsgi_req->hvec[i+1].iov_len);

	buf_append_fcgi_rec(ub, FCGI_PARAMS, 1, rlen, NULL);

        for(i = 0; i < wsgi_req->var_cnt; i += 2) {
		buf_append_fcgi_nameval(ub,
			wsgi_req->hvec[i].iov_base,
			wsgi_req->hvec[i].iov_len,
			wsgi_req->hvec[i+1].iov_base,
			wsgi_req->hvec[i+1].iov_len);
	}

	buf_append_fcgi_rec(ub, FCGI_PARAMS, 1, 0, NULL);
	buf_append_fcgi_rec(ub, FCGI_STDIN, 1, 0, NULL);

	return ub;
}

/*

	each fastcgi packet is composed by a header and a body
	the parser rebuild a whole packet until it finds a 0 STDIN

*/

int upsgi_proto_fastcgi_parser(struct wsgi_request *wsgi_req) {

	// allocate space for a fastcgi record
	if (!wsgi_req->proto_parser_buf) {
		wsgi_req->proto_parser_buf = upsgi_malloc(upsgi.buffer_size);
		wsgi_req->proto_parser_buf_size = upsgi.buffer_size;
	}

	ssize_t len = read(wsgi_req->fd, wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos, wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos);
	if (len > 0) {
		goto parse;
	}
	if (len < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
			return UPSGI_AGAIN;
		}
		upsgi_error("upsgi_proto_fastcgi_parser()");
		return -1;
	}
	// mute on 0 len...
	if (wsgi_req->proto_parser_pos > 0) {
		upsgi_error("upsgi_proto_fastcgi_parser()");
	}
	return -1;
parse:
	wsgi_req->proto_parser_pos += len;
	// ok let's see what we need to do
	for (;;) {
		if (wsgi_req->proto_parser_pos >= sizeof(struct fcgi_record)) {
			struct fcgi_record *fr = (struct fcgi_record *) wsgi_req->proto_parser_buf;
			uint16_t fcgi_len = upsgi_be16((char *) &fr->cl1);
			uint32_t fcgi_all_len = sizeof(struct fcgi_record) + fcgi_len + fr->pad;
			uint8_t fcgi_type = fr->type;
			uint8_t *sid = (uint8_t *) & wsgi_req->stream_id;
			sid[0] = fr->req0;
			sid[1] = fr->req1;
			// if STDIN, end of the loop
			if (fcgi_type == 5) {
				wsgi_req->uh->modifier1 = upsgi.fastcgi_modifier1;
				wsgi_req->uh->modifier2 = upsgi.fastcgi_modifier2;
				// does the request stream ended ?
				if (fcgi_len == 0) wsgi_req->proto_parser_eof = 1;
				return UPSGI_OK;
			}
			// if we have a full packet, parse it and reset the memory
			if (wsgi_req->proto_parser_pos >= fcgi_all_len) {
				// PARAMS ? (ignore other types)
				if (fcgi_type == 4) {
					if (fastcgi_to_upsgi(wsgi_req, wsgi_req->proto_parser_buf + sizeof(struct fcgi_record), fcgi_len)) {
						return -1;
					}
				}
				memmove(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_buf + fcgi_all_len, wsgi_req->proto_parser_pos - fcgi_all_len);
				wsgi_req->proto_parser_pos -= fcgi_all_len;
			}
			else if (fcgi_all_len > wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos) {
				char *tmp_buf = realloc(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_buf_size + fcgi_all_len - (wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos));
				if (!tmp_buf) {
					upsgi_error("upsgi_proto_fastcgi_parser()/realloc()");
					return -1;
				}
				wsgi_req->proto_parser_buf = tmp_buf;
				wsgi_req->proto_parser_buf_size += (fcgi_all_len - (wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos));
				break;
			}
			else {
				break;
			}
		}
		else {
			break;
		}
	}
	return UPSGI_AGAIN;

}


ssize_t upsgi_proto_fastcgi_read_body(struct wsgi_request * wsgi_req, char *buf, size_t len) {
	int has_read = 0;
	if (wsgi_req->proto_parser_remains > 0) {
		size_t remains = UMIN(wsgi_req->proto_parser_remains, len);
		memcpy(buf, wsgi_req->proto_parser_remains_buf, remains);
		wsgi_req->proto_parser_remains -= remains;
		wsgi_req->proto_parser_remains_buf += remains;
		// we consumed all of the body, we can safely move the memory
		if (wsgi_req->proto_parser_remains == 0 && wsgi_req->proto_parser_move) {
			memmove(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_buf + wsgi_req->proto_parser_move, wsgi_req->proto_parser_pos);
			wsgi_req->proto_parser_move = 0;
		}
		return remains;
	}

	// if we already have seen eof, return 0
	if (wsgi_req->proto_parser_eof) return 0;

	ssize_t rlen;

	for (;;) {
		if (wsgi_req->proto_parser_pos >= sizeof(struct fcgi_record)) {
			struct fcgi_record *fr = (struct fcgi_record *) wsgi_req->proto_parser_buf;
			uint16_t fcgi_len = upsgi_be16((char *) &fr->cl1);
			uint32_t fcgi_all_len = sizeof(struct fcgi_record) + fcgi_len + fr->pad;
			uint8_t fcgi_type = fr->type;
			// if we have a full packet, parse it and reset the memory
			if (wsgi_req->proto_parser_pos >= fcgi_all_len) {
				// STDIN ? (ignore other types)
				if (fcgi_type == 5) {
					// EOF ?
					if (fcgi_len == 0) {
						wsgi_req->proto_parser_eof = 1;
						return 0;	
					}
					// copy data to the buf
					size_t remains = UMIN(fcgi_len, len);
					memcpy(buf, wsgi_req->proto_parser_buf + sizeof(struct fcgi_record), remains);
					// copy remaining
					wsgi_req->proto_parser_remains = fcgi_len - remains;
					wsgi_req->proto_parser_remains_buf = wsgi_req->proto_parser_buf + sizeof(struct fcgi_record) + remains;
					// we consumed all of the body, we can safely move the memory
					if (wsgi_req->proto_parser_remains == 0) {
						memmove(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_buf + fcgi_all_len, wsgi_req->proto_parser_pos - fcgi_all_len);
					}
					else {
						// postpone memory move
						wsgi_req->proto_parser_move = fcgi_all_len;
					}
					wsgi_req->proto_parser_pos -= fcgi_all_len;
					return remains;
				}
				memmove(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_buf + fcgi_all_len, wsgi_req->proto_parser_pos - fcgi_all_len);
				wsgi_req->proto_parser_pos -= fcgi_all_len;
			}
			else if (fcgi_all_len > wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos) {
				char *tmp_buf = realloc(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_buf_size + fcgi_all_len - (wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos));
				if (!tmp_buf) {
					upsgi_error("upsgi_proto_fastcgi_read_body()/realloc()");
					return -1;
				}
				wsgi_req->proto_parser_buf = tmp_buf;
				wsgi_req->proto_parser_buf_size += fcgi_all_len - (wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos);
			}
			if (!has_read)
				goto gather;
			errno = EAGAIN;
			return -1;
		}
		else {
gather:
			rlen = read(wsgi_req->fd, wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos, wsgi_req->proto_parser_buf_size - wsgi_req->proto_parser_pos);
			if (rlen > 0) {
				has_read = 1;
				wsgi_req->proto_parser_pos += rlen;
				continue;
			}
			return rlen;
		}
	}

	return -1;

}

// write a STDOUT packet
int upsgi_proto_fastcgi_write(struct wsgi_request *wsgi_req, char *buf, size_t len) {

	// fastcgi packets are limited to 64k
	if (wsgi_req->proto_parser_status == 0) {
		uint16_t fcgi_len = UMIN(len - wsgi_req->write_pos, 0xffff);
		wsgi_req->proto_parser_status = fcgi_len;
		struct fcgi_record fr;
		fr.version = 1;
		fr.type = 6;
		uint8_t *sid = (uint8_t *) & wsgi_req->stream_id;
		fr.req1 = sid[1];
		fr.req0 = sid[0];
		fr.pad = 0;
		fr.reserved = 0;
		fr.cl0 = (uint8_t) (fcgi_len & 0xff);
		fr.cl1 = (uint8_t) ((fcgi_len >> 8) & 0xff);
		if (upsgi_write_true_nb(wsgi_req->fd, (char *) &fr, sizeof(struct fcgi_record), upsgi.socket_timeout)) {
			return -1;
		}
	}

	ssize_t wlen = write(wsgi_req->fd, buf + wsgi_req->write_pos, wsgi_req->proto_parser_status);
	if (wlen > 0) {
		wsgi_req->write_pos += wlen;
		wsgi_req->proto_parser_status -= wlen;
		if (wsgi_req->write_pos == len) {
			return UPSGI_OK;
		}
		return UPSGI_AGAIN;
	}
	if (wlen < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
			return UPSGI_AGAIN;
		}
	}
	return -1;

}

void upsgi_proto_fastcgi_close(struct wsgi_request *wsgi_req) {
	// before sending the END_REQUEST and closing the connection we need to check for EOF
	if (!wsgi_req->proto_parser_eof) {
		// we use a custom tiny buffer, all the data will be discarded...
		char buf[4096];
		for(;;) {
			ssize_t rlen = upsgi_proto_fastcgi_read_body(wsgi_req, buf, 4096);
			if (rlen < 0) {
				if (upsgi_is_again()) {
					int ret = upsgi.wait_read_hook(wsgi_req->fd, upsgi.socket_timeout);
					if (ret <= 0) goto end;
					continue;
				}
				goto end;
			}
			if (rlen == 0) break;
		}
	}
	// special case here, we run in void context, so we need to wait directly here
	char end_request[24];
	memcpy(end_request, FCGI_END_REQUEST_BYTES, 24);
	char *sid = (char *) &wsgi_req->stream_id;
	// update with request id
	end_request[2] = sid[1];
	end_request[3] = sid[0];
	end_request[10] = sid[1];
	end_request[11] = sid[0];
	(void) upsgi_write_true_nb(wsgi_req->fd, end_request, 24, upsgi.socket_timeout);
end:
	upsgi_proto_base_close(wsgi_req);
}

int upsgi_proto_fastcgi_sendfile(struct wsgi_request *wsgi_req, int fd, size_t pos, size_t len) {

	// fastcgi packets are limited to 64k
	if (wsgi_req->proto_parser_status == 0) {
		uint16_t fcgi_len = (uint16_t) UMIN(len - wsgi_req->write_pos, 0xffff);
		wsgi_req->proto_parser_status = fcgi_len;
		struct fcgi_record fr;
		fr.version = 1;
		fr.type = 6;
		uint8_t *sid = (uint8_t *) & wsgi_req->stream_id;
		fr.req1 = sid[1];
		fr.req0 = sid[0];
		fr.pad = 0;
		fr.reserved = 0;
		fr.cl0 = (uint8_t) (fcgi_len & 0xff);
		fr.cl1 = (uint8_t) ((fcgi_len >> 8) & 0xff);
		if (upsgi_write_true_nb(wsgi_req->fd, (char *) &fr, sizeof(struct fcgi_record), upsgi.socket_timeout)) {
			return -1;
		}
	}

	ssize_t wlen = upsgi_sendfile_do(wsgi_req->fd, fd, pos + wsgi_req->write_pos, wsgi_req->proto_parser_status);
	if (wlen > 0) {
		wsgi_req->write_pos += wlen;
		wsgi_req->proto_parser_status -= wlen;
		if (wsgi_req->write_pos == len) {
			return UPSGI_OK;
		}
		return UPSGI_AGAIN;
	}
	if (wlen < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
			return UPSGI_AGAIN;
		}
	}
	return -1;
}

void upsgi_proto_fastcgi_setup(struct upsgi_socket *upsgi_sock) {
	upsgi_sock->proto = upsgi_proto_fastcgi_parser;
	upsgi_sock->proto_accept = upsgi_proto_base_accept;
	upsgi_sock->proto_prepare_headers = upsgi_proto_base_cgi_prepare_headers;
	upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
	upsgi_sock->proto_fix_headers = upsgi_proto_base_fix_headers;
	upsgi_sock->proto_read_body = upsgi_proto_fastcgi_read_body;
	upsgi_sock->proto_write = upsgi_proto_fastcgi_write;
	upsgi_sock->proto_write_headers = upsgi_proto_fastcgi_write;
	upsgi_sock->proto_sendfile = upsgi_proto_fastcgi_sendfile;
	upsgi_sock->proto_close = upsgi_proto_fastcgi_close;
}

void upsgi_proto_fastcgi_nph_setup(struct upsgi_socket *upsgi_sock) {
	upsgi_sock->proto = upsgi_proto_fastcgi_parser;
	upsgi_sock->proto_accept = upsgi_proto_base_accept;
	upsgi_sock->proto_prepare_headers = upsgi_proto_base_prepare_headers;
	upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
	upsgi_sock->proto_fix_headers = upsgi_proto_base_fix_headers;
	upsgi_sock->proto_read_body = upsgi_proto_fastcgi_read_body;
	upsgi_sock->proto_write = upsgi_proto_fastcgi_write;
	upsgi_sock->proto_write_headers = upsgi_proto_fastcgi_write;
	upsgi_sock->proto_sendfile = upsgi_proto_fastcgi_sendfile;
	upsgi_sock->proto_close = upsgi_proto_fastcgi_close;
}
