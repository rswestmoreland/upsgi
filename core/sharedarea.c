#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

This is an high-performance memory area shared by all workers/cores/threads

Contrary to the caching subsystem it is 1-copy (caching for non-c apps is 2-copy)

Languages not allowing that kind of access should emulate it calling upsgi_malloc and then copying it back to
the language object.

The memory areas could be monitored for changes (read: cores can be suspended while waiting for values)

You can configure multiple areas specifying multiple --sharedarea options

This is a very low-level api, try to use it to build higher-level primitives or rely on the caching subsystem

*/

struct upsgi_sharedarea *upsgi_sharedarea_get_by_id(int id, uint64_t pos) {
	if (id > upsgi.sharedareas_cnt-1) return NULL;
	struct upsgi_sharedarea *sa = upsgi.sharedareas[id];
	if (pos > sa->max_pos) return NULL;
	return sa;
}

int upsgi_sharedarea_update(int id) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, 0);
        if (!sa) return -1;
	sa->updates++;
        return 0;
}

int upsgi_sharedarea_rlock(int id) {
	struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, 0);
        if (!sa) return -1;	
	upsgi_rlock(sa->lock);
	return 0;
}

int upsgi_sharedarea_wlock(int id) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, 0);
        if (!sa) return -1;
        upsgi_wlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_unlock(int id) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, 0);
        if (!sa) return -1;
        upsgi_rwunlock(sa->lock);
        return 0;
}


int64_t upsgi_sharedarea_read(int id, uint64_t pos, char *blob, uint64_t len) {
	struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + len > sa->max_pos + 1) return -1;
	if (len == 0) len = (sa->max_pos + 1) - pos;
	if (sa->honour_used && sa->used-pos < len) len = sa->used-pos ;
        upsgi_rlock(sa->lock);
        memcpy(blob, sa->area + pos, len);
        sa->hits++;
        upsgi_rwunlock(sa->lock);
        return len;
} 

int upsgi_sharedarea_write(int id, uint64_t pos, char *blob, uint64_t len) {
	struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
	if (!sa) return -1;
	if (pos + len > sa->max_pos + 1) return -1;
	upsgi_wlock(sa->lock);
	memcpy(sa->area + pos, blob, len);	
	sa->updates++;
	upsgi_rwunlock(sa->lock);
	return 0;
} 

int upsgi_sharedarea_read64(int id, uint64_t pos, int64_t *value) {
	int64_t rlen = upsgi_sharedarea_read(id, pos, (char *) value, 8);
	return rlen == 8 ? 0 : -1;
}

int upsgi_sharedarea_write64(int id, uint64_t pos, int64_t *value) {
	return upsgi_sharedarea_write(id, pos, (char *) value, 8);
}

int upsgi_sharedarea_read8(int id, uint64_t pos, int8_t *value) {
	int64_t rlen = upsgi_sharedarea_read(id, pos, (char *) value, 1);
	return rlen == 1 ? 0 : -1;
}

int upsgi_sharedarea_write8(int id, uint64_t pos, int8_t *value) {
	return upsgi_sharedarea_write(id, pos, (char *) value, 1);
}

int upsgi_sharedarea_read16(int id, uint64_t pos, int16_t *value) {
	int64_t rlen = upsgi_sharedarea_read(id, pos, (char *) value, 2);
	return rlen == 2 ? 0 : -1;
}

int upsgi_sharedarea_write16(int id, uint64_t pos, int16_t *value) {
	return upsgi_sharedarea_write(id, pos, (char *) value, 2);
}


int upsgi_sharedarea_read32(int id, uint64_t pos, int32_t *value) {
	int64_t rlen = upsgi_sharedarea_read(id, pos, (char *) value, 4);
	return rlen == 4 ? 0 : -1;
}

int upsgi_sharedarea_write32(int id, uint64_t pos, int32_t *value) {
	return upsgi_sharedarea_write(id, pos, (char *) value, 4);
}

