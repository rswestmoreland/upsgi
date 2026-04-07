#include "upsgi.h"

extern struct upsgi_server upsgi;
#define cache_item(x) (struct upsgi_cache_item *) (((char *)uc->items) + ((sizeof(struct upsgi_cache_item)+uc->keysize) * x))

// block bitmap manager

/* how the cache bitmap works:

	a bitmap is a shared memory area allocated when requested by the user with --cache2

	Each block maps to a bit in the bitmap. If the corresponding bit is cleared
	the block is usable otherwise the block scanner will search for the next one.

	Object can be placed only on consecutive blocks, fragmentation is not allowed.

	To increase the scan performance, a 64bit pointer to the last used bit + 1 is hold

	To search for free blocks you run

	uint64_t upsgi_cache_find_free_block(struct upsgi_cache *uc, size_t need)

	where need is the size of the object

*/

static void cache_full(struct upsgi_cache *uc) {
	uint64_t i;
	int clear_cache = uc->clear_on_full;

	if (!uc->ignore_full) {
        	if (uc->purge_lru)
                	upsgi_log("LRU item will be purged from cache \"%s\"\n", uc->name);
                else
                	upsgi_log("*** DANGER cache \"%s\" is FULL !!! ***\n", uc->name);
	}

        uc->full++;

        if (uc->purge_lru && uc->lru_head)
        	upsgi_cache_del2(uc, NULL, 0, uc->lru_head, UPSGI_CACHE_FLAG_LOCAL);

	// we do not need locking here !
	if (uc->sweep_on_full) {
		uint64_t removed = 0;
		uint64_t now = (uint64_t) upsgi_now();
		if (uc->next_scan <= now) {
			uc->next_scan = now + uc->sweep_on_full;
			for (i = 1; i < uc->max_items; i++) {
				struct upsgi_cache_item *uci = cache_item(i);
				if (uci->expires > 0 && uci->expires <= now) {
					if (!upsgi_cache_del2(uc, NULL, 0, i, 0)) {
						removed++;
					}
				}
			}
		}
		if (removed) {
			clear_cache = 0;
		}
	}

	if (clear_cache) {
                for (i = 1; i < uc->max_items; i++) {
                	upsgi_cache_del2(uc, NULL, 0, i, 0);
                }
	}
}

static uint64_t upsgi_cache_find_free_blocks(struct upsgi_cache *uc, uint64_t need) {
	// how many blocks we need ?
	uint64_t needed_blocks = need/uc->blocksize;
	if (need % uc->blocksize > 0) needed_blocks++;

	// which is the first free bit?
	uint64_t bitmap_byte = 0;
	uint8_t bitmap_bit = 0;
	
	if (uc->blocks_bitmap_pos > 0) {
		bitmap_byte = uc->blocks_bitmap_pos/8;
		bitmap_bit = uc->blocks_bitmap_pos % 8;
	}

	// ok we now have the start position, let's search for contiguous blocks
	uint8_t *bitmap = uc->blocks_bitmap;
	uint64_t base = 0xffffffffffffffffLLU;
	uint8_t base_bit = 0;
	uint64_t j;
	uint64_t found = 0;
	uint64_t need_to_scan = uc->blocks_bitmap_size;
	// we make an addition round for the corner case of a single byte map not starting from 0
	if (bitmap_bit > 0) need_to_scan++;
	j = bitmap_byte;
	//upsgi_log("start scanning %llu bytes starting from %llu need: %llu\n", (unsigned long long) need_to_scan, (unsigned long long) bitmap_byte, (unsigned long long) needed_blocks);
	while(need_to_scan) {
		uint8_t num = bitmap[j];
		uint8_t i;
		uint8_t bit_pos = 0;
		if (j == bitmap_byte) {
			i = 1 << (7-bitmap_bit);
			bit_pos = bitmap_bit;
		}
		else {
			i = 1 <<7;
		}	
		while(i > 0) {
			// used block
                	if (num & i) {
                                found = 0;
                                base = 0xffffffffffffffffLLU;
                                base_bit = 0;
                        }
			// free block
                        else {
                                if (base == 0xffffffffffffffffLLU ) {
                                        base = j;
					base_bit = bit_pos;
                                }
                                found++;
                                if (found == needed_blocks) {
#ifdef UPSGI_DEBUG
                                        printf("found %llu consecutive bit starting from byte %llu\n", (unsigned long long) found, (unsigned long long) base);
#endif
					return ((base*8) + base_bit);
                                }
                        }
                        i >>= 1;
			bit_pos++;
                }
		j++;
		need_to_scan--;
		// check for overlap (that is not supported)
		if (j >= uc->blocks_bitmap_size) {
			j = 0;
			found = 0;
			base = 0xffffffffffffffffLLU;
			base_bit = 0;
			// we use bitmap_bit only at the first round
			bitmap_bit = 0;
		}
	}

	
	// no more free blocks
	return 0xffffffffffffffffLLU;
}

static uint64_t cache_mark_blocks(struct upsgi_cache *uc, uint64_t index, uint64_t len) {
	uint64_t needed_blocks = len/uc->blocksize;
	if (len % uc->blocksize > 0) needed_blocks++;

	uint64_t first_byte = index/8;
	uint8_t first_byte_bit = index % 8;
	// offset starts with 0, so actual last bit is index + needed_blocks - 1
	uint64_t last_byte = (index + needed_blocks - 1) / 8;
	uint8_t last_byte_bit = (index + needed_blocks - 1) % 8;
	
	uint64_t needed_bytes = (last_byte - first_byte) + 1;

	//upsgi_log("%llu %u %llu %u\n", first_byte, first_byte_bit, last_byte, last_byte_bit);

	uint8_t mask = 0xff >> first_byte_bit;
	
	if (needed_bytes == 1) {
		// kinda hacky, but it does the job
		mask >>= (7 - last_byte_bit);
		mask <<= (7 - last_byte_bit);
	}
	
	uc->blocks_bitmap[first_byte] |= mask;

	if (needed_bytes > 1) {
		mask = 0xff << (7 - last_byte_bit);
		uc->blocks_bitmap[last_byte] |= mask;
	}

	if (needed_bytes > 2) {
		uint8_t *ptr = &uc->blocks_bitmap[first_byte+1];
		memset(ptr, 0xff, needed_bytes-2);
	}
	return needed_blocks;
}

static void cache_unmark_blocks(struct upsgi_cache *uc, uint64_t index, uint64_t len) {
	uint64_t needed_blocks = len/uc->blocksize;
        if (len % uc->blocksize > 0) needed_blocks++;

        uint64_t first_byte = index/8;
        uint8_t first_byte_bit = index % 8;
        // offset starts with 0, so actual last bit is index + needed_blocks - 1
        uint64_t last_byte = (index + needed_blocks - 1)/8;
        uint8_t last_byte_bit = (index + needed_blocks - 1) % 8;

	uint64_t needed_bytes = (last_byte - first_byte) + 1;

	uint8_t mask = 0xff >> first_byte_bit;

	if (needed_bytes == 1) {
                // kinda hacky, but it does the job
                mask >>= (7 - last_byte_bit);
                mask <<= (7 - last_byte_bit);
        }
	
        // here we use AND (0+0 = 0 | 1+0 = 0 | 0+1 = 0| 1+1 = 1)
    	// 0 in mask means "unmark", 1 in mask means "do not change" 
    	// so we need to invert the mask 
        uc->blocks_bitmap[first_byte] &= ~mask;

        if (needed_bytes > 1) {
                mask = 0xff << (7 - last_byte_bit);
                uc->blocks_bitmap[last_byte] &= ~mask;
        }

        if (needed_bytes > 2) {
                uint8_t *ptr = &uc->blocks_bitmap[first_byte+1];
                memset(ptr, 0, needed_bytes-2);
        }
}

static void cache_send_udp_command(struct upsgi_cache *, char *, uint16_t, char *, uint16_t, uint64_t, uint8_t);

static void cache_sync_hook(char *k, uint16_t kl, char *v, uint16_t vl, void *data) {
	struct upsgi_cache *uc = (struct upsgi_cache *) data;
	if (!upsgi_strncmp(k, kl, "items", 5)) {
		size_t num = upsgi_str_num(v, vl);
		if (num != uc->max_items) {
			upsgi_log("[cache-sync] invalid cache size, expected %llu received %llu\n", (unsigned long long) uc->max_items, (unsigned long long) num);
			exit(1);
		}
	}
	if (!upsgi_strncmp(k, kl, "blocksize", 9)) {
		size_t num = upsgi_str_num(v, vl);
		if (num != uc->blocksize) {
			upsgi_log("[cache-sync] invalid cache block size, expected %llu received %llu\n", (unsigned long long) uc->blocksize, (unsigned long long) num);
			exit(1);
		}
	}
}

