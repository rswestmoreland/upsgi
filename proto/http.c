/* async http protocol parser */

#include "upsgi.h"

extern struct upsgi_server upsgi;

int http_status_code(char *buf, int len) {
	char *p = buf;

	for (;;) {
		if (len < 11) return -1;
		if (len >= 2 && *p == '\r' && *(p+1) == '\n') return -1;
		if (memcmp(p, "Status:", 7)) {
			char *q;
			if ((q = memchr(p, '\r', len)) == NULL)
				return -1;
			if ((q+1) >= (p+len))
				return -1;
			if (q[1] != '\n')
				return -1;
			len -= (q - p) + 2;
			p = q + 2;
		}
		p += 8;
		len -= 8;
		while (len > 0 && isspace((unsigned char) *p)) {
			p++;
			len--;
		}
		if (len <= 0) return -1;
		return atoi(p);
	}

	return -1;
}

static int http_header_name_eq(char *key, size_t keylen, const char *normalized, size_t normalized_len) {
	size_t i;
	if (keylen != normalized_len) return 0;
	for (i = 0; i < keylen; i++) {
		char c = key[i];
		if (c >= 'a' && c <= 'z') c = (char) (c - 32);
		if (c == '-') c = '_';
		if (c != normalized[i]) return 0;
	}
	return 1;
}

static void http_header_normalize_name(char *key, size_t keylen, char *dst) {
	size_t i;
	for (i = 0; i < keylen; i++) {
		char c = key[i];
		if (c >= 'a' && c <= 'z') c = (char) (c - 32);
		if (c == '-') c = '_';
		dst[i] = c;
	}
	dst[keylen] = 0;
}

static int http_header_allow_duplicate_merge(size_t keylen, char *key) {
	if (keylen == 6 && http_header_name_eq(key, keylen, "ACCEPT", 6)) return 1;
	if (keylen == 14 && http_header_name_eq(key, keylen, "ACCEPT_CHARSET", 14)) return 1;
	if (keylen == 15 && http_header_name_eq(key, keylen, "ACCEPT_ENCODING", 15)) return 1;
	if (keylen == 15 && http_header_name_eq(key, keylen, "ACCEPT_LANGUAGE", 15)) return 1;
	if (keylen == 13 && http_header_name_eq(key, keylen, "CACHE_CONTROL", 13)) return 1;
	if (keylen == 10 && http_header_name_eq(key, keylen, "CONNECTION", 10)) return 1;
	if (keylen == 9 && http_header_name_eq(key, keylen, "FORWARDED", 9)) return 1;
	if (keylen == 8 && http_header_name_eq(key, keylen, "IF_MATCH", 8)) return 1;
	if (keylen == 13 && http_header_name_eq(key, keylen, "IF_NONE_MATCH", 13)) return 1;
	if (keylen == 6 && http_header_name_eq(key, keylen, "PRAGMA", 6)) return 1;
	if (keylen == 2 && http_header_name_eq(key, keylen, "TE", 2)) return 1;
	if (keylen == 7 && http_header_name_eq(key, keylen, "TRAILER", 7)) return 1;
	if (keylen == 7 && http_header_name_eq(key, keylen, "UPGRADE", 7)) return 1;
	if (keylen == 3 && http_header_name_eq(key, keylen, "VIA", 3)) return 1;
	if (keylen == 7 && http_header_name_eq(key, keylen, "WARNING", 7)) return 1;
	if (keylen == 15 && http_header_name_eq(key, keylen, "X_FORWARDED_FOR", 15)) return 1;
	return 0;
}

static int http_header_reject_duplicate(size_t keylen, char *key) {
	if (keylen == 4 && http_header_name_eq(key, keylen, "HOST", 4)) return 1;
	if (keylen == 14 && http_header_name_eq(key, keylen, "CONTENT_LENGTH", 14)) return 1;
	if (keylen == 12 && http_header_name_eq(key, keylen, "CONTENT_TYPE", 12)) return 1;
	if (keylen == 17 && http_header_name_eq(key, keylen, "TRANSFER_ENCODING", 17)) return 1;
	if (http_header_allow_duplicate_merge(keylen, key)) return 0;
	return 1;
}

static int http_header_value_has_token(char *value, size_t value_len, const char *token, size_t token_len) {
	size_t i = 0;
	while (i < value_len) {
		while (i < value_len && (value[i] == ' ' || value[i] == '\t' || value[i] == ',')) i++;
		if (i >= value_len) break;
		size_t start = i;
		while (i < value_len && value[i] != ',' && value[i] != ';') i++;
		size_t end = i;
		while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) end--;
		if (end - start == token_len && !upsgi_strnicmp(value + start, token_len, (char *) token, token_len)) {
			return 1;
		}
		while (i < value_len && value[i] != ',') i++;
	}
	return 0;
}

static int http_header_value_has_chunked_token(char *value, size_t value_len) {
	return http_header_value_has_token(value, value_len, "chunked", 7);
}

static int upsgi_http11_request_wants_close(struct wsgi_request *wsgi_req) {
	uint16_t connection_len = 0;
	char *connection = upsgi_get_var(wsgi_req, (char *) "HTTP_CONNECTION", 15, &connection_len);
	if (!connection || connection_len == 0) return 0;
	return http_header_value_has_token(connection, connection_len, "close", 5);
}

