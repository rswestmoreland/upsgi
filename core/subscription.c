#include "upsgi.h"

/*

	subscription subsystem

	each subscription slot is as an hashed item in a dictionary

	each slot has a circular linked list containing the nodes names

	the structure and system is very similar to upsgi_dyn_dict already used by the mime type parser

	This system is not mean to run on shared memory. If you have multiple processes for the same app, you have to create
	a new subscriptions slot list.

	To avoid removal of already using nodes, a reference count system has been implemented

*/


extern struct upsgi_server upsgi;

char *upsgi_subscription_algo_name(void *ptr) {
	struct upsgi_string_list *usl = upsgi.subscription_algos;
	while(usl) {
		if (usl->custom_ptr == ptr) {
			return usl->value;
		}
		usl = usl->next;
	}
	return NULL;
}

#ifdef UPSGI_SSL
static void upsgi_subscription_sni_check(struct upsgi_subscribe_slot *current_slot, struct upsgi_subscribe_req *usr) {
	if (usr->sni_key_len > 0 && usr->sni_crt_len > 0) {
		if (!current_slot->sni_enabled) {
			char *sni_key = upsgi_concat2n(usr->sni_key, usr->sni_key_len, "", 0);
			char *sni_crt = upsgi_concat2n(usr->sni_crt, usr->sni_crt_len, "", 0);
			char *sni_ca = NULL;
			if (usr->sni_ca_len > 0) {
				sni_ca = upsgi_concat2n(usr->sni_ca, usr->sni_ca_len, "", 0);
			}
			char *servername = NULL;
			char *colon = memchr(current_slot->key, ':', current_slot->keylen);
			if (colon) {
				servername = upsgi_concat2n(current_slot->key, colon - current_slot->key, "", 0);
			}
			else {
				servername = upsgi_concat2n(current_slot->key, current_slot->keylen, "", 0);
			}
			if (upsgi_ssl_add_sni_item(servername, sni_crt, sni_key, upsgi.sni_dir_ciphers, sni_ca)) {
				current_slot->sni_enabled = 1;
			}
			if (sni_key)
				free(sni_key);
			if (sni_crt)
				free(sni_crt);
			if (sni_ca)
				free(sni_ca);
		}
	}
}
#endif

int upsgi_subscription_credentials_check(struct upsgi_subscribe_slot *slot, struct upsgi_subscribe_req *usr) {
	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, upsgi.subscriptions_credentials_check_dir) {
		char *filename = upsgi_concat2n(usl->value, usl->len, slot->key, slot->keylen);
		struct stat st;
		int ret = stat(filename, &st);
		free(filename);
		if (ret != 0)
			continue;
		if (st.st_uid != usr->uid)
			continue;
		if (st.st_gid != usr->gid)
			continue;
		// accepted...
		return 1;
	}
	return 0;
}

struct upsgi_subscribe_slot *upsgi_get_subscribe_slot(struct upsgi_subscribe_slot **slot, char *key, uint16_t keylen) {
	int retried = 0;
retry:

	if (keylen > 0xff)
		return NULL;

	uint32_t hash = djb33x_hash(key, keylen);
	int hash_key = hash % 0xffff;

	struct upsgi_subscribe_slot *current_slot = slot[hash_key];


#ifdef UPSGI_DEBUG
	upsgi_log("****************************\n");
	while (current_slot) {
		upsgi_log("slot %.*s %d\n", current_slot->keylen, current_slot->key, current_slot->hits);
		current_slot = current_slot->next;
	}
	upsgi_log("****************************\n");
	current_slot = slot[hash_key];
#endif

	while (current_slot) {
		if (!upsgi_strncmp(key, keylen, current_slot->key, current_slot->keylen)) {
			// auto optimization
			if (current_slot->prev) {
				if (current_slot->hits > current_slot->prev->hits) {
					struct upsgi_subscribe_slot *slot_parent = current_slot->prev->prev, *slot_prev = current_slot->prev;
					if (slot_parent) {
						slot_parent->next = current_slot;
					}
					else {
						slot[hash_key] = current_slot;
					}

					if (current_slot->next) {
						current_slot->next->prev = slot_prev;
					}

					slot_prev->prev = current_slot;
					slot_prev->next = current_slot->next;

					current_slot->next = slot_prev;
					current_slot->prev = slot_parent;

				}
			}
			return current_slot;
		}
		current_slot = current_slot->next;
		// check for loopy optimization
		if (current_slot == slot[hash_key])
			break;
	}

	// if we are here and in mountpoints mode, try the domain only variant
	if (upsgi.subscription_mountpoints && !retried) {
		char *slash = memchr(key, '/', keylen);
		if (slash) {
			keylen = slash - key;
			retried = 1;
			goto retry;
		}
	}

	return NULL;
}

struct upsgi_subscribe_node *upsgi_get_subscribe_node(struct upsgi_subscribe_slot **slot, char *key, uint16_t keylen, struct upsgi_subscription_client *client) {

	if (keylen > 0xff)
		return NULL;

	struct upsgi_subscribe_slot *current_slot = upsgi_get_subscribe_slot(slot, key, keylen);
	if (!current_slot)
		return NULL;

	// slot found, move up in the list increasing hits
	current_slot->hits++;
	time_t now = upsgi_now();
	struct upsgi_subscribe_node *node = current_slot->nodes;
	int subscription_age;

	while (node) {
		subscription_age = now - node->last_check;
		// is the node alive ?
		if ((node->len == 0 && (subscription_age > upsgi.subscription_tolerance_inactive)) || (node->len > 0 && (subscription_age > upsgi.subscription_tolerance))) {
			if (node->death_mark == 0) {
				if (node->len > 0) {
					upsgi_log("[upsgi-subscription for pid %d] %.*s => marking %.*s as failed (no announce received in %d seconds)\n", (int) upsgi.mypid, (int) keylen, key, (int) node->len, node->name, upsgi.subscription_tolerance);
				}
				else if (node->vassal_len > 0) {
					upsgi_log("[upsgi-subscription for pid %d] %.*s => marking vassal %.*s as failed (no announce received in %d seconds)\n", (int) upsgi.mypid, (int) keylen, key, (int) node->vassal_len, node->vassal, upsgi.subscription_tolerance_inactive);
				}
			}
			node->failcnt++;
			node->death_mark = 1;
		}
		// do i need to remove the node ?
		if (node->death_mark && node->reference == 0) {
			// remove the node and move to next
			struct upsgi_subscribe_node *dead_node = node;
			node = node->next;
			// if the slot has been removed, return NULL;
			if (upsgi_remove_subscribe_node(slot, dead_node) == 1) {
				return NULL;
			}
			continue;
		}

		struct upsgi_subscribe_node *chosen_node = current_slot->algo(current_slot, node, client);
		if (chosen_node)
			return chosen_node;

		node = node->next;
	}

