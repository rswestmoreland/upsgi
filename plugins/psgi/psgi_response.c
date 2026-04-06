#include "psgi.h"

extern struct uwsgi_server uwsgi;

/*
 * PSGI response layer:
 * - validate the returned PSGI triplet/object
 * - prepare status and headers
 * - emit body chunks or sendfile bodies
 *
 * Request dispatch stays in psgi_plugin.c.
 * Perl bridge/app loading stays in psgi_loader.c.
 */

static int psgi_invalid_response(struct wsgi_request *wsgi_req, char *message) {
	uwsgi_log("%s\n", message);
	if (!wsgi_req->headers_sent) {
		uwsgi_500(wsgi_req);
	}
	return UWSGI_OK;
}

static int psgi_status_forbids_entity_headers(int status) {
	if (status >= 100 && status < 200) return 1;
	if (status == 204 || status == 304) return 1;
	return 0;
}

static int psgi_header_name_is_valid(const char *key, STRLEN key_len) {
	STRLEN i = 0;
	if (!key || key_len == 0) return 0;
	if (!((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= 'a' && key[0] <= 'z'))) return 0;
	if (key[key_len - 1] == '-' || key[key_len - 1] == '_') return 0;
	if (key_len == 6 && !uwsgi_strnicmp((char *) key, key_len, "Status", 6)) return 0;
	for (i = 0; i < key_len; i++) {
		char c = key[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
			continue;
		}
		return 0;
	}
	return 1;
}

static int psgi_header_value_is_valid(const char *value, STRLEN value_len) {
	STRLEN i = 0;
	if (!value) return 0;
	for (i = 0; i < value_len; i++) {
		if (((unsigned char) value[i]) < 32) {
			return 0;
		}
	}
	return 1;
}

static int psgi_body_chunk_is_valid(SV *chunk) {
	if (!chunk || !SvOK(chunk)) return 0;
	if (SvROK(chunk)) return 0;
	if (SvUTF8(chunk)) return 0;
	return 1;
}

#define PSGI_BODY_COALESCE_SIZE 8192

struct psgi_body_coalescer {
	char buf[PSGI_BODY_COALESCE_SIZE];
	size_t len;
};

static int psgi_body_coalescer_flush(struct wsgi_request *wsgi_req, struct psgi_body_coalescer *coalescer) {
	if (!coalescer || coalescer->len == 0) return UWSGI_OK;
	uwsgi_response_write_body_do(wsgi_req, coalescer->buf, coalescer->len);
	uwsgi_pl_check_write_errors {
		coalescer->len = 0;
		return UWSGI_OK;
	}
	coalescer->len = 0;
	return UWSGI_OK;
}

static int psgi_body_coalescer_write(struct wsgi_request *wsgi_req, struct psgi_body_coalescer *coalescer, const char *chunk, size_t chunk_len) {
	if (!coalescer || !chunk || chunk_len == 0) return UWSGI_OK;
	if (chunk_len > sizeof(coalescer->buf)) {
		if (psgi_body_coalescer_flush(wsgi_req, coalescer) != UWSGI_OK) return UWSGI_OK;
		uwsgi_response_write_body_do(wsgi_req, (char *) chunk, chunk_len);
		uwsgi_pl_check_write_errors {
			return UWSGI_OK;
		}
		return UWSGI_OK;
	}
	if (coalescer->len > 0 && coalescer->len + chunk_len > sizeof(coalescer->buf)) {
		if (psgi_body_coalescer_flush(wsgi_req, coalescer) != UWSGI_OK) return UWSGI_OK;
	}
	memcpy(coalescer->buf + coalescer->len, chunk, chunk_len);
	coalescer->len += chunk_len;
	return UWSGI_OK;
}

static int psgi_validate_status(SV *status_sv, int *status_code) {
	STRLEN status_len = 0;
	char *status_str = NULL;
	if (!status_sv || !SvOK(status_sv)) return -1;
	status_str = SvPV(status_sv, status_len);
	if (!status_str || status_len == 0) return -1;
	if (!is_a_number(status_str)) return -1;
	*status_code = atoi(status_str);
	if (*status_code < 100) return -1;
	return 0;
}

