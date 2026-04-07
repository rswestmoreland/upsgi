#include "upsgi.h"

extern struct upsgi_server upsgi;

/*
 * Static-serving ownership for upsgi v1.
 *
 * core/protocol.c decides whether a request should be intercepted by baseline
 * static features such as check_static and static-map. This file owns the
 * filesystem-facing side after that decision: path resolution, path safety,
 * metadata checks, response headers, and body emission.
 */

int upsgi_static_want_gzip(struct wsgi_request *wsgi_req, char *filename, size_t *filename_len, struct stat *st) {
	char can_gzip = 0, can_br = 0;

	// check for filename size
	if (*filename_len + 4 > PATH_MAX) return 0;
	// check for supported encodings
	can_br = upsgi_contains_n(wsgi_req->encoding, wsgi_req->encoding_len, "br", 2);
	can_gzip = upsgi_contains_n(wsgi_req->encoding, wsgi_req->encoding_len, "gzip", 4);

	if(!can_br && !can_gzip)
		return 0;

	// check for 'all'
	if (upsgi.static_gzip_all) goto gzip;

	// check for dirs/prefix
	struct upsgi_string_list *usl = upsgi.static_gzip_dir;
	while(usl) {
		if (!upsgi_starts_with(filename, *filename_len, usl->value, usl->len)) {
			goto gzip;
		}
		usl = usl->next;
	}

	// check for ext/suffix
	usl = upsgi.static_gzip_ext;
	while(usl) {
		if (!upsgi_strncmp(filename + (*filename_len - usl->len), usl->len, usl->value, usl->len)) {
			goto gzip;
		}
		usl = usl->next;
	}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	// check for regexp
	struct upsgi_regexp_list *url = upsgi.static_gzip;
	while(url) {
		if (upsgi_regexp_match(url->pattern, filename, *filename_len) >= 0) {
			goto gzip;
		}
		url = url->next;
	}
#endif
	return 0;

gzip:

	if(can_br) {
		memcpy(filename + *filename_len, ".br\0", 4);
		*filename_len += 3;
		if (!stat(filename, st)) return 2;
		*filename_len -= 3;
		filename[*filename_len] = 0;
	}

	if(can_gzip) {
		memcpy(filename + *filename_len, ".gz\0", 4);
		*filename_len += 3;
		if (!stat(filename, st)) return 1;
		*filename_len -= 3;
		filename[*filename_len] = 0;
	}

	return 0;
}