	return current_slot->algo(current_slot, node, client);
}

struct upsgi_subscribe_node *upsgi_get_subscribe_node_by_name(struct upsgi_subscribe_slot **slot, char *key, uint16_t keylen, char *val, uint16_t vallen) {

	if (keylen > 0xff)
		return NULL;
	struct upsgi_subscribe_slot *current_slot = upsgi_get_subscribe_slot(slot, key, keylen);
	if (current_slot) {
		struct upsgi_subscribe_node *node = current_slot->nodes;
		while (node) {
			if (!upsgi_strncmp(val, vallen, node->name, node->len)) {
				return node;
			}
			node = node->next;
		}
	}

	return NULL;
}

int upsgi_remove_subscribe_node(struct upsgi_subscribe_slot **slot, struct upsgi_subscribe_node *node) {

	int ret = 0;

	struct upsgi_subscribe_node *a_node;
	struct upsgi_subscribe_slot *node_slot = node->slot;
	struct upsgi_subscribe_slot *prev_slot = node_slot->prev;
	struct upsgi_subscribe_slot *next_slot = node_slot->next;

	int hash_key = node_slot->hash;

	// over-engineering to avoid race conditions
	node->len = 0;

	if (node == node_slot->nodes) {
		node_slot->nodes = node->next;
	}
	else {
		a_node = node_slot->nodes;
		while (a_node) {
			if (a_node->next == node) {
				a_node->next = node->next;
				break;
			}
			a_node = a_node->next;
		}
	}

	free(node);
	// no more nodes, remove the slot too
	if (node_slot->nodes == NULL) {

		ret = 1;

		// first check if i am the only node
		if ((!prev_slot && !next_slot) || next_slot == node_slot) {
#ifdef UPSGI_SSL
			if (node_slot->sign_ctx) {
				EVP_PKEY_free(node_slot->sign_public_key);
				EVP_MD_CTX_destroy(node_slot->sign_ctx);
			}
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
			// if there is a SNI context active, destroy it
			if (node_slot->sni_enabled) {
				upsgi_ssl_del_sni_item(node_slot->key, node_slot->keylen);
			}
#endif
#endif
			free(node_slot);
			slot[hash_key] = NULL;
			goto end;
		}

		// if i am the main entry point, set the next value
		if (node_slot == slot[hash_key]) {
			slot[hash_key] = next_slot;
		}

		if (prev_slot) {
			prev_slot->next = next_slot;
		}
		if (next_slot) {
			next_slot->prev = prev_slot;
		}

#ifdef UPSGI_SSL
		if (node_slot->sign_ctx) {
			EVP_PKEY_free(node_slot->sign_public_key);
			EVP_MD_CTX_destroy(node_slot->sign_ctx);
		}
#endif
		free(node_slot);
	}

end:

	return ret;
}

#ifdef UPSGI_SSL
static int subscription_new_sign_ctx(struct upsgi_subscribe_slot *, struct upsgi_subscribe_req *);
static int subscription_is_safe(struct upsgi_subscribe_req *);
#endif

struct upsgi_subscribe_node *upsgi_add_subscribe_node(struct upsgi_subscribe_slot **slot, struct upsgi_subscribe_req *usr) {

	struct upsgi_subscribe_slot *current_slot = upsgi_get_subscribe_slot(slot, usr->key, usr->keylen), *old_slot = NULL, *a_slot;
	struct upsgi_subscribe_node *node, *old_node = NULL;

	if ((usr->address_len > 0xff || usr->address_len == 0) && (usr->vassal_len > 0xff || usr->vassal_len == 0))
		return NULL;

	if (upsgi.subscription_vassal_required && usr->vassal_len == 0)
		return NULL;