static int psgi_validate_headers(AV *headers, int status_code, int *has_content_type, int *has_content_length) {
	int i = 0;
	int headers_len = 0;
	SV **hitem = NULL;
	STRLEN key_len = 0;
	STRLEN value_len = 0;
	char *key = NULL;
	char *value = NULL;

	if (!headers) return -1;
	headers_len = (int) av_len(headers);
	if (headers_len % 2 != 1) return -1;

	for (i = 0; i <= headers_len; i += 2) {
		hitem = av_fetch(headers, i, 0);
		if (!hitem || !*hitem || !SvOK(*hitem) || SvROK(*hitem)) return -1;
		key = SvPV(*hitem, key_len);
		if (!psgi_header_name_is_valid(key, key_len)) return -1;

		hitem = av_fetch(headers, i + 1, 0);
		if (!hitem || !*hitem || !SvOK(*hitem) || SvROK(*hitem)) return -1;
		value = SvPV(*hitem, value_len);
		if (!psgi_header_value_is_valid(value, value_len)) return -1;

		if (key_len == 12 && !uwsgi_strnicmp(key, key_len, "Content-Type", 12)) {
			*has_content_type = 1;
		}
		else if (key_len == 14 && !uwsgi_strnicmp(key, key_len, "Content-Length", 14)) {
			*has_content_length = 1;
		}
	}

	if (psgi_status_forbids_entity_headers(status_code)) {
		if (*has_content_type) return -2;
		if (*has_content_length) return -3;
	}
	else {
		if (!*has_content_type) return -4;
	}

	return 0;
}

static int psgi_validate_body_slot(SV **body_item, int body_optional) {
	SV *rv = NULL;
	IO *io = NULL;
	AV *body = NULL;
	SV **hitem = NULL;
	int i = 0;

	if (!body_item) {
		return body_optional ? 0 : -1;
	}
	if (!*body_item || !SvOK(*body_item) || !SvROK(*body_item)) {
		return -1;
	}

	rv = SvRV(*body_item);
	if (!rv) return -1;

	io = GvIO(rv);
	if (io) return 0;

	if (SvOBJECT(rv)) {
		if (uwsgi_perl_obj_can(*body_item, "path", 4)) return 0;
		if (uwsgi_perl_obj_can(*body_item, STR_WITH_LEN("getline"))) return 0;
		return -1;
	}

	if (SvTYPE(rv) != SVt_PVAV) return -1;

	body = (AV *) rv;
	for (i = 0; i <= av_len(body); i++) {
		hitem = av_fetch(body, i, 0);
		if (!hitem || !psgi_body_chunk_is_valid(*hitem)) return -1;
	}

	return 0;
}

static int psgi_write_raw_buffer(struct wsgi_request *wsgi_req, struct uwsgi_buffer *ub) {
	size_t old_write_pos = wsgi_req->write_pos;
	int ret = -1;
	if (!ub) return -1;
	wsgi_req->write_pos = 0;
	for (;;) {
		errno = 0;
		ret = wsgi_req->socket->proto_write_headers(wsgi_req, ub->buf, ub->pos);
		if (ret < 0) {
			if (!uwsgi.ignore_write_errors) {
				uwsgi_req_error("psgi_write_raw_buffer()/proto_write_headers");
			}
			wsgi_req->write_errors++;
			wsgi_req->write_pos = old_write_pos;
			return -1;
		}
		if (ret == UWSGI_OK) {
			break;
		}
		if (!uwsgi_is_again()) continue;
		ret = uwsgi_wait_write_req(wsgi_req);
		if (ret <= 0) {
			if (ret == 0) {
				uwsgi_log("psgi_write_raw_buffer() TIMEOUT !!!\n");
			}
			wsgi_req->write_errors++;
			wsgi_req->write_pos = old_write_pos;
			return -1;
		}
	}
	wsgi_req->write_pos = old_write_pos;
	return 0;
}