static int upsgi_http_response_has_connection_header(struct wsgi_request *wsgi_req) {
	if (!wsgi_req->headers || wsgi_req->headers->pos == 0) return 0;
	char *buf = wsgi_req->headers->buf;
	size_t pos = 0;
	while (pos + 1 < wsgi_req->headers->pos) {
		char *line = buf + pos;
		char *eol = memchr(line, '\n', wsgi_req->headers->pos - pos);
		if (!eol) break;
		size_t line_len = (size_t) (eol - line);
		if (line_len > 0 && line[line_len - 1] == '\r') line_len--;
		pos = (size_t) ((eol - buf) + 1);
		if (line_len == 0) break;
		if (memchr(line, ':', line_len) == NULL) continue;
		if (line_len >= 11 && !upsgi_strnicmp(line, 10, (char *) "Connection", 10) && line[10] == ':') {
			return 1;
		}
	}
	return 0;
}

static int http_path_needs_decode(char *path, uint16_t path_len) {
	uint16_t i;
	if (path_len == 0) return 0;
	if (path[0] != '/') return 1;
	for (i = 0; i < path_len; i++) {
		if (path[i] == '%' || path[i] == '#') return 1;
		if (path[i] != '/') continue;
		if (i + 1 >= path_len) continue;
		if (path[i + 1] == '/') return 1;
		if (path[i + 1] != '.') continue;
		if (i + 2 == path_len) return 1;
		if (path[i + 2] == '/') return 1;
		if (path[i + 2] == '.' && (i + 3 == path_len || path[i + 3] == '/')) return 1;
	}
	return 0;
}

static int http_add_path_info(struct wsgi_request *wsgi_req, char *path, uint16_t path_len) {
	if (!http_path_needs_decode(path, path_len)) {
		wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "PATH_INFO", 9, path, path_len);
		return 0;
	}
	char *decoded_path = upsgi_malloc(path_len);
	uint16_t decoded_path_len = path_len;
	http_url_decode4(path, &decoded_path_len, decoded_path, upsgi.http_path_info_no_decode_slashes);
	wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "PATH_INFO", 9, decoded_path, decoded_path_len);
	free(decoded_path);
	return 0;
}

static void upsgi_http_setup_server_port_cache(struct upsgi_socket *upsgi_sock) {
	char *server_port = NULL;
	upsgi_sock->server_port = NULL;
	upsgi_sock->server_port_len = 0;
	if (!upsgi_sock->name) return;
	server_port = strrchr(upsgi_sock->name, ':');
	if (!server_port || !server_port[1]) return;
	upsgi_sock->server_port = server_port + 1;
	upsgi_sock->server_port_len = strlen(server_port + 1);
}

static char * http_header_to_cgi(char *hh, size_t hhlen, size_t *keylen, size_t *vallen, int *has_prefix) {
	size_t i;
	char *val = NULL;
	int status = 0;
	*keylen = 0;
	*vallen = 0;
	for (i = 0; i < hhlen; i++) {
		if (!status) {
			if (hh[i] == ':') {
				status = 1;
				*keylen = i;
			}
		}
		else if (status == 1 && hh[i] != ' ') {
			status = 2;
			val = hh + i;
			*vallen = 1;
		}
		else if (status == 2) {
			*vallen += 1;
		}
	}

	if (!(*keylen) || !val)
		return NULL;

	if (upsgi_strnicmp(hh, *keylen, (char *) "Content-Length", 14) && upsgi_strnicmp(hh, *keylen, (char *) "Content-Type", 12)) {
		*has_prefix = 0x02;
	}

	return val;
}


static uint64_t http_add_upsgi_header(struct wsgi_request *wsgi_req, char *hh, size_t hhlen, char *hv, size_t hvlen, int has_prefix) {

	char *buffer = wsgi_req->buffer + wsgi_req->len;
	char *watermark = wsgi_req->buffer + upsgi.buffer_size;
	char *ptr = buffer;
	size_t keylen = hhlen;
	size_t i;

	if (has_prefix) keylen += 5;

	if (buffer + keylen + hvlen + 2 + 2 >= watermark) {
		if (keylen <= 0xff && hvlen <= 0xff) {
			if (has_prefix) {
				upsgi_log("[WARNING] unable to add HTTP_%.*s=%.*s to upsgi packet, consider increasing buffer size\n", (int) hhlen, hh, (int) hvlen, hv);
			}
			else {
				upsgi_log("[WARNING] unable to add %.*s=%.*s to upsgi packet, consider increasing buffer size\n", (int) hhlen, hh, (int) hvlen, hv);
			}
		}
		else {
			upsgi_log("[WARNING] unable to build upsgi packet from http request, consider increasing buffer size\n");
		}
		return 0;
	}

	*ptr++ = (uint8_t) (keylen & 0xff);
	*ptr++ = (uint8_t) ((keylen >> 8) & 0xff);

	if (has_prefix) {
		memcpy(ptr, "HTTP_", 5);
		ptr += 5;
	}
	for (i = 0; i < hhlen; i++) {
		char c = hh[i];
		if (c >= 'a' && c <= 'z') c = (char) (c - 32);
		if (c == '-') c = '_';
		*ptr++ = c;
	}

	*ptr++ = (uint8_t) (hvlen & 0xff);
	*ptr++ = (uint8_t) ((hvlen >> 8) & 0xff);
	memcpy(ptr, hv, hvlen);

	return 2 + keylen + 2 + hvlen;
}

