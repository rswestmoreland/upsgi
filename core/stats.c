#include "upsgi.h"
/*
	utility functions for fast generating json output for the stats subsystem
*/

extern struct upsgi_server upsgi;

struct upsgi_stats *upsgi_stats_new(size_t chunk_size) {
	struct upsgi_stats *us = upsgi_malloc(sizeof(struct upsgi_stats));
	us->base = upsgi_malloc(chunk_size);
	us->base[0] = '{';
	us->pos = 1;
	us->chunk = chunk_size;
	us->size = chunk_size;
	us->tabs = 1;
	us->dirty = 0;
	us->minified = upsgi.stats_minified;
	if (!us->minified) {
		us->base[1] = '\n';
		us->pos++;
	}
	return us;
}

int upsgi_stats_symbol(struct upsgi_stats *us, char sym) {
	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;

	if (ptr + 1 > watermark) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
	}

	*ptr = sym;
	us->pos++;
	return 0;
}

int upsgi_stats_symbol_nl(struct upsgi_stats *us, char sym) {
	if (upsgi_stats_symbol(us, sym)) {
		return -1;
	}
	if (us->minified)
		return 0;
	return upsgi_stats_symbol(us, '\n');
}


int upsgi_stats_comma(struct upsgi_stats *us) {
	return upsgi_stats_symbol_nl(us, ',');
}

int upsgi_stats_apply_tabs(struct upsgi_stats *us) {
	if (us->minified)
		return 0;
	size_t i;
	for (i = 0; i < us->tabs; i++) {
		if (upsgi_stats_symbol(us, '\t'))
			return -1;
	};
	return 0;
}


int upsgi_stats_object_open(struct upsgi_stats *us) {
	if (upsgi_stats_apply_tabs(us))
		return -1;
	if (!us->minified)
		us->tabs++;
	return upsgi_stats_symbol_nl(us, '{');
}

int upsgi_stats_object_close(struct upsgi_stats *us) {
	if (!us->minified) {
		if (upsgi_stats_symbol(us, '\n'))
			return -1;
		us->tabs--;
		if (upsgi_stats_apply_tabs(us))
			return -1;
	}
	return upsgi_stats_symbol(us, '}');
}

int upsgi_stats_list_open(struct upsgi_stats *us) {
	us->tabs++;
	return upsgi_stats_symbol_nl(us, '[');
}

int upsgi_stats_list_close(struct upsgi_stats *us) {
	if (!us->minified) {
		if (upsgi_stats_symbol(us, '\n'))
			return -1;
		us->tabs--;
		if (upsgi_stats_apply_tabs(us))
			return -1;
	}
	return upsgi_stats_symbol(us, ']');
}

int upsgi_stats_keyval(struct upsgi_stats *us, char *key, char *value) {

	if (upsgi_stats_apply_tabs(us))
		return -1;

	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;
	size_t available = watermark - ptr;

	int ret = snprintf(ptr, available, "\"%s\":\"%s\"", key, value);
	if (ret <= 0)
		return -1;
	while (ret >= (int) available) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
		watermark = us->base + us->size;
		available = watermark - ptr;
		ret = snprintf(ptr, available, "\"%s\":\"%s\"", key, value);
		if (ret <= 0)
			return -1;
	}

	us->pos += ret;
	return 0;

}

int upsgi_stats_keyval_comma(struct upsgi_stats *us, char *key, char *value) {
	int ret = upsgi_stats_keyval(us, key, value);
	if (ret)
		return -1;
	return upsgi_stats_comma(us);
}

int upsgi_stats_keyvalnum(struct upsgi_stats *us, char *key, char *value, unsigned long long num) {

	if (upsgi_stats_apply_tabs(us))
		return -1;

	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;
	size_t available = watermark - ptr;

	int ret = snprintf(ptr, available, "\"%s\":\"%s%llu\"", key, value, num);
	if (ret <= 0)
		return -1;
	while (ret >= (int) available) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
		watermark = us->base + us->size;
		available = watermark - ptr;
		ret = snprintf(ptr, available, "\"%s\":\"%s%llu\"", key, value, num);
		if (ret <= 0)
			return -1;
	}

	us->pos += ret;
	return 0;

}