int psgi_informational_response(struct wsgi_request *wsgi_req, int status, AV *headers) {
	struct uwsgi_buffer *ub = NULL;
	struct uwsgi_buffer *hh = NULL;
	SV **hitem = NULL;
	STRLEN key_len = 0;
	STRLEN value_len = 0;
	char *key = NULL;
	char *value = NULL;
	char status_buf[16];
	char *status_line = status_buf;
	size_t status_line_len = 0;
	const char *status_msg = NULL;
	uint16_t status_msg_len = 0;
	int i = 0;
	int has_content_type = 0;
	int has_content_length = 0;
	int header_validation = 0;

	if (!wsgi_req || !headers) return -1;
	if (wsgi_req->headers_sent || wsgi_req->response_size) return -1;
	if (!(wsgi_req->protocol_len == 8 && !uwsgi_strncmp("HTTP/1.1", 8, wsgi_req->protocol, wsgi_req->protocol_len))) {
		return -1;
	}
	if (status < 100 || status >= 200 || status == 101) return -1;

	header_validation = psgi_validate_headers(headers, status, &has_content_type, &has_content_length);
	if (header_validation != 0) return -1;

	uwsgi_num2str2(status, status_buf);
	status_msg = uwsgi_http_status_msg(status_buf, &status_msg_len);
	if (status_msg) {
		status_line = uwsgi_concat3n(status_buf, 3, " ", 1, (char *) status_msg, status_msg_len);
		status_line_len = 4 + status_msg_len;
	}
	else {
		status_line_len = 3;
	}

	ub = wsgi_req->socket->proto_prepare_headers(wsgi_req, status_line, (uint16_t) status_line_len);
	if (status_line != status_buf) free(status_line);
	if (!ub) return -1;

	for (i = 0; i <= av_len(headers); i += 2) {
		hitem = av_fetch(headers, i, 0);
		key = SvPV(*hitem, key_len);
		hitem = av_fetch(headers, i + 1, 0);
		value = SvPV(*hitem, value_len);
		hh = wsgi_req->socket->proto_add_header(wsgi_req, key, key_len, value, value_len);
		if (!hh) goto error;
		if (uwsgi_buffer_append(ub, hh->buf, hh->pos)) {
			uwsgi_buffer_destroy(hh);
			goto error;
		}
		uwsgi_buffer_destroy(hh);
	}

	if (uwsgi_buffer_append(ub, "\r\n", 2)) goto error;
	if (psgi_write_raw_buffer(wsgi_req, ub)) goto error;
	uwsgi_buffer_destroy(ub);
	return 0;

error:
	uwsgi_buffer_destroy(ub);
	return -1;
}