typedef struct http_header_slot {
	char *key;
	size_t key_len;
	char *value;
	size_t value_len;
	int has_prefix;
	uint8_t owns_value;
} http_header_slot;

typedef struct http_header_table {
	http_header_slot *slots;
	size_t pos;
	size_t size;
} http_header_table;

static int http_header_table_reserve(http_header_table *table) {
	if (table->pos < table->size) return 0;
	size_t new_size = table->size ? table->size * 2 : 16;
	http_header_slot *new_slots = realloc(table->slots, sizeof(http_header_slot) * new_size);
	if (!new_slots) {
		upsgi_error("realloc()");
		return -1;
	}
	table->slots = new_slots;
	table->size = new_size;
	return 0;
}

static http_header_slot *http_header_table_find(http_header_table *table, char *key, size_t key_len) {
	size_t i;
	for (i = 0; i < table->pos; i++) {
		http_header_slot *slot = &table->slots[i];
		if (slot->key_len == key_len && !upsgi_strnicmp(slot->key, slot->key_len, key, key_len)) {
			return slot;
		}
	}
	return NULL;
}

static void http_header_table_free(http_header_table *table) {
	size_t i;
	for (i = 0; i < table->pos; i++) {
		if (table->slots[i].owns_value) {
			free(table->slots[i].value);
		}
	}
	free(table->slots);
	table->slots = NULL;
	table->pos = 0;
	table->size = 0;
}


char *proxy1_parse(char *ptr, char *watermark, char **src, uint16_t *src_len, char **dst, uint16_t *dst_len,  char **src_port, uint16_t *src_port_len, char **dst_port, uint16_t *dst_port_len) {
	// check for PROXY header
	if (watermark - ptr > 6) {
		if (memcmp(ptr, "PROXY ", 6)) return ptr;
	}
	else {
		return ptr;
	}

	ptr+= 6;
	char *base = ptr;
	while (ptr < watermark) {
		if (*ptr == ' ') {
                        ptr++;
                        break;
                }
		else if (*ptr == '\n') {
			return ptr+1;
		}
                ptr++;
	}

	// SRC address
	base = ptr;
	while (ptr < watermark) {
		if (*ptr == ' ') {
			*src = base;
			*src_len = ptr - base;
                        ptr++;
                        break;
                }
                else if (*ptr == '\n') {
                        return ptr+1;
                }
                ptr++;
	}

	// DST address
        base = ptr;
        while (ptr < watermark) {
                if (*ptr == ' ') {
                        *dst = base;
                        *dst_len = ptr - base;
                        ptr++;
                        break;
                }
                else if (*ptr == '\n') {
                        return ptr+1;
                }
                ptr++;
        }

	// SRC port
        base = ptr;
        while (ptr < watermark) {
                if (*ptr == ' ') {
                        *src_port = base;
                        *src_port_len = ptr - base;
                        ptr++;
                        break;
                }
                else if (*ptr == '\n') {
                        return ptr+1;
                }
                ptr++;
        }

        // DST port
        base = ptr;
        while (ptr < watermark) {
                if (*ptr == '\r') {
                        *dst_port = base;
                        *dst_port_len = ptr - base;
                        ptr++;
                        break;
                }
                else if (*ptr == '\n') {
                        return ptr+1;
                }
                ptr++;
        }

	// check for \n
	while (ptr < watermark) {
		if (*ptr == '\n') return ptr+1;
		ptr++;
	}

	return ptr;
}