int upsgi_stats_keyvalnum_comma(struct upsgi_stats *us, char *key, char *value, unsigned long long num) {
	int ret = upsgi_stats_keyvalnum(us, key, value, num);
	if (ret)
		return -1;
	return upsgi_stats_comma(us);
}


int upsgi_stats_keyvaln(struct upsgi_stats *us, char *key, char *value, int vallen) {

	if (upsgi_stats_apply_tabs(us))
		return -1;

	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;
	size_t available = watermark - ptr;

	int ret = snprintf(ptr, available, "\"%s\":\"%.*s\"", key, vallen, value);
	if (ret <= 0)
		return -1;
	while (ret >= (int) available) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
		watermark = us->base + us->size;
		available = watermark - ptr;
		ret = snprintf(ptr, available, "\"%s\":\"%.*s\"", key, vallen, value);
		if (ret <= 0)
			return -1;
	}

	us->pos += ret;
	return 0;

}

int upsgi_stats_keyvaln_comma(struct upsgi_stats *us, char *key, char *value, int vallen) {
	int ret = upsgi_stats_keyvaln(us, key, value, vallen);
	if (ret)
		return -1;
	return upsgi_stats_comma(us);
}


int upsgi_stats_key(struct upsgi_stats *us, char *key) {

	if (upsgi_stats_apply_tabs(us))
		return -1;

	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;
	size_t available = watermark - ptr;

	int ret = snprintf(ptr, available, "\"%s\":", key);
	if (ret <= 0)
		return -1;
	while (ret >= (int) available) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
		watermark = us->base + us->size;
		available = watermark - ptr;
		ret = snprintf(ptr, available, "\"%s\":", key);
		if (ret <= 0)
			return -1;
	}

	us->pos += ret;
	return 0;
}

int upsgi_stats_str(struct upsgi_stats *us, char *str) {

	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;
	size_t available = watermark - ptr;

	int ret = snprintf(ptr, available, "\"%s\"", str);
	if (ret <= 0)
		return -1;
	while (ret >= (int) available) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
		watermark = us->base + us->size;
		available = watermark - ptr;
		ret = snprintf(ptr, available, "\"%s\"", str);
		if (ret <= 0)
			return -1;
	}

	us->pos += ret;
	return 0;
}




int upsgi_stats_keylong(struct upsgi_stats *us, char *key, unsigned long long num) {

	if (upsgi_stats_apply_tabs(us))
		return -1;

	char *ptr = us->base + us->pos;
	char *watermark = us->base + us->size;
	size_t available = watermark - ptr;

	int ret = snprintf(ptr, available, "\"%s\":%llu", key, num);
	if (ret <= 0)
		return -1;
	while (ret >= (int) available) {
		char *new_base = realloc(us->base, us->size + us->chunk);
		if (!new_base)
			return -1;
		us->base = new_base;
		us->size += us->chunk;
		ptr = us->base + us->pos;
		watermark = us->base + us->size;
		available = watermark - ptr;
		ret = snprintf(ptr, available, "\"%s\":%llu", key, num);
		if (ret <= 0)
			return -1;
	}

	us->pos += ret;
	return 0;
}


int upsgi_stats_keylong_comma(struct upsgi_stats *us, char *key, unsigned long long num) {
	int ret = upsgi_stats_keylong(us, key, num);
	if (ret)
		return -1;
	return upsgi_stats_comma(us);
}

int upsgi_stats_keyslong(struct upsgi_stats *us, char *key, long long num) {

        if (upsgi_stats_apply_tabs(us))
                return -1;

        char *ptr = us->base + us->pos;
        char *watermark = us->base + us->size;
        size_t available = watermark - ptr;

        int ret = snprintf(ptr, available, "\"%s\":%lld", key, num);
        if (ret <= 0)
                return -1;
        while (ret >= (int) available) {
                char *new_base = realloc(us->base, us->size + us->chunk);
                if (!new_base)
                        return -1;
                us->base = new_base;
                us->size += us->chunk;
                ptr = us->base + us->pos;
                watermark = us->base + us->size;
                available = watermark - ptr;
                ret = snprintf(ptr, available, "\"%s\":%lld", key, num);
                if (ret <= 0)
                        return -1;
        }

        us->pos += ret;
        return 0;
}