	if (current_slot) {
#ifdef UPSGI_SSL
		if (upsgi.subscriptions_sign_check_dir && !upsgi_subscription_sign_check(current_slot, usr)) {
			return NULL;
		}
#endif

		if (upsgi.subscriptions_credentials_check_dir && !upsgi_subscription_credentials_check(current_slot, usr)) {
			return NULL;
		}

		int has_address_and_vassal = 0;
		if (usr->address_len > 0 && usr->vassal_len > 0)
			has_address_and_vassal = 1;

		node = current_slot->nodes;
		while (node) {
			if ((usr->address_len > 0 && !upsgi_strncmp(node->name, node->len, usr->address, usr->address_len))
				|| (usr->vassal_len > 0 && !upsgi_strncmp(node->vassal, node->vassal_len, usr->vassal, usr->vassal_len))) {
#ifdef UPSGI_SSL
				// this should avoid sending sniffed packets...
				if (current_slot->sign_ctx && !subscription_is_safe(usr) && usr->unix_check <= node->unix_check) {
					upsgi_log("[upsgi-subscription for pid %d] invalid (sniffed ?) packet sent for slot: %.*s node: %.*s unix_check: %lu\n", (int) upsgi.mypid, usr->keylen, usr->key, (int) usr->address_len, usr->address, (unsigned long) usr->unix_check);
					return NULL;
				}
				// eventually the packet could be upgraded to sni...
				upsgi_subscription_sni_check(current_slot, usr);
#endif
				// only for vassal mode
				if (has_address_and_vassal) {
					if (usr->address_len == node->len && !memcmp(usr->address, node->name, node->len)) {
						// record already exists, clear it ?
						if (usr->clear) {
							node->len = 0;
							upsgi_log("[upsgi-subscription for pid %d] %.*s => cleared address for vassal node: %.*s (weight: %d, backup: %d)\n", (int) upsgi.mypid, usr->keylen, usr->key, (int) usr->vassal_len, usr->vassal, usr->weight, usr->backup_level);
						}
					}
					else {
						memcpy(node->name, usr->address, usr->address_len);
						node->len = usr->address_len;
						upsgi_log("[upsgi-subscription for pid %d] %.*s => updated vassal node: %.*s with address %.*s (weight: %d, backup: %d)\n", (int) upsgi.mypid, usr->keylen, usr->key, (int) usr->vassal_len, usr->vassal, (int) usr->address_len, usr->address, usr->weight, usr->backup_level);	
					}
				}

				// remove death mark and update cores and load
				node->death_mark = 0;
				node->last_check = upsgi_now();
				node->cores = usr->cores;
				node->load = usr->load;
				node->weight = usr->weight;
				node->backup_level = usr->backup_level;
				if (usr->proto_len > 0) {
					node->proto = usr->proto[0];
				}	
				if (!node->weight)
					node->weight = 1;
				node->last_requests = 0;
				return node;
			}
			old_node = node;
			node = node->next;
		}

#ifdef UPSGI_SSL
		if (current_slot->sign_ctx && !subscription_is_safe(usr) && usr->unix_check < (upsgi_now() - (time_t) upsgi.subscriptions_sign_check_tolerance)) {
			upsgi_log("[upsgi-subscription for pid %d] invalid (sniffed ?) packet sent for slot: %.*s node: %.*s unix_check: %lu\n", (int) upsgi.mypid, usr->keylen, usr->key, usr->address_len, usr->address, (unsigned long) usr->unix_check);
                        return NULL;
		}
		// check here as we are sure the node will be added
		upsgi_subscription_sni_check(current_slot, usr);
#endif

		node = upsgi_malloc(sizeof(struct upsgi_subscribe_node));
		node->len = usr->address_len;
		node->modifier1 = usr->modifier1;
		node->modifier2 = usr->modifier2;
		node->requests = 0;
		node->last_requests = 0;
		node->tx = 0;
		node->rx = 0;
		node->reference = 0;
		node->death_mark = 0;
		node->failcnt = 0;
		node->cores = usr->cores;
		node->load = usr->load;
		node->weight = usr->weight;
		node->backup_level = usr->backup_level;
		if (usr->proto_len > 0) {
			node->proto = usr->proto[0];
		}
		node->unix_check = usr->unix_check;
		if (!node->weight)
			node->weight = 1;
		node->wrr = 0;
		node->pid = usr->pid;
		node->uid = usr->uid;
		node->gid = usr->gid;
		node->notify[0] = 0;
		if (usr->notify_len > 0 && usr->notify_len < 102) {
			memcpy(node->notify, usr->notify, usr->notify_len);
			node->notify[usr->notify_len] = 0;
		}
		node->last_check = upsgi_now();
		node->slot = current_slot;
		node->vassal_len = usr->vassal_len;

		if (node->len > 0)
			memcpy(node->name, usr->address, node->len);
		if (usr->vassal_len > 0)
			memcpy(node->vassal, usr->vassal, node->vassal_len);
		
		if (old_node) {
			old_node->next = node;
		}
		node->next = NULL;

		upsgi_log("[upsgi-subscription for pid %d] %.*s => new node: %.*s (weight: %d, backup: %d)\n", (int) upsgi.mypid, usr->keylen, usr->key, usr->address_len, usr->address, usr->weight, usr->backup_level);
		if (node->notify[0]) {
			char buf[1024];
			int ret = snprintf(buf, 1024, "[subscription ack] %.*s => new node: %.*s", usr->keylen, usr->key, usr->address_len, usr->address);
			if (ret > 0 && ret < 1024)
				upsgi_notify_msg(node->notify, buf, ret);
		}
		return node;
	}
	else {
		current_slot = upsgi_malloc(sizeof(struct upsgi_subscribe_slot));
#ifdef UPSGI_SSL
		current_slot->sign_ctx = NULL;
		if (upsgi.subscriptions_sign_check_dir && !subscription_new_sign_ctx(current_slot, usr)) {
			free(current_slot);
			return NULL;
		}
#endif
		uint32_t hash = djb33x_hash(usr->key, usr->keylen);
		int hash_key = hash % 0xffff;
		current_slot->hash = hash_key;
		current_slot->keylen = usr->keylen;
		memcpy(current_slot->key, usr->key, usr->keylen);
		if (upsgi.subscriptions_credentials_check_dir) {
			if (!upsgi_subscription_credentials_check(current_slot, usr)) {
				free(current_slot);
				return NULL;
			}
		}

		current_slot->key[usr->keylen] = 0;
		current_slot->hits = 0;
#ifdef UPSGI_SSL
		current_slot->sni_enabled = 0;
		upsgi_subscription_sni_check(current_slot, usr);
#endif
		current_slot->nodes = upsgi_malloc(sizeof(struct upsgi_subscribe_node));
		current_slot->nodes->slot = current_slot;
		current_slot->nodes->len = usr->address_len;
		current_slot->nodes->reference = 0;
		current_slot->nodes->requests = 0;
		current_slot->nodes->last_requests = 0;
		current_slot->nodes->tx = 0;
		current_slot->nodes->rx = 0;
		current_slot->nodes->death_mark = 0;
		current_slot->nodes->failcnt = 0;
		current_slot->nodes->modifier1 = usr->modifier1;
		current_slot->nodes->modifier2 = usr->modifier2;
		current_slot->nodes->cores = usr->cores;
		current_slot->nodes->load = usr->load;
		current_slot->nodes->weight = usr->weight;
		current_slot->nodes->backup_level = usr->backup_level;
		if (usr->proto_len > 0) {
			current_slot->nodes->proto = usr->proto[0];
		}
		current_slot->nodes->unix_check = usr->unix_check;
		if (!current_slot->nodes->weight)
			current_slot->nodes->weight = 1;
		current_slot->nodes->wrr = 0;
		current_slot->nodes->pid = usr->pid;
		current_slot->nodes->uid = usr->uid;
		current_slot->nodes->gid = usr->gid;
		current_slot->nodes->notify[0] = 0;
		if (usr->notify_len > 0 && usr->notify_len < 102) {
			memcpy(current_slot->nodes->notify, usr->notify, usr->notify_len);
			current_slot->nodes->notify[usr->notify_len] = 0;
		}
		if (usr->address_len > 0)
			memcpy(current_slot->nodes->name, usr->address, usr->address_len);
		current_slot->nodes->vassal_len = usr->vassal_len;
		if (current_slot->nodes->vassal_len > 0)
			memcpy(current_slot->nodes->vassal, usr->vassal, usr->vassal_len);
		current_slot->nodes->last_check = upsgi_now();

		current_slot->nodes->next = NULL;

		a_slot = slot[hash_key];
		while (a_slot) {
			old_slot = a_slot;
			a_slot = a_slot->next;
		}


		if (old_slot) {
			old_slot->next = current_slot;
		}

		current_slot->prev = old_slot;
		current_slot->next = NULL;

		current_slot->algo = usr->algo;
		if (!current_slot->algo) current_slot->algo = upsgi.subscription_algo;

		if (!slot[hash_key] || current_slot->prev == NULL) {
			slot[hash_key] = current_slot;
		}

		upsgi_log("[upsgi-subscription for pid %d] new pool: %.*s (hash key: %d, algo: %s)\n", (int) upsgi.mypid, usr->keylen, usr->key, current_slot->hash, upsgi_subscription_algo_name(current_slot->algo));
		if (usr->address_len > 0) {
			upsgi_log("[upsgi-subscription for pid %d] %.*s => new node: %.*s (weight: %d, backup: %d)\n", (int) upsgi.mypid, usr->keylen, usr->key, usr->address_len, usr->address, usr->weight, usr->backup_level);
		}
		else {
			upsgi_log("[upsgi-subscription for pid %d] %.*s => new vassal node: %.*s (weight: %d, backup: %d)\n", (int) upsgi.mypid, usr->keylen, usr->key, usr->vassal_len, usr->vassal, usr->weight, usr->backup_level);
		}

		if (current_slot->nodes->notify[0]) {
			char buf[1024];
			int ret = snprintf(buf, 1024, "[subscription ack] %.*s => new node: %.*s", usr->keylen, usr->key, usr->address_len, usr->address);
			if (ret > 0 && ret < 1024)
				upsgi_notify_msg(current_slot->nodes->notify, buf, ret);
		}
		return current_slot->nodes;
	}

}