static int http_parse(struct wsgi_request *wsgi_req, char *watermark) {

	char *ptr = wsgi_req->proto_parser_buf;
	char *base = ptr;
	char *query_string = NULL;
	char ip[INET6_ADDRSTRLEN+1];
	struct sockaddr *http_sa = (struct sockaddr *) &wsgi_req->client_addr;
	char *proxy_src = NULL;
	char *proxy_dst = NULL;
	char *proxy_src_port = NULL;
	char *proxy_dst_port = NULL;
	uint16_t proxy_src_len = 0;
	uint16_t proxy_dst_len = 0;
	uint16_t proxy_src_port_len = 0;
	uint16_t proxy_dst_port_len = 0;

	if (upsgi.enable_proxy_protocol) {
		ptr = proxy1_parse(ptr, watermark, &proxy_src, &proxy_src_len, &proxy_dst, &proxy_dst_len, &proxy_src_port, &proxy_src_port_len, &proxy_dst_port, &proxy_dst_port_len);
		base = ptr;
	}

	// REQUEST_METHOD 
	while (ptr < watermark) {
		if (*ptr == ' ') {
			size_t method_len = ptr - base;
			wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REQUEST_METHOD", 14, base, method_len);
			if (method_len == 4 && !upsgi_strncmp("HEAD", 4, base, method_len)) {
				wsgi_req->ignore_body = 1;
			}
			ptr++;
			break;
		}
		ptr++;
	}

	// REQUEST_URI / PATH_INFO / QUERY_STRING
	base = ptr;
	while (ptr < watermark) {
		if (*ptr == '?' && !query_string) {
			if (http_add_path_info(wsgi_req, base, ptr - base)) return -1;
			query_string = ptr + 1;
		}
		else if (*ptr == ' ') {
			wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REQUEST_URI", 11, base, ptr - base);
			if (!query_string) {
				if (http_add_path_info(wsgi_req, base, ptr - base)) return -1;
				wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "QUERY_STRING", 12, "", 0);
			}
			else {
				wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "QUERY_STRING", 12, query_string, ptr - query_string);
			}
			ptr++;
			break;
		}
		ptr++;
	}
	// SERVER_PROTOCOL
	base = ptr;
	while (ptr < watermark) {
		if (*ptr == '\r') {
			if (ptr + 1 >= watermark)
				return -1 ;
			if (*(ptr + 1) != '\n')
				return -1;
			if (ptr - base > 0xffff) return -1;
			wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SERVER_PROTOCOL", 15, base, ptr - base);
			ptr += 2;
			break;
		}
		ptr++;
	}

	// SCRIPT_NAME
	if (!upsgi.manage_script_name) {
		wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SCRIPT_NAME", 11, "", 0);
	}
	

	// SERVER_NAME
	wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SERVER_NAME", 11, upsgi.hostname, upsgi.hostname_len);

	// SERVER_PORT / SERVER_ADDR
	if (proxy_dst) {
		wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SERVER_ADDR", 11, proxy_dst, proxy_dst_len);
                if (proxy_dst_port) {
                        wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SERVER_PORT", 11, proxy_dst_port, proxy_dst_port_len);
                }
	}
	else {
		if (wsgi_req->socket->server_port && wsgi_req->socket->server_port_len > 0) {
			wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SERVER_PORT", 11, wsgi_req->socket->server_port, wsgi_req->socket->server_port_len);
		}
		else {
			wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "SERVER_PORT", 11, "80", 2);
		}
	}

	// REMOTE_ADDR
	if (proxy_src) {
		wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REMOTE_ADDR", 11, proxy_src, proxy_src_len);
		if (proxy_src_port) {
			wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REMOTE_PORT", 11, proxy_src_port, proxy_src_port_len);
		}
	}
	else {
		// TODO log something useful for AF_UNIX sockets
		switch(http_sa->sa_family) {
		case AF_INET6: {
			memset(ip, 0, sizeof(ip));
			struct sockaddr_in6* http_sin = (struct sockaddr_in6*)http_sa;
			/* check if it's an IPv6-mapped-IPv4 address and, if so,
			 * represent it as an IPv4 address
			 *
			 * these IPv6 macros are defined in POSIX.1-2001.
			 */
			if (IN6_IS_ADDR_V4MAPPED(&http_sin->sin6_addr)) {
				/* just grab the last 4 bytes and pretend they're
				 * IPv4. None of the word/half-word convenience
				 * functions are in POSIX, so just stick to .s6_addr
				 */
				union {
					unsigned char s6[4];
					uint32_t s4;
				} addr_parts;
				memcpy(addr_parts.s6, &http_sin->sin6_addr.s6_addr[12], 4);
				uint32_t in4_addr = addr_parts.s4;
				memset(ip, 0, sizeof(ip));
				if (inet_ntop(AF_INET, (void*)&in4_addr, ip, INET_ADDRSTRLEN)) {
					wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REMOTE_ADDR", 11, ip, strlen(ip));
				} else {
					upsgi_error("inet_ntop()");
					return -1;
				}
			} else {
				if (inet_ntop(AF_INET6, (void *) &http_sin->sin6_addr, ip, INET6_ADDRSTRLEN)) {
					wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REMOTE_ADDR", 11, ip, strlen(ip));
				} else {
					upsgi_error("inet_ntop()");
					return -1;
				}
			}
			}
			break;
		case AF_INET:
		default: {
			struct sockaddr_in* http_sin = (struct sockaddr_in*)http_sa;
			memset(ip, 0, sizeof(ip));
			if (inet_ntop(AF_INET, (void *) &http_sin->sin_addr, ip, INET_ADDRSTRLEN)) {
				wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "REMOTE_ADDR", 11, ip, strlen(ip));
			}
			else {
				upsgi_error("inet_ntop()");
				return -1;
			}
			}
			break;
		}
	}

	if (wsgi_req->https_len > 0) {
		wsgi_req->len += proto_base_add_upsgi_var(wsgi_req, "HTTPS", 5, wsgi_req->https, wsgi_req->https_len);
	}

	//HEADERS
	base = ptr;

	http_header_table headers;
	memset(&headers, 0, sizeof(headers));

	while (ptr < watermark) {
		if (*ptr == '\r') {
			if (ptr + 1 >= watermark)
				return -1;
			if (*(ptr + 1) != '\n')
				return -1;
			// multiline header ?
			if (ptr + 2 < watermark) {
				if (*(ptr + 2) == ' ' || *(ptr + 2) == '\t') {
					ptr += 2;
					continue;
				}
			}
			size_t key_len = 0, value_len = 0;
			int has_prefix = 0;
			if (ptr - base == 0) break;
			if (ptr - base > 0xffff) return -1;
			char *value = http_header_to_cgi(base, ptr - base, &key_len, &value_len, &has_prefix);
			if (!value) {
				upsgi_log_verbose("invalid HTTP request\n");
				goto clear;
			}
			http_header_slot *slot = http_header_table_find(&headers, base, key_len);
			if (slot && http_header_reject_duplicate(key_len, base)) {
				char normalized_name[key_len + 1];
				http_header_normalize_name(base, key_len, normalized_name);
				upsgi_log("invalid HTTP request: duplicate %s header\n", normalized_name);
				goto clear;
			}
			if (slot) {
				char *old_value = slot->value;
				slot->value = upsgi_concat3n(old_value, slot->value_len, ", ", 2, value, value_len);
				if (!slot->value) goto clear;
				slot->value_len += 2 + value_len;
				if (slot->owns_value) free(old_value);
				slot->owns_value = 1;
			}
			else {
				if (http_header_table_reserve(&headers)) goto clear;
				slot = &headers.slots[headers.pos++];
				slot->key = base;
				slot->key_len = key_len;
				slot->value = value;
				slot->value_len = value_len;
				slot->has_prefix = has_prefix;
				slot->owns_value = 0;
			}
			ptr++;
			base = ptr + 1;
		}
		ptr++;
	}

	int broken = 0;
	int saw_content_length = 0;
	int saw_chunked_transfer_encoding = 0;
	size_t i;
	for (i = 0; i < headers.pos; i++) {
		http_header_slot *slot = &headers.slots[i];
		if (!broken) {
			if (slot->key_len == 14 && !upsgi_strnicmp(slot->key, slot->key_len, (char *) "Content-Length", 14)) {
				if (saw_chunked_transfer_encoding) {
					upsgi_log("invalid HTTP request: chunked Transfer-Encoding with Content-Length\n");
					broken = 1;
				}
				saw_content_length = 1;
			}
			else if (slot->key_len == 17 && !upsgi_strnicmp(slot->key, slot->key_len, (char *) "Transfer-Encoding", 17) && http_header_value_has_chunked_token(slot->value, slot->value_len)) {
				if (saw_content_length) {
					upsgi_log("invalid HTTP request: chunked Transfer-Encoding with Content-Length\n");
					broken = 1;
				}
				saw_chunked_transfer_encoding = 1;
			}
			if (!broken) {
				uint64_t old_len = wsgi_req->len;
				wsgi_req->len += http_add_upsgi_header(wsgi_req, slot->key, slot->key_len, slot->value, slot->value_len, slot->has_prefix & 0x02);
				if (old_len == wsgi_req->len) {
					broken = 1;
				}
			}
		}
	}

	http_header_table_free(&headers);
	return broken;