int upsgi_http_date(time_t t, char *dst) {

	static char *week[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static char *months[] = {
		"Jan", "Feb", "Mar", "Apr",
		"May", "Jun", "Jul", "Aug",
		"Sep", "Oct", "Nov", "Dec"
	};

	struct tm *hdtm = gmtime(&t);

	int ret = snprintf(dst, 31, "%s, %02d %s %4d %02d:%02d:%02d GMT",
		week[hdtm->tm_wday], hdtm->tm_mday, months[hdtm->tm_mon], hdtm->tm_year + 1900, hdtm->tm_hour, hdtm->tm_min, hdtm->tm_sec);
	if (ret <= 0 || ret > 31) {
		return 0;
	}
	return ret;
}

// only RFC 1123 is supported
static time_t parse_http_date(char *date, uint16_t len) {

	struct tm hdtm;

	if (len != 29 && date[3] != ',')
		return 0;

	hdtm.tm_mday = upsgi_str2_num(date + 5);

	switch (date[8]) {
	case 'J':
		if (date[9] == 'a') {
			hdtm.tm_mon = 0;
			break;
		}

		if (date[9] == 'u') {
			if (date[10] == 'n') {
				hdtm.tm_mon = 5;
				break;
			}

			if (date[10] == 'l') {
				hdtm.tm_mon = 6;
				break;
			}

			return 0;
		}

		return 0;

	case 'F':
		hdtm.tm_mon = 1;
		break;

	case 'M':
		if (date[9] != 'a')
			return 0;

		if (date[10] == 'r') {
			hdtm.tm_mon = 2;
			break;
		}

		if (date[10] == 'y') {
			hdtm.tm_mon = 4;
			break;
		}

		return 0;

	case 'A':
		if (date[10] == 'r') {
			hdtm.tm_mon = 3;
			break;
		}
		if (date[10] == 'g') {
			hdtm.tm_mon = 7;
			break;
		}
		return 0;

	case 'S':
		hdtm.tm_mon = 8;
		break;

	case 'O':
		hdtm.tm_mon = 9;
		break;

	case 'N':
		hdtm.tm_mon = 10;
		break;

	case 'D':
		hdtm.tm_mon = 11;
		break;
	default:
		return 0;
	}

	hdtm.tm_year = upsgi_str4_num(date + 12) - 1900;

	hdtm.tm_hour = upsgi_str2_num(date + 17);
	hdtm.tm_min = upsgi_str2_num(date + 20);
	hdtm.tm_sec = upsgi_str2_num(date + 23);

	return timegm(&hdtm);

}

time_t upsgi_parse_http_date(char *buf, uint16_t len) {
	return parse_http_date(buf, len);
}


int upsgi_add_expires_type(struct wsgi_request *wsgi_req, char *mime_type, int mime_type_len, struct stat *st) {

	struct upsgi_dyn_dict *udd = upsgi.static_expires_type;
	time_t now = wsgi_req->start_of_request / 1000000;
	// 30+1
	char expires[31];

	while (udd) {
		if (!upsgi_strncmp(udd->key, udd->keylen, mime_type, mime_type_len)) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(now + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	udd = upsgi.static_expires_type_mtime;
	while (udd) {
		if (!upsgi_strncmp(udd->key, udd->keylen, mime_type, mime_type_len)) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(st->st_mtime + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	return 0;
}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
int upsgi_add_expires(struct wsgi_request *wsgi_req, char *filename, int filename_len, struct stat *st) {

	struct upsgi_dyn_dict *udd = upsgi.static_expires;
	time_t now = wsgi_req->start_of_request / 1000000;
	// 30+1
	char expires[31];

	while (udd) {
		if (upsgi_regexp_match(udd->pattern, filename, filename_len) >= 0) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(now + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	udd = upsgi.static_expires_mtime;
	while (udd) {
		if (upsgi_regexp_match(udd->pattern, filename, filename_len) >= 0) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(st->st_mtime + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	return 0;
}

int upsgi_add_expires_path_info(struct wsgi_request *wsgi_req, struct stat *st) {

	struct upsgi_dyn_dict *udd = upsgi.static_expires_path_info;
	time_t now = wsgi_req->start_of_request / 1000000;
	// 30+1
	char expires[31];

	while (udd) {
		if (upsgi_regexp_match(udd->pattern, wsgi_req->path_info, wsgi_req->path_info_len) >= 0) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(now + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	udd = upsgi.static_expires_path_info_mtime;
	while (udd) {
		if (upsgi_regexp_match(udd->pattern, wsgi_req->path_info, wsgi_req->path_info_len) >= 0) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(st->st_mtime + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	return 0;
}

int upsgi_add_expires_uri(struct wsgi_request *wsgi_req, struct stat *st) {

	struct upsgi_dyn_dict *udd = upsgi.static_expires_uri;
	time_t now = wsgi_req->start_of_request / 1000000;
	// 30+1
	char expires[31];

	while (udd) {
		if (upsgi_regexp_match(udd->pattern, wsgi_req->uri, wsgi_req->uri_len) >= 0) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(now + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	udd = upsgi.static_expires_uri_mtime;
	while (udd) {
		if (upsgi_regexp_match(udd->pattern, wsgi_req->uri, wsgi_req->uri_len) >= 0) {
			int delta = upsgi_str_num(udd->value, udd->vallen);
			int size = upsgi_http_date(st->st_mtime + delta, expires);
			if (size > 0) {
				if (upsgi_response_add_header(wsgi_req, "Expires", 7, expires, size)) return -1;
			}
			return 0;
		}
		udd = udd->next;
	}

	return 0;
}



#endif


char *upsgi_get_mime_type(char *name, int namelen, size_t *size) {

	int i;
	int count = 0;
	char *ext = NULL;
	for (i = namelen - 1; i >= 0; i--) {
		if (!isalnum((int) name[i])) {
			if (name[i] == '.') {
				ext = name + (namelen - count);
				break;
			}
		}
		count++;
	}

	if (!ext)
		return NULL;


	if (upsgi.threads > 1)
		pthread_mutex_lock(&upsgi.lock_static);

	struct upsgi_dyn_dict *udd = upsgi.mimetypes;
	while (udd) {
		if (!upsgi_strncmp(ext, count, udd->key, udd->keylen)) {
			udd->hits++;
			// auto optimization
			if (udd->prev) {
				if (udd->hits > udd->prev->hits) {
					struct upsgi_dyn_dict *udd_parent = udd->prev->prev, *udd_prev = udd->prev;
					if (udd_parent) {
						udd_parent->next = udd;
					}

					if (udd->next) {
						udd->next->prev = udd_prev;
					}

					udd_prev->prev = udd;
					udd_prev->next = udd->next;

					udd->prev = udd_parent;
					udd->next = udd_prev;

					if (udd->prev == NULL) {
						upsgi.mimetypes = udd;
					}
				}
			}
			*size = udd->vallen;
			if (upsgi.threads > 1)
				pthread_mutex_unlock(&upsgi.lock_static);
			return udd->value;
		}
		udd = udd->next;
	}

	if (upsgi.threads > 1)
		pthread_mutex_unlock(&upsgi.lock_static);

	return NULL;
}

ssize_t upsgi_append_static_path(char *dir, size_t dir_len, char *file, size_t file_len) {


	size_t len = dir_len;
	if (len == 0) {
		return -1;
	}

	if (len + 1 + file_len > PATH_MAX) {
		return -1;
	}

	if (dir[len - 1] == '/') {
		memcpy(dir + len, file, file_len);
		dir[len + file_len] = 0;
		len += file_len;
	}
	else {
		dir[len] = '/';
		memcpy(dir + len + 1, file, file_len);
		dir[len + 1 + file_len] = 0;
		len += 1 + file_len;
	}

	return len;
}

static int upsgi_static_stat(struct wsgi_request *wsgi_req, char *filename, size_t *filename_len, struct stat *st, struct upsgi_string_list **index) {

	int ret = stat(filename, st);
	// if non-existent return -1
	if (ret < 0)
		return -1;

	if (S_ISREG(st->st_mode))
		return 0;

	// check for index
	if (S_ISDIR(st->st_mode)) {
		struct upsgi_string_list *usl = upsgi.static_index;
		while (usl) {
			ssize_t new_len = upsgi_append_static_path(filename, *filename_len, usl->value, usl->len);
			if (new_len >= 0) {
#ifdef UPSGI_DEBUG
				upsgi_log("checking for %s\n", filename);
#endif
				if (upsgi_is_file2(filename, st)) {
					*index = usl;
					*filename_len = new_len;
					return 0;
				}
				// reset to original name
				filename[*filename_len] = 0;
			}
			usl = usl->next;
		}
	}

	return -1;
}

void upsgi_request_fix_range_for_size(struct wsgi_request *wsgi_req, int64_t size) {
	upsgi_fix_range_for_size(&wsgi_req->range_parsed,
			&wsgi_req->range_from, &wsgi_req->range_to, size);
}

int upsgi_real_file_serve(struct wsgi_request *wsgi_req, char *real_filename, size_t real_filename_len, struct stat *st) {

	size_t mime_type_size = 0;
	char http_last_modified[49];
	int use_gzip = 0;

	char *mime_type = upsgi_get_mime_type(real_filename, real_filename_len, &mime_type_size);

	// here we need to choose if we want the gzip variant;
	use_gzip = upsgi_static_want_gzip(wsgi_req, real_filename, &real_filename_len, st);

	if (wsgi_req->if_modified_since_len) {
		time_t ims = parse_http_date(wsgi_req->if_modified_since, wsgi_req->if_modified_since_len);
		if (st->st_mtime <= ims) {
			if (upsgi_response_prepare_headers(wsgi_req, "304 Not Modified", 16))
				return -1;
			return upsgi_response_write_headers_do(wsgi_req);
		}
	}
#ifdef UPSGI_DEBUG
	upsgi_log("[upsgi-fileserve] file %s found, mimetype %s\n", real_filename, mime_type);
#endif

	// static file - don't update avg_rt after request
	wsgi_req->do_not_account_avg_rt = 1;

	int64_t fsize = (int64_t)st->st_size;
	upsgi_request_fix_range_for_size(wsgi_req, fsize);
	switch (wsgi_req->range_parsed) {
	case UPSGI_RANGE_INVALID:
		if (upsgi_response_prepare_headers(wsgi_req,
					"416 Requested Range Not Satisfiable", 35))
			return -1;
		if (upsgi_response_add_content_range(wsgi_req, -1, -1, st->st_size)) return -1;
		return 0;
	case UPSGI_RANGE_VALID:
		{
			time_t when = 0;
			if (wsgi_req->if_range != NULL) {
				when = parse_http_date(wsgi_req->if_range, wsgi_req->if_range_len);
				// an ETag will result in when == 0
			}
		
			if (when < st->st_mtime) {
				fsize = wsgi_req->range_to - wsgi_req->range_from + 1;
				if (upsgi_response_prepare_headers(wsgi_req, "206 Partial Content", 19)) return -1;
				break;
			}
		}
		/* fallthrough */
	default: /* UPSGI_RANGE_NOT_PARSED */
		if (upsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) return -1;
	}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	upsgi_add_expires(wsgi_req, real_filename, real_filename_len, st);
	upsgi_add_expires_path_info(wsgi_req, st);
	upsgi_add_expires_uri(wsgi_req, st);
#endif

	if (use_gzip == 1) {
		if (upsgi_response_add_header(wsgi_req, "Content-Encoding", 16, "gzip", 4)) return -1;
	} else if (use_gzip == 2) {
		if (upsgi_response_add_header(wsgi_req, "Content-Encoding", 16, "br", 2)) return -1;
    }

	// Content-Type (if available)
	if (mime_type_size > 0 && mime_type) {
		if (upsgi_response_add_content_type(wsgi_req, mime_type, mime_type_size)) return -1;
		// check for content-type related headers
		upsgi_add_expires_type(wsgi_req, mime_type, mime_type_size, st);
	}

	// increase static requests counter
	upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].static_requests++;

	// nginx
	if (upsgi.file_serve_mode == 1) {
		if (upsgi_response_add_header(wsgi_req, "X-Accel-Redirect", 16, real_filename, real_filename_len)) return -1;
		// this is the final header (\r\n added)
		int size = upsgi_http_date(st->st_mtime, http_last_modified);
		if (upsgi_response_add_header(wsgi_req, "Last-Modified", 13, http_last_modified, size)) return -1;
	}
	// apache
	else if (upsgi.file_serve_mode == 2) {
		if (upsgi_response_add_header(wsgi_req, "X-Sendfile", 10, real_filename, real_filename_len)) return -1;
		// this is the final header (\r\n added)
		int size = upsgi_http_date(st->st_mtime, http_last_modified);
		if (upsgi_response_add_header(wsgi_req, "Last-Modified", 13, http_last_modified, size)) return -1;
	}
	// raw
	else {
		// set Content-Length (to fsize NOT st->st_size)
		if (upsgi_response_add_content_length(wsgi_req, fsize)) return -1;
		if (wsgi_req->range_parsed == UPSGI_RANGE_VALID) {
			// here use the original size !!!
			if (upsgi_response_add_content_range(wsgi_req, wsgi_req->range_from, wsgi_req->range_to, st->st_size)) return -1;
		}
		int size = upsgi_http_date(st->st_mtime, http_last_modified);
		if (upsgi_response_add_header(wsgi_req, "Last-Modified", 13, http_last_modified, size)) return -1;

		// if it is a HEAD request just skip transfer
		if (!upsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "HEAD", 4)) {
			wsgi_req->status = 200;
			return 0;
		}

		// Ok, the file must be transferred from upsgi
		// offloading will be automatically managed
		int fd = open(real_filename, O_RDONLY);
		if (fd < 0) return -1;
		// fd will be closed in the following function
		upsgi_response_sendfile_do(wsgi_req, fd, wsgi_req->range_from, fsize);
	}

	wsgi_req->status = 200;
	return 0;
}


/*
 * File-serving boundary.
 *
 * Inputs arrive here only after the request path has already decided that a
 * baseline static rule matched. This function is responsible for building the
 * filesystem target, resolving it safely, rejecting traversal outside the
 * allowed root/safe paths, and emitting the response when the target is valid.
 */
int upsgi_file_serve(struct wsgi_request *wsgi_req, char *document_root, uint16_t document_root_len, char *path_info, uint16_t path_info_len, int is_a_file) {

	struct stat st;
	char real_filename[PATH_MAX + 1];
	char stack_filename[PATH_MAX + 1];
	size_t real_filename_len = 0;
	char *filename = NULL;
	size_t filename_len = 0;
	int filename_needs_free = 0;

	struct upsgi_string_list *index = NULL;

	if (!is_a_file) {
		filename_len = document_root_len + 1 + path_info_len;
		if (filename_len <= PATH_MAX) {
			filename = stack_filename;
			memcpy(filename, document_root, document_root_len);
			filename[document_root_len] = '/';
			memcpy(filename + document_root_len + 1, path_info, path_info_len);
			filename[filename_len] = 0;
		}
		else {
			filename = upsgi_concat3n(document_root, document_root_len, "/", 1, path_info, path_info_len);
			filename_needs_free = 1;
		}
	}
	else {
		filename_len = document_root_len;
		if (filename_len <= PATH_MAX) {
			filename = stack_filename;
			memcpy(filename, document_root, document_root_len);
			filename[filename_len] = 0;
		}
		else {
			filename = upsgi_concat2n(document_root, document_root_len, "", 0);
			filename_needs_free = 1;
		}
	}

#ifdef UPSGI_DEBUG
	upsgi_log("[upsgi-fileserve] checking for %s\n", filename);
#endif

	if (upsgi.static_cache_paths) {
		upsgi_rlock(upsgi.static_cache_paths->lock);
		uint64_t item_len;
		char *item = upsgi_cache_get2(upsgi.static_cache_paths, filename, filename_len, &item_len);
		if (item && item_len > 0 && item_len <= PATH_MAX) {
			memcpy(real_filename, item, item_len);
			real_filename_len = item_len;
			real_filename[real_filename_len] = 0;
			upsgi_rwunlock(upsgi.static_cache_paths->lock);
			goto found;
		}
		upsgi_rwunlock(upsgi.static_cache_paths->lock);
	}

	if (!realpath(filename, real_filename)) {
#ifdef UPSGI_DEBUG
		upsgi_log("[upsgi-fileserve] unable to get realpath() of the static file\n");
#endif
		if (filename_needs_free) free(filename);
		return -1;
	}
	real_filename_len = strlen(real_filename);

	if (upsgi.static_cache_paths) {
		upsgi_wlock(upsgi.static_cache_paths->lock);
		upsgi_cache_set2(upsgi.static_cache_paths, filename, filename_len, real_filename, real_filename_len, upsgi.use_static_cache_paths, UPSGI_CACHE_FLAG_UPDATE);
		upsgi_rwunlock(upsgi.static_cache_paths->lock);
	}

found:
	if (filename_needs_free) free(filename);

	/*
	 * Containment check.
	 *
	 * core/protocol.c chooses the docroot or mapped root. This layer ensures the
	 * resolved target stays within that root, unless it falls under an explicit
	 * safe path. This keeps traversal protection local to the file-serving code.
	 */
	if (upsgi_starts_with(real_filename, real_filename_len, document_root, document_root_len)) {
		struct upsgi_string_list *safe = upsgi.static_safe;
		while(safe) {
			if (!upsgi_starts_with(real_filename, real_filename_len, safe->value, safe->len)) {
				goto safe;
			}
			safe = safe->next;
		}
		upsgi_log("[upsgi-fileserve] security error: %s is not under %.*s or a safe path\n", real_filename, document_root_len, document_root);
		return -1;
	}

safe:

	/*
	 * Static metadata and response emission.
	 *
	 * Once the target path is known-safe, this layer performs stat/index logic,
	 * method checks, optional route pre-send hooks, and final file response
	 * emission. Missing files simply return control to the caller so the request
	 * path can continue toward PSGI.
	 */
	if (!upsgi_static_stat(wsgi_req, real_filename, &real_filename_len, &st, &index)) {

		if (index) {
			// if we are here the PATH_INFO need to be changed
			if (upsgi_req_append_path_info_with_index(wsgi_req, index->value, index->len)) {
				return -1;
			}
		}

		// skip methods other than GET and HEAD
		if (upsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "GET", 3) && upsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "HEAD", 4)) {
			return -1;
		}

		// check for skippable ext
		struct upsgi_string_list *sse = upsgi.static_skip_ext;
		while (sse) {
			if (real_filename_len >= sse->len) {
				if (!upsgi_strncmp(real_filename + (real_filename_len - sse->len), sse->len, sse->value, sse->len)) {
					return -1;
				}
			}
			sse = sse->next;
		}

#ifdef UPSGI_ROUTING
		// before sending the file, we need to check if some rule applies
		if (!wsgi_req->is_routing && upsgi_apply_routes_do(upsgi.routes, wsgi_req, NULL, 0) == UPSGI_ROUTE_BREAK) {
			return 0;
		}
		wsgi_req->routes_applied = 1;
#endif

		return upsgi_real_file_serve(wsgi_req, real_filename, real_filename_len, &st);
	}

	return -1;

}