static void send_subscription(int sfd, char *host, char *message, uint16_t message_size) {

	int fd = sfd;
	struct sockaddr_in udp_addr;
	struct sockaddr_un un_addr;
	ssize_t ret;

	char *udp_port = strchr(host, ':');

	if (fd == -1) {
		if (udp_port) {
			fd = socket(AF_INET, SOCK_DGRAM, 0);
		}
		else {
			fd = socket(AF_UNIX, SOCK_DGRAM, 0);
		}
		if (fd < 0) {
			upsgi_error("send_subscription()/socket()");
			return;
		}
		upsgi_socket_nb(fd);
	}
	else if (fd == -2) {
		static int unix_fd = -1;
		static int inet_fd = -1;
		if (udp_port) {
			if (inet_fd == -1) {
				inet_fd = socket(AF_INET, SOCK_DGRAM, 0);
				if (inet_fd < 0) {
					upsgi_error("send_subscription()/socket()");
					return;
				}
				upsgi_socket_nb(inet_fd);
			}
			fd = inet_fd;
		}
		else {
			if (unix_fd == -1) {
				unix_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
				if (unix_fd < 0) {
					upsgi_error("send_subscription()/socket()");
					return;
				}
				upsgi_socket_nb(unix_fd);
			}
			fd = unix_fd;
		}
	}

	if (udp_port) {
		udp_port[0] = 0;
		memset(&udp_addr, 0, sizeof(struct sockaddr_in));
		udp_addr.sin_family = AF_INET;
		udp_addr.sin_port = htons(atoi(udp_port + 1));
		udp_addr.sin_addr.s_addr = inet_addr(host);
		ret = sendto(fd, message, message_size, 0, (struct sockaddr *) &udp_addr, sizeof(udp_addr));
		udp_port[0] = ':';
	}
	else {
		memset(&un_addr, 0, sizeof(struct sockaddr_un));
		un_addr.sun_family = AF_UNIX;
		// use 102 as the magic number
		strncat(un_addr.sun_path, host, 102);
		if (upsgi.subscriptions_use_credentials) {
			// could be useless as internally the socket could add them automagically
			ret = upsgi_pass_cred2(fd, message, message_size, (struct sockaddr *) &un_addr, sizeof(un_addr));
		}
		else {
			ret = sendto(fd, message, message_size, 0, (struct sockaddr *) &un_addr, sizeof(un_addr));
		}
	}

	if (ret < 0) {
		upsgi_error("send_subscription()/sendto()");
	}

	if (sfd == -1)
		close(fd);
}

static int upsgi_subscription_ub_fix(struct upsgi_buffer *ub, uint8_t modifier1, uint8_t modifier2, uint8_t cmd, char *sign) {
	#ifdef UPSGI_SSL
        if (sign) {
                if (upsgi_buffer_append_keynum(ub, "unix", 4, (upsgi_now() + (time_t) cmd)))
                        return -1;

                unsigned int signature_len = 0;
                char *signature = upsgi_rsa_sign(sign, ub->buf + 4, ub->pos - 4, &signature_len);
                if (signature && signature_len > 0) {
                        if (upsgi_buffer_append_keyval(ub, "sign", 4, signature, signature_len)) {
                                free(signature);
				return -1;
                        }
                        free(signature);
                }
        }
#endif

        // add upsgi header
        if (upsgi_buffer_set_uh(ub, 224, cmd)) return -1;

	return 0;
}

static struct upsgi_buffer *upsgi_subscription_ub(char *key, size_t keysize, uint8_t modifier1, uint8_t modifier2, uint8_t cmd, char *socket_name, char *sign, char *sni_key, char *sni_crt, char *sni_ca) {
	struct upsgi_buffer *ub = upsgi_buffer_new(4096);

	// make space for upsgi header
	ub->pos = 4;

	if (upsgi_buffer_append_keyval(ub, "key", 3, key, keysize))
		goto end;
	if (upsgi_buffer_append_keyval(ub, "address", 7, socket_name, strlen(socket_name)))
		goto end;

	if (upsgi.subscribe_with_modifier1) {
		modifier1 = atoi(upsgi.subscribe_with_modifier1);
	}
	if (upsgi_buffer_append_keynum(ub, "modifier1", 9, modifier1))
		goto end;
	if (upsgi_buffer_append_keynum(ub, "modifier2", 9, modifier2))
		goto end;
	if (upsgi_buffer_append_keynum(ub, "cores", 5, upsgi.numproc * upsgi.cores))
		goto end;
	if (upsgi_buffer_append_keynum(ub, "load", 4, upsgi.shared->load))
		goto end;
	if (upsgi.auto_weight) {
		if (upsgi_buffer_append_keynum(ub, "weight", 6, upsgi.numproc * upsgi.cores))
			goto end;
	}
	else {
		if (upsgi_buffer_append_keynum(ub, "weight", 6, upsgi.weight))
			goto end;
	}

