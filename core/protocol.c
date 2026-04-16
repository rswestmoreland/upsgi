#include "upsgi.h"

extern struct upsgi_server upsgi;

// this is like upsgi_str_num but with security checks
static int get_content_length(char *buf, uint16_t size, size_t *value) {
        int i;
        size_t val = 0;

        if (size == 0) {
                return -1;
        }

        for (i = 0; i < size; i++) {
                if (buf[i] < '0' || buf[i] > '9') {
                        return -1;
                }
                if (val > ((SIZE_MAX - (size_t) (buf[i] - '0')) / 10)) {
                        return -1;
                }
                val = (val * 10) + (size_t) (buf[i] - '0');
        }

        *value = val;
        return 0;
}


int upsgi_read_response(int fd, struct upsgi_header *uh, int timeout, char **buf) {

	char *ptr = (char *) uh;
	size_t remains = 4;
	int ret = -1;
	int rlen;
	ssize_t len;

	while (remains > 0) {
		rlen = upsgi_waitfd(fd, timeout);
		if (rlen > 0) {
			len = read(fd, ptr, remains);
			if (len <= 0)
				break;
			remains -= len;
			ptr += len;
			if (remains == 0) {
				ret = uh->modifier2;
				break;
			}
			continue;
		}
		// timed out ?
		else if (ret == 0)
			ret = -2;
		break;
	}

	if (buf && uh->_pktsize > 0) {
		if (*buf == NULL)
			*buf = upsgi_malloc(uh->_pktsize);
		remains = uh->_pktsize;
		ptr = *buf;
		ret = -1;
		while (remains > 0) {
			rlen = upsgi_waitfd(fd, timeout);
			if (rlen > 0) {
				len = read(fd, ptr, remains);
				if (len <= 0)
					break;
				remains -= len;
				ptr += len;
				if (remains == 0) {
					ret = uh->modifier2;
					break;
				}
				continue;
			}
			// timed out ?
			else if (ret == 0)
				ret = -2;
			break;
		}
	}

	return ret;
}

int upsgi_parse_array(char *buffer, uint16_t size, char **argv, uint16_t argvs[], uint8_t * argc) {

	char *ptrbuf, *bufferend;
	uint16_t strsize = 0;

	uint8_t max = *argc;
	*argc = 0;

	ptrbuf = buffer;
	bufferend = ptrbuf + size;

	while (ptrbuf < bufferend && *argc < max) {
		if (ptrbuf + 2 < bufferend) {
			memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
			strsize = upsgi_swap16(strsize);
#endif

			ptrbuf += 2;
			/* item cannot be null */
			if (!strsize)
				continue;

			if (ptrbuf + strsize <= bufferend) {
				// item
				argv[*argc] = upsgi_cheap_string(ptrbuf, strsize);
				argvs[*argc] = strsize;
#ifdef UPSGI_DEBUG
				upsgi_log("arg %s\n", argv[*argc]);
#endif
				ptrbuf += strsize;
				*argc = *argc + 1;
			}
			else {
				upsgi_log("invalid upsgi array. skip this var.\n");
				return -1;
			}
		}
		else {
			upsgi_log("invalid upsgi array. skip this request.\n");
			return -1;
		}
	}


	return 0;
}

int upsgi_simple_parse_vars(struct wsgi_request *wsgi_req, char *ptrbuf, char *bufferend) {

	uint16_t strsize;

	while (ptrbuf < bufferend) {
		if (ptrbuf + 2 < bufferend) {
			memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
			strsize = upsgi_swap16(strsize);
#endif
			/* key cannot be null */
			if (!strsize) {
				upsgi_log("upsgi key cannot be null. skip this request.\n");
				return -1;
			}

			ptrbuf += 2;
			if (ptrbuf + strsize < bufferend) {
				// var key
				wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptrbuf;
				wsgi_req->hvec[wsgi_req->var_cnt].iov_len = strsize;
				ptrbuf += strsize;
				// value can be null (even at the end) so use <=
				if (ptrbuf + 2 <= bufferend) {
					memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
					strsize = upsgi_swap16(strsize);
#endif
					ptrbuf += 2;
					if (ptrbuf + strsize <= bufferend) {

						if (wsgi_req->var_cnt < upsgi.vec_size - (4 + 1)) {
							wsgi_req->var_cnt++;
						}
						else {
							upsgi_log("max vec size reached. skip this header.\n");
							return -1;
						}
						// var value
						wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptrbuf;
						wsgi_req->hvec[wsgi_req->var_cnt].iov_len = strsize;

						if (wsgi_req->var_cnt < upsgi.vec_size - (4 + 1)) {
							wsgi_req->var_cnt++;
						}
						else {
							upsgi_log("max vec size reached. skip this var.\n");
							return -1;
						}
						ptrbuf += strsize;
					}
					else {
						upsgi_log("invalid upsgi request (current strsize: %d). skip.\n", strsize);
						return -1;
					}
				}
				else {
					upsgi_log("invalid upsgi request (current strsize: %d). skip.\n", strsize);
					return -1;
				}
			}
		}
	}

	return 0;
}

#define upsgi_proto_key(x, y) memcmp(x, key, y)