int upsgi_stats_keyslong_comma(struct upsgi_stats *us, char *key, long long num) {
        int ret = upsgi_stats_keyslong(us, key, num);
        if (ret)
                return -1;
        return upsgi_stats_comma(us);
}


void upsgi_send_stats(int fd, struct upsgi_stats *(*func) (void)) {

	struct sockaddr_un client_src;
	socklen_t client_src_len = 0;

	int client_fd = accept(fd, (struct sockaddr *) &client_src, &client_src_len);
	if (client_fd < 0) {
		upsgi_error("accept()");
		return;
	}

	if (upsgi.stats_http) {
		if (upsgi_send_http_stats(client_fd)) {
			close(client_fd);
			return;
		}
	}

	struct upsgi_stats *us = func();
	if (!us)
		goto end;

	size_t remains = us->pos;
	off_t pos = 0;
	while (remains > 0) {
		int ret = upsgi_waitfd_write(client_fd, upsgi.socket_timeout);
		if (ret <= 0) {
			goto end0;
		}
		ssize_t res = write(client_fd, us->base + pos, remains);
		if (res <= 0) {
			if (res < 0) {
				upsgi_error("write()");
			}
			goto end0;
		}
		pos += res;
		remains -= res;
	}

end0:
	free(us->base);
	free(us);

end:
	close(client_fd);
}

struct upsgi_stats_pusher *upsgi_stats_pusher_get(char *name) {
	struct upsgi_stats_pusher *usp = upsgi.stats_pushers;
	while (usp) {
		if (!strcmp(usp->name, name)) {
			return usp;
		}
		usp = usp->next;
	}
	return usp;
}

struct upsgi_stats_pusher_instance *upsgi_stats_pusher_add(struct upsgi_stats_pusher *pusher, char *arg) {
	struct upsgi_stats_pusher_instance *old_uspi = NULL, *uspi = upsgi.stats_pusher_instances;
	while (uspi) {
		old_uspi = uspi;
		uspi = uspi->next;
	}

	uspi = upsgi_calloc(sizeof(struct upsgi_stats_pusher_instance));
	uspi->pusher = pusher;
	if (arg) {
		uspi->arg = upsgi_str(arg);
	}
	uspi->raw = pusher->raw;
	if (old_uspi) {
		old_uspi->next = uspi;
	}
	else {
		upsgi.stats_pusher_instances = uspi;
	}

	return uspi;
}

void upsgi_stats_pusher_loop(struct upsgi_thread *ut) {
	void *events = event_queue_alloc(1);
	for (;;) {
		int nevents = event_queue_wait_multi(ut->queue, 1, events, 1);
		if (nevents < 0) {
			if (errno == EINTR) continue;
			upsgi_log_verbose("ending the stats pusher thread...\n");
			return;
		}
		if (nevents > 0) {
			int interesting_fd = event_queue_interesting_fd(events, 0);
			char buf[4096];
			ssize_t len = read(interesting_fd, buf, 4096);
			if (len <= 0) {
				upsgi_log("[upsgi-stats-pusher] goodbye...\n");
				return;
			}
			upsgi_log("[upsgi-stats-pusher] message received from master: %.*s\n", (int) len, buf);
			continue;
		}

		time_t now = upsgi_now();
		struct upsgi_stats_pusher_instance *uspi = upsgi.stats_pusher_instances;
		struct upsgi_stats *us = NULL;
		while (uspi) {
			int delta = uspi->freq ? uspi->freq : upsgi.stats_pusher_default_freq;
			if (((uspi->last_run + delta) <= now) || (uspi->needs_retry && (uspi->next_retry <= now))) {
				if (uspi->needs_retry) uspi->retries++;
				if (uspi->raw) {
					uspi->pusher->func(uspi, now, NULL, 0);
				}
				else {
					if (!us) {
						us = upsgi_master_generate_stats();
						if (!us)
							goto next;
					}
					uspi->pusher->func(uspi, now, us->base, us->pos);
				}
				uspi->last_run = now;
				if (uspi->needs_retry && uspi->max_retries > 0 && uspi->retries < uspi->max_retries) {
					upsgi_log("[upsgi-stats-pusher] %s failed (%d), retry in %ds\n", uspi->pusher->name, uspi->retries, uspi->retry_delay);
					uspi->next_retry = now + uspi->retry_delay;
				} else if (uspi->needs_retry && uspi->retries >= uspi->max_retries) {
					upsgi_log("[upsgi-stats-pusher] %s failed and maximum number of retries was reached (%d)\n", uspi->pusher->name, uspi->retries);
					uspi->needs_retry = 0;
					uspi->retries = 0;
				} else if (uspi->retries) {
					upsgi_log("[upsgi-stats-pusher] retry succeeded for %s\n", uspi->pusher->name);
					uspi->retries = 0;
				}
			}
next:
			uspi = uspi->next;
		}

		if (us) {
			free(us->base);
			free(us);
		}
	}
}