static int psgi_response_do(struct wsgi_request *wsgi_req, AV *response, int allow_streaming_start) {
	SV **status_code = NULL;
	SV **hitem = NULL;
	SV **body_item = NULL;
	AV *headers = NULL;
	AV *body = NULL;
	STRLEN hlen = 0;
	STRLEN hlen2 = 0;
	int i = 0;
	char *chitem = NULL;
	char *chitem2 = NULL;
	SV **harakiri = NULL;
	int response_len = 0;
	int status = 0;
	int has_content_type = 0;
	int has_content_length = 0;
	int header_validation = 0;
	int body_optional = 0;

	/* Resume an async body iterator previously parked by the response layer. */
	if (wsgi_req->async_force_again) {

		wsgi_req->async_force_again = 0;

		wsgi_req->switches++;
		SV *chunk = uwsgi_perl_obj_call(wsgi_req->async_placeholder, "getline");
		if (!chunk) {
			uwsgi_500(wsgi_req);
			return UWSGI_OK;
		}

		if (!SvOK(chunk)) {
			SvREFCNT_dec(chunk);
			SV *closed = uwsgi_perl_obj_call(wsgi_req->async_placeholder, "close");
			if (closed) {
				SvREFCNT_dec(closed);
			}

			harakiri = hv_fetch((HV *) SvRV((SV *) wsgi_req->async_environ), "psgix.harakiri.commit", 21, 0);
			if (harakiri) {
				if (SvTRUE(*harakiri)) wsgi_req->async_plagued = 1;
			}

			SvREFCNT_dec(wsgi_req->async_result);
			return UWSGI_OK;
		}

		if (!psgi_body_chunk_is_valid(chunk)) {
			SvREFCNT_dec(chunk);
			return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
		}

		chitem = SvPV(chunk, hlen);

		if (hlen <= 0) {
			SvREFCNT_dec(chunk);
			SV *closed = uwsgi_perl_obj_call(wsgi_req->async_placeholder, "close");
			if (closed) {
				SvREFCNT_dec(closed);
			}

			/* check for psgix.harakiri */
			harakiri = hv_fetch((HV *) SvRV((SV *) wsgi_req->async_environ), "psgix.harakiri.commit", 21, 0);
			if (harakiri) {
				if (SvTRUE(*harakiri)) wsgi_req->async_plagued = 1;
			}

			SvREFCNT_dec(wsgi_req->async_result);
			return UWSGI_OK;
		}

		uwsgi_response_write_body_do(wsgi_req, chitem, hlen);
		uwsgi_pl_check_write_errors {
			SvREFCNT_dec(chunk);
			return UWSGI_OK;
		}
		SvREFCNT_dec(chunk);
		wsgi_req->async_force_again = 1;
		return UWSGI_AGAIN;
	}

	/* PSGI responses must resolve to an array reference contract here. */
	if (SvTYPE((SV *) response) != SVt_PVAV) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI response type");
	}

	response_len = (int) av_len(response);
	if (allow_streaming_start) {
		if (response_len != 1 && response_len != 2) {
			return psgi_invalid_response(wsgi_req, "invalid PSGI response arity");
		}
		body_optional = (response_len == 1);
	}
	else {
		if (response_len != 2) {
			return psgi_invalid_response(wsgi_req, "invalid PSGI response arity");
		}
	}

	status_code = av_fetch(response, 0, 0);
	if (!status_code || psgi_validate_status(*status_code, &status)) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI status code");
	}

	hitem = av_fetch(response, 1, 0);
	if (!hitem || !*hitem || !SvOK(*hitem) || !SvROK(*hitem) || SvTYPE(SvRV(*hitem)) != SVt_PVAV) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI headers");
	}

	headers = (AV *) SvRV(*hitem);
	if (!headers) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI headers");
	}

	header_validation = psgi_validate_headers(headers, status, &has_content_type, &has_content_length);
	if (header_validation == -1) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI headers");
	}
	if (header_validation == -2) {
		return psgi_invalid_response(wsgi_req, "forbidden PSGI Content-Type for status");
	}
	if (header_validation == -3) {
		return psgi_invalid_response(wsgi_req, "forbidden PSGI Content-Length for status");
	}
	if (header_validation == -4) {
		return psgi_invalid_response(wsgi_req, "missing PSGI Content-Type");
	}

	body_item = av_fetch(response, 2, 0);
	if (psgi_validate_body_slot(body_item, body_optional)) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
	}

	if (uwsgi_response_prepare_headers_int(wsgi_req, status)) {
		return UWSGI_OK;
	}

	for (i = 0; i <= (int) av_len(headers); i += 2) {
		hitem = av_fetch(headers, i, 0);
		chitem = SvPV(*hitem, hlen);
		hitem = av_fetch(headers, i + 1, 0);
		chitem2 = SvPV(*hitem, hlen2);
		if (uwsgi_response_add_header(wsgi_req, chitem, hlen, chitem2, hlen2)) return UWSGI_OK;
	}

	if (!body_item && body_optional) {
		return UWSGI_OK;
	}
	if (!body_item) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
	}

	SV *rv = SvRV(*body_item);
	if (!rv) {
		return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
	}

	IO *io = GvIO(rv);
	if (io) {
		const int fd = PerlIO_fileno(IoIFP(io));
		if (fd >= 0) {
			wsgi_req->sendfile_fd = fd;
			uwsgi_response_sendfile_do(wsgi_req, wsgi_req->sendfile_fd, 0, 0);
			uwsgi_pl_check_write_errors {
				/* noop */
			}
			return UWSGI_OK;
		}
	}

	if (SvOBJECT(rv)) {
		if (uwsgi_perl_obj_can(*body_item, "path", 4)) {
			SV *p = uwsgi_perl_obj_call(*body_item, "path");
			if (!p || !SvOK(p)) {
				if (p) SvREFCNT_dec(p);
				return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
			}
			int fd = open(SvPV_nolen(p), O_RDONLY);
			SvREFCNT_dec(p);
			if (fd < 0) {
				return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
			}
			uwsgi_response_sendfile_do(wsgi_req, fd, 0, 0);
			uwsgi_pl_check_write_errors {
				/* noop */
			}
			return UWSGI_OK;
		}
		else if (uwsgi_perl_obj_can(*body_item, STR_WITH_LEN("getline"))) {
			struct psgi_body_coalescer coalescer;
			coalescer.len = 0;
			for (;;) {
				SV *chunk = NULL;
				wsgi_req->switches++;
				chunk = uwsgi_perl_obj_call(*body_item, "getline");
				if (!chunk) {
					uwsgi_500(wsgi_req);
					break;
				}

				if (!SvOK(chunk)) {
					SvREFCNT_dec(chunk);
					break;
				}

				if (!psgi_body_chunk_is_valid(chunk)) {
					SvREFCNT_dec(chunk);
					return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
				}

				chitem = SvPV(chunk, hlen);
				if (hlen <= 0) {
					SvREFCNT_dec(chunk);
					if (uwsgi.async > 0 && wsgi_req->async_force_again) {
						wsgi_req->async_placeholder = (SV *) *body_item;
						return UWSGI_AGAIN;
					}
					break;
				}

				if (uwsgi.async > 0) {
					uwsgi_response_write_body_do(wsgi_req, chitem, hlen);
					uwsgi_pl_check_write_errors {
						SvREFCNT_dec(chunk);
						break;
					}
				}
				else {
					if (psgi_body_coalescer_write(wsgi_req, &coalescer, chitem, hlen) != UWSGI_OK) {
						SvREFCNT_dec(chunk);
						break;
					}
					uwsgi_pl_check_write_errors {
						SvREFCNT_dec(chunk);
						break;
					}
				}
				SvREFCNT_dec(chunk);
				if (uwsgi.async > 0) {
					wsgi_req->async_placeholder = (SV *) *body_item;
					wsgi_req->async_force_again = 1;
					return UWSGI_AGAIN;
				}
			}

			if (uwsgi.async == 0 && psgi_body_coalescer_flush(wsgi_req, &coalescer) != UWSGI_OK) {
				return UWSGI_OK;
			}

			SV *closed = uwsgi_perl_obj_call(*body_item, "close");
			if (closed) {
				SvREFCNT_dec(closed);
			}
		}
	}
	else if (SvTYPE(rv) == SVt_PVAV) {
		struct psgi_body_coalescer coalescer;
		coalescer.len = 0;
		body = (AV *) rv;
		for (i = 0; i <= av_len(body); i++) {
			hitem = av_fetch(body, i, 0);
			if (!hitem || !psgi_body_chunk_is_valid(*hitem)) {
				return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
			}
			chitem = SvPV(*hitem, hlen);
			if (psgi_body_coalescer_write(wsgi_req, &coalescer, chitem, hlen) != UWSGI_OK) {
				break;
			}
			uwsgi_pl_check_write_errors {
				break;
			}
		}
		if (psgi_body_coalescer_flush(wsgi_req, &coalescer) != UWSGI_OK) {
			return UWSGI_OK;
		}
	}
	else {
		return psgi_invalid_response(wsgi_req, "invalid PSGI response body");
	}

	return UWSGI_OK;
}

int psgi_response(struct wsgi_request *wsgi_req, AV *response) {
	return psgi_response_do(wsgi_req, response, 0);
}

int psgi_response_stream_start(struct wsgi_request *wsgi_req, AV *response) {
	return psgi_response_do(wsgi_req, response, 1);
}