static void upsgi_cache_add_items(struct upsgi_cache *uc) {
	struct upsgi_string_list *usl = upsgi.add_cache_item;
	while(usl) {
		char *space = strchr(usl->value, ' ');
		char *key = usl->value;
		uint16_t key_len;
		if (space) {
			// need to skip ?
			if (upsgi_strncmp(uc->name, uc->name_len, usl->value, space-usl->value)) {
				goto next;
			}
			key = space+1;
		}
		char *value = strchr(key, '=');
		if (!value) {
			upsgi_log("[cache] unable to store item %s\n", usl->value);
			goto next;
		}
		key_len = value - key;
		value++;
		uint64_t len = (usl->value + usl->len) - value;
		upsgi_wlock(uc->lock);
		if (!upsgi_cache_set2(uc, key, key_len, value, len, 0, 0)) {
			upsgi_log("[cache] stored \"%.*s\" in \"%s\"\n", key_len, key, uc->name);
		}
		else {
			upsgi_log("[cache-error] unable to store \"%.*s\" in \"%s\"\n", key_len, key, uc->name);
		}
		upsgi_rwunlock(uc->lock);
next:
		usl = usl->next;
	}
}

static void upsgi_cache_load_files(struct upsgi_cache *uc) {

	struct upsgi_string_list *usl = upsgi.load_file_in_cache;
	while(usl) {
		size_t len = 0;
		char *value = NULL;
		char *key = usl->value;
		uint16_t key_len = usl->len;
		char *space = strchr(usl->value, ' ');
		if (space) {
			// need to skip ?
			if (upsgi_strncmp(uc->name, uc->name_len, usl->value, space-usl->value)) {
				goto next;
			}
			key = space+1;
			key_len = usl->len - ((space-usl->value)+1);
		}
		value = upsgi_open_and_read(key, &len, 0, NULL);
		if (value) {
			upsgi_wlock(uc->lock);
			if (!upsgi_cache_set2(uc, key, key_len, value, len, 0, 0)) {
				upsgi_log("[cache] stored \"%.*s\" in \"%s\"\n", key_len, key, uc->name);
			}		
			else {
				upsgi_log("[cache-error] unable to store \"%.*s\" in \"%s\"\n", key_len, key, uc->name);
			}
			upsgi_rwunlock(uc->lock);
			free(value);
		}
		else {
			upsgi_log("[cache-error] unable to read file \"%.*s\"\n", key_len, key);
		}
next:
		usl = usl->next;
	}

#ifdef UPSGI_ZLIB
	usl = upsgi.load_file_in_cache_gzip;
        while(usl) {
                size_t len = 0;
                char *value = NULL;
                char *key = usl->value;
                uint16_t key_len = usl->len;
                char *space = strchr(usl->value, ' ');
                if (space) {
                        // need to skip ?
                        if (upsgi_strncmp(uc->name, uc->name_len, usl->value, space-usl->value)) {
                                goto next2;
                        }
                        key = space+1;
                        key_len = usl->len - ((space-usl->value)+1);
                }
                value = upsgi_open_and_read(key, &len, 0, NULL);
                if (value) {
			struct upsgi_buffer *gzipped = upsgi_gzip(value, len);
			if (gzipped) {
                        	upsgi_wlock(uc->lock);
                        	if (!upsgi_cache_set2(uc, key, key_len, gzipped->buf, gzipped->len, 0, 0)) {
                                	upsgi_log("[cache-gzip] stored \"%.*s\" in \"%s\"\n", key_len, key, uc->name);
                        	}
                        	upsgi_rwunlock(uc->lock);
				upsgi_buffer_destroy(gzipped);
			}
                        free(value);
                }
next2:
                usl = usl->next;
        }
#endif
}