void upsgi_stats_pusher_setup() {
	struct upsgi_string_list *usl = upsgi.requested_stats_pushers;
	while (usl) {
		char *ssp = upsgi_str(usl->value);
		struct upsgi_stats_pusher *pusher = NULL;
		char *colon = strchr(ssp, ':');
		if (colon) {
			*colon = 0;
		}
		pusher = upsgi_stats_pusher_get(ssp);
		if (!pusher) {
			upsgi_log("unable to find \"%s\" stats_pusher\n", ssp);
			free(ssp);
			exit(1);
		}
		char *arg = NULL;
		if (colon) {
			arg = colon + 1;
			*colon = ':';
		}
		upsgi_stats_pusher_add(pusher, arg);
		usl = usl->next;
		free(ssp);
	}
}

struct upsgi_stats_pusher *upsgi_register_stats_pusher(char *name, void (*func) (struct upsgi_stats_pusher_instance *, time_t, char *, size_t)) {

	struct upsgi_stats_pusher *pusher = upsgi.stats_pushers, *old_pusher = NULL;

	while (pusher) {
		old_pusher = pusher;
		pusher = pusher->next;
	}

	pusher = upsgi_calloc(sizeof(struct upsgi_stats_pusher));
	pusher->name = name;
	pusher->func = func;

	if (old_pusher) {
		old_pusher->next = pusher;
	}
	else {
		upsgi.stats_pushers = pusher;
	}

	return pusher;
}

static void stats_dump_var(char *k, uint16_t kl, char *v, uint16_t vl, void *data) {
	struct upsgi_stats *us = (struct upsgi_stats *) data;
	if (us->dirty) return;
	char *var = upsgi_concat3n(k, kl, "=", 1, v,vl);
	char *escaped_var = upsgi_malloc(((kl+vl+1)*2)+1);
	escape_json(var, strlen(var), escaped_var);
	free(var);
	if (upsgi_stats_str(us, escaped_var)) {
		us->dirty = 1;
		free(escaped_var);
		return;
	}
	free(escaped_var);
	if (upsgi_stats_comma(us)) {
		us->dirty = 1;
	}	
	return;
}

int upsgi_stats_dump_vars(struct upsgi_stats *us, struct upsgi_core *uc) {
	if (!uc->in_request) return 0;
	uint64_t pktsize = uc->req.len;
	if (!pktsize) return 0;
	char *dst = upsgi.workers[0].cores[0].buffer;
	memcpy(dst, uc->buffer+4, upsgi.buffer_size);
	// ok now check if something changed...
	if (!uc->in_request) return 0;
	if (uc->req.len != pktsize) return 0;
	if (memcmp(dst, uc->buffer+4, upsgi.buffer_size)) return 0;
	// nothing changed let's dump vars
	int ret = upsgi_hooked_parse(dst, pktsize, stats_dump_var, us);
	if (ret) return -1;
	if (us->dirty) return -1;
	if (upsgi_stats_str(us, "")) return -1;
	return 0;
}

int upsgi_stats_dump_request(struct upsgi_stats *us, struct upsgi_core *uc) {
	if (!uc->in_request) return 0;
	struct wsgi_request req = uc->req;
	upsgi_stats_keylong(us, "request_start", req.start_of_request_in_sec);

	return 0;
}
