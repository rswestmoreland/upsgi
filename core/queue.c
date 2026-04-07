#include "upsgi.h"

extern struct upsgi_server upsgi;

void upsgi_init_queue() {
	if (!upsgi.queue_blocksize)
		upsgi.queue_blocksize = 8192;

	if ((upsgi.queue_blocksize * upsgi.queue_size) % upsgi.page_size != 0) {
		upsgi_log("invalid queue size/blocksize %llu: must be a multiple of memory page size (%d bytes)\n", (unsigned long long) upsgi.queue_blocksize, upsgi.page_size);
		exit(1);
	}



	if (upsgi.queue_store) {
		upsgi.queue_filesize = upsgi.queue_blocksize * upsgi.queue_size + 16;
		int queue_fd;
		struct stat qst;

		if (stat(upsgi.queue_store, &qst)) {
			upsgi_log("creating a new queue store file: %s\n", upsgi.queue_store);
			queue_fd = open(upsgi.queue_store, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
			if (queue_fd >= 0) {
				// fill the queue store
				if (ftruncate(queue_fd, upsgi.queue_filesize)) {
					upsgi_log("ftruncate()");
					exit(1);
				}
			}
		}
		else {
			if ((size_t) qst.st_size != upsgi.queue_filesize || !S_ISREG(qst.st_mode)) {
				upsgi_log("invalid queue store file. Please remove it or fix queue blocksize/items to match its size\n");
				exit(1);
			}
			queue_fd = open(upsgi.queue_store, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
			upsgi_log("recovered queue from backing store file: %s\n", upsgi.queue_store);
		}

		if (queue_fd < 0) {
			upsgi_error_open(upsgi.queue_store);
			exit(1);
		}
		upsgi.queue = mmap(NULL, upsgi.queue_filesize, PROT_READ | PROT_WRITE, MAP_SHARED, queue_fd, 0);

		// fix header
		upsgi.queue_header = upsgi.queue;
		upsgi.queue += 16;
		close(queue_fd);
	}
	else {
		upsgi.queue = mmap(NULL, (upsgi.queue_blocksize * upsgi.queue_size) + 16, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
		// fix header
		upsgi.queue_header = upsgi.queue;
		upsgi.queue += 16;
		upsgi.queue_header->pos = 0;
		upsgi.queue_header->pull_pos = 0;
	}
	if (upsgi.queue == MAP_FAILED) {
		upsgi_error("mmap()");
		exit(1);
	}



	upsgi.queue_lock = upsgi_rwlock_init("queue");

	upsgi_log("*** Queue subsystem initialized: %luMB preallocated ***\n", (upsgi.queue_blocksize * upsgi.queue_size) / (1024 * 1024));
}

char *upsgi_queue_get(uint64_t index, uint64_t * size) {

	struct upsgi_queue_item *uqi;
	char *ptr = (char *) upsgi.queue;

	if (index >= upsgi.queue_size)
		return NULL;

	ptr = ptr + (upsgi.queue_blocksize * index);

	uqi = (struct upsgi_queue_item *) ptr;

	*size = uqi->size;

	return ptr + sizeof(struct upsgi_queue_item);

}


char *upsgi_queue_pop(uint64_t * size) {

	struct upsgi_queue_item *uqi;
	char *ptr = (char *) upsgi.queue;

	if (upsgi.queue_header->pos == 0) {
		upsgi.queue_header->pos = upsgi.queue_size - 1;
	}
	else {
		upsgi.queue_header->pos--;
	}

	ptr = ptr + (upsgi.queue_blocksize * upsgi.queue_header->pos);
	uqi = (struct upsgi_queue_item *) ptr;

	if (!uqi->size)
		return NULL;

	*size = uqi->size;
	// remove item
	uqi->size = 0;

	return ptr + sizeof(struct upsgi_queue_item);
}


char *upsgi_queue_pull(uint64_t * size) {

	struct upsgi_queue_item *uqi;
	char *ptr = (char *) upsgi.queue;

	ptr = ptr + (upsgi.queue_blocksize * upsgi.queue_header->pull_pos);
	uqi = (struct upsgi_queue_item *) ptr;

	if (!uqi->size)
		return NULL;

	*size = uqi->size;

	upsgi.queue_header->pull_pos++;

	if (upsgi.queue_header->pull_pos >= upsgi.queue_size)
		upsgi.queue_header->pull_pos = 0;

	// remove item
	uqi->size = 0;

	return ptr + sizeof(struct upsgi_queue_item);

}

int upsgi_queue_push(char *message, uint64_t size) {

	struct upsgi_queue_item *uqi;
	char *ptr = (char *) upsgi.queue;

	if (size > upsgi.queue_blocksize - sizeof(struct upsgi_queue_item))
		return 0;

	if (!size)
		return 0;

	ptr = ptr + (upsgi.queue_blocksize * upsgi.queue_header->pos);
	uqi = (struct upsgi_queue_item *) ptr;

	ptr += sizeof(struct upsgi_queue_item);

	uqi->size = size;
	uqi->ts = upsgi_now();
	memcpy(ptr, message, size);

	upsgi.queue_header->pos++;

	if (upsgi.queue_header->pos >= upsgi.queue_size)
		upsgi.queue_header->pos = 0;

	return 1;
}

int upsgi_queue_set(uint64_t pos, char *message, uint64_t size) {

	struct upsgi_queue_item *uqi;
	char *ptr = (char *) upsgi.queue;

	if (size > upsgi.queue_blocksize + sizeof(struct upsgi_queue_item))
		return 0;

	if (!size)
		return 0;

	if (pos >= upsgi.queue_size)
		return 0;

	ptr = ptr + (upsgi.queue_blocksize * pos);
	uqi = (struct upsgi_queue_item *) ptr;

	ptr += sizeof(struct upsgi_queue_item);

	uqi->size = size;
	uqi->ts = upsgi_now();
	memcpy(ptr, message, size);

	return 1;
}