void upsgi_cache_init(struct upsgi_cache *uc) {

	uc->hashtable = upsgi_calloc_shared(sizeof(uint64_t) * uc->hashsize);
	uc->unused_blocks_stack = upsgi_calloc_shared(sizeof(uint64_t) * uc->max_items);
	uc->unused_blocks_stack_ptr = 0;
	uc->filesize = ( (sizeof(struct upsgi_cache_item)+uc->keysize) * uc->max_items) + (uc->blocksize * uc->blocks);

	uint64_t i;
	for (i = 1; i < uc->max_items; i++) {
        	uc->unused_blocks_stack_ptr++;
                uc->unused_blocks_stack[uc->unused_blocks_stack_ptr] = i;
        }

	if (uc->use_blocks_bitmap) {
		uc->blocks_bitmap_size = uc->blocks/8;
                uint8_t m = uc->blocks % 8;
		if (m > 0) uc->blocks_bitmap_size++;
		uc->blocks_bitmap = upsgi_calloc_shared(uc->blocks_bitmap_size);
		if (m > 0) {
			uc->blocks_bitmap[uc->blocks_bitmap_size-1] = 0xff >> m;
		}
	}

	//upsgi.cache_items = (struct upsgi_cache_item *) mmap(NULL, sizeof(struct upsgi_cache_item) * upsgi.cache_max_items, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (uc->store) {
		int cache_fd;
		struct stat cst;

        if (uc->store_delete && !stat(uc->store, &cst) && ((size_t) cst.st_size != uc->filesize || !S_ISREG(cst.st_mode))) {
            upsgi_log("Removing invalid cache store file: %s\n", uc->store);
            if (unlink(uc->store) != 0) {
                upsgi_log("Cannot remove invalid cache store file: %s\n", uc->store);
                exit(1);
            }
        }

		if (stat(uc->store, &cst)) {
			upsgi_log("creating a new cache store file: %s\n", uc->store);
			cache_fd = open(uc->store, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
			if (cache_fd >= 0) {
				// fill the caching store
				if (ftruncate(cache_fd, uc->filesize)) {
					upsgi_log("ftruncate()");
					exit(1);
				}
			}
		}
		else {
			if ((size_t) cst.st_size != uc->filesize || !S_ISREG(cst.st_mode)) {
				upsgi_log("invalid cache store file. Please remove it or fix cache blocksize/items to match its size\n");
				exit(1);
			}
			cache_fd = open(uc->store, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
			upsgi_log("recovered cache from backing store file: %s\n", uc->store);
		}

		if (cache_fd < 0) {
			upsgi_error_open(uc->store);
			exit(1);
		}
		uc->items = (struct upsgi_cache_item *) mmap(NULL, uc->filesize, PROT_READ | PROT_WRITE, MAP_SHARED, cache_fd, 0);
		if (uc->items == MAP_FAILED) {
			upsgi_error("upsgi_cache_init()/mmap() [with store]");
			exit(1);
		}

		upsgi_cache_fix(uc);
		close(cache_fd);
	}
	else {
		uc->items = (struct upsgi_cache_item *) mmap(NULL, uc->filesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
		if (uc->items == MAP_FAILED) {
			upsgi_error("upsgi_cache_init()/mmap()");
			exit(1);
		}
		uint64_t i;
		for (i = 0; i < uc->max_items; i++) {
			// here we only need to clear the item header
			memset(cache_item(i), 0, sizeof(struct upsgi_cache_item));
		}
	}

	uc->data = ((char *)uc->items) + ((sizeof(struct upsgi_cache_item)+uc->keysize) * uc->max_items);

	if (uc->name) {
		// can't free that until shutdown
		char *lock_name = upsgi_concat2("cache_", uc->name);
		uc->lock = upsgi_rwlock_init(lock_name);
	}
	else {
		uc->lock = upsgi_rwlock_init("cache");
	}

	upsgi_log("*** Cache \"%s\" initialized: %lluMB (key: %llu bytes, keys: %llu bytes, data: %llu bytes, bitmap: %llu bytes) preallocated ***\n",
			uc->name,
			(unsigned long long) uc->filesize / (1024 * 1024),
			(unsigned long long) sizeof(struct upsgi_cache_item)+uc->keysize,
			(unsigned long long) ((sizeof(struct upsgi_cache_item)+uc->keysize) * uc->max_items), (unsigned long long) (uc->blocksize * uc->blocks),
			(unsigned long long) uc->blocks_bitmap_size);

	upsgi_cache_setup_nodes(uc);

	uc->udp_node_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (uc->udp_node_socket < 0) {
		upsgi_error("[cache-udp-node] socket()");
		exit(1);
	}
	upsgi_socket_nb(uc->udp_node_socket);

	upsgi_cache_sync_from_nodes(uc);

	upsgi_cache_load_files(uc);

	upsgi_cache_add_items(uc);

}

static uint64_t check_lazy(struct upsgi_cache *uc, struct upsgi_cache_item *uci, uint64_t slot) {
	if (!uci->expires || !uc->lazy_expire) return slot;
	uint64_t now = (uint64_t) upsgi_now();
	// expired ?
	if (uci->expires <= now) {
		upsgi_cache_del2(uc, NULL, 0, slot, UPSGI_CACHE_FLAG_LOCAL);
		return 0;
	}
	return slot;
}

static uint64_t upsgi_cache_get_index(struct upsgi_cache *uc, char *key, uint16_t keylen) {

	uint32_t hash = uc->hash->func(key, keylen);
	uint32_t hash_key = hash % uc->hashsize;

	uint64_t slot = uc->hashtable[hash_key];

	// optimization
	if (slot == 0) return 0;

	//upsgi_log("hash_key = %lu slot = %llu\n", hash_key, (unsigned long long) slot);

	struct upsgi_cache_item *uci = cache_item(slot);
	uint64_t rounds = 0;

	// first round
	if (uci->hash % uc->hashsize != hash_key)
		return 0;
	if (uci->hash != hash)
		goto cycle;
	if (uci->keysize != keylen)
		goto cycle;
	if (memcmp(uci->key, key, keylen))
		goto cycle;

	return check_lazy(uc, uci, slot);

cycle:
	while (uci->next) {
		slot = uci->next;
		uci = cache_item(slot);
		rounds++;
		if (rounds > uc->max_items) {
			upsgi_log("ALARM !!! cache-loop (and potential deadlock) detected slot = %lu prev = %lu next = %lu\n", slot, uci->prev, uci->next);
			// terrible case: the whole upsgi stack can deadlock, leaving only the master alive
			// if the master is available, trigger a brutal reload
			if (upsgi.master_process) {
				kill(upsgi.workers[0].pid, SIGTERM);
			}
			// otherwise kill the current worker (could be pretty useless...)
			else {
				exit(1);
			}
		}
		if (uci->hash != hash)
			continue;
		if (uci->keysize != keylen)
			continue;
		if (!memcmp(uci->key, key, keylen)) {
			return check_lazy(uc, uci, slot);
		}
	}

	return 0;
}

uint32_t upsgi_cache_exists2(struct upsgi_cache *uc, char *key, uint16_t keylen) {

	return upsgi_cache_get_index(uc, key, keylen);
}

static void lru_remove_item(struct upsgi_cache *uc, uint64_t index)
{
	struct upsgi_cache_item *prev, *next, *curr = cache_item(index);

	if (curr->lru_next) {
		next = cache_item(curr->lru_next);
		next->lru_prev = curr->lru_prev;
	} else
		uc->lru_tail = curr->lru_prev;

	if (curr->lru_prev) {
		prev = cache_item(curr->lru_prev);
		prev->lru_next = curr->lru_next;
	} else
		uc->lru_head = curr->lru_next;
}

static void lru_add_item(struct upsgi_cache *uc, uint64_t index)
{
	struct upsgi_cache_item *prev, *curr = cache_item(index);

	if (uc->lru_tail) {
		prev = cache_item(uc->lru_tail);
		prev->lru_next = index;
	} else
		uc->lru_head = index;

	curr->lru_next = 0;
	curr->lru_prev = uc->lru_tail;
	uc->lru_tail = index;
}

char *upsgi_cache_get2(struct upsgi_cache *uc, char *key, uint16_t keylen, uint64_t * valsize) {

	uint64_t index = upsgi_cache_get_index(uc, key, keylen);

	if (index) {
		struct upsgi_cache_item *uci = cache_item(index);
		if (uci->flags & UPSGI_CACHE_FLAG_UNGETTABLE)
			return NULL;
		*valsize = uci->valsize;
		if (uc->purge_lru) {
			lru_remove_item(uc, index);
			lru_add_item(uc, index);
		}
		uci->hits++;
		uc->hits++;
		return uc->data + (uci->first_block * uc->blocksize);
	}

	uc->miss++;

	return NULL;
}

int64_t upsgi_cache_num2(struct upsgi_cache *uc, char *key, uint16_t keylen) {

        uint64_t index = upsgi_cache_get_index(uc, key, keylen);

        if (index) {
                struct upsgi_cache_item *uci = cache_item(index);
		if (uci->flags & UPSGI_CACHE_FLAG_UNGETTABLE)
                        return 0;
                uci->hits++;
                uc->hits++;
		int64_t *num = (int64_t *) (uc->data + (uci->first_block * uc->blocksize));
		return *num;
        }

        uc->miss++;
	return 0;
}

char *upsgi_cache_get3(struct upsgi_cache *uc, char *key, uint16_t keylen, uint64_t * valsize, uint64_t *expires) {

        uint64_t index = upsgi_cache_get_index(uc, key, keylen);

        if (index) {
                struct upsgi_cache_item *uci = cache_item(index);
                if (uci->flags & UPSGI_CACHE_FLAG_UNGETTABLE)
                        return NULL;
                *valsize = uci->valsize;
		if (expires)
			*expires = uci->expires;
		if (uc->purge_lru) {
			lru_remove_item(uc, index);
			lru_add_item(uc, index);
		}
                uci->hits++;
                uc->hits++;
                return uc->data + (uci->first_block * uc->blocksize);
        }

        uc->miss++;

        return NULL;
}

char *upsgi_cache_get4(struct upsgi_cache *uc, char *key, uint16_t keylen, uint64_t * valsize, uint64_t *hits) {

        uint64_t index = upsgi_cache_get_index(uc, key, keylen);

        if (index) {
                struct upsgi_cache_item *uci = cache_item(index);
                if (uci->flags & UPSGI_CACHE_FLAG_UNGETTABLE)
                        return NULL;
                *valsize = uci->valsize;
                if (hits)
                        *hits = uci->hits;
                uci->hits++;
                uc->hits++;
                return uc->data + (uci->first_block * uc->blocksize);
        }

        uc->miss++;

        return NULL;
}


int upsgi_cache_del2(struct upsgi_cache *uc, char *key, uint16_t keylen, uint64_t index, uint16_t flags) {


	struct upsgi_cache_item *uci;
	int ret = -1;

	if (!index) index = upsgi_cache_get_index(uc, key, keylen);

	if (index) {
		uci = cache_item(index);
		if (uci->keysize > 0) {
			// unmark blocks
			if (uc->blocks_bitmap) cache_unmark_blocks(uc, uci->first_block, uci->valsize);
			// put back the block in unused stack
			uc->unused_blocks_stack_ptr++;
			uc->unused_blocks_stack[uc->unused_blocks_stack_ptr] = index;

			// unlink prev and next (if any)
			if (uci->prev) {
                        	struct upsgi_cache_item *ucii = cache_item(uci->prev);
                        	ucii->next = uci->next;
                	}
                	else {
                        	// set next as the new entry point (could be 0)
                        	uc->hashtable[uci->hash % uc->hashsize] = uci->next;
                	}

                	if (uci->next) {
                        	struct upsgi_cache_item *ucii = cache_item(uci->next);
                        	ucii->prev = uci->prev;
                	}

                	if (!uci->prev && !uci->next) {
                        	// reset hashtable entry
                        	uc->hashtable[uci->hash % uc->hashsize] = 0;
                	}

			if (uc->purge_lru)
				lru_remove_item(uc, index);

			uc->n_items--;
		}

		ret = 0;

		uci->keysize = 0;
		uci->valsize = 0;
		uci->hash = 0;
		uci->prev = 0;
		uci->next = 0;
		uci->expires = 0;

		if (uc->use_last_modified) {
			uc->last_modified_at = upsgi_now();
		}
	}

	if (uc->nodes && ret == 0 && !(flags & UPSGI_CACHE_FLAG_LOCAL)) {
                cache_send_udp_command(uc, key, keylen, NULL, 0, 0, 11);
        }

	return ret;
}

void upsgi_cache_fix(struct upsgi_cache *uc) {

	uint64_t i;
	unsigned long long restored = 0;
	uint64_t next_scan = 0;

	// reset unused blocks
	uc->unused_blocks_stack_ptr = 0;

	for (i = 1; i < uc->max_items; i++) {
		// valid record ?
		struct upsgi_cache_item *uci = cache_item(i);
		if (uci->keysize) {
			if (!uci->prev) {
				// put value in hash_table
				uc->hashtable[uci->hash % uc->hashsize] = i;
			}
			if (uci->expires && (!next_scan || next_scan > uci->expires)) {
				next_scan = uci->expires;
			}
			if (!uc->lru_head && !uci->lru_prev) {
				uc->lru_head = i;
			}
			if (!uc->lru_tail && !uci->lru_next) {
				uc->lru_tail = i;
			}
			restored++;
		}
		else {
			// put this record in unused stack
			uc->unused_blocks_stack_ptr++;
			uc->unused_blocks_stack[uc->unused_blocks_stack_ptr] = i;
		}
	}

	uc->next_scan = next_scan;
	uc->n_items = restored;
	upsgi_log("[upsgi-cache] restored %llu items\n", uc->n_items);
}

int upsgi_cache_set2(struct upsgi_cache *uc, char *key, uint16_t keylen, char *val, uint64_t vallen, uint64_t expires, uint64_t flags) {

	uint64_t index = 0, last_index = 0;

	struct upsgi_cache_item *uci, *ucii;

	// used to reset key allocation in bitmap mode

	int ret = -1;
	time_t now = 0;

	if (!keylen || !vallen)
		return -1;

	if (keylen > uc->keysize)
		return -1;

	if (vallen > uc->max_item_size) return -1;

	if ((flags & UPSGI_CACHE_FLAG_MATH) && vallen != 8) return -1;

	//upsgi_log("putting cache data in key %.*s %d\n", keylen, key, vallen);
	index = upsgi_cache_get_index(uc, key, keylen);
	if (!index) {
		if (!uc->unused_blocks_stack_ptr) {
			cache_full(uc);
			if (!uc->unused_blocks_stack_ptr)
				goto end;
		}

		index = uc->unused_blocks_stack[uc->unused_blocks_stack_ptr];
		uc->unused_blocks_stack_ptr--;

		uci = cache_item(index);
		if (!uc->blocks_bitmap) {
			uci->first_block = index;
		}
		else {
			uci->first_block = upsgi_cache_find_free_blocks(uc, vallen);
			if (uci->first_block == 0xffffffffffffffffLLU) {
				uc->unused_blocks_stack_ptr++;
				cache_full(uc);
                                goto end;
			}
			// mark used blocks;
			uint64_t needed_blocks = cache_mark_blocks(uc, uci->first_block, vallen);
			// optimize the scan
			if (uci->first_block + needed_blocks >= uc->blocks) {
                        	uc->blocks_bitmap_pos = 0;
                        }
                        else {
                        	uc->blocks_bitmap_pos = uci->first_block + needed_blocks;
                        }
		}
		if (uc->purge_lru)
			lru_add_item(uc, index);
		else if (expires && !(flags & UPSGI_CACHE_FLAG_ABSEXPIRE)) {
			now = upsgi_now();
			expires += now;
			if (!uc->next_scan || uc->next_scan > expires)
				uc->next_scan = expires;
		}
		uci->expires = expires;
		uci->hash = uc->hash->func(key, keylen);
		uci->hits = 0;
		uci->flags = flags;
		memcpy(uci->key, key, keylen);

		if ( !(flags & UPSGI_CACHE_FLAG_MATH)) {
			memcpy(((char *) uc->data) + (uci->first_block * uc->blocksize), val, vallen);
		}		
		// ok math operations here
		else {
			int64_t *num = (int64_t *)(((char *) uc->data) + (uci->first_block * uc->blocksize));
			*num = uc->math_initial;
			int64_t *delta = (int64_t *) val;
			if (flags & UPSGI_CACHE_FLAG_INC) {
				*num += *delta;
			}
			else if (flags & UPSGI_CACHE_FLAG_DEC) {
				*num -= *delta;
			}
			else if (flags & UPSGI_CACHE_FLAG_MUL) {
				*num *= *delta;
			}
			else if (flags & UPSGI_CACHE_FLAG_DIV) {
				if (*delta == 0) {
					*num = 0;
				}
				else {
					*num /= *delta;
				}
			}
		}

		// set this as late as possible (to reduce races risk)

		uci->valsize = vallen;
		uci->keysize = keylen;
		ret = 0;
		// now put the value in the hashtable
		uint32_t slot = uci->hash % uc->hashsize;
		// reset values
		uci->prev = 0;
		uci->next = 0;

		last_index = uc->hashtable[slot];
		if (last_index == 0) {
			uc->hashtable[slot] = index;
		}
		else {
			// append to first available next
			ucii = cache_item(last_index);
			while (ucii->next) {
				last_index = ucii->next;
				ucii = cache_item(last_index);
			}
			ucii->next = index;
			uci->prev = last_index;
		}

		uc->n_items++ ;
	}
	else if (flags & UPSGI_CACHE_FLAG_UPDATE) {
		uci = cache_item(index);
		if (!(flags & UPSGI_CACHE_FLAG_FIXEXPIRE)) {
			if (uc->purge_lru) {
				lru_remove_item(uc, index);
				lru_add_item(uc, index);
			} else if (expires && !(flags & UPSGI_CACHE_FLAG_ABSEXPIRE)) {
				now = upsgi_now();
				expires += now;
				if (!uc->next_scan || uc->next_scan > expires)
					uc->next_scan = expires;
			}
			uci->expires = expires;
		}
		if (uc->blocks_bitmap) {
			// we have a special case here, as we need to find a new series of free blocks
			uint64_t old_first_block = uci->first_block;
			uci->first_block = upsgi_cache_find_free_blocks(uc, vallen);
                        if (uci->first_block == 0xffffffffffffffffLLU) {
				uci->first_block = old_first_block;
				cache_full(uc);
                                goto end;
                        }
                        // mark used blocks;
                        uint64_t needed_blocks = cache_mark_blocks(uc, uci->first_block, vallen);
                        // optimize the scan
                        if (uci->first_block + needed_blocks >= uc->blocks) {
                                uc->blocks_bitmap_pos = 0;
                        }
                        else {
                                uc->blocks_bitmap_pos = uci->first_block + needed_blocks;
                        }
			// unmark the old blocks
			cache_unmark_blocks(uc, old_first_block, uci->valsize);
		}
		if ( !(flags & UPSGI_CACHE_FLAG_MATH)) {
			memcpy(((char *) uc->data) + (uci->first_block * uc->blocksize), val, vallen);
		}
		else {
			int64_t *num = (int64_t *)(((char *) uc->data) + (uci->first_block * uc->blocksize));
                        int64_t *delta = (int64_t *) val;
                        if (flags & UPSGI_CACHE_FLAG_INC) {
                                *num += *delta;
                        }
                        else if (flags & UPSGI_CACHE_FLAG_DEC) {
                                *num -= *delta;
                        }
                        else if (flags & UPSGI_CACHE_FLAG_MUL) {
                                *num *= *delta;
                        }
                        else if (flags & UPSGI_CACHE_FLAG_DIV) {
                                if (*delta == 0) {
                                        *num = 0;
                                }
                                else {
                                        *num /= *delta;
                                }
                        }
		}
		uci->valsize = vallen;
		ret = 0;
	}

	if (uc->use_last_modified) {
		uc->last_modified_at = (now ? now : upsgi_now());
	}

	if (uc->nodes && ret == 0 && !(flags & UPSGI_CACHE_FLAG_LOCAL)) {
		cache_send_udp_command(uc, key, keylen, val, vallen, expires, 10);
	}


end:
	return ret;

}


static void cache_send_udp_command(struct upsgi_cache *uc, char *key, uint16_t keylen, char *val, uint16_t vallen, uint64_t expires, uint8_t cmd) {

		struct upsgi_header uh;
		uint8_t u_k[2];
		uint8_t u_v[2];
		uint8_t u_e[2];
		uint16_t vallen16 = vallen;
		struct iovec iov[7];
		struct msghdr mh;

		memset(&mh, 0, sizeof(struct msghdr));
		mh.msg_iov = iov;
		mh.msg_iovlen = 3;

		if (cmd == 10) {
			mh.msg_iovlen = 7;
		}

		iov[0].iov_base = &uh;
		iov[0].iov_len = 4;

		u_k[0] = (uint8_t) (keylen & 0xff);
        	u_k[1] = (uint8_t) ((keylen >> 8) & 0xff);

		iov[1].iov_base = u_k;
		iov[1].iov_len = 2;

		iov[2].iov_base = key;
		iov[2].iov_len = keylen;

		uh._pktsize = 2 + keylen;

		if (cmd == 10) {
			u_v[0] = (uint8_t) (vallen16 & 0xff);
        		u_v[1] = (uint8_t) ((vallen16 >> 8) & 0xff);

			iov[3].iov_base = u_v;
			iov[3].iov_len = 2;

			iov[4].iov_base = val;
			iov[4].iov_len = vallen16;

			char es[sizeof(UMAX64_STR) + 1];
        		uint16_t es_size = upsgi_long2str2n(expires, es, sizeof(UMAX64_STR));

			u_e[0] = (uint8_t) (es_size & 0xff);
        		u_e[1] = (uint8_t) ((es_size >> 8) & 0xff);

			iov[5].iov_base = u_e;
                	iov[5].iov_len = 2;

                	iov[6].iov_base = es;
                	iov[6].iov_len = es_size;

			uh._pktsize += 2 + vallen16 + 2 + es_size;
		}

		uh.modifier1 = 111;
		uh.modifier2 = cmd;

		struct upsgi_string_list *usl = uc->nodes;
		while(usl) {
			mh.msg_name = usl->custom_ptr;
			mh.msg_namelen = usl->custom;
			if (sendmsg(uc->udp_node_socket, &mh, 0) <= 0) {
				upsgi_error("[cache-udp-node] sendmsg()");
			}
			usl = usl->next;
		}

}

void *cache_udp_server_loop(void *ucache) {
        // block all signals
        sigset_t smask;
        sigfillset(&smask);
        pthread_sigmask(SIG_BLOCK, &smask, NULL);

	struct upsgi_cache *uc = (struct upsgi_cache *) ucache;

        int queue = event_queue_init();
        struct upsgi_string_list *usl = uc->udp_servers;
        while(usl) {
                if (strchr(usl->value, ':')) {
                        int fd = bind_to_udp(usl->value, 0, 0);
                        if (fd < 0) {
                                upsgi_log("[cache-udp-server] cannot bind to %s\n", usl->value);
                                exit(1);
                        }
                        upsgi_socket_nb(fd);
                        event_queue_add_fd_read(queue, fd);
                        upsgi_log("*** udp server for cache \"%s\" running on %s ***\n", uc->name, usl->value);
                }
                usl = usl->next;
        }

        // allocate 64k chunk to receive messages
        char *buf = upsgi_malloc(UMAX16);
	
	for(;;) {
                uint16_t pktsize = 0, ss = 0;
                int interesting_fd = -1;
                int rlen = event_queue_wait(queue, -1, &interesting_fd);
                if (rlen <= 0) continue;
                if (interesting_fd < 0) continue;
                ssize_t len = read(interesting_fd, buf, UMAX16);
                if (len <= 7) {
                        upsgi_error("[cache-udp-server] read()");
                }
                if (buf[0] != 111) continue;
                memcpy(&pktsize, buf+1, 2);
                if (pktsize != len-4) continue;

                memcpy(&ss, buf + 4, 2);
                if (4+ss > pktsize) continue;
                uint16_t keylen = ss;
                char *key = buf + 6;

                // cache set/update
                if (buf[3] == 10) {
                        if (keylen + 2 + 2 > pktsize) continue;
                        memcpy(&ss, buf + 6 + keylen, 2);
                        if (4+keylen+ss > pktsize) continue;
                        uint16_t vallen = ss;
                        char *val = buf + 8 + keylen;
                        uint64_t expires = 0;
                        if (2 + keylen + 2 + vallen + 2 < pktsize) {
                                memcpy(&ss, buf + 8 + keylen + vallen , 2);
                                if (6+keylen+vallen+ss > pktsize) continue;
                                expires = upsgi_str_num(buf + 10 + keylen+vallen, ss);
                        }
                        upsgi_wlock(uc->lock);
                        if (upsgi_cache_set2(uc, key, keylen, val, vallen, expires, UPSGI_CACHE_FLAG_UPDATE|UPSGI_CACHE_FLAG_LOCAL|UPSGI_CACHE_FLAG_ABSEXPIRE)) {
                                upsgi_log("[cache-udp-server] unable to update cache\n");
                        }
                        upsgi_rwunlock(uc->lock);
                }
                // cache del
                else if (buf[3] == 11) {
                        upsgi_wlock(uc->lock);
                        if (upsgi_cache_del2(uc, key, keylen, 0, UPSGI_CACHE_FLAG_LOCAL)) {
                                upsgi_log("[cache-udp-server] unable to update cache\n");
                        }
                        upsgi_rwunlock(uc->lock);
                }
        }

        return NULL;
}

static uint64_t cache_sweeper_free_items(struct upsgi_cache *uc) {
	uint64_t i;
	uint64_t freed_items = 0;

	if (uc->no_expire || uc->purge_lru || uc->lazy_expire)
		return 0;

	upsgi_rlock(uc->lock);
	if (!uc->next_scan || uc->next_scan > (uint64_t)upsgi.current_time) {
		upsgi_rwunlock(uc->lock);
		return 0;
	}
	upsgi_rwunlock(uc->lock);

	// skip the first slot
	for (i = 1; i < uc->max_items; i++) {
		struct upsgi_cache_item *uci = cache_item(i);

		upsgi_wlock(uc->lock);
		// we reset next scan time first, then we find the least
		// expiration time from those that are NOT expired yet.
		if (i == 1)
			uc->next_scan = 0;

		if (uci->expires) {
			if (uci->expires <= (uint64_t)upsgi.current_time) {
				upsgi_cache_del2(uc, NULL, 0, i, UPSGI_CACHE_FLAG_LOCAL);
				freed_items++;
			} else if (!uc->next_scan || uc->next_scan > uci->expires) {
				uc->next_scan = uci->expires;
			}
		}
		upsgi_rwunlock(uc->lock);
	}

	return freed_items;
}

static void *cache_sweeper_loop(void *ucache) {

        // block all signals
        sigset_t smask;
        sigfillset(&smask);
        pthread_sigmask(SIG_BLOCK, &smask, NULL);

        if (!upsgi.cache_expire_freq)
                upsgi.cache_expire_freq = 3;

        // remove expired cache items TODO use rb_tree timeouts
        for (;;) {
		struct upsgi_cache *uc;

		for (uc = (struct upsgi_cache *)ucache; uc; uc = uc->next) {
			uint64_t freed_items = cache_sweeper_free_items(uc);
			if (upsgi.cache_report_freed_items && freed_items)
				upsgi_log("freed %llu items for cache \"%s\"\n", (unsigned long long)freed_items, uc->name);
		}

		sleep(upsgi.cache_expire_freq);
        }

        return NULL;
}

void upsgi_cache_sync_all() {

	struct upsgi_cache *uc = upsgi.caches;
	while(uc) {
		if (uc->store && (upsgi.master_cycles == 0 || (uc->store_sync > 0 && (upsgi.master_cycles % uc->store_sync) == 0))) {
                	if (msync(uc->items, uc->filesize, MS_ASYNC)) {
                        	upsgi_error("upsgi_cache_sync_all()/msync()");
                        }
		}
		uc = uc->next;
	}
}

void upsgi_cache_start_sweepers() {
	struct upsgi_cache *uc = upsgi.caches;

	if (upsgi.cache_no_expire)
		return;

	int need_to_run = 0;
	while(uc) {
		if (!uc->no_expire && !uc->purge_lru && !uc->lazy_expire) {
			need_to_run = 1;
			break;
		}
		uc = uc->next;
        }

	if (!need_to_run) return;

	pthread_t cache_sweeper;
        if (pthread_create(&cache_sweeper, NULL, cache_sweeper_loop, upsgi.caches)) {
        	upsgi_error("upsgi_cache_start_sweepers()/pthread_create()");
                upsgi_log("unable to run the cache sweeper!!!\n");
		return;
	}
        upsgi_log("cache sweeper thread enabled\n");
}

void upsgi_cache_start_sync_servers() {

	struct upsgi_cache *uc = upsgi.caches;
	while(uc) {
		if (!uc->udp_servers) goto next;		
		pthread_t cache_udp_server;
                if (pthread_create(&cache_udp_server, NULL, cache_udp_server_loop, (void *) uc)) {
                        upsgi_error("pthread_create()");
                        upsgi_log("unable to run the cache udp server !!!\n");
                }
                else {
                        upsgi_log("udp server thread enabled for cache \"%s\"\n", uc->name);
                }
next:
		uc = uc->next;
        }
}

struct upsgi_cache *upsgi_cache_create(char *arg) {
	struct upsgi_cache *old_uc = NULL, *uc = upsgi.caches;
	while(uc) {
		old_uc = uc;
		uc = uc->next;
	}

	uc = upsgi_calloc_shared(sizeof(struct upsgi_cache));
	if (old_uc) {
		old_uc->next = uc;
	}
	else {
		upsgi.caches = uc;
	}

	// default (old-style) cache ?
	if (!arg) {
		uc->name = "default";
		uc->name_len = strlen(uc->name);
		uc->blocksize = upsgi.cache_blocksize;
		if (!uc->blocksize) uc->blocksize = UMAX16;
		uc->max_item_size = uc->blocksize;
		uc->max_items = upsgi.cache_max_items;
		uc->blocks = upsgi.cache_max_items;
		uc->keysize = 2048;
		uc->hashsize = UMAX16;
		uc->hash = upsgi_hash_algo_get("djb33x");
		uc->store = upsgi.cache_store;
		uc->nodes = upsgi.cache_udp_node;
		uc->udp_servers = upsgi.cache_udp_server;
		uc->store_sync = upsgi.cache_store_sync;
		uc->use_last_modified = (uint8_t) upsgi.cache_use_last_modified;

		if (upsgi.cache_sync) {
			upsgi_string_new_list(&uc->sync_nodes, upsgi.cache_sync);
		}
	}
	else {
		char *c_name = NULL;
		char *c_max_items = NULL;
		char *c_blocksize = NULL;
		char *c_blocks = NULL;
		char *c_hash = NULL;
		char *c_hashsize = NULL;
		char *c_keysize = NULL;
		char *c_store = NULL;
		char *c_store_sync = NULL;
		char *c_store_delete = NULL;
		char *c_nodes = NULL;
		char *c_sync = NULL;
		char *c_udp_servers = NULL;
		char *c_bitmap = NULL;
		char *c_use_last_modified = NULL;
		char *c_math_initial = NULL;
		char *c_ignore_full = NULL;
		char *c_purge_lru = NULL;
		char *c_lazy_expire = NULL;
		char *c_sweep_on_full = NULL;
		char *c_clear_on_full = NULL;
		char *c_no_expire = NULL;

		if (upsgi_kvlist_parse(arg, strlen(arg), ',', '=',
                        "name", &c_name,
                        "max_items", &c_max_items,
                        "maxitems", &c_max_items,
                        "items", &c_max_items,
                        "blocksize", &c_blocksize,
                        "blocks", &c_blocks,
                        "hash", &c_hash,
                        "hashsize", &c_hashsize,
                        "hash_size", &c_hashsize,
                        "keysize", &c_keysize,
                        "key_size", &c_keysize,
                        "store", &c_store,
                        "store_sync", &c_store_sync,
                        "storesync", &c_store_sync,
                        "store_delete", &c_store_delete,
                        "storedelete", &c_store_delete,
                        "node", &c_nodes,
                        "nodes", &c_nodes,
                        "sync", &c_sync,
                        "udp", &c_udp_servers,
                        "udp_servers", &c_udp_servers,
                        "udp_server", &c_udp_servers,
                        "udpservers", &c_udp_servers,
                        "udpserver", &c_udp_servers,
                        "bitmap", &c_bitmap,
                        "lastmod", &c_use_last_modified,
                        "math_initial", &c_math_initial,
                        "ignore_full", &c_ignore_full,
			"purge_lru", &c_purge_lru,
			"lru", &c_purge_lru,
			"lazy_expire", &c_lazy_expire,
			"lazy", &c_lazy_expire,
			"sweep_on_full", &c_sweep_on_full,
			"clear_on_full", &c_clear_on_full,
			"no_expire", &c_no_expire,
                	NULL)) {
			upsgi_log("unable to parse cache definition\n");
			exit(1);
        	}
		if (!c_name) {
			upsgi_log("you have to specify a cache name\n");
			exit(1);
		}
		if (!c_max_items) {
			upsgi_log("you have to specify the maximum number of cache items\n");
			exit(1);
		}

		uc->name = c_name;
		uc->name_len = strlen(c_name);
		uc->max_items = upsgi_n64(c_max_items);
		if (!uc->max_items) {
			upsgi_log("you have to specify the maximum number of cache items\n");
			exit(1);
		}
		
		// defaults
		uc->blocks = uc->max_items;
		uc->blocksize = UMAX16;
		uc->keysize = 2048;
		uc->hashsize = UMAX16;
		uc->hash = upsgi_hash_algo_get("djb33x");

		// customize
		if (c_blocksize) uc->blocksize = upsgi_n64(c_blocksize);
		if (!uc->blocksize) { upsgi_log("invalid cache blocksize for \"%s\"\n", uc->name); exit(1); }
		// set the true max size of an item
		uc->max_item_size = uc->blocksize;

		if (c_blocks) uc->blocks = upsgi_n64(c_blocks);
		if (!uc->blocks) { upsgi_log("invalid cache blocks for \"%s\"\n", uc->name); exit(1); }
		if (c_hash) uc->hash = upsgi_hash_algo_get(c_hash);
		if (!uc->hash) { upsgi_log("invalid cache hash for \"%s\"\n", uc->name); exit(1); }
		if (c_hashsize) uc->hashsize = upsgi_n64(c_hashsize);
		if (!uc->hashsize) { upsgi_log("invalid cache hashsize for \"%s\"\n", uc->name); exit(1); }
		if (c_keysize) uc->keysize = upsgi_n64(c_keysize);
		if (!uc->keysize || uc->keysize >= UMAX16) { upsgi_log("invalid cache keysize for \"%s\"\n", uc->name); exit(1); }
		if (c_bitmap) {
			uc->use_blocks_bitmap = 1; 
			uc->max_item_size = uc->blocksize * uc->blocks;
		}
		if (c_use_last_modified) uc->use_last_modified = 1;
		if (c_ignore_full) uc->ignore_full = 1;

		if (c_store_delete) uc->store_delete = 1;

		if (c_math_initial) uc->math_initial = strtol(c_math_initial, NULL, 10);

		if (c_lazy_expire) uc->lazy_expire = 1;
		if (c_sweep_on_full) {
			uc->sweep_on_full = upsgi_n64(c_sweep_on_full);
		}
		if (c_clear_on_full) uc->clear_on_full = 1;
		if (c_no_expire) uc->no_expire = 1;

		uc->store_sync = upsgi.cache_store_sync;
		if (c_store_sync) { uc->store_sync = upsgi_n64(c_store_sync); }

		if (uc->blocks < uc->max_items) {
			upsgi_log("invalid number of cache blocks for \"%s\", must be higher than max_items (%llu)\n", uc->name, uc->max_items);
			exit(1);
		}

		uc->store = c_store;

		if (c_nodes) {
			char *p, *ctx = NULL;
			upsgi_foreach_token(c_nodes, ";", p, ctx) {
				upsgi_string_new_list(&uc->nodes, p);
			}
		}

		if (c_sync) {
			char *p, *ctx = NULL;
			upsgi_foreach_token(c_sync, ";", p, ctx) {
                                upsgi_string_new_list(&uc->sync_nodes, p);
                        }
		}

		if (c_udp_servers) {
			char *p, *ctx = NULL;
                        upsgi_foreach_token(c_udp_servers, ";", p, ctx) {
                                upsgi_string_new_list(&uc->udp_servers, p);
                        }
                }
		
		if (c_purge_lru)
			uc->purge_lru = 1;
	}

	upsgi_cache_init(uc);
	return uc;
}

struct upsgi_cache *upsgi_cache_by_name(char *name) {
	struct upsgi_cache *uc = upsgi.caches;
	if (!name || *name == 0) {
		return upsgi.caches;
	}
	while(uc) {
		if (uc->name && !strcmp(uc->name, name)) {
			return uc;
		}
		uc = uc->next;
	}
	return NULL;
}

struct upsgi_cache *upsgi_cache_by_namelen(char *name, uint16_t len) {
        struct upsgi_cache *uc = upsgi.caches;
        if (!name || *name == 0) {
                return upsgi.caches;
        }
        while(uc) {
                if (uc->name && !upsgi_strncmp(uc->name, uc->name_len, name, len)) {
                        return uc;
                }
                uc = uc->next;
        }
        return NULL;
}

void upsgi_cache_create_all() {

	if (upsgi.cache_setup) return;

	// register embedded hash algorithms
        upsgi_hash_algo_register_all();

        // setup default cache
        if (upsgi.cache_max_items > 0) {
                upsgi_cache_create(NULL);
        }

        // setup new generation caches
        struct upsgi_string_list *usl = upsgi.cache2;
        while(usl) {
                upsgi_cache_create(usl->value);
                usl = usl->next;
        }

	upsgi.cache_setup = 1;
}

/*
 * upsgi cache magic functions. They can be used by plugin to easily access local and remote caches
 *
 * they generate (when needed) a new memory buffer. Locking is automatically managed
 *
 * You have to free the returned memory !!!
 *
 */

void upsgi_cache_magic_context_hook(char *key, uint16_t key_len, char *value, uint16_t vallen, void *data) {
	struct upsgi_cache_magic_context *ucmc = (struct upsgi_cache_magic_context *) data;

	if (!upsgi_strncmp(key, key_len, "cmd", 3)) {
		ucmc->cmd = value;
		ucmc->cmd_len = vallen;
		return;
	}

	if (!upsgi_strncmp(key, key_len, "key", 3)) {
                ucmc->key = value;
                ucmc->key_len = vallen; 
                return;
        }

	if (!upsgi_strncmp(key, key_len, "expires", 7)) {
                ucmc->expires = upsgi_str_num(value, vallen);
                return;
        }

	if (!upsgi_strncmp(key, key_len, "size", 4)) {
                ucmc->size = upsgi_str_num(value, vallen);
                return;
        }

	if (!upsgi_strncmp(key, key_len, "cache", 5)) {
                ucmc->cache = value;
                ucmc->cache_len = vallen; 
                return;
        }

	if (!upsgi_strncmp(key, key_len, "status", 6)) {
                ucmc->status = value;
                ucmc->status_len = vallen;
                return;
        }
}

static struct upsgi_buffer *upsgi_cache_prepare_magic_get(char *cache_name, uint16_t cache_name_len, char *key, uint16_t key_len) {
	struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	ub->pos = 4;

	if (upsgi_buffer_append_keyval(ub, "cmd", 3, "get", 3)) goto error;
	if (upsgi_buffer_append_keyval(ub, "key", 3, key, key_len)) goto error;
	if (cache_name) {
		if (upsgi_buffer_append_keyval(ub, "cache", 5, cache_name, cache_name_len)) goto error;
	}

	return ub;
error:
	upsgi_buffer_destroy(ub);
	return NULL;
}

struct upsgi_buffer *upsgi_cache_prepare_magic_exists(char *cache_name, uint16_t cache_name_len, char *key, uint16_t key_len) {
        struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	ub->pos = 4;

        if (upsgi_buffer_append_keyval(ub, "cmd", 3, "exists", 6)) goto error;
        if (upsgi_buffer_append_keyval(ub, "key", 3, key, key_len)) goto error;
        if (cache_name) {
                if (upsgi_buffer_append_keyval(ub, "cache", 5, cache_name, cache_name_len)) goto error;
        }

        return ub;
error:
        upsgi_buffer_destroy(ub);
        return NULL;
}

struct upsgi_buffer *upsgi_cache_prepare_magic_del(char *cache_name, uint16_t cache_name_len, char *key, uint16_t key_len) {
        struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	ub->pos = 4;

        if (upsgi_buffer_append_keyval(ub, "cmd", 3, "del", 3)) goto error;
        if (upsgi_buffer_append_keyval(ub, "key", 3, key, key_len)) goto error;
        if (cache_name) {
                if (upsgi_buffer_append_keyval(ub, "cache", 5, cache_name, cache_name_len)) goto error;
        }

        return ub;
error:
        upsgi_buffer_destroy(ub);
        return NULL;
}

struct upsgi_buffer *upsgi_cache_prepare_magic_clear(char *cache_name, uint16_t cache_name_len) {
        struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
        ub->pos = 4;

        if (upsgi_buffer_append_keyval(ub, "cmd", 3, "clear", 5)) goto error;
        if (cache_name) {
                if (upsgi_buffer_append_keyval(ub, "cache", 5, cache_name, cache_name_len)) goto error;
        }

        return ub;
error:
        upsgi_buffer_destroy(ub);
        return NULL;
}


struct upsgi_buffer *upsgi_cache_prepare_magic_set(char *cache_name, uint16_t cache_name_len, char *key, uint16_t key_len, uint64_t len, uint64_t expires) {
        struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	ub->pos = 4;

        if (upsgi_buffer_append_keyval(ub, "cmd", 3, "set", 3)) goto error;
        if (upsgi_buffer_append_keyval(ub, "key", 3, key, key_len)) goto error;
        if (upsgi_buffer_append_keynum(ub, "size", 4, len)) goto error;
	if (expires > 0) {
		if (upsgi_buffer_append_keynum(ub, "expires", 7, expires)) goto error;
	}
        if (upsgi_buffer_append_keynum(ub, "size", 4, len)) goto error;
        if (cache_name) {
                if (upsgi_buffer_append_keyval(ub, "cache", 5, cache_name, cache_name_len)) goto error;
        }

        return ub;
error:
        upsgi_buffer_destroy(ub);
        return NULL;
}

struct upsgi_buffer *upsgi_cache_prepare_magic_update(char *cache_name, uint16_t cache_name_len, char *key, uint16_t key_len, uint64_t len, uint64_t expires) {
        struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	ub->pos = 4;

        if (upsgi_buffer_append_keyval(ub, "cmd", 3, "update", 6)) goto error;
        if (upsgi_buffer_append_keyval(ub, "key", 3, key, key_len)) goto error;
        if (upsgi_buffer_append_keynum(ub, "size", 4, len)) goto error;
        if (expires > 0) {
                if (upsgi_buffer_append_keynum(ub, "expires", 7, expires)) goto error;
        }
        if (upsgi_buffer_append_keynum(ub, "size", 4, len)) goto error;
        if (cache_name) {
                if (upsgi_buffer_append_keyval(ub, "cache", 5, cache_name, cache_name_len)) goto error;
        }

        return ub;
error:
        upsgi_buffer_destroy(ub);
        return NULL;
}

static int cache_magic_send_and_manage(int fd, struct upsgi_buffer *ub, char *stream, uint64_t stream_len, int timeout, struct upsgi_cache_magic_context *ucmc) {
	if (upsgi_buffer_set_uh(ub, 111, 17)) return -1;

	if (stream) {
		if (upsgi_buffer_append(ub, stream, stream_len)) return -1;
	}

	if (upsgi_write_true_nb(fd, ub->buf, ub->pos, timeout)) return -1;

	// ok now wait for the response, using the same buffer of the request
	// NOTE: after using a upsgi_buffer in that way we basically destroy (even if we can safely free it)
	size_t rlen = ub->pos;
	if (upsgi_read_with_realloc(fd, &ub->buf, &rlen, timeout, NULL, NULL)) return -1;
	// try to fix the buffer to maintain size info
	ub->pos = rlen;

	// now we have a upsgi dictionary with all of the options needed, let's parse it
	memset(ucmc, 0, sizeof(struct upsgi_cache_magic_context));
	if (upsgi_hooked_parse(ub->buf, rlen, upsgi_cache_magic_context_hook, ucmc)) return -1;
	return 0;
}

char *upsgi_cache_magic_get(char *key, uint16_t keylen, uint64_t *vallen, uint64_t *expires, char *cache) {
	struct upsgi_cache_magic_context ucmc;
	struct upsgi_cache *uc = NULL;
	char *cache_server = NULL;
	char *cache_name = NULL;
	uint16_t cache_name_len = 0;
	if (cache) {
		char *at = strchr(cache, '@');
		if (!at) {
			uc = upsgi_cache_by_name(cache);
		}
		else {
			cache_server = at + 1;
			cache_name = cache;
			cache_name_len = at - cache;
		}
	}
	// use default (local) cache
	else {
		uc = upsgi.caches;
	}

	// we have a local cache !!!
	if (uc) {
		if (uc->purge_lru)
			upsgi_wlock(uc->lock);
		else
			upsgi_rlock(uc->lock);
		char *value = upsgi_cache_get3(uc, key, keylen, vallen, expires);
		if (!value) {
			upsgi_rwunlock(uc->lock);
			return NULL;
		}
		char *buf = upsgi_malloc(*vallen);
		memcpy(buf, value, *vallen);
		upsgi_rwunlock(uc->lock);
		return buf;
	}

	// we have a remote one
	if (cache_server) {
		int fd = upsgi_connect(cache_server, 0, 1);
		if (fd < 0) return NULL;

		int ret = upsgi.wait_write_hook(fd, upsgi.socket_timeout);
		if (ret <= 0) {
			close(fd);
			return NULL;
		}

		struct upsgi_buffer *ub = upsgi_cache_prepare_magic_get(cache_name, cache_name_len, key, keylen);
		if (!ub) {
			close(fd);
			return NULL;
		}

		if (cache_magic_send_and_manage(fd, ub, NULL, 0, upsgi.socket_timeout, &ucmc)) {
			close(fd);
                        upsgi_buffer_destroy(ub);
                        return NULL;
		}

		if (upsgi_strncmp(ucmc.status, ucmc.status_len, "ok", 2)) {
			close(fd);
                        upsgi_buffer_destroy(ub);
                        return NULL;
		}

		if (ucmc.size == 0) {
			close(fd);
                        upsgi_buffer_destroy(ub);
                        return NULL;
		}

		// ok we now need to fix our buffer (if needed)
		if (ucmc.size > ub->pos) {
			char *tmp_buf = realloc(ub->buf, ucmc.size);
			if (!tmp_buf) {
				upsgi_error("upsgi_cache_magic_get()/realloc()");
				close(fd);
				upsgi_buffer_destroy(ub);
				return NULL;
			}
			ub->buf = tmp_buf;
		}

		// read the raw value from the socket
		if (upsgi_read_whole_true_nb(fd, ub->buf, ucmc.size, upsgi.socket_timeout)) {
			close(fd);
			upsgi_buffer_destroy(ub);
			return NULL;
		}

		// now the magic, we dereference the internal buffer and return it to the caller
		close(fd);
		char *value = ub->buf;
		ub->buf = NULL;
		upsgi_buffer_destroy(ub);
		*vallen = ucmc.size;
		if (expires) {
			*expires = ucmc.expires;
		}
		return value;
		
	}

	return NULL;
}

int upsgi_cache_magic_exists(char *key, uint16_t keylen, char *cache) {
        struct upsgi_cache_magic_context ucmc;
        struct upsgi_cache *uc = NULL;
        char *cache_server = NULL;
        char *cache_name = NULL;
        uint16_t cache_name_len = 0;
        if (cache) {
                char *at = strchr(cache, '@');
                if (!at) {
                        uc = upsgi_cache_by_name(cache);
                }
                else {
                        cache_server = at + 1;
                        cache_name = cache;
                        cache_name_len = at - cache;
                }
        }
        // use default (local) cache
        else {
                uc = upsgi.caches;
        }

        // we have a local cache !!!
        if (uc) {
                upsgi_rlock(uc->lock);
                if (!upsgi_cache_exists2(uc, key, keylen)) {
                        upsgi_rwunlock(uc->lock);
                        return 0;
                }
		upsgi_rwunlock(uc->lock);
		return 1;
        }

	// we have a remote one
        if (cache_server) {
                int fd = upsgi_connect(cache_server, 0, 1);
                if (fd < 0) return 0;

                int ret = upsgi.wait_write_hook(fd, upsgi.socket_timeout);
                if (ret <= 0) {
                        close(fd);
			return 0;
                }

                struct upsgi_buffer *ub = upsgi_cache_prepare_magic_exists(cache_name, cache_name_len, key, keylen);
                if (!ub) {
                        close(fd);
			return 0;
                }

                if (cache_magic_send_and_manage(fd, ub, NULL, 0, upsgi.socket_timeout, &ucmc)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
			return 0;
                }

                if (upsgi_strncmp(ucmc.status, ucmc.status_len, "ok", 2)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
			return 0;
                }

		close(fd);
		upsgi_buffer_destroy(ub);
		return 1;
        }

        return 0;
}


int upsgi_cache_magic_set(char *key, uint16_t keylen, char *value, uint64_t vallen, uint64_t expires, uint64_t flags, char *cache) {
	struct upsgi_cache_magic_context ucmc;
        struct upsgi_cache *uc = NULL;
        char *cache_server = NULL;
        char *cache_name = NULL;
        uint16_t cache_name_len = 0;

        if (cache) {
                char *at = strchr(cache, '@');
                if (!at) {
                        uc = upsgi_cache_by_name(cache);
                }
                else {
                        cache_server = at + 1;
                        cache_name = cache;
                        cache_name_len = at - cache;
                }
        }
	// use default (local) cache
	else {
                uc = upsgi.caches;
        }

	// we have a local cache !!!
	if (uc) {
                upsgi_wlock(uc->lock);
                int ret = upsgi_cache_set2(uc, key, keylen, value, vallen, expires, flags);
                upsgi_rwunlock(uc->lock);
		return ret;
        }

	// we have a remote one
	if (cache_server) {
		int fd = upsgi_connect(cache_server, 0, 1);
                if (fd < 0) return -1;

                int ret = upsgi.wait_write_hook(fd, upsgi.socket_timeout);
                if (ret <= 0) {
                        close(fd);
                        return -1;
                }

		struct upsgi_buffer *ub = NULL;
		if (flags & UPSGI_CACHE_FLAG_UPDATE) {
                	ub = upsgi_cache_prepare_magic_update(cache_name, cache_name_len, key, keylen, vallen, expires);
		}
		else {
                	ub = upsgi_cache_prepare_magic_set(cache_name, cache_name_len, key, keylen, vallen, expires);
		}
                if (!ub) {
                        close(fd);
                        return -1;
                }

                if (cache_magic_send_and_manage(fd, ub, value, vallen, upsgi.socket_timeout, &ucmc)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
                        return -1;
                }

                if (upsgi_strncmp(ucmc.status, ucmc.status_len, "ok", 2)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
                        return -1;
                }

		close(fd);
		upsgi_buffer_destroy(ub);
		return 0;

        }

        return -1;

}

int upsgi_cache_magic_del(char *key, uint16_t keylen, char *cache) {

	struct upsgi_cache_magic_context ucmc;
        struct upsgi_cache *uc = NULL;
        char *cache_server = NULL;
        char *cache_name = NULL;
        uint16_t cache_name_len = 0;
        if (cache) {
                char *at = strchr(cache, '@');
                if (!at) {
                        uc = upsgi_cache_by_name(cache);
                }
                else {
                        cache_server = at + 1;
                        cache_name = cache;
                        cache_name_len = at - cache;
                }
        }
        // use default (local) cache
        else {
                uc = upsgi.caches;
        }

        // we have a local cache !!!
        if (uc) {
                upsgi_wlock(uc->lock);
                if (upsgi_cache_del2(uc, key, keylen, 0, 0)) {
                        upsgi_rwunlock(uc->lock);
                        return -1;
                }
                upsgi_rwunlock(uc->lock);
                return 0;
        }

        // we have a remote one
        if (cache_server) {
                int fd = upsgi_connect(cache_server, 0, 1);
                if (fd < 0) return -1;

                int ret = upsgi.wait_write_hook(fd, upsgi.socket_timeout);
                if (ret <= 0) {
                        close(fd);
                        return -1;
                }

                struct upsgi_buffer *ub = upsgi_cache_prepare_magic_del(cache_name, cache_name_len, key, keylen);
                if (!ub) {
                        close(fd);
                        return -1;
                }

                if (cache_magic_send_and_manage(fd, ub, NULL, 0, upsgi.socket_timeout, &ucmc)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
                        return -1;
                }

                if (upsgi_strncmp(ucmc.status, ucmc.status_len, "ok", 2)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
                        return -1;
                }

		close(fd);
		upsgi_buffer_destroy(ub);
                return 0;
        }

        return -1 ;

}

int upsgi_cache_magic_clear(char *cache) {

        struct upsgi_cache_magic_context ucmc;
        struct upsgi_cache *uc = NULL;
        char *cache_server = NULL;
        char *cache_name = NULL;
        uint16_t cache_name_len = 0;
        if (cache) {
                char *at = strchr(cache, '@');
                if (!at) {
                        uc = upsgi_cache_by_name(cache);
                }
                else {
                        cache_server = at + 1;
                        cache_name = cache;
                        cache_name_len = at - cache;
                }
        }
        // use default (local) cache
        else {
                uc = upsgi.caches;
        }

        // we have a local cache !!!
        if (uc) {
		uint64_t i;
                upsgi_wlock(uc->lock);
		for (i = 1; i < uc->max_items; i++) {
                	if (upsgi_cache_del2(uc, NULL, 0, i, 0)) {
                        	upsgi_rwunlock(uc->lock);
                        	return -1;
                	}
		}
                upsgi_rwunlock(uc->lock);
                return 0;
        }

        // we have a remote one
        if (cache_server) {
                int fd = upsgi_connect(cache_server, 0, 1);
                if (fd < 0) return -1;

                int ret = upsgi.wait_write_hook(fd, upsgi.socket_timeout);
                if (ret <= 0) {
                        close(fd);
                        return -1;
                }

                struct upsgi_buffer *ub = upsgi_cache_prepare_magic_clear(cache_name, cache_name_len);
                if (!ub) {
                        close(fd);
                        return -1;
                }

                if (cache_magic_send_and_manage(fd, ub, NULL, 0, upsgi.socket_timeout, &ucmc)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
                        return -1;
                }

                if (upsgi_strncmp(ucmc.status, ucmc.status_len, "ok", 2)) {
                        close(fd);
                        upsgi_buffer_destroy(ub);
                        return -1;
                }

		close(fd);
		upsgi_buffer_destroy(ub);
                return 0;
        }

        return -1 ;

}


void upsgi_cache_sync_from_nodes(struct upsgi_cache *uc) {
	struct upsgi_string_list *usl = uc->sync_nodes;
	while(usl) {
		upsgi_log("[cache-sync] getting cache dump from %s ...\n", usl->value);
		int fd = upsgi_connect(usl->value, 0, 0);
		if (fd < 0) {
			upsgi_log("[cache-sync] unable to connect to the cache server\n");
			goto next;
		}

		struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size + uc->filesize);
		ub->pos = 4;
		if (uc->name && upsgi_buffer_append(ub, uc->name, uc->name_len)) {
			upsgi_buffer_destroy(ub);
			close(fd);
			goto next;
		}

		if (upsgi_buffer_set_uh(ub, 111, 6)) {
			upsgi_buffer_destroy(ub);
			close(fd);
			goto next;
		}

		if (upsgi_write_nb(fd, ub->buf, ub->pos, upsgi.socket_timeout)) {
			upsgi_buffer_destroy(ub);
			upsgi_log("[cache-sync] unable to write to the cache server\n");
			close(fd);
			goto next;
		}

		size_t rlen = ub->pos;
		if (upsgi_read_with_realloc(fd, &ub->buf, &rlen, upsgi.socket_timeout, NULL, NULL)) {
			upsgi_buffer_destroy(ub);
			upsgi_log("[cache-sync] unable to read from the cache server\n");
			close(fd);
			goto next;
		}

		upsgi_hooked_parse(ub->buf, rlen, cache_sync_hook, uc);

		if (upsgi_read_nb(fd, (char *) uc->items, uc->filesize, upsgi.socket_timeout)) {
			upsgi_buffer_destroy(ub);
			close(fd);
                        upsgi_log("[cache-sync] unable to read from the cache server\n");
			goto next;
                }

		// reset the hashtable
		memset(uc->hashtable, 0, sizeof(uint64_t) * UMAX16);
		// re-fill the hashtable
                upsgi_cache_fix(uc);

		upsgi_buffer_destroy(ub);
		close(fd);
		break;
next:
		if (!usl->next) {
			exit(1);
		}
		upsgi_log("[cache-sync] trying with the next sync node...\n");
		usl = usl->next;
	}
}


void upsgi_cache_setup_nodes(struct upsgi_cache *uc) {
	struct upsgi_string_list *usl = uc->nodes;
	while(usl) {
		char *port = strchr(usl->value, ':');
		if (!port) {
			upsgi_log("[cache-udp-node] invalid udp address: %s\n", usl->value);
			exit(1);
		}
		// no need to zero the memory, socket_to_in_addr will do that
		struct sockaddr_in *sin = upsgi_malloc(sizeof(struct sockaddr_in));
		usl->custom = socket_to_in_addr(usl->value, port, 0, sin);
		usl->custom_ptr = sin;
		upsgi_log("added udp node %s for cache \"%s\"\n", usl->value, uc->name);
		usl = usl->next;
	}
}

struct upsgi_cache_item *upsgi_cache_keys(struct upsgi_cache *uc, uint64_t *pos, struct upsgi_cache_item **uci) {

	// security check
	if (*pos >= uc->hashsize) return NULL;
	// iterate hashtable
	uint64_t orig_pos = *pos;
	for(;*pos<uc->hashsize;(*pos)++) {
		// get the cache slot
		uint64_t slot = uc->hashtable[*pos];
		if (*pos == orig_pos && *uci) {
			slot = (*uci)->next;	
		}
		if (slot == 0) continue;

		*uci = cache_item(slot);
		return *uci;
	}

	(*pos)++;
	return NULL;
}

void upsgi_cache_rlock(struct upsgi_cache *uc) {
	upsgi_rlock(uc->lock);
}

void upsgi_cache_rwunlock(struct upsgi_cache *uc) {
	upsgi_rwunlock(uc->lock);
}

char *upsgi_cache_item_key(struct upsgi_cache_item *uci) {
	return uci->key;
}