clear:
	http_header_table_free(&headers);
	return -1;
}



static int upsgi_proto_http_parser(struct wsgi_request *wsgi_req) {

	ssize_t j;
	char *ptr;

	// first round ? (wsgi_req->proto_parser_buf is freed at the end of the request)
	if (!wsgi_req->proto_parser_buf) {
		wsgi_req->proto_parser_buf = upsgi_malloc(upsgi.buffer_size);
	}

	if (upsgi.buffer_size - wsgi_req->proto_parser_pos == 0) {
		upsgi_log("invalid HTTP request size (max %u)...skip\n", upsgi.buffer_size);
		return -1;
	}

	ssize_t len = read(wsgi_req->fd, wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos, upsgi.buffer_size - wsgi_req->proto_parser_pos);
	if (len > 0) {
		goto parse;
	}
	if (len < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) {
                	return UPSGI_AGAIN;
                }
		upsgi_error("upsgi_proto_http_parser()");
		return -1;
	}
	// mute on 0 len...
	if (wsgi_req->proto_parser_pos > 0) {
		upsgi_log("upsgi_proto_http_parser() -> client closed connection\n");
	}
	return -1;

parse:
	ptr = wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos;
	wsgi_req->proto_parser_pos += len;

	for (j = 0; j < len; j++) {
		if (*ptr == '\r' && (wsgi_req->proto_parser_status == 0 || wsgi_req->proto_parser_status == 2)) {
			wsgi_req->proto_parser_status++;
		}
		else if (*ptr == '\r') {
			wsgi_req->proto_parser_status = 1;
		}
		else if (*ptr == '\n' && wsgi_req->proto_parser_status == 1) {
			wsgi_req->proto_parser_status = 2;
		}
		else if (*ptr == '\n' && wsgi_req->proto_parser_status == 3) {
			ptr++;
			wsgi_req->proto_parser_remains = len - (j + 1);
			if (wsgi_req->proto_parser_remains > 0) {
				wsgi_req->proto_parser_remains_buf = (wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos) - wsgi_req->proto_parser_remains;
			}
			if (http_parse(wsgi_req, ptr)) return -1;
			wsgi_req->uh->modifier1 = upsgi.http_modifier1;
			wsgi_req->uh->modifier2 = upsgi.http_modifier2;
			return UPSGI_OK;
		}
		else {
			wsgi_req->proto_parser_status = 0;
		}
		ptr++;
	}

	return UPSGI_AGAIN;
}

static void upsgi_httpize_var(char *buf, size_t len) {
	size_t i;
	int upper = 1;
	for(i=0;i<len;i++) {
		if (upper) {
			upper = 0;
			continue;
		}

		if (buf[i] == '_') {
			buf[i] = '-';
			upper = 1;
			continue;
		}

		buf[i] = tolower((unsigned char) buf[i]);
	}
}

struct upsgi_buffer *upsgi_to_http_dumb(struct wsgi_request *wsgi_req, char *host, uint16_t host_len, char *uri, uint16_t uri_len) {

        struct upsgi_buffer *ub = upsgi_buffer_new(4096);

        if (upsgi_buffer_append(ub, wsgi_req->method, wsgi_req->method_len)) goto clear;
        if (upsgi_buffer_append(ub, " ", 1)) goto clear;