int upsgi_sharedarea_inc8(int id, uint64_t pos, int8_t amount) {
	struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 1 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
	int8_t *n_ptr = (int8_t *) (sa->area + pos);
        *n_ptr+=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_inc16(int id, uint64_t pos, int16_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 2 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int16_t *n_ptr = (int16_t *) (sa->area + pos);
        *n_ptr+=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_inc32(int id, uint64_t pos, int32_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 4 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int32_t *n_ptr = (int32_t *) (sa->area + pos);
        *n_ptr+=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_inc64(int id, uint64_t pos, int64_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 4 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int64_t *n_ptr = (int64_t *) (sa->area + pos);
        *n_ptr+=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}


int upsgi_sharedarea_dec8(int id, uint64_t pos, int8_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 1 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int8_t *n_ptr = (int8_t *) (sa->area + pos);
        *n_ptr-=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_dec16(int id, uint64_t pos, int16_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 2 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int16_t *n_ptr = (int16_t *) (sa->area + pos);
        *n_ptr-=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_dec32(int id, uint64_t pos, int32_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 4 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int32_t *n_ptr = (int32_t *) (sa->area + pos);
        *n_ptr-=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}

int upsgi_sharedarea_dec64(int id, uint64_t pos, int64_t amount) {
        struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, pos);
        if (!sa) return -1;
        if (pos + 4 > sa->max_pos + 1) return -1;
        upsgi_wlock(sa->lock);
        int64_t *n_ptr = (int64_t *) (sa->area + pos);
        *n_ptr-=amount;
        sa->updates++;
        upsgi_rwunlock(sa->lock);
        return 0;
}



/*
	returns:
		0 -> on updates
		-1 -> on error
		-2 -> on timeout
*/
int upsgi_sharedarea_wait(int id, int freq, int timeout) {
	int waiting = 0;
	struct upsgi_sharedarea *sa = upsgi_sharedarea_get_by_id(id, 0);
	if (!sa) return -1;
	if (!freq) freq = 100;
	upsgi_rlock(sa->lock);
	uint64_t updates = sa->updates;
	upsgi_rwunlock(sa->lock);
	while(timeout == 0 || waiting == 0 || (timeout > 0 && waiting > 0 && (waiting/1000) < timeout)) {
		if (upsgi.wait_milliseconds_hook(freq)) {
			upsgi_rwunlock(sa->lock);
			return -1;
		}
		waiting += freq;
		// lock sa
		upsgi_rlock(sa->lock);
		if (sa->updates != updates) {
			upsgi_rwunlock(sa->lock);
			return 0;
		}
		// unlock sa
		upsgi_rwunlock(sa->lock);
	}
	return -2;
}

int upsgi_sharedarea_new_id() {
	int id = upsgi.sharedareas_cnt;
        upsgi.sharedareas_cnt++;
        if (!upsgi.sharedareas) {
                upsgi.sharedareas = upsgi_malloc(sizeof(struct upsgi_sharedarea *));
        }
        else {
                struct upsgi_sharedarea **usa = realloc(upsgi.sharedareas, ((sizeof(struct upsgi_sharedarea *)) * upsgi.sharedareas_cnt));
                if (!usa) {
                        upsgi_error("upsgi_sharedarea_init()/realloc()");
                        exit(1);
                }
                upsgi.sharedareas = usa;
        }
	return id;
}

static struct upsgi_sharedarea *announce_sa(struct upsgi_sharedarea *sa) {
	upsgi_log("sharedarea %d created at %p (%d pages, area at %p)\n", sa->id, sa, sa->pages, sa->area);
	return sa;
}


struct upsgi_sharedarea *upsgi_sharedarea_init_fd(int fd, uint64_t len, off_t offset) {
        int id = upsgi_sharedarea_new_id();
        upsgi.sharedareas[id] = upsgi_calloc_shared(sizeof(struct upsgi_sharedarea));
        upsgi.sharedareas[id]->area = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, offset); 
	if (upsgi.sharedareas[id]->area == MAP_FAILED) {
		upsgi_error("upsgi_sharedarea_init_fd()/mmap()");
		exit(1);
	}
        upsgi.sharedareas[id]->id = id;
        upsgi.sharedareas[id]->fd = fd;
        upsgi.sharedareas[id]->pages = len / (size_t) upsgi.page_size;
        if (len % (size_t) upsgi.page_size != 0) upsgi.sharedareas[id]->pages++;
        upsgi.sharedareas[id]->max_pos = len-1;
        char *id_str = upsgi_num2str(id);
        upsgi.sharedareas[id]->lock = upsgi_rwlock_init(upsgi_concat2("sharedarea", id_str));
        free(id_str);
        return announce_sa(upsgi.sharedareas[id]);
}


struct upsgi_sharedarea *upsgi_sharedarea_init(int pages) {
	int id = upsgi_sharedarea_new_id();
	upsgi.sharedareas[id] = upsgi_calloc_shared((size_t)upsgi.page_size * (size_t)(pages + 1));
	upsgi.sharedareas[id]->area = ((char *) upsgi.sharedareas[id]) + (size_t) upsgi.page_size;
	upsgi.sharedareas[id]->id = id;
	upsgi.sharedareas[id]->fd = -1;
	upsgi.sharedareas[id]->pages = pages;
	upsgi.sharedareas[id]->max_pos = ((size_t)upsgi.page_size * (size_t)pages) -1;
	char *id_str = upsgi_num2str(id);
	upsgi.sharedareas[id]->lock = upsgi_rwlock_init(upsgi_concat2("sharedarea", id_str));
	free(id_str);
	return announce_sa(upsgi.sharedareas[id]);
}

struct upsgi_sharedarea *upsgi_sharedarea_init_ptr(char *area, uint64_t len) {
        int id = upsgi_sharedarea_new_id();
        upsgi.sharedareas[id] = upsgi_calloc_shared(sizeof(struct upsgi_sharedarea));
        upsgi.sharedareas[id]->area = area;
        upsgi.sharedareas[id]->id = id;
        upsgi.sharedareas[id]->fd = -1;
        upsgi.sharedareas[id]->pages = len / (size_t) upsgi.page_size;
	if (len % (size_t) upsgi.page_size != 0) upsgi.sharedareas[id]->pages++;
        upsgi.sharedareas[id]->max_pos = len-1;
        char *id_str = upsgi_num2str(id);
        upsgi.sharedareas[id]->lock = upsgi_rwlock_init(upsgi_concat2("sharedarea", id_str));
        free(id_str);
	return announce_sa(upsgi.sharedareas[id]);
}

struct upsgi_sharedarea *upsgi_sharedarea_init_keyval(char *arg) {
	char *s_pages = NULL;
	char *s_file = NULL;
	char *s_fd = NULL;
	char *s_ptr = NULL;
	char *s_size = NULL;
	char *s_offset = NULL;
	if (upsgi_kvlist_parse(arg, strlen(arg), ',', '=',
		"pages", &s_pages,
		"file", &s_file,
		"fd", &s_fd,
		"ptr", &s_ptr,
		"size", &s_size,
		"offset", &s_offset,
		NULL)) {
		upsgi_log("invalid sharedarea keyval syntax\n");
		exit(1);
	}

	uint64_t len = 0;
	off_t offset = 0;
	int pages = 0;
	if (s_size) {
		if (strlen(s_size) > 2 && s_size[0] == '0' && s_size[1] == 'x') {
			len = strtoul(s_size+2, NULL, 16);
		}
		else {
			len = upsgi_n64(s_size);
		}
		pages = len / (size_t) upsgi.page_size;
        	if (len % (size_t) upsgi.page_size != 0) pages++;
	}

	if (s_offset) {
		if (strlen(s_offset) > 2 && s_offset[0] == '0' && s_offset[1] == 'x') {
                        offset = strtoul(s_offset+2, NULL, 16);
                }
                else {
                        offset = upsgi_n64(s_offset);
                }
	}
	
	if (s_pages) {
		pages = atoi(s_pages);	
	}

	char *area = NULL;
	struct upsgi_sharedarea *sa = NULL;

	int fd = -1;
	if (s_file) {
		fd = open(s_file, O_RDWR|O_SYNC);
		if (fd < 0) {
			upsgi_error_open(s_file);
			exit(1);
		}	
	}
	else if (s_fd) {
		fd = atoi(s_fd);
	}
	else if (s_ptr) {
	}

	if (pages) {
		if (fd > -1) {
			sa = upsgi_sharedarea_init_fd(fd, len, offset);
		}
		else if (area) {
			sa = upsgi_sharedarea_init_ptr(area, len);
		}
		else {
			sa = upsgi_sharedarea_init(pages);	
		}
	}
	else {
		upsgi_log("you need to set a size for a sharedarea !!! [%s]\n", arg);
		exit(1);
	}

	if (s_pages) free(s_pages);
	if (s_file) free(s_file);
	if (s_fd) free(s_fd);
	if (s_ptr) free(s_ptr);
	if (s_size) free(s_size);
	if (s_offset) free(s_offset);

	return sa;
}


void upsgi_sharedareas_init() {
	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, upsgi.sharedareas_list) {
		char *is_keyval = strchr(usl->value, '=');
		if (!is_keyval) {
			upsgi_sharedarea_init(atoi(usl->value));
		}
		else {
			upsgi_sharedarea_init_keyval(usl->value);
		}
	}
}