	if (sni_key) {
		if (upsgi_buffer_append_keyval(ub, "sni_key", 7, sni_key, strlen(sni_key)))
			goto end;
	}

	if (sni_crt) {
		if (upsgi_buffer_append_keyval(ub, "sni_crt", 7, sni_crt, strlen(sni_crt)))
			goto end;
	}

	if (sni_ca) {
		if (upsgi_buffer_append_keyval(ub, "sni_ca", 6, sni_ca, strlen(sni_ca)))
			goto end;
	}

	if (upsgi.subscription_notify_socket) {
		if (upsgi_buffer_append_keyval(ub, "notify", 6, upsgi.subscription_notify_socket, strlen(upsgi.subscription_notify_socket)))
			goto end;
	}
	else if (upsgi.notify_socket_fd > -1 && upsgi.notify_socket) {
		if (upsgi_buffer_append_keyval(ub, "notify", 6, upsgi.notify_socket, strlen(upsgi.notify_socket)))
			goto end;
	}

	if (upsgi_subscription_ub_fix(ub, modifier1, modifier2, cmd, sign)) goto end;

	return ub;

end:
	upsgi_buffer_destroy(ub);
	return NULL;
}

void upsgi_send_subscription_from_fd(int fd, char *udp_address, char *key, size_t keysize, uint8_t modifier1, uint8_t modifier2, uint8_t cmd, char *socket_name, char *sign, char *sni_key, char *sni_crt, char *sni_ca) {

	if (socket_name == NULL && !upsgi.sockets)
		return;

	if (!socket_name) {
		socket_name = upsgi.sockets->name;
	}

	struct upsgi_buffer *ub = upsgi_subscription_ub(key, keysize, modifier1, modifier2, cmd, socket_name, sign, sni_key, sni_crt, sni_ca);

	if (!ub)
		return;

	send_subscription(fd, udp_address, ub->buf, ub->pos);
	upsgi_buffer_destroy(ub);
}


void upsgi_send_subscription(char *udp_address, char *key, size_t keysize, uint8_t modifier1, uint8_t modifier2, uint8_t cmd, char *socket_name, char *sign, char *sni_key, char *sni_crt, char *sni_ca) {
	upsgi_send_subscription_from_fd(-1, udp_address, key, keysize, modifier1, modifier2, cmd, socket_name, sign, sni_key, sni_crt, sni_ca);
}

#ifdef UPSGI_SSL
static int subscription_is_safe(struct upsgi_subscribe_req *usr) {
	struct upsgi_string_list *usl = NULL;
        upsgi_foreach(usl, upsgi.subscriptions_sign_skip_uid) {
                if (usl->custom == 0) {
                        usl->custom = atoi(usl->value);
                }
                if (usr->uid > 0 && usr->uid == (uid_t) usl->custom) {
                        return 1;
                }
        }
	return 0;
}
static int subscription_new_sign_ctx(struct upsgi_subscribe_slot *slot, struct upsgi_subscribe_req *usr) {
	if (subscription_is_safe(usr)) return 1;

	if (usr->sign_len == 0 || usr->base_len == 0)
		return 0;

	if (usr->unix_check < (upsgi_now() - (time_t) upsgi.subscriptions_sign_check_tolerance)) {
        	upsgi_log("[upsgi-subscription for pid %d] invalid (sniffed ?) packet sent for slot: %.*s node: %.*s unix_check: %lu\n", (int) upsgi.mypid, usr->keylen, usr->key, usr->address_len, usr->address, (unsigned long) usr->unix_check);
		return 0;
        }

	char *keyfile = upsgi_sanitize_cert_filename(upsgi.subscriptions_sign_check_dir, usr->key, usr->keylen);
	FILE *kf = fopen(keyfile, "r");
	free(keyfile);
	if (!kf) return 0;
	slot->sign_public_key = PEM_read_PUBKEY(kf, NULL, NULL, NULL);
	fclose(kf);
	if (!slot->sign_public_key) {
        	upsgi_log("unable to load public key for %.*s\n", usr->keylen, usr->key);
		return 0;
	}
	slot->sign_ctx = EVP_MD_CTX_create();
	if (!slot->sign_ctx) {
        	upsgi_log("unable to initialize EVP context for %.*s\n", usr->keylen, usr->key);
                EVP_PKEY_free(slot->sign_public_key);
		return 0;
	}

	if (!upsgi_subscription_sign_check(slot, usr)) {
		EVP_PKEY_free(slot->sign_public_key);
		EVP_MD_CTX_destroy(slot->sign_ctx);
		return 0;
	}

	return 1;
}
int upsgi_subscription_sign_check(struct upsgi_subscribe_slot *slot, struct upsgi_subscribe_req *usr) {
	if (subscription_is_safe(usr)) return 1;

	if (usr->sign_len == 0 || usr->base_len == 0)
		return 0;

	if (!slot->sign_ctx) {
		if (!subscription_new_sign_ctx(slot, usr)) return 0;
	}

	if (EVP_VerifyInit_ex(slot->sign_ctx, upsgi.subscriptions_sign_check_md, NULL) == 0) {
		ERR_print_errors_fp(stderr);
		return 0;
	}

	if (EVP_VerifyUpdate(slot->sign_ctx, usr->base, usr->base_len) == 0) {
		ERR_print_errors_fp(stderr);
		return 0;
	}

	if (EVP_VerifyFinal(slot->sign_ctx, (unsigned char *) usr->sign, usr->sign_len, slot->sign_public_key) != 1) {
#ifdef UPSGI_DEBUG
		ERR_print_errors_fp(stderr);
#endif
		return 0;
	}


	return 1;
}
#endif

int upsgi_no_subscriptions(struct upsgi_subscribe_slot **slot) {
	int i;
	for (i = 0; i < UMAX16; i++) {
		if (slot[i])
			return 0;
	}
	return 1;
}