        if (uri_len && uri) {
                if (upsgi_buffer_append(ub, uri, uri_len)) goto clear;
        }
        else {
                if (upsgi_buffer_append(ub, wsgi_req->uri, wsgi_req->uri_len)) goto clear;
        }

        if (upsgi_buffer_append(ub, " ", 1)) goto clear;
        if (upsgi_buffer_append(ub, wsgi_req->protocol, wsgi_req->protocol_len)) goto clear;
        if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;

        int i;
        char *x_forwarded_for = NULL;
        size_t x_forwarded_for_len = 0;

        // start adding headers
        for(i=0;i<wsgi_req->var_cnt;i++) {
                if (!upsgi_starts_with(wsgi_req->hvec[i].iov_base, wsgi_req->hvec[i].iov_len, "HTTP_", 5)) {

                        char *header = wsgi_req->hvec[i].iov_base+5;
                        size_t header_len = wsgi_req->hvec[i].iov_len-5;

                        if (host && !upsgi_strncmp(header, header_len, "HOST", 4)) goto next;

                        if (!upsgi_strncmp(header, header_len, "X_FORWARDED_FOR", 15)) {
                                x_forwarded_for = wsgi_req->hvec[i+1].iov_base;
                                x_forwarded_for_len = wsgi_req->hvec[i+1].iov_len;
                                goto next;
                        }

                        if (upsgi_buffer_append(ub, header, header_len)) goto clear;

                        // transofmr upsgi var to http header
                        upsgi_httpize_var((ub->buf+ub->pos) - header_len, header_len);

                        if (upsgi_buffer_append(ub, ": ", 2)) goto clear;
                        if (upsgi_buffer_append(ub, wsgi_req->hvec[i+1].iov_base, wsgi_req->hvec[i+1].iov_len)) goto clear;
                        if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;

                }
next:
                i++;
        }


        // append custom Host (if needed)
        if (host) {
                if (upsgi_buffer_append(ub, "Host: ", 6)) goto clear;
                if (upsgi_buffer_append(ub, host, host_len)) goto clear;
                if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;
        }

        if (wsgi_req->content_type_len > 0) {
                if (upsgi_buffer_append(ub, "Content-Type: ", 14)) goto clear;
                if (upsgi_buffer_append(ub, wsgi_req->content_type, wsgi_req->content_type_len)) goto clear;
                if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;
        }

        if (wsgi_req->post_cl > 0) {
                if (upsgi_buffer_append(ub, "Content-Length: ", 16)) goto clear;
                if (upsgi_buffer_num64(ub, (int64_t) wsgi_req->post_cl)) goto clear;
                if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;
        }

        // append required headers
        if (upsgi_buffer_append(ub, "X-Forwarded-For: ", 17)) goto clear;

        if (x_forwarded_for_len > 0) {
                if (upsgi_buffer_append(ub, x_forwarded_for, x_forwarded_for_len)) goto clear;
                if (upsgi_buffer_append(ub, ", ", 2)) goto clear;
        }

        if (upsgi_buffer_append(ub, wsgi_req->remote_addr, wsgi_req->remote_addr_len)) goto clear;

        if (upsgi_buffer_append(ub, "\r\n\r\n", 4)) goto clear;

        return ub;
clear:
        upsgi_buffer_destroy(ub);
        return NULL;
}


struct upsgi_buffer *upsgi_to_http(struct wsgi_request *wsgi_req, char *host, uint16_t host_len, char *uri, uint16_t uri_len) {

        struct upsgi_buffer *ub = upsgi_buffer_new(4096);

        if (upsgi_buffer_append(ub, wsgi_req->method, wsgi_req->method_len)) goto clear;
        if (upsgi_buffer_append(ub, " ", 1)) goto clear;

	if (uri_len && uri) {
        	if (upsgi_buffer_append(ub, uri, uri_len)) goto clear;
	}
	else {
        	if (upsgi_buffer_append(ub, wsgi_req->uri, wsgi_req->uri_len)) goto clear;
	}

	// force HTTP/1.0
        if (upsgi_buffer_append(ub, " HTTP/1.0\r\n", 11)) goto clear;

        int i;
	char *x_forwarded_for = NULL;
	size_t x_forwarded_for_len = 0;

        // start adding headers
        for(i=0;i<wsgi_req->var_cnt;i++) {
		if (!upsgi_starts_with(wsgi_req->hvec[i].iov_base, wsgi_req->hvec[i].iov_len, "HTTP_", 5)) {

			char *header = wsgi_req->hvec[i].iov_base+5;
			size_t header_len = wsgi_req->hvec[i].iov_len-5;

			if (host && !upsgi_strncmp(header, header_len, "HOST", 4)) goto next;

			// remove dangerous headers
			if (!upsgi_strncmp(header, header_len, "CONNECTION", 10)) goto next;
			if (!upsgi_strncmp(header, header_len, "KEEP_ALIVE", 10)) goto next;
			if (!upsgi_strncmp(header, header_len, "TE", 2)) goto next;
			if (!upsgi_strncmp(header, header_len, "TRAILER", 7)) goto next;
			if (!upsgi_strncmp(header, header_len, "X_FORWARDED_FOR", 15)) {
				x_forwarded_for = wsgi_req->hvec[i+1].iov_base;
				x_forwarded_for_len = wsgi_req->hvec[i+1].iov_len;
				goto next;
			}

			if (upsgi_buffer_append(ub, header, header_len)) goto clear;

			// transofmr upsgi var to http header
			upsgi_httpize_var((ub->buf+ub->pos) - header_len, header_len);

			if (upsgi_buffer_append(ub, ": ", 2)) goto clear;
			if (upsgi_buffer_append(ub, wsgi_req->hvec[i+1].iov_base, wsgi_req->hvec[i+1].iov_len)) goto clear;
			if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;

		}
next:
		i++;
        }