static int upsgi_proto_check_5(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {

	if (!upsgi_proto_key("HTTPS", 5)) {
		wsgi_req->https = buf;
		wsgi_req->https_len = len;
		return 0;
	}

	return 0;
}

static int upsgi_proto_check_9(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {

	if (!upsgi_proto_key("PATH_INFO", 9)) {
		wsgi_req->path_info = buf;
		wsgi_req->path_info_len = len;
		wsgi_req->path_info_pos = wsgi_req->var_cnt + 1;
#ifdef UPSGI_DEBUG
		upsgi_debug("PATH_INFO=%.*s\n", wsgi_req->path_info_len, wsgi_req->path_info);
#endif
		return 0;
	}

	if (!upsgi_proto_key("HTTP_HOST", 9)) {
		wsgi_req->host = buf;
		wsgi_req->host_len = len;
#ifdef UPSGI_DEBUG
		upsgi_debug("HTTP_HOST=%.*s\n", wsgi_req->host_len, wsgi_req->host);
#endif
		return 0;
	}

	return 0;
}

static void upsgi_parse_http_range(char *buf, uint16_t len, enum upsgi_range *parsed, int64_t *from, int64_t *to) {
	*parsed = UPSGI_RANGE_INVALID;
	*from = 0;
	*to = 0;
	uint16_t rlen = 0;
	uint16_t i;
	for(i=0;i<len;i++) {
		if (buf[i] == ',') break;
		rlen++;
	}

	// bytes=X-
	if (rlen < 8) return;
	char *equal = memchr(buf, '=', rlen);
	if (!equal) return;
	if (equal-buf != 5) return;
	if (memcmp(buf, "bytes", 5)) return;
	char *range = equal+1;
	rlen -= 6;
	char *dash = memchr(range, '-', rlen);
	if (!dash) return;
	if (dash != range) {
		*from = upsgi_str_num(range, dash-range);
		if (dash == range+(rlen-1)) {
			/* RFC7233 prefix range
			 * `bytes=start-` is a same as `byte=start-0x7ffffffffffffff`
			 */
			*to = INT64_MAX;
		} else {
			*to = upsgi_str_num(dash+1, rlen - ((dash+1)-range));
		}
		if (*to >= *from) {
			*parsed = UPSGI_RANGE_PARSED;
		} else {
			*from = 0;
			*to = 0;
		}
	} else {
		/* RFC7233 suffix-byte-range-spec: `bytes=-500` */
		*from = -(int64_t)upsgi_str_num(dash+1, rlen - ((dash+1)-range));
		if (*from < 0) {
			*to = INT64_MAX;
			*parsed = UPSGI_RANGE_PARSED;
		}
	}
}

static int upsgi_proto_check_10(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {

	if (upsgi.honour_range && !upsgi_proto_key("HTTP_IF_RANGE", 13)) {
		wsgi_req->if_range = buf;
		wsgi_req->if_range_len = len;
	}

	if (upsgi.honour_range && !upsgi_proto_key("HTTP_RANGE", 10)) {
		upsgi_parse_http_range(buf, len, &wsgi_req->range_parsed,
				&wsgi_req->range_from, &wsgi_req->range_to);
		// set deprecated fields for binary compatibility
		wsgi_req->__range_from = (size_t)wsgi_req->range_from;
		wsgi_req->__range_to = (size_t)wsgi_req->range_to;
		return 0;
	}

	if (upsgi.dynamic_apps && !upsgi_proto_key("UPSGI_FILE", 10)) {
		wsgi_req->file = buf;
		wsgi_req->file_len = len;
		wsgi_req->dynamic = 1;
		return 0;
	}

	if (upsgi.dynamic_apps && !upsgi_proto_key("UPSGI_HOME", 10)) {
		wsgi_req->home = buf;
		wsgi_req->home_len = len;
		return 0;
	}

	return 0;
}

static int upsgi_proto_check_11(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {

	if (!upsgi_proto_key("SCRIPT_NAME", 11)) {
		wsgi_req->script_name = buf;
		wsgi_req->script_name_len = len;
		wsgi_req->script_name_pos = wsgi_req->var_cnt + 1;
#ifdef UPSGI_DEBUG
		upsgi_debug("SCRIPT_NAME=%.*s\n", wsgi_req->script_name_len, wsgi_req->script_name);
#endif
		return 0;
	}

	if (!upsgi_proto_key("REQUEST_URI", 11)) {
		wsgi_req->uri = buf;
		wsgi_req->uri_len = len;
		return 0;
	}

	if (!upsgi_proto_key("REMOTE_USER", 11)) {
		wsgi_req->remote_user = buf;
		wsgi_req->remote_user_len = len;
		return 0;
	}

	if (wsgi_req->host_len == 0 && !upsgi_proto_key("SERVER_NAME", 11)) {
		wsgi_req->host = buf;
		wsgi_req->host_len = len;
#ifdef UPSGI_DEBUG
		upsgi_debug("SERVER_NAME=%.*s\n", wsgi_req->host_len, wsgi_req->host);
#endif
		return 0;
	}

	if (wsgi_req->remote_addr_len == 0 && !upsgi_proto_key("REMOTE_ADDR", 11)) {
		wsgi_req->remote_addr = buf;
                wsgi_req->remote_addr_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_COOKIE", 11)) {
		wsgi_req->cookie = buf;
		wsgi_req->cookie_len = len;
		return 0;
	}


	if (!upsgi_proto_key("UPSGI_APPID", 11)) {
		wsgi_req->appid = buf;
		wsgi_req->appid_len = len;
		return 0;
	}

	if (!upsgi_proto_key("UPSGI_CHDIR", 11)) {
		wsgi_req->chdir = buf;
		wsgi_req->chdir_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_ORIGIN", 11)) {
                wsgi_req->http_origin = buf;
                wsgi_req->http_origin_len = len;
                return 0;
        }

	return 0;
}

static int upsgi_proto_check_12(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (!upsgi_proto_key("QUERY_STRING", 12)) {
		wsgi_req->query_string = buf;
		wsgi_req->query_string_len = len;
		return 0;
	}

	if (!upsgi_proto_key("CONTENT_TYPE", 12)) {
		wsgi_req->content_type = buf;
		wsgi_req->content_type_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_REFERER", 12)) {
                wsgi_req->referer = buf;
                wsgi_req->referer_len = len;
                return 0;
        }

	if (!upsgi_proto_key("UPSGI_SCHEME", 12)) {
		wsgi_req->scheme = buf;
		wsgi_req->scheme_len = len;
		return 0;
	}

	if (upsgi.dynamic_apps && !upsgi_proto_key("UPSGI_SCRIPT", 12)) {
		wsgi_req->script = buf;
		wsgi_req->script_len = len;
		wsgi_req->dynamic = 1;
		return 0;
	}

	if (upsgi.dynamic_apps && !upsgi_proto_key("UPSGI_MODULE", 12)) {
		wsgi_req->module = buf;
		wsgi_req->module_len = len;
		wsgi_req->dynamic = 1;
		return 0;
	}

	if (!upsgi_proto_key("UPSGI_PYHOME", 12)) {
		wsgi_req->home = buf;
		wsgi_req->home_len = len;
		return 0;
	}

	if (!upsgi_proto_key("UPSGI_SETENV", 12)) {
		char *env_value = memchr(buf, '=', len);
		if (env_value) {
			env_value[0] = 0;
			env_value = upsgi_concat2n(env_value + 1, len - ((env_value + 1) - buf), "", 0);
			if (setenv(buf, env_value, 1)) {
				upsgi_error("setenv()");
			}
			free(env_value);
		}
		return 0;
	}
	return 0;
}

static int upsgi_proto_check_13(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (!upsgi_proto_key("DOCUMENT_ROOT", 13)) {
		wsgi_req->document_root = buf;
		wsgi_req->document_root_len = len;
		return 0;
	}
	return 0;
}

static int upsgi_proto_check_14(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (!upsgi_proto_key("REQUEST_METHOD", 14)) {
		wsgi_req->method = buf;
		wsgi_req->method_len = len;
		return 0;
	}

	if (!upsgi_proto_key("CONTENT_LENGTH", 14)) {
		size_t parsed_content_length = 0;
		if (get_content_length(buf, len, &parsed_content_length)) {
			upsgi_log("Invalid CONTENT_LENGTH. skip.\n");
			return -1;
		}
		if (wsgi_req->body_is_chunked) {
			upsgi_log("Invalid request: chunked Transfer-Encoding with Content-Length. skip.\n");
			return -1;
		}
		wsgi_req->post_cl = parsed_content_length;
		wsgi_req->has_content_length = 1;
		if (wsgi_req->post_cl > 0) {
			upsgi_body_sched_note_request(wsgi_req);
		}
		if (upsgi.limit_post) {
			if (wsgi_req->post_cl > upsgi.limit_post) {
				upsgi_log("Invalid (too big) CONTENT_LENGTH. skip.\n");
				return -1;
			}
		}
		return 0;
	}
	if (!upsgi_proto_key("UPSGI_POSTFILE", 14)) {
		char *postfile = upsgi_concat2n(buf, len, "", 0);
		wsgi_req->post_file = fopen(postfile, "r");
		if (!wsgi_req->post_file) {
			upsgi_error_open(postfile);
		}
		free(postfile);
		return 0;
	}

	if (upsgi.dynamic_apps && !upsgi_proto_key("UPSGI_CALLABLE", 14)) {
		wsgi_req->callable = buf;
		wsgi_req->callable_len = len;
		wsgi_req->dynamic = 1;
		return 0;
	}

	return 0;
}


static int upsgi_proto_check_15(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (!upsgi_proto_key("SERVER_PROTOCOL", 15)) {
		wsgi_req->protocol = buf;
		wsgi_req->protocol_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_USER_AGENT", 15)) {
		wsgi_req->user_agent = buf;
		wsgi_req->user_agent_len = len;
		return 0;
	}

	return 0;
}

static int upsgi_proto_check_18(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (!upsgi_proto_key("HTTP_AUTHORIZATION", 18)) {
		wsgi_req->authorization = buf;
		wsgi_req->authorization_len = len;
		return 0;
	}

	if (!upsgi_proto_key("UPSGI_TOUCH_RELOAD", 18)) {
		wsgi_req->touch_reload = buf;
		wsgi_req->touch_reload_len = len;
		return 0;
	}

	return 0;
}


/*
 * Request identity preprocessing boundary.
 *
 * Header-driven overrides that affect request logging, such as
 * --log-x-forwarded-for, are applied here while parsing protocol vars. The
 * generic logging core only consumes the resulting request fields.
 */
static int upsgi_proto_check_20(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (upsgi.logging_options.log_x_forwarded_for && !upsgi_proto_key("HTTP_X_FORWARDED_FOR", 20)) {
		wsgi_req->remote_addr = buf;
		wsgi_req->remote_addr_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_X_FORWARDED_SSL", 20)) {
		wsgi_req->https = buf;
                wsgi_req->https_len = len;
	}

	if (!upsgi_proto_key("HTTP_ACCEPT_ENCODING", 20)) {
		wsgi_req->encoding = buf;
		wsgi_req->encoding_len = len;
		return 0;
	}

	return 0;
}

static int upsgi_proto_check_22(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {
	if (!upsgi_proto_key("HTTP_IF_MODIFIED_SINCE", 22)) {
		wsgi_req->if_modified_since = buf;
		wsgi_req->if_modified_since_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_SEC_WEBSOCKET_KEY", 22)) {
		wsgi_req->http_sec_websocket_key = buf;
		wsgi_req->http_sec_websocket_key_len = len;
		return 0;
	}

	if (!upsgi_proto_key("HTTP_X_FORWARDED_PROTO", 22)) {
                wsgi_req->scheme = buf;
                wsgi_req->scheme_len = len;
        }

	if (!upsgi_proto_key("HTTP_TRANSFER_ENCODING", 22)) {
		if (!upsgi_strnicmp(buf, len, "chunked", 7)) {
			if (wsgi_req->has_content_length) {
				upsgi_log("Invalid request: chunked Transfer-Encoding with Content-Length. skip.\n");
				return -1;
			}
                	wsgi_req->body_is_chunked = 1;
			upsgi_body_sched_note_request(wsgi_req);
		}
	}


	return 0;
}

static int upsgi_proto_check_27(struct wsgi_request *wsgi_req, char *key, char *buf, uint16_t len) {

        if (!upsgi_proto_key("HTTP_SEC_WEBSOCKET_PROTOCOL", 27)) {
                wsgi_req->http_sec_websocket_protocol = buf;
                wsgi_req->http_sec_websocket_protocol_len = len;
                return 0;
        }

        return 0;
}


void upsgi_proto_hooks_setup() {
	int i = 0;
	for(i=0;i<UPSGI_PROTO_MAX_CHECK;i++) {
		upsgi.proto_hooks[i] = NULL;
	}

	upsgi.proto_hooks[5] = upsgi_proto_check_5;
	upsgi.proto_hooks[9] = upsgi_proto_check_9;
	upsgi.proto_hooks[10] = upsgi_proto_check_10;
	upsgi.proto_hooks[11] = upsgi_proto_check_11;
	upsgi.proto_hooks[12] = upsgi_proto_check_12;
	upsgi.proto_hooks[13] = upsgi_proto_check_13;
	upsgi.proto_hooks[14] = upsgi_proto_check_14;
	upsgi.proto_hooks[15] = upsgi_proto_check_15;
	upsgi.proto_hooks[18] = upsgi_proto_check_18;
	upsgi.proto_hooks[20] = upsgi_proto_check_20;
	upsgi.proto_hooks[22] = upsgi_proto_check_22;
	upsgi.proto_hooks[27] = upsgi_proto_check_27;
}


/*
 * Request-path ownership for upsgi v1.
 *
 * This file owns request parsing, request identity normalization, and the
 * decision points for static interception before control passes to the PSGI
 * adapter. Actual file validation and emission live in core/static.c.
 */
int upsgi_parse_vars(struct wsgi_request *wsgi_req) {

	char *buffer = wsgi_req->buffer;

	char *ptrbuf, *bufferend;

	uint16_t strsize = 0;
	struct upsgi_dyn_dict *udd;

	ptrbuf = buffer;
	bufferend = ptrbuf + wsgi_req->len;
	int i;

	/* set an HTTP 500 status as default */
	wsgi_req->status = 500;

	// skip if already parsed
	if (wsgi_req->parsed)
		return 0;

	// has the protocol already parsed the request ?
	if (wsgi_req->uri_len > 0) {
		wsgi_req->parsed = 1;
		i = upsgi_simple_parse_vars(wsgi_req, ptrbuf, bufferend);
		if (i == 0)
			goto next;
		return i;
	}

	wsgi_req->parsed = 1;
	wsgi_req->script_name_pos = -1;
	wsgi_req->path_info_pos = -1;

	while (ptrbuf < bufferend) {
		if (ptrbuf + 2 < bufferend) {
			memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
			strsize = upsgi_swap16(strsize);
#endif
			/* key cannot be null */
			if (!strsize) {
				upsgi_log("upsgi key cannot be null. skip this var.\n");
				return -1;
			}

			ptrbuf += 2;
			if (ptrbuf + strsize < bufferend) {
				// var key
				wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptrbuf;
				wsgi_req->hvec[wsgi_req->var_cnt].iov_len = strsize;
				ptrbuf += strsize;
				// value can be null (even at the end) so use <=
				if (ptrbuf + 2 <= bufferend) {
					memcpy(&strsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
					strsize = upsgi_swap16(strsize);
#endif
					ptrbuf += 2;
					if (ptrbuf + strsize <= bufferend) {
						if (wsgi_req->hvec[wsgi_req->var_cnt].iov_len > UPSGI_PROTO_MIN_CHECK &&
							wsgi_req->hvec[wsgi_req->var_cnt].iov_len < UPSGI_PROTO_MAX_CHECK &&
								upsgi.proto_hooks[wsgi_req->hvec[wsgi_req->var_cnt].iov_len]) {
							if (upsgi.proto_hooks[wsgi_req->hvec[wsgi_req->var_cnt].iov_len](wsgi_req, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, ptrbuf, strsize)) {
								return -1;
							}
						}
						//upsgi_log("upsgi %.*s = %.*s\n", wsgi_req->hvec[wsgi_req->var_cnt].iov_len, wsgi_req->hvec[wsgi_req->var_cnt].iov_base, strsize, ptrbuf);

						if (wsgi_req->var_cnt < upsgi.vec_size - (4 + 1)) {
							wsgi_req->var_cnt++;
						}
						else {
							upsgi_log("max vec size reached. skip this var.\n");
							return -1;
						}
						// var value
						wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptrbuf;
						wsgi_req->hvec[wsgi_req->var_cnt].iov_len = strsize;
						//upsgi_log("%.*s = %.*s\n", wsgi_req->hvec[wsgi_req->var_cnt-1].iov_len, wsgi_req->hvec[wsgi_req->var_cnt-1].iov_base, wsgi_req->hvec[wsgi_req->var_cnt].iov_len, wsgi_req->hvec[wsgi_req->var_cnt].iov_base);
						if (wsgi_req->var_cnt < upsgi.vec_size - (4 + 1)) {
							wsgi_req->var_cnt++;
						}
						else {
							upsgi_log("max vec size reached. skip this var.\n");
							return -1;
						}
						ptrbuf += strsize;
					}
					else {
						upsgi_log("invalid upsgi request (current strsize: %d). skip.\n", strsize);
						return -1;
					}
				}
				else {
					upsgi_log("invalid upsgi request (current strsize: %d). skip.\n", strsize);
					return -1;
				}
			}
		}
		else {
			upsgi_log("invalid upsgi request (current strsize: %d). skip.\n", strsize);
			return -1;
		}
	}

next:

	// manage post buffering (if needed as post_file could be created before)
	if (upsgi.post_buffering > 0 && !wsgi_req->post_file) {
		// read to disk if post_cl > post_buffering (it will eventually do upload progress...)
		if (wsgi_req->post_cl >= upsgi.post_buffering) {
			if (upsgi_postbuffer_do_in_disk(wsgi_req)) {
				return -1;
			}
		}
		// on tiny post use memory
		else {
			if (upsgi_postbuffer_do_in_mem(wsgi_req)) {
				return -1;
			}
		}
	}


	if (upsgi.manage_script_name) {
		if (upsgi_apps_cnt > 0 && wsgi_req->path_info_len >= 1 && wsgi_req->path_info_pos != -1) {
			// starts with 1 as the 0 app is the default (/) one
			int best_found = 0;
			char *orig_path_info = wsgi_req->path_info;
			int orig_path_info_len = wsgi_req->path_info_len;
			// if SCRIPT_NAME is not allocated, add a slot for it
			if (wsgi_req->script_name_pos == -1) {
				if (wsgi_req->var_cnt >= upsgi.vec_size - (4 + 2)) {
					upsgi_log("max vec size reached. skip this var.\n");
					return -1;
				}
				wsgi_req->hvec[wsgi_req->var_cnt].iov_base = "SCRIPT_NAME";
				wsgi_req->hvec[wsgi_req->var_cnt].iov_len = 11;
				wsgi_req->var_cnt++;
				wsgi_req->script_name_pos = wsgi_req->var_cnt;
				wsgi_req->hvec[wsgi_req->script_name_pos].iov_base = "";
				wsgi_req->hvec[wsgi_req->script_name_pos].iov_len = 0;
				wsgi_req->var_cnt++;
			}

			for (i = 0; i < upsgi_apps_cnt; i++) {
				char* mountpoint = upsgi_apps[i].mountpoint;
				int mountpoint_len = upsgi_apps[i].mountpoint_len;

				// Ignore trailing mountpoint slashes
				if (mountpoint_len > 0 && mountpoint[mountpoint_len - 1] == '/') {
					mountpoint_len -= 1;
				}

				//upsgi_log("app mountpoint = %.*s\n", upsgi_apps[i].mountpoint_len, upsgi_apps[i].mountpoint);

				// Check if mountpoint could be a possible candidate
				if (orig_path_info_len < mountpoint_len || // it should be shorter than or equal to path_info
					mountpoint_len <= best_found || // it should be better than the previous found
					// should have the same prefix of path_info
					upsgi_startswith(orig_path_info, mountpoint, mountpoint_len) ||
					// and should not be "misleading"
					(orig_path_info_len > mountpoint_len && orig_path_info[mountpoint_len] != '/' )) {
					continue;
				}

				best_found = mountpoint_len;
				wsgi_req->script_name = upsgi_apps[i].mountpoint;
				wsgi_req->script_name_len = upsgi_apps[i].mountpoint_len;
				wsgi_req->path_info = orig_path_info + wsgi_req->script_name_len;
				wsgi_req->path_info_len = orig_path_info_len - wsgi_req->script_name_len;

				wsgi_req->hvec[wsgi_req->script_name_pos].iov_base = wsgi_req->script_name;
				wsgi_req->hvec[wsgi_req->script_name_pos].iov_len = wsgi_req->script_name_len;

				wsgi_req->hvec[wsgi_req->path_info_pos].iov_base = wsgi_req->path_info;
				wsgi_req->hvec[wsgi_req->path_info_pos].iov_len = wsgi_req->path_info_len;
#ifdef UPSGI_DEBUG
				upsgi_log("managed SCRIPT_NAME = %.*s PATH_INFO = %.*s\n", wsgi_req->script_name_len, wsgi_req->script_name, wsgi_req->path_info_len, wsgi_req->path_info);
#endif
			}
		}
	}


	/*
	 * Request/static boundary.
	 *
	 * Static interception is decided in the request/protocol path after request
	 * identity preprocessing and before handing control to the PSGI adapter.
	 * Actual file serving is delegated to core/static.c.
	 */
	/* CHECK FOR STATIC FILES */

	// skip extensions
	struct upsgi_string_list *sse = upsgi.static_skip_ext;
	while (sse) {
		if (wsgi_req->path_info_len >= sse->len) {
			if (!upsgi_strncmp(wsgi_req->path_info + (wsgi_req->path_info_len - sse->len), sse->len, sse->value, sse->len)) {
				return 0;
			}
		}
		sse = sse->next;
	}

	/*
	 * Core-path static interception begins here.
	 *
	 * check_static/check_static_docroot/static-map stay in the request path as
	 * baseline upsgi features. They decide whether a request should be served
	 * directly from the filesystem before the PSGI app sees it.
	 */
	// check if a file named upsgi.check_static+env['PATH_INFO'] exists
	udd = upsgi.check_static;
	while (udd) {
		// need to build the path ?
		if (udd->value == NULL) {
			if (upsgi.threads > 1)
				pthread_mutex_lock(&upsgi.lock_static);
			udd->value = upsgi_malloc(PATH_MAX + 1);
			if (!realpath(udd->key, udd->value)) {
				free(udd->value);
				udd->value = NULL;
			}
			if (upsgi.threads > 1)
				pthread_mutex_unlock(&upsgi.lock_static);
			if (!udd->value)
				goto nextcs;
			udd->vallen = strlen(udd->value);
		}

		if (!upsgi_file_serve(wsgi_req, udd->value, udd->vallen, wsgi_req->path_info, wsgi_req->path_info_len, 0)) {
			return -1;
		}
nextcs:
		udd = udd->next;
	}

	/*
	 * static-map boundary.
	 *
	 * Prefix matching and docroot selection stay here in core/protocol.c.
	 * Once a mapping matches, actual path resolution and file serving are
	 * delegated to upsgi_file_serve() in core/static.c.
	 */
	// check static-map
	udd = upsgi.static_maps;
	while (udd) {
#ifdef UPSGI_DEBUG
		upsgi_log("checking for %.*s <-> %.*s %.*s\n", (int)wsgi_req->path_info_len, wsgi_req->path_info, (int)udd->keylen, udd->key, (int) udd->vallen, udd->value);
#endif
		if (udd->status == 0) {
			if (upsgi.threads > 1)
				pthread_mutex_lock(&upsgi.lock_static);
			char *real_docroot = upsgi_malloc(PATH_MAX + 1);
			if (!realpath(udd->value, real_docroot)) {
				free(real_docroot);
				real_docroot = NULL;
				udd->value = NULL;
			}
			if (upsgi.threads > 1)
				pthread_mutex_unlock(&upsgi.lock_static);
			if (!real_docroot)
				goto nextsm;
			udd->value = real_docroot;
			udd->vallen = strlen(udd->value);
			udd->status = 1 + upsgi_is_file(real_docroot);
		}

		if (!upsgi_starts_with(wsgi_req->path_info, wsgi_req->path_info_len, udd->key, udd->keylen)) {
			if (!upsgi_file_serve(wsgi_req, udd->value, udd->vallen, wsgi_req->path_info + udd->keylen, wsgi_req->path_info_len - udd->keylen, udd->status - 1)) {
				return -1;
			}
		}
nextsm:
		udd = udd->next;
	}

	/*
	 * Append-mode static maps are the same baseline interception pattern with a
	 * different request-to-docroot join strategy. They still hand off actual
	 * filesystem validation and response emission to core/static.c.
	 */
	// check for static_maps in append mode
	udd = upsgi.static_maps2;
	while (udd) {
#ifdef UPSGI_DEBUG
		upsgi_log("checking for %.*s <-> %.*s\n", wsgi_req->path_info_len, wsgi_req->path_info, udd->keylen, udd->key);
#endif
		if (udd->status == 0) {
			if (upsgi.threads > 1)
				pthread_mutex_lock(&upsgi.lock_static);
			char *real_docroot = upsgi_malloc(PATH_MAX + 1);
			if (!realpath(udd->value, real_docroot)) {
				free(real_docroot);
				real_docroot = NULL;
				udd->value = NULL;
			}
			if (upsgi.threads > 1)
				pthread_mutex_unlock(&upsgi.lock_static);
			if (!real_docroot)
				goto nextsm2;
			udd->value = real_docroot;
			udd->vallen = strlen(udd->value);
			udd->status = 1 + upsgi_is_file(real_docroot);
		}

		if (!upsgi_starts_with(wsgi_req->path_info, wsgi_req->path_info_len, udd->key, udd->keylen)) {
			if (!upsgi_file_serve(wsgi_req, udd->value, udd->vallen, wsgi_req->path_info, wsgi_req->path_info_len, udd->status - 1)) {
				return -1;
			}
		}
nextsm2:
		udd = udd->next;
	}


	/*
	 * check_static_docroot is the final core-path static interception fallback
	 * before the request proceeds to PSGI. Missing files fall through to the
	 * application path rather than being treated as a router_static concern.
	 */
	// finally check for docroot
	if (upsgi.check_static_docroot && wsgi_req->document_root_len > 0) {
		char real_docroot[PATH_MAX + 1];
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].static_realpath_calls++;
		if (!upsgi_expand_path(wsgi_req->document_root, wsgi_req->document_root_len, real_docroot)) {
			return -1;
		}
		if (!upsgi_file_serve(wsgi_req, real_docroot, strlen(real_docroot), wsgi_req->path_info, wsgi_req->path_info_len, 0)) {
			return -1;
		}
	}

	return 0;
}

int upsgi_hooked_parse(char *buffer, size_t len, void (*hook) (char *, uint16_t, char *, uint16_t, void *), void *data) {

	char *ptrbuf, *bufferend;
	uint16_t keysize = 0, valsize = 0;
	char *key;

	ptrbuf = buffer;
	bufferend = buffer + len;

	while (ptrbuf < bufferend) {
		if (ptrbuf + 2 >= bufferend)
			return -1;
		memcpy(&keysize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
		keysize = upsgi_swap16(keysize);
#endif
		/* key cannot be null */
		if (!keysize)
			return -1;

		ptrbuf += 2;
		if (ptrbuf + keysize > bufferend)
			return -1;

		// key
		key = ptrbuf;
		ptrbuf += keysize;
		// value can be null
		if (ptrbuf + 2 > bufferend)
			return -1;

		memcpy(&valsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
		valsize = upsgi_swap16(valsize);
#endif
		ptrbuf += 2;
		if (ptrbuf + valsize > bufferend)
			return -1;

		// now call the hook
		hook(key, keysize, ptrbuf, valsize, data);
		ptrbuf += valsize;
	}

	return 0;

}

int upsgi_hooked_parse_array(char *buffer, size_t len, void (*hook) (uint16_t, char *, uint16_t, void *), void *data) {

        char *ptrbuf, *bufferend;
        uint16_t valsize = 0;
        char *value;
	uint16_t pos = 0;

        ptrbuf = buffer;
        bufferend = buffer + len;

        while (ptrbuf < bufferend) {
                if (ptrbuf + 2 > bufferend)
                        return -1;
                memcpy(&valsize, ptrbuf, 2);
#ifdef __BIG_ENDIAN__
                valsize = upsgi_swap16(valsize);
#endif
                ptrbuf += 2;
                if (ptrbuf + valsize > bufferend)
                        return -1;

                // key
                value = ptrbuf;
                // now call the hook
                hook(pos, value, valsize, data);
                ptrbuf += valsize;
		pos++;
        }

        return 0;

}


// this functions transform a raw HTTP response to a upsgi-managed response
int upsgi_blob_to_response(struct wsgi_request *wsgi_req, char *body, size_t len) {
	char *line = body;
	size_t line_len = 0;
	size_t i;
	int status_managed = 0;
	for(i=0;i<len;i++) {
		if (body[i] == '\n') {
			// invalid line
			if (line_len < 1) {
				return -1;
			}
			if (line[line_len-1] != '\r') {
				return -1;
			}
			// end of the headers
			if (line_len == 1) {
				break;
			}

			if (status_managed) {
				char *colon = memchr(line, ':', line_len-1);
				if (!colon) return -1;
				if (colon[1] != ' ') return -1;
				if (upsgi_response_add_header(wsgi_req, line, colon-line, colon+2, (line_len-1) - ((colon+2)-line))) return -1;
			}
			else {
				char *space = memchr(line, ' ', line_len-1);
				if (!space) return -1;
				if ((line_len-1) - ((space+1)-line) < 3) return -1;
				if (upsgi_response_prepare_headers(wsgi_req, space+1, (line_len-1) - ((space+1)-line))) return -1;
				status_managed = 1;
			}
			line = NULL;
			line_len = 0;
		}
		else {
			if (!line) {
				line = body + i;
			}
			line_len++;
		}
	}

	if ((i+1) < len) {
		if (upsgi_response_write_body_do(wsgi_req, body + (i + 1), len-(i+1))) {
			return -1;
		}
	}

	return 0;
}

/*

the following functions need to take in account that POST data could be already available in wsgi_req->buffer (generally when upsgi protocol is in use)

In such a case, allocate a proto_parser_buf and move data there

*/

char *upsgi_req_append(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, char *val, uint16_t vallen) {

	if (!wsgi_req->proto_parser_buf) {
		if (wsgi_req->proto_parser_remains > 0) {
			wsgi_req->proto_parser_buf = upsgi_malloc(wsgi_req->proto_parser_remains);
			memcpy(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_remains_buf, wsgi_req->proto_parser_remains);
			wsgi_req->proto_parser_remains_buf = wsgi_req->proto_parser_buf;
		}
	}

	if ((wsgi_req->len + (2 + keylen + 2 + vallen)) > upsgi.buffer_size) {
		upsgi_log("not enough buffer space to add %.*s variable, consider increasing it with the --buffer-size option\n", keylen, key);
		return NULL;
	}

	if (wsgi_req->var_cnt >= upsgi.vec_size - (4 + 2)) {
        	upsgi_log("max vec size reached. skip this header.\n");
		return NULL;
	}

	char *ptr = wsgi_req->buffer + wsgi_req->len;

	*ptr++ = (uint8_t) (keylen & 0xff);
	*ptr++ = (uint8_t) ((keylen >> 8) & 0xff);

	memcpy(ptr, key, keylen);
	wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptr;
        wsgi_req->hvec[wsgi_req->var_cnt].iov_len = keylen;
	wsgi_req->var_cnt++;
	ptr += keylen;



	*ptr++ = (uint8_t) (vallen & 0xff);
	*ptr++ = (uint8_t) ((vallen >> 8) & 0xff);

	memcpy(ptr, val, vallen);
	wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptr;
        wsgi_req->hvec[wsgi_req->var_cnt].iov_len = vallen;
	wsgi_req->var_cnt++;

	wsgi_req->len += (2 + keylen + 2 + vallen);

	return ptr;
}

int upsgi_req_append_path_info_with_index(struct wsgi_request *wsgi_req, char *index, uint16_t index_len) {

	if (!wsgi_req->proto_parser_buf) {
                if (wsgi_req->proto_parser_remains > 0) {
                        wsgi_req->proto_parser_buf = upsgi_malloc(wsgi_req->proto_parser_remains);
                        memcpy(wsgi_req->proto_parser_buf, wsgi_req->proto_parser_remains_buf, wsgi_req->proto_parser_remains);
                        wsgi_req->proto_parser_remains_buf = wsgi_req->proto_parser_buf;
                }
        }

	uint8_t need_slash = 0;
	if (wsgi_req->path_info_len > 0) {
		if (wsgi_req->path_info[wsgi_req->path_info_len-1] != '/') {
			need_slash = 1;
		}
	}

	wsgi_req->path_info_len += need_slash + index_len;

	// 2 + 9 + 2
	if ((wsgi_req->len + (13 + wsgi_req->path_info_len)) > upsgi.buffer_size) {
                upsgi_log("not enough buffer space to transform the PATH_INFO variable, consider increasing it with the --buffer-size option\n");
                return -1;
        }

	if (wsgi_req->var_cnt >= upsgi.vec_size - (4 + 2)) {
                upsgi_log("max vec size reached for PATH_INFO + index. skip this request.\n");
                return -1;
        }

	uint16_t keylen = 9;
	char *ptr = wsgi_req->buffer + wsgi_req->len;
	*ptr++ = (uint8_t) (keylen & 0xff);
        *ptr++ = (uint8_t) ((keylen >> 8) & 0xff);

	memcpy(ptr, "PATH_INFO", keylen);
	wsgi_req->hvec[wsgi_req->var_cnt].iov_base = ptr;
	wsgi_req->hvec[wsgi_req->var_cnt].iov_len = keylen;
	wsgi_req->var_cnt++;
        ptr += keylen;

	*ptr++ = (uint8_t) (wsgi_req->path_info_len & 0xff);
        *ptr++ = (uint8_t) ((wsgi_req->path_info_len >> 8) & 0xff);

	char *new_path_info = ptr;

	memcpy(ptr, wsgi_req->path_info, wsgi_req->path_info_len - (need_slash + index_len));
	ptr+=wsgi_req->path_info_len - (need_slash + index_len);
	if (need_slash) {
		*ptr ++= '/';
	}
	memcpy(ptr, index, index_len);

	wsgi_req->hvec[wsgi_req->var_cnt].iov_base = new_path_info;
        wsgi_req->hvec[wsgi_req->var_cnt].iov_len = wsgi_req->path_info_len;
        wsgi_req->var_cnt++;

	wsgi_req->len += 13 + wsgi_req->path_info_len;
	wsgi_req->path_info = new_path_info;

	return 0;
}