void upsgi_subscribe(char *subscription, uint8_t cmd) {

	size_t subfile_size;
	size_t i;
	char *key = NULL;
	int keysize = 0;
	char *modifier1 = NULL;
	int modifier1_len = 0;
	char *socket_name = NULL;
	char *udp_address = subscription;
	char *udp_port = NULL;
	char *subscription_key = NULL;
	char *sign = NULL;

	// check for explicit socket_name
	char *equal = strchr(subscription, '=');
	if (equal) {
		socket_name = subscription;
		if (socket_name[0] == '=') {
			equal = strchr(socket_name + 1, '=');
			if (!equal)
				return;
			*equal = '\0';
			struct upsgi_socket *us = upsgi_get_shared_socket_by_num(atoi(socket_name + 1));
			if (!us)
				return;
			socket_name = us->name;
		}
		*equal = '\0';
		udp_address = equal + 1;
	}

	// check for unix socket
	if (udp_address[0] != '/') {
		udp_port = strchr(udp_address, ':');
		if (!udp_port) {
			if (equal)
				*equal = '=';
			return;
		}
		subscription_key = strchr(udp_port + 1, ':');
	}
	else {
		subscription_key = strchr(udp_address + 1, ':');
	}

	if (!subscription_key) {
		if (equal)
			*equal = '=';
		return;
	}

	udp_address = upsgi_concat2n(udp_address, subscription_key - udp_address, "", 0);

	if (subscription_key[1] == '@') {
		if (!upsgi_file_exists(subscription_key + 2))
			goto clear;
		char *lines = upsgi_open_and_read(subscription_key + 2, &subfile_size, 1, NULL);
		if (subfile_size > 0) {
			key = lines;
			for (i = 0; i < subfile_size; i++) {
				if (lines[i] == 0) {
					if (keysize > 0) {
						if (key[0] != '#' && key[0] != '\n') {
							modifier1 = strchr(key, ',');
							if (modifier1) {
								modifier1[0] = 0;
								modifier1++;
								modifier1_len = strlen(modifier1);
								keysize = strlen(key);
							}
							upsgi_send_subscription(udp_address, key, keysize, upsgi_str_num(modifier1, modifier1_len), 0, cmd, socket_name, sign, NULL, NULL, NULL);
							modifier1 = NULL;
							modifier1_len = 0;
						}
					}
					break;
				}
				else if (lines[i] == '\n') {
					if (keysize > 0) {
						if (key[0] != '#' && key[0] != '\n') {
							lines[i] = 0;
							modifier1 = strchr(key, ',');
							if (modifier1) {
								modifier1[0] = 0;
								modifier1++;
								modifier1_len = strlen(modifier1);
								keysize = strlen(key);
							}
							upsgi_send_subscription(udp_address, key, keysize, upsgi_str_num(modifier1, modifier1_len), 0, cmd, socket_name, sign, NULL, NULL, NULL);
							modifier1 = NULL;
							modifier1_len = 0;
							lines[i] = '\n';
						}
					}
					key = lines + i + 1;
					keysize = 0;
					continue;
				}
				keysize++;
			}
		}
		free(lines);
	}
	else {
		modifier1 = strchr(subscription_key + 1, ',');
		if (modifier1) {
			modifier1[0] = 0;
			modifier1++;

			sign = strchr(modifier1 + 1, ',');
			if (sign) {
				*sign = 0;
				sign++;
			}
			modifier1_len = strlen(modifier1);
		}

		upsgi_send_subscription(udp_address, subscription_key + 1, strlen(subscription_key + 1), upsgi_str_num(modifier1, modifier1_len), 0, cmd, socket_name, sign, NULL, NULL, NULL);
		if (modifier1)
			modifier1[-1] = ',';
		if (sign)
			sign[-1] = ',';
	}

clear:
	if (equal)
		*equal = '=';
	free(udp_address);

}