	// append custom Host (if needed)
	if (host) {
		if (upsgi_buffer_append(ub, "Host: ", 6)) goto clear;
		if (upsgi_buffer_append(ub, host, host_len)) goto clear;
		if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;
	}

	if (wsgi_req->content_type_len > 0) {
		if (upsgi_buffer_append(ub, "Content-Type: ", 14)) goto clear;
		if (upsgi_buffer_append(ub, wsgi_req->content_type, wsgi_req->content_type_len)) goto clear;
		if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;
	}

	if (wsgi_req->post_cl > 0) {
		if (upsgi_buffer_append(ub, "Content-Length: ", 16)) goto clear;
		if (upsgi_buffer_num64(ub, (int64_t) wsgi_req->post_cl)) goto clear;
		if (upsgi_buffer_append(ub, "\r\n", 2)) goto clear;
	}

	// append required headers
	if (upsgi_buffer_append(ub, "Connection: close\r\n", 19)) goto clear;
	if (upsgi_buffer_append(ub, "X-Forwarded-For: ", 17)) goto clear;

	if (x_forwarded_for_len > 0) {
		if (upsgi_buffer_append(ub, x_forwarded_for, x_forwarded_for_len)) goto clear;
		if (upsgi_buffer_append(ub, ", ", 2)) goto clear;
	}

	if (upsgi_buffer_append(ub, wsgi_req->remote_addr, wsgi_req->remote_addr_len)) goto clear;

	if (upsgi_buffer_append(ub, "\r\n\r\n", 4)) goto clear;

	return ub;
clear:
        upsgi_buffer_destroy(ub);
        return NULL;
}

int upsgi_is_full_http(struct upsgi_buffer *ub) {
	size_t i;
	int status = 0;
	for(i=0;i<ub->pos;i++) {
		switch(status) {
			// \r
			case 0:
				if (ub->buf[i] == '\r') status = 1;
				break;
			// \r\n
			case 1:
				if (ub->buf[i] == '\n') {
					status = 2;
					break;
				}
				status = 0;
				break;
			// \r\n\r
			case 2:
				if (ub->buf[i] == '\r') {
					status = 3;
					break;
				}
				status = 0;
				break;
			// \r\n\r\n
			case 3:
				if (ub->buf[i] == '\n') {
					return 1;
				}
				status = 0;
				break;
			default:
				status = 0;
				break;
			
		}
	}

	return 0;
}

void upsgi_proto_http_setup(struct upsgi_socket *upsgi_sock) {
	upsgi_http_setup_server_port_cache(upsgi_sock);
	upsgi_sock->proto = upsgi_proto_http_parser;
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

/*
close the connection on errors, incomplete parsing, HTTP/1.0, explicit Connection: close,
pipelined or offloaded requests
*/
void upsgi_proto_http11_close(struct wsgi_request *wsgi_req) {
	int must_close = 0;
	if (wsgi_req->write_errors || wsgi_req->proto_parser_status != 3 || wsgi_req->proto_parser_remains > 0
		|| wsgi_req->post_pos < wsgi_req->post_cl || wsgi_req->via == UPSGI_VIA_OFFLOAD
		|| !upsgi_strncmp("HTTP/1.0", 8, wsgi_req->protocol, wsgi_req->protocol_len)
		|| upsgi_http11_request_wants_close(wsgi_req)) {
		must_close = 1;
	}
	if (must_close) {
		close(wsgi_req->fd);
		wsgi_req->socket->retry[wsgi_req->async_id] = 0;
		wsgi_req->socket->fd_threads[wsgi_req->async_id] = -1;
	}
	else {
		wsgi_req->socket->retry[wsgi_req->async_id] = 1;
		wsgi_req->socket->fd_threads[wsgi_req->async_id] = wsgi_req->fd;
	}
}

static int upsgi_proto_http11_fix_headers(struct wsgi_request *wsgi_req) {
	if ((upsgi_http11_request_wants_close(wsgi_req)
		|| !upsgi_strncmp("HTTP/1.0", 8, wsgi_req->protocol, wsgi_req->protocol_len))
		&& !upsgi_http_response_has_connection_header(wsgi_req)) {
		if (upsgi_buffer_append(wsgi_req->headers, "Connection: close\r\n", 19)) {
			return -1;
		}
	}
	return upsgi_proto_base_fix_headers(wsgi_req);
}

int upsgi_proto_http11_accept(struct wsgi_request *wsgi_req, int fd) {
	if (wsgi_req->socket->retry[wsgi_req->async_id]) {
		wsgi_req->fd = wsgi_req->socket->fd_threads[wsgi_req->async_id];
		wsgi_req->c_len = sizeof(struct sockaddr_un);
		int ret = getsockname(wsgi_req->fd, (struct sockaddr *) &wsgi_req->client_addr, (socklen_t *) &wsgi_req->c_len);
		if (ret < 0)
			goto error;
		ret = upsgi_wait_read_req(wsgi_req);
		if (ret <= 0)
			goto error;
		return wsgi_req->socket->fd_threads[wsgi_req->async_id];
	}
	return upsgi_proto_base_accept(wsgi_req, fd);

error:
	close(wsgi_req->fd);
	wsgi_req->socket->retry[wsgi_req->async_id] = 0;
	wsgi_req->socket->fd_threads[wsgi_req->async_id] = -1;
	return -1;
}

void upsgi_proto_http11_setup(struct upsgi_socket *upsgi_sock) {
        upsgi_http_setup_server_port_cache(upsgi_sock);
        upsgi_sock->proto = upsgi_proto_http_parser;
        upsgi_sock->proto_accept = upsgi_proto_http11_accept;
        upsgi_sock->proto_prepare_headers = upsgi_proto_base_prepare_headers;
        upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
        upsgi_sock->proto_fix_headers = upsgi_proto_http11_fix_headers;
        upsgi_sock->proto_read_body = upsgi_proto_base_read_body;
        upsgi_sock->proto_write = upsgi_proto_base_write;
        upsgi_sock->proto_writev = upsgi_proto_base_writev;
        upsgi_sock->proto_write_headers = upsgi_proto_base_write;
        upsgi_sock->proto_sendfile = upsgi_proto_base_sendfile;
        upsgi_sock->proto_close = upsgi_proto_http11_close;
        if (upsgi.offload_threads > 0)
        	upsgi_sock->can_offload = 1;
	upsgi_sock->fd_threads = upsgi_malloc(sizeof(int) * upsgi.cores);
        memset(upsgi_sock->fd_threads, -1, sizeof(int) * upsgi.cores);
        upsgi_sock->retry = upsgi_calloc(sizeof(int) * upsgi.cores);
        upsgi.is_et = 1;
}

#ifdef UPSGI_SSL
static int upsgi_proto_https_parser(struct wsgi_request *wsgi_req) {

        ssize_t j;
        char *ptr;
	int len = -1;

        // first round ? (wsgi_req->proto_parser_buf is freed at the end of the request)
        if (!wsgi_req->proto_parser_buf) {
                wsgi_req->proto_parser_buf = upsgi_malloc(upsgi.buffer_size);
        }

        if (upsgi.buffer_size - wsgi_req->proto_parser_pos == 0) {
                upsgi_log("invalid HTTPS request size (max %u)...skip\n", upsgi.buffer_size);
                return -1;
        }

retry:
        len = SSL_read(wsgi_req->ssl, wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos, upsgi.buffer_size - wsgi_req->proto_parser_pos);
        if (len > 0) {
                goto parse;
        }
	if (len == 0) goto empty;

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
                	upsgi_error("upsgi_proto_https_parser()/SSL_read()");
        }
	return -1;
empty:
        // mute on 0 len...
        if (wsgi_req->proto_parser_pos > 0) {
                upsgi_log("upsgi_proto_https_parser() -> client closed connection");
        }
        return -1;

parse:
        ptr = wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos;
        wsgi_req->proto_parser_pos += len;

        for (j = 0; j < len; j++) {
                if (*ptr == '\r' && (wsgi_req->proto_parser_status == 0 || wsgi_req->proto_parser_status == 2)) {
                        wsgi_req->proto_parser_status++;
                }
                else if (*ptr == '\r') {
                        wsgi_req->proto_parser_status = 1;
                }
                else if (*ptr == '\n' && wsgi_req->proto_parser_status == 1) {
                        wsgi_req->proto_parser_status = 2;
                }
                else if (*ptr == '\n' && wsgi_req->proto_parser_status == 3) {
                        ptr++;
                        wsgi_req->proto_parser_remains = len - (j + 1);
                        if (wsgi_req->proto_parser_remains > 0) {
                                wsgi_req->proto_parser_remains_buf = (wsgi_req->proto_parser_buf + wsgi_req->proto_parser_pos) - wsgi_req->proto_parser_remains;
                        }
			wsgi_req->https = "on";
			wsgi_req->https_len = 2;
                        if (http_parse(wsgi_req, ptr)) return -1;
                        wsgi_req->uh->modifier1 = upsgi.https_modifier1;
                        wsgi_req->uh->modifier2 = upsgi.https_modifier2;
                        return UPSGI_OK;
                }
                else {
                        wsgi_req->proto_parser_status = 0;
                }
                ptr++;
        }

        return UPSGI_AGAIN;
}

void upsgi_proto_https_setup(struct upsgi_socket *upsgi_sock) {
        upsgi_http_setup_server_port_cache(upsgi_sock);
        upsgi_sock->proto = upsgi_proto_https_parser;
                        upsgi_sock->proto_accept = upsgi_proto_ssl_accept;
                        upsgi_sock->proto_prepare_headers = upsgi_proto_base_prepare_headers;
                        upsgi_sock->proto_add_header = upsgi_proto_base_add_header;
                        upsgi_sock->proto_fix_headers = upsgi_proto_base_fix_headers;
                        upsgi_sock->proto_read_body = upsgi_proto_ssl_read_body;
                        upsgi_sock->proto_write = upsgi_proto_ssl_write;
                        upsgi_sock->proto_write_headers = upsgi_proto_ssl_write;
                        upsgi_sock->proto_sendfile = upsgi_proto_ssl_sendfile;
                        upsgi_sock->proto_close = upsgi_proto_ssl_close;

}
#endif