void upsgi_subscribe2(char *arg, uint8_t cmd) {

	char *s2_server = NULL;
	char *s2_key = NULL;
	char *s2_socket = NULL;
	char *s2_addr = NULL;
	char *s2_weight = NULL;
	char *s2_sign = NULL;
	char *s2_modifier1 = NULL;
	char *s2_modifier2 = NULL;
	char *s2_check = NULL;
	char *s2_sni_key = NULL;
	char *s2_sni_crt = NULL;
	char *s2_sni_ca = NULL;
	char *s2_proto = NULL;
	char *s2_algo = NULL;
	char *s2_backup = NULL;
	char *s2_vassal = NULL;
	char *s2_inactive = NULL;
	struct upsgi_buffer *ub = NULL;

	if (upsgi_kvlist_parse(arg, strlen(arg), ',', '=',
		"server", &s2_server,
		"key", &s2_key,
		"socket", &s2_socket,
		"addr", &s2_addr,
		"address", &s2_addr,
		"weight", &s2_weight,
		"modifier1", &s2_modifier1,
		"modifier2", &s2_modifier2,
		"sign", &s2_sign,
		"check", &s2_check,
		"sni_key", &s2_sni_key,
		"sni_crt", &s2_sni_crt,
		"sni_ca", &s2_sni_ca,
		"proto", &s2_proto,
		"algo", &s2_algo,
		"backup", &s2_backup,
		"vassal", &s2_vassal,
		"inactive", &s2_inactive,
		NULL)) {
		return;
	}

	if (!s2_server || !s2_key)
		goto end;

	if (s2_check) {
		if (upsgi_file_exists(s2_check))
			goto end;
	}

	int weight = 1;
	int backup = 0;
	if (upsgi.auto_weight) weight = upsgi.numproc * upsgi.cores;
	if (s2_weight) {
		weight = atoi(s2_weight);
	}

	if (s2_backup) {
		backup = atoi(s2_backup);
	}

	if (s2_socket) {
		struct upsgi_socket *us = upsgi_get_socket_by_num(atoi(s2_socket));
		if (us) {
			if (s2_addr) {
				free(s2_addr);
			}
			s2_addr = upsgi_str(us->name);
		}
	}

	uint8_t modifier1 = 0;
	uint8_t modifier2 = 0;

	if (s2_modifier1) {
		modifier1 = atoi(s2_modifier1);
	}

	if (s2_modifier2) {
		modifier2 = atoi(s2_modifier2);
	}

	if (s2_addr == NULL) {
		// no socket... no subscription
		if (!upsgi.sockets) goto end;
		s2_addr = upsgi_str(upsgi.sockets->name);
	}

        ub = upsgi_buffer_new(upsgi.page_size);
        if (!ub) goto end;
	// leave space for the header
	ub->pos = 4;

	if (upsgi_buffer_append_keyval(ub, "key", 3, s2_key, strlen(s2_key)))
                goto end;
        if (upsgi_buffer_append_keyval(ub, "address", 7, s2_addr, strlen(s2_addr)))
                goto end;
        if (upsgi_buffer_append_keynum(ub, "modifier1", 9, modifier1))
                goto end;
        if (upsgi_buffer_append_keynum(ub, "modifier2", 9, modifier2))
                goto end;
        if (upsgi_buffer_append_keynum(ub, "cores", 5, upsgi.numproc * upsgi.cores))
                goto end;
        if (upsgi_buffer_append_keynum(ub, "load", 4, upsgi.shared->load))
                goto end;
        if (upsgi_buffer_append_keynum(ub, "weight", 6, weight))
        	goto end;
        if (upsgi_buffer_append_keynum(ub, "backup", 6, backup))
        	goto end;

	if (s2_vassal) {
                if (upsgi_buffer_append_keyval(ub, "vassal", 6, s2_vassal, strlen(s2_vassal)))
                        goto end;
	}

        if (s2_sni_key) {
                if (upsgi_buffer_append_keyval(ub, "sni_key", 7, s2_sni_key, strlen(s2_sni_key)))
                        goto end;
        }

        if (s2_sni_crt) {
                if (upsgi_buffer_append_keyval(ub, "sni_crt", 7, s2_sni_crt, strlen(s2_sni_crt)))
                        goto end;
        }

        if (s2_sni_ca) {
                if (upsgi_buffer_append_keyval(ub, "sni_ca", 6, s2_sni_ca, strlen(s2_sni_ca)))
                        goto end;
        }

	if (s2_proto) {
                if (upsgi_buffer_append_keyval(ub, "proto", 5, s2_proto, strlen(s2_proto)))
                        goto end;
	}

	if (s2_algo) {
                if (upsgi_buffer_append_keyval(ub, "algo", 4, s2_algo, strlen(s2_algo)))
                        goto end;
	}

	if (s2_inactive) {
                if (upsgi_buffer_append_keyval(ub, "inactive", 8, s2_inactive, strlen(s2_inactive)))
                        goto end;
	}

        if (upsgi.subscription_notify_socket) {
                if (upsgi_buffer_append_keyval(ub, "notify", 6, upsgi.subscription_notify_socket, strlen(upsgi.subscription_notify_socket)))
                        goto end;
        }
        else if (upsgi.notify_socket_fd > -1 && upsgi.notify_socket) {
                if (upsgi_buffer_append_keyval(ub, "notify", 6, upsgi.notify_socket, strlen(upsgi.notify_socket)))
                        goto end;
        }

	// clear instead of unsubscribe
	if (upsgi_instance_is_dying && cmd == 1 && upsgi.subscription_clear_on_shutdown) {
		if (upsgi_buffer_append_keynum(ub, "clear", 5, 1))
                	goto end;
		cmd = 0;
	}

        if (upsgi_subscription_ub_fix(ub, modifier1, modifier2, cmd, s2_sign)) goto end;

        send_subscription(-1, s2_server, ub->buf, ub->pos);

end:
	if (ub)
		upsgi_buffer_destroy(ub);

	if (s2_server)
		free(s2_server);
	if (s2_key)
		free(s2_key);
	if (s2_socket)
		free(s2_socket);
	if (s2_addr)
		free(s2_addr);
	if (s2_weight)
		free(s2_weight);
	if (s2_modifier1)
		free(s2_modifier1);
	if (s2_modifier2)
		free(s2_modifier2);
	if (s2_sign)
		free(s2_sign);
	if (s2_check)
		free(s2_check);
	if (s2_sni_crt)
		free(s2_sni_crt);
	if (s2_sni_key)
		free(s2_sni_key);
	if (s2_sni_ca)
		free(s2_sni_ca);
	if (s2_proto)
		free(s2_proto);
	if (s2_algo)
		free(s2_algo);
	if (s2_inactive)
		free(s2_inactive);
	if (s2_backup)
		free(s2_backup);
	if (s2_vassal)
		free(s2_vassal);
}

void upsgi_subscribe_all(uint8_t cmd, int verbose) {

	if (upsgi.subscriptions_blocked)
		return;
	// -- subscribe
	struct upsgi_string_list *subscriptions = upsgi.subscriptions;
	while (subscriptions) {
		if (verbose) {
			upsgi_log("%s %s\n", cmd ? "unsubscribing from" : "subscribing to", subscriptions->value);
		}
		upsgi_subscribe(subscriptions->value, cmd);
		subscriptions = subscriptions->next;
	}

	// --subscribe2
	subscriptions = upsgi.subscriptions2;
	while (subscriptions) {
		if (verbose) {
			upsgi_log("%s %s\n", cmd ? "unsubscribing from" : "subscribing to", subscriptions->value);
		}
		upsgi_subscribe2(subscriptions->value, cmd);
		subscriptions = subscriptions->next;
	}

}

// iphash
static struct upsgi_subscribe_node *upsgi_subscription_algo_iphash(struct upsgi_subscribe_slot *current_slot, struct upsgi_subscribe_node *node, struct upsgi_subscription_client *client) {
        // if node is NULL we are in the second step (in lrc mode we do not use the first step)
        if (node)
                return NULL;

	// iphash does not support requests without client data
	if (!client) return NULL;
	if (!client->sockaddr) return NULL;
	uint64_t count = 0;
	// first step is counting the number of nodes
	node = current_slot->nodes;
	while(node) {
		if (!node->death_mark) count++;
		node = node->next;
	}
	if (count == 0) return NULL;

	uint64_t hash = 0;

	//hash the ip
	if (client->sockaddr->sa.sa_family == AF_INET) {
		hash = client->sockaddr->sa_in.sin_addr.s_addr % count;
	}
#ifdef AF_INET6
	else if (client->sockaddr->sa.sa_family == AF_INET6) {
		hash = djb33x_hash((char *)client->sockaddr->sa_in6.sin6_addr.s6_addr, 16) % count;
	}
#endif
		
	// now re-iterate until count matches;
	count = 0;
        struct upsgi_subscribe_node *chosen_node = NULL;
        node = current_slot->nodes;
        while (node) {
                if (!node->death_mark) {
			if (count == hash) {
				chosen_node = node;
				break;
			}
			count++;
                }
                node = node->next;
        }

        if (chosen_node) {
                chosen_node->reference++;
        }

        return chosen_node;
}

// least reference count
static struct upsgi_subscribe_node *upsgi_subscription_algo_lrc(struct upsgi_subscribe_slot *current_slot, struct upsgi_subscribe_node *node, struct upsgi_subscription_client *client) {
	uint64_t backup_level = 0;
        uint64_t has_backup = 0;

        // if node is NULL we are in the second step (in lrc mode we do not use the first step)
        if (node)
                return NULL;

        struct upsgi_subscribe_node *chosen_node = NULL;
retry:
        node = current_slot->nodes;
        uint64_t min_rc = 0;
        while (node) {
                if (!node->death_mark) {
			if (node->backup_level == backup_level) {
                        	if (min_rc == 0 || node->reference < min_rc) {
                                	min_rc = node->reference;
                                	chosen_node = node;
                                	if (min_rc == 0 && !(node->next && node->next->reference <= node->reference && node->next->last_requests <= node->last_requests))
                                        	break;
                        	}
			}
			else if (node->backup_level > backup_level && (!has_backup || has_backup > node->backup_level)) {
                                has_backup = node->backup_level;
                        }
                }
                node = node->next;
        }

        if (chosen_node) {
                chosen_node->reference++;
        }
	else if (has_backup) {
                backup_level = has_backup;
                goto retry;
        }

        return chosen_node;
}

// weighted least reference count
static struct upsgi_subscribe_node *upsgi_subscription_algo_wlrc(struct upsgi_subscribe_slot *current_slot, struct upsgi_subscribe_node *node, struct upsgi_subscription_client *client) {
	uint64_t backup_level = 0;
        uint64_t has_backup = 0;

        // if node is NULL we are in the second step (in wlrc mode we do not use the first step)
        if (node)
                return NULL;

        struct upsgi_subscribe_node *chosen_node = NULL;
retry:
        node = current_slot->nodes;
	has_backup = 0;
        double min_rc = 0;
        while (node) {
                if (!node->death_mark) {
			if (node->backup_level == backup_level) {
                        	// node->weight is always >= 1, we can safely use it as divider
                        	double ref = (double) node->reference / (double) node->weight;
                        	double next_node_ref = 0;
                        	if (node->next)
                                	next_node_ref = (double) node->next->reference / (double) node->next->weight;

                        	if (min_rc == 0 || ref < min_rc) {
                                	min_rc = ref;
                                	chosen_node = node;
                                	if (min_rc == 0 && !(node->next && next_node_ref <= ref && node->next->last_requests <= node->last_requests))
                                	        break;
                        	}
			}
			else if (node->backup_level > backup_level && (!has_backup || has_backup > node->backup_level)) {
                                has_backup = node->backup_level;
                        }
                }
                node = node->next;
        }

        if (chosen_node) {
                chosen_node->reference++;
        }
	else if (has_backup) {
                backup_level = has_backup;
                goto retry;
        }

        return chosen_node;
}

// weighted round robin algo (with backup support)
static struct upsgi_subscribe_node *upsgi_subscription_algo_wrr(struct upsgi_subscribe_slot *current_slot, struct upsgi_subscribe_node *node, struct upsgi_subscription_client *client) {
	uint64_t backup_level = 0;
	uint64_t has_backup = 0;
        // if node is NULL we are in the second step
        if (node) {
                if (node->death_mark == 0 && node->wrr > 0) {
                        node->wrr--;
                        node->reference++;
                        return node;
                }
                return NULL;
        }

        // no wrr > 0 node found, reset them
        node = current_slot->nodes;
        uint64_t min_weight = 0;
        while (node) {
                if (!node->death_mark) {
                        if (min_weight == 0 || node->weight < min_weight)
                                min_weight = node->weight;
                }
                node = node->next;
        }

        // now set wrr
retry:
        node = current_slot->nodes;
	has_backup = 0;
        struct upsgi_subscribe_node *chosen_node = NULL;
        while (node) {
                if (!node->death_mark) {
			if (node->backup_level == backup_level) {
                        	node->wrr = node->weight / min_weight;
                        	chosen_node = node;
                	}
			else if (node->backup_level > backup_level && (!has_backup || has_backup > node->backup_level)) {
				has_backup = node->backup_level;
			}
		}
                node = node->next;
        }
        if (chosen_node) {
                chosen_node->wrr--;
                chosen_node->reference++;
        }
	else if (has_backup) {
		backup_level = has_backup;
		goto retry;
	}
        return chosen_node;
}

void upsgi_subscription_init_algos() {

	upsgi_register_subscription_algo("wrr", upsgi_subscription_algo_wrr);
	upsgi_register_subscription_algo("lrc", upsgi_subscription_algo_lrc);
	upsgi_register_subscription_algo("wlrc", upsgi_subscription_algo_wlrc);
	upsgi_register_subscription_algo("iphash", upsgi_subscription_algo_iphash);
}

void upsgi_subscription_set_algo(char *algo) {
	if (!upsgi.subscription_algos) {
		upsgi_subscription_init_algos();
	}
	if (!algo)
                goto wrr;
	upsgi.subscription_algo = upsgi_subscription_algo_get(algo, strlen(algo));
	if (upsgi.subscription_algo) return ;

wrr:
        upsgi.subscription_algo = upsgi_subscription_algo_wrr;
}

// we are lazy for subscription algos, we initialize them only if needed
struct upsgi_subscribe_slot **upsgi_subscription_init_ht() {
        if (!upsgi.subscription_algo) {
                upsgi_subscription_set_algo(NULL);
        }
        return upsgi_calloc(sizeof(struct upsgi_subscription_slot *) * UMAX16);
}

struct upsgi_subscribe_node *(*upsgi_subscription_algo_get(char *name , size_t len))(struct upsgi_subscribe_slot *, struct upsgi_subscribe_node *, struct upsgi_subscription_client *) {
	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, upsgi.subscription_algos) {
		if (!upsgi_strncmp(usl->value, usl->len, name, len)) {
			return (struct upsgi_subscribe_node *(*)(struct upsgi_subscribe_slot *, struct upsgi_subscribe_node *, struct upsgi_subscription_client *)) usl->custom_ptr;
		}
	}
	return NULL;
}

void upsgi_register_subscription_algo(char *name, struct upsgi_subscribe_node *(*func)(struct upsgi_subscribe_slot *, struct upsgi_subscribe_node *, struct upsgi_subscription_client *)) {
	struct upsgi_string_list *usl = upsgi_string_new_list(&upsgi.subscription_algos, name);	
	usl->custom_ptr = func;
}
