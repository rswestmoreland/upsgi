#include "../upsgi.h"

extern struct upsgi_server upsgi;

/*

	upsgi Legions subsystem

	A Legion is a group of upsgi instances sharing a single object. This single
	object can be owned only by the instance with the higher valor. Such an instance is the
	Lord of the Legion. There can only be one (and only one) Lord for each Legion.
	If a member of a Legion spawns with an higher valor than the current Lord, it became the new Lord.


*/

struct upsgi_legion *upsgi_legion_get_by_socket(int fd) {
	struct upsgi_legion *ul = upsgi.legions;
	while (ul) {
		if (ul->socket == fd) {
			return ul;
		}
		ul = ul->next;
	}

	return NULL;
}

struct upsgi_legion *upsgi_legion_get_by_name(char *name) {
	struct upsgi_legion *ul = upsgi.legions;
	while (ul) {
		if (!strcmp(name, ul->legion)) {
			return ul;
		}
		ul = ul->next;
	}

	return NULL;
}


void upsgi_parse_legion(char *key, uint16_t keylen, char *value, uint16_t vallen, void *data) {
	struct upsgi_legion *ul = (struct upsgi_legion *) data;

	if (!upsgi_strncmp(key, keylen, "legion", 6)) {
		ul->legion = value;
		ul->legion_len = vallen;
	}
	else if (!upsgi_strncmp(key, keylen, "valor", 5)) {
		ul->valor = upsgi_str_num(value, vallen);
	}
	else if (!upsgi_strncmp(key, keylen, "name", 4)) {
		ul->name = value;
		ul->name_len = vallen;
	}
	else if (!upsgi_strncmp(key, keylen, "pid", 3)) {
		ul->pid = upsgi_str_num(value, vallen);
	}
	else if (!upsgi_strncmp(key, keylen, "unix", 4)) {
		ul->unix_check = upsgi_str_num(value, vallen);
	}
	else if (!upsgi_strncmp(key, keylen, "checksum", 8)) {
		ul->checksum = upsgi_str_num(value, vallen);
	}
	else if (!upsgi_strncmp(key, keylen, "uuid", 4)) {
		if (vallen == 36) {
			memcpy(ul->uuid, value, 36);
		}
	}
	else if (!upsgi_strncmp(key, keylen, "lord_valor", 10)) {
		ul->lord_valor = upsgi_str_num(value, vallen);
	}
	else if (!upsgi_strncmp(key, keylen, "lord_uuid", 9)) {
		if (vallen == 36) {
			memcpy(ul->lord_uuid, value, 36);
		}
	}
	else if (!upsgi_strncmp(key, keylen, "scroll", 6)) {
		ul->scroll = value;
		ul->scroll_len = vallen;
	}
	else if (!upsgi_strncmp(key, keylen, "dead", 4)) {
		ul->dead = 1;
	}
}

// this function is called when a node is added or removed (heavy locking is needed)
static void legion_rebuild_scrolls(struct upsgi_legion *ul) {
	uint64_t max_size = ul->scrolls_max_size;

	// first, try to add myself
	if (ul->scroll_len + (uint64_t) 2 > max_size) {
		upsgi_log("[DANGER] you have configured a too much tiny buffer for the scrolls list !!! tune it with --legion-scroll-list-max-size\n");
		ul->scroll_len = 0;
		return;
	}

	char *ptr = ul->scrolls;
	*ptr ++= (uint8_t) (ul->scroll_len & 0xff);
	*ptr ++= (uint8_t) ((ul->scroll_len >> 8) &0xff);
	memcpy(ptr, ul->scroll, ul->scroll_len); ptr += ul->scroll_len;
	ul->scrolls_len = 2 + ul->scroll_len;
	// ok start adding nodes;
	struct upsgi_legion_node *uln = ul->nodes_head;
	while(uln) {
		if (ul->scrolls_len + 2 + uln->scroll_len > max_size) {
			upsgi_log("[DANGER] you have configured a too much tiny buffer for the scrolls list !!! tune it with --legion-scroll-list-max-size\n");
			return;
		}
		*ptr ++= (uint8_t) (uln->scroll_len & 0xff);
        	*ptr ++= (uint8_t) ((uln->scroll_len >> 8) &0xff);
        	memcpy(ptr, uln->scroll, uln->scroll_len); ptr += uln->scroll_len;
        	ul->scrolls_len += 2 + uln->scroll_len;
		uln = uln->next;
	}
}

// critical section (remember to lock when you use it)
struct upsgi_legion_node *upsgi_legion_add_node(struct upsgi_legion *ul, uint16_t valor, char *name, uint16_t name_len, char *uuid) {

	struct upsgi_legion_node *node = upsgi_calloc(sizeof(struct upsgi_legion_node));
	if (!name_len)
		goto error;
	node->name = upsgi_calloc(name_len);
	node->name_len = name_len;
	memcpy(node->name, name, name_len);
	node->valor = valor;
	memcpy(node->uuid, uuid, 36);

	if (ul->nodes_tail) {
		node->prev = ul->nodes_tail;
		ul->nodes_tail->next = node;
	}

	ul->nodes_tail = node;

	if (!ul->nodes_head) {
		ul->nodes_head = node;
	}


	return node;


error:
	free(node);
	return NULL;
}

// critical section (remember to lock when you use it)
void upsgi_legion_remove_node(struct upsgi_legion *ul, struct upsgi_legion_node *node) {
	// check if the node is the first one
	if (node == ul->nodes_head) {
		ul->nodes_head = node->next;
	}

	// check if the node is the last one
	if (node == ul->nodes_tail) {
		ul->nodes_tail = node->prev;
	}

	if (node->prev) {
		node->prev->next = node->next;
	}

	if (node->next) {
		node->next->prev = node->prev;
	}

	if (node->name_len) {
		free(node->name);
	}

	if (node->scroll_len) {
		free(node->scroll);
	}

	free(node);

	legion_rebuild_scrolls(ul);
}

struct upsgi_legion_node *upsgi_legion_get_node(struct upsgi_legion *ul, uint64_t valor, char *name, uint16_t name_len, char *uuid) {
	struct upsgi_legion_node *nodes = ul->nodes_head;
	while (nodes) {
		if (valor != nodes->valor)
			goto next;
		if (name_len != nodes->name_len)
			goto next;
		if (memcmp(nodes->name, name, name_len))
			goto next;
		if (memcmp(nodes->uuid, uuid, 36))
			goto next;
		return nodes;
next:
		nodes = nodes->next;
	}
	return NULL;
}

static void legions_check_nodes() {

	struct upsgi_legion *legion = upsgi.legions;
	while (legion) {
		time_t now = upsgi_now();

		struct upsgi_legion_node *node = legion->nodes_head;
		while (node) {
			if (now - node->last_seen > upsgi.legion_tolerance) {
				struct upsgi_legion_node *tmp_node = node;
				node = node->next;
				upsgi_log("[upsgi-legion] %s: %.*s valor: %llu uuid: %.*s left Legion %s\n", tmp_node->valor > 0 ? "node" : "arbiter", tmp_node->name_len, tmp_node->name, tmp_node->valor, 36, tmp_node->uuid, legion->legion);
				upsgi_wlock(legion->lock);
				upsgi_legion_remove_node(legion, tmp_node);
				upsgi_rwunlock(legion->lock);
				// trigger node_left hooks
				struct upsgi_string_list *usl = legion->node_left_hooks;
				while (usl) {
					int ret = upsgi_legion_action_call("node_left", legion, usl);
					if (ret) {
						upsgi_log("[upsgi-legion] ERROR, node_left hook returned: %d\n", ret);
					}
					usl = usl->next;
				}
				continue;
			}
			node = node->next;
		}

		legion = legion->next;
	}
}

struct upsgi_legion_node *upsgi_legion_get_lord(struct upsgi_legion *);

static void legions_report_quorum(struct upsgi_legion *ul, uint64_t best_valor, char *best_uuid, int votes) {
	struct upsgi_legion_node *nodes = ul->nodes_head;
	upsgi_log("[upsgi-legion] --- WE HAVE QUORUM FOR LEGION %s !!! (valor: %llu uuid: %.*s checksum: %llu votes: %d) ---\n", ul->legion, best_valor, 36, best_uuid, ul->checksum, votes);
	while (nodes) {
		upsgi_log("[upsgi-legion-node] %s: %.*s valor: %llu uuid: %.*s last_seen: %d vote_valor: %llu vote_uuid: %.*s\n", nodes->valor > 0 ? "node" : "arbiter", nodes->name_len, nodes->name, nodes->valor, 36, nodes->uuid, nodes->last_seen, nodes->lord_valor, 36, nodes->lord_uuid);
		nodes = nodes->next;
	}
	upsgi_log("[upsgi-legion] --- END OF QUORUM REPORT ---\n");
}

uint64_t upsgi_legion_checksum(struct upsgi_legion *ul) {
	uint16_t i;
	uint64_t checksum = ul->valor;
	for(i=0;i<36;i++) {
		checksum += ul->uuid[i];
	}

	struct upsgi_legion_node *nodes = ul->nodes_head;
	while (nodes) {
		checksum += nodes->valor;
		for(i=0;i<36;i++) {
			checksum += nodes->uuid[i];
		}
		nodes = nodes->next;
	}

	return checksum;	
	
}

static void legions_check_nodes_step2() {
	struct upsgi_legion *ul = upsgi.legions;
	while (ul) {
		// ok now we can check the status of the lord
		int i_am_the_best = 0;
		uint64_t best_valor = 0;
		char best_uuid[36];
		struct upsgi_legion_node *node = upsgi_legion_get_lord(ul);
		if (node) {
			// a node is the best candidate
			best_valor = node->valor;
			memcpy(best_uuid, node->uuid, 36);
		}
		// go on if i am not an arbiter
		// no potential Lord is available, i will propose myself
		// but only if i am not suspended...
		else if (ul->valor > 0 && upsgi_now() > ul->suspended_til) {
			best_valor = ul->valor;
			memcpy(best_uuid, ul->uuid, 36);
			i_am_the_best = 1;
		}
		else {
			// empty lord
			memset(best_uuid, 0, 36);
		}

		// calculate the checksum
		uint64_t new_checksum = upsgi_legion_checksum(ul);
		if (new_checksum != ul->checksum) {
			ul->changed = 1;
		}
		ul->checksum = new_checksum;

		// ... ok let's see if all of the nodes agree on the lord
		// ... but first check if i am not alone...
		int votes = 1;
		struct upsgi_legion_node *nodes = ul->nodes_head;
		while (nodes) {
			if (nodes->checksum != ul->checksum) {
				votes = 0;
				break;
			}
			if (nodes->lord_valor != best_valor) {
				votes = 0;
				break;
			}
			if (memcmp(nodes->lord_uuid, best_uuid, 36)) {
				votes = 0;
				break;
			}
			votes++;
			nodes = nodes->next;
		}

		// we have quorum !!!
		if (votes > 0 && votes >= ul->quorum) {
			if (!ul->joined) {
				// triggering join hooks
				struct upsgi_string_list *usl = ul->join_hooks;
				while (usl) {
					int ret = upsgi_legion_action_call("join", ul, usl);
					if (ret) {
						upsgi_log("[upsgi-legion] ERROR, join hook returned: %d\n", ret);
					}
					usl = usl->next;
				}
				ul->joined = 1;
			}
			// something changed ???
			if (ul->changed) {
				legions_report_quorum(ul, best_valor, best_uuid, votes);
				ul->changed = 0;
			}
			if (i_am_the_best) {
				if (!ul->i_am_the_lord) {
					// triggering lord hooks
					upsgi_log("[upsgi-legion] attempting to become the Lord of the Legion %s\n", ul->legion);
					struct upsgi_string_list *usl = ul->lord_hooks;
					while (usl) {
						int ret = upsgi_legion_action_call("lord", ul, usl);
						if (ret) {
							upsgi_log("[upsgi-legion] ERROR, lord hook returned: %d\n", ret);
							if (upsgi.legion_death_on_lord_error) {
								ul->dead = 1;
                						upsgi_legion_announce(ul);
								ul->suspended_til = upsgi_now() + upsgi.legion_death_on_lord_error;
								upsgi_log("[upsgi-legion] suspending myself from Legion \"%s\" for %d seconds\n", ul->legion, upsgi.legion_death_on_lord_error);
								goto next;
							}
						}
						usl = usl->next;
					}
					if (ul->scroll_len > 0 && ul->scroll_len <= ul->lord_scroll_size) {
                				upsgi_wlock(ul->lock);
                				ul->lord_scroll_len = ul->scroll_len;
                				memcpy(ul->lord_scroll, ul->scroll, ul->lord_scroll_len);
                				upsgi_rwunlock(ul->lock);
        				}
        				else {
                				ul->lord_scroll_len = 0;
        				}
					upsgi_log("[upsgi-legion] i am now the Lord of the Legion %s\n", ul->legion);
					ul->i_am_the_lord = upsgi_now();
					// trick: reduce the time needed by the old lord to unlord itself
					upsgi_legion_announce(ul);
				}
			}
			else {
				if (ul->i_am_the_lord) {
					upsgi_log("[upsgi-legion] a new Lord (valor: %llu uuid: %.*s) raised for Legion %s...\n", ul->lord_valor, 36, ul->lord_uuid, ul->legion);
					if (ul->lord_scroll_len > 0) {
						upsgi_log("*********** The New Lord Scroll ***********\n");
						upsgi_log("%.*s\n", ul->lord_scroll_len, ul->lord_scroll);
						upsgi_log("*********** End of the New Lord Scroll ***********\n");
					}
					// no more lord, trigger unlord hooks
					struct upsgi_string_list *usl = ul->unlord_hooks;
					while (usl) {
						int ret = upsgi_legion_action_call("unlord", ul, usl);
						if (ret) {
							upsgi_log("[upsgi-legion] ERROR, unlord hook returned: %d\n", ret);
						}
						usl = usl->next;
					}
					ul->i_am_the_lord = 0;
				}
			}
		}
		else if (votes > 0 && votes < ul->quorum && (upsgi_now() - ul->last_warning >= 60)) {
			upsgi_log("[upsgi-legion] no quorum: only %d vote(s) for Legion %s, %d needed to elect a Lord\n", votes, ul->legion, ul->quorum);
			// no more quorum, leave the Lord state
			if (ul->i_am_the_lord) {
				upsgi_log("[upsgi-legion] i cannot be The Lord of The Legion %s without a quorum ...\n", ul->legion);
				// no more lord, trigger unlord hooks
                                struct upsgi_string_list *usl = ul->unlord_hooks;
                                while (usl) {
                                	int ret = upsgi_legion_action_call("unlord", ul, usl);
                                        if (ret) {
                                        	upsgi_log("[upsgi-legion] ERROR, unlord hook returned: %d\n", ret);
                                        }
                                        usl = usl->next;
                                }
                                ul->i_am_the_lord = 0;
			}
			ul->last_warning = upsgi_now();
		}
next:
		ul = ul->next;
	}
}

// check who should be the lord of the legion
struct upsgi_legion_node *upsgi_legion_get_lord(struct upsgi_legion *ul) {

	char best_uuid[36];

	memcpy(best_uuid, ul->uuid, 36);
	uint64_t best_valor = ul->valor;
	
	struct upsgi_legion_node *best_node = NULL;

	struct upsgi_legion_node *nodes = ul->nodes_head;
	while (nodes) {
		// skip arbiters
		if (nodes->valor == 0) goto next;
		if (nodes->valor > best_valor) {
			best_node = nodes;
			best_valor = nodes->valor;
			memcpy(best_uuid, nodes->uuid, 36);
		}
		else if (nodes->valor == best_valor) {
			if (upsgi_uuid_cmp(nodes->uuid, best_uuid) > 0) {
				best_node = nodes;
				best_valor = nodes->valor;
				memcpy(best_uuid, nodes->uuid, 36);
			}
		}
next:
		nodes = nodes->next;
	}

	// first round ? (skip first round if arbiter)
	if (ul->valor > 0 && ul->lord_valor == 0) {
		ul->changed = 1;
	}
	else if (best_valor != ul->lord_valor) {
		ul->changed = 1;
	}
	else {
		if (memcmp(best_uuid, ul->lord_uuid, 36)) {
			ul->changed = 1;
		}
	}

	ul->lord_valor = best_valor;
	memcpy(ul->lord_uuid, best_uuid, 36);

	if (!best_node) return NULL;

	if (best_node->scroll_len > 0 && best_node->scroll_len <= ul->lord_scroll_size) {
		upsgi_wlock(ul->lock);
		ul->lord_scroll_len = best_node->scroll_len;
		memcpy(ul->lord_scroll, best_node->scroll, ul->lord_scroll_len);
		upsgi_rwunlock(ul->lock);
	}
	else {
		ul->lord_scroll_len = 0;
	}

	return best_node;
}


static void *legion_loop(void *foobar) {

	time_t last_round = upsgi_now();

	unsigned char *crypted_buf = upsgi_malloc(UMAX16 - EVP_MAX_BLOCK_LENGTH - 4);
	unsigned char *clear_buf = upsgi_malloc(UMAX16);

	struct upsgi_legion legion_msg;

	if (!upsgi.legion_freq)
		upsgi.legion_freq = 3;
	if (!upsgi.legion_tolerance)
		upsgi.legion_tolerance = 15;
	if (!upsgi.legion_skew_tolerance)
		upsgi.legion_skew_tolerance = 60;

	int first_round = 1;
	for (;;) {
		int timeout = upsgi.legion_freq;
		time_t now = upsgi_now();
		if (now > last_round) {
			timeout -= (now - last_round);
			if (timeout < 0) {
				timeout = 0;
			}
		}
		last_round = now;
		// wait for event
		int interesting_fd = -1;
		if (upsgi_instance_is_reloading || upsgi_instance_is_dying) return NULL;
		int rlen = event_queue_wait(upsgi.legion_queue, timeout, &interesting_fd);

		if (rlen < 0 && errno != EINTR) {
			if (upsgi_instance_is_reloading || upsgi_instance_is_dying) return NULL;
			upsgi_nuclear_blast();
			return NULL;	
		}

		now = upsgi_now();
		if (timeout == 0 || rlen == 0 || (now - last_round) >= timeout) {
			struct upsgi_legion *legions = upsgi.legions;
			while (legions) {
				upsgi_legion_announce(legions);
				legions = legions->next;
			}
			last_round = now;
		}

		// check the nodes
		legions_check_nodes();

		if (rlen > 0) {
			struct upsgi_legion *ul = upsgi_legion_get_by_socket(interesting_fd);
			if (!ul)
				continue;
			// ensure the first 4 bytes are valid
			ssize_t len = read(ul->socket, crypted_buf, (UMAX16 - EVP_MAX_BLOCK_LENGTH - 4));
			if (len < 0) {
				upsgi_error("[upsgi-legion] read()");
				continue;
			}
			else if (len < 4) {
				upsgi_log("[upsgi-legion] invalid packet size: %d\n", (int) len);
				continue;
			}

			struct upsgi_header *uh = (struct upsgi_header *) crypted_buf;

			if (uh->modifier1 != 109) {
				upsgi_log("[upsgi-legion] invalid modifier1");
				continue;
			}

			int d_len = 0;
			int d2_len = 0;
			// decrypt packet using the secret
			if (EVP_DecryptInit_ex(ul->decrypt_ctx, NULL, NULL, NULL, NULL) <= 0) {
				upsgi_error("[upsgi-legion] EVP_DecryptInit_ex()");
				continue;
			}

			if (EVP_DecryptUpdate(ul->decrypt_ctx, clear_buf, &d_len, crypted_buf + 4, len - 4) <= 0) {
				upsgi_error("[upsgi-legion] EVP_DecryptUpdate()");
				continue;
			}

			if (EVP_DecryptFinal_ex(ul->decrypt_ctx, clear_buf + d_len, &d2_len) <= 0) {
				ERR_print_errors_fp(stderr);
				upsgi_log("[upsgi-legion] EVP_DecryptFinal_ex()\n");
				continue;
			}

			d_len += d2_len;

			if (d_len != uh->_pktsize) {
				upsgi_log("[upsgi-legion] invalid packet size\n");
				continue;
			}

			// parse packet
			memset(&legion_msg, 0, sizeof(struct upsgi_legion));
			if (upsgi_hooked_parse((char *) clear_buf, d_len, upsgi_parse_legion, &legion_msg)) {
				upsgi_log("[upsgi-legion] invalid packet\n");
				continue;
			}

			if (upsgi_strncmp(ul->legion, ul->legion_len, legion_msg.legion, legion_msg.legion_len)) {
				upsgi_log("[upsgi-legion] invalid legion name\n");
				continue;
			}

			// check for loop packets... (especially when in multicast mode)
			if (!upsgi_strncmp(upsgi.hostname, upsgi.hostname_len, legion_msg.name, legion_msg.name_len)) {
				if (legion_msg.pid == ul->pid) {
					if (legion_msg.valor == ul->valor) {
						if (!memcmp(legion_msg.uuid, ul->uuid, 36)) {
							continue;
						}
					}
				}
			}

			// check for "tolerable" unix time
			if (legion_msg.unix_check < (upsgi_now() - upsgi.legion_skew_tolerance)) {
				upsgi_log("[upsgi-legion] untolerable packet received for Legion %s , check your clock !!!\n", ul->legion);
				continue;
			}

			// check if the node is already accounted
			struct upsgi_legion_node *node = upsgi_legion_get_node(ul, legion_msg.valor, legion_msg.name, legion_msg.name_len, legion_msg.uuid);
			if (!node) {
				// if a lord hook election fails, a node can announce itself as dead for long time...
				if (legion_msg.dead) continue;
				// add the new node
				upsgi_wlock(ul->lock);
				node = upsgi_legion_add_node(ul, legion_msg.valor, legion_msg.name, legion_msg.name_len, legion_msg.uuid);
				if (!node) continue;
				if (legion_msg.scroll_len > 0) {
					node->scroll = upsgi_malloc(legion_msg.scroll_len);
					node->scroll_len = legion_msg.scroll_len;
					memcpy(node->scroll, legion_msg.scroll, node->scroll_len);
				}
				// we are still locked (and safe), let's rebuild the scrolls list
				legion_rebuild_scrolls(ul);
				upsgi_rwunlock(ul->lock);
				upsgi_log("[upsgi-legion] %s: %.*s valor: %llu uuid: %.*s joined Legion %s\n", node->valor > 0 ? "node" : "arbiter", node->name_len, node->name, node->valor, 36, node->uuid, ul->legion);
				// trigger node_joined hooks
				struct upsgi_string_list *usl = ul->node_joined_hooks;
				while (usl) {
					int ret = upsgi_legion_action_call("node_joined", ul, usl);
					if (ret) {
						upsgi_log("[upsgi-legion] ERROR, node_joined hook returned: %d\n", ret);
					}
					usl = usl->next;
				}
			}
			// remove node announcing death
			else if (legion_msg.dead) {
				upsgi_log("[upsgi-legion] %s: %.*s valor: %llu uuid: %.*s announced its death to Legion %s\n", node->valor > 0 ? "node" : "arbiter", node->name_len, node->name, node->valor, 36, node->uuid, ul->legion);
                                upsgi_wlock(ul->lock);
                                upsgi_legion_remove_node(ul, node);
                                upsgi_rwunlock(ul->lock);
				continue;
			}

			node->last_seen = upsgi_now();
			node->lord_valor = legion_msg.lord_valor;
			node->checksum = legion_msg.checksum;
			memcpy(node->lord_uuid, legion_msg.lord_uuid, 36);

		}

		// skip the first round if i no packet is received
		if (first_round) {
			first_round = 0;
			continue;
		}
		legions_check_nodes_step2();
	}

	return NULL;
}

int upsgi_legion_action_call(char *phase, struct upsgi_legion *ul, struct upsgi_string_list *usl) {
	struct upsgi_legion_action *ula = upsgi_legion_action_get(usl->custom_ptr);
	if (!ula) {
		upsgi_log("[upsgi-legion] ERROR unable to find legion_action \"%s\"\n", (char *) usl->custom_ptr);
		return -1;
	}

	if (ula->log_msg) {
		upsgi_log("[upsgi-legion] (phase: %s legion: %s) %s\n", phase, ul->legion, ula->log_msg);
	}
	else {
		upsgi_log("[upsgi-legion] (phase: %s legion: %s) calling %s\n", phase, ul->legion, usl->value);
	}
	return ula->func(ul, usl->value + usl->custom);
}

static int legion_action_cmd(struct upsgi_legion *ul, char *arg) {
	return upsgi_run_command_and_wait(NULL, arg);
}

static int legion_action_signal(struct upsgi_legion *ul, char *arg) {
	return upsgi_signal_send(upsgi.signal_socket, atoi(arg));
}

static int legion_action_log(struct upsgi_legion *ul, char *arg) {
	char *logline = upsgi_concat2(arg, "\n");
	upsgi_log(logline);
	free(logline);
	return 0;
}

static int legion_action_alarm(struct upsgi_legion *ul, char *arg) {
	char *space = strchr(arg,' ');
        if (!space) {
                upsgi_log("invalid alarm action syntax, must be: <alarm> <msg>\n");
                return -1;
        }
        *space = 0;
        upsgi_alarm_trigger(arg, space+1,  strlen(space+1));
        *space = ' ';
        return 0;
}

void upsgi_start_legions() {
	pthread_t legion_loop_t;

	if (!upsgi.legions)
		return;

	// register embedded actions
	upsgi_legion_action_register("cmd", legion_action_cmd);
	upsgi_legion_action_register("exec", legion_action_cmd);
	upsgi_legion_action_register("signal", legion_action_signal);
	upsgi_legion_action_register("log", legion_action_log);
	upsgi_legion_action_register("alarm", legion_action_alarm);

	upsgi.legion_queue = event_queue_init();
	struct upsgi_legion *legion = upsgi.legions;
	while (legion) {
		char *colon = strchr(legion->addr, ':');
		if (colon) {
			legion->socket = bind_to_udp(legion->addr, 0, 0);
		}
		else {
			legion->socket = bind_to_unix_dgram(legion->addr);
		}
		if (legion->socket < 0 || event_queue_add_fd_read(upsgi.legion_queue, legion->socket)) {
			upsgi_log("[upsgi-legion] unable to activate legion %s\n", legion->legion);
			exit(1);
		}
		upsgi_socket_nb(legion->socket);
		legion->pid = upsgi.mypid;
		upsgi_uuid(legion->uuid);
		struct upsgi_string_list *usl = legion->setup_hooks;
		while (usl) {
			int ret = upsgi_legion_action_call("setup", legion, usl);
			if (ret) {
				upsgi_log("[upsgi-legion] ERROR, setup hook returned: %d\n", ret);
			}
			usl = usl->next;
		}
		legion = legion->next;
	}

#ifndef UPSGI_UUID
	upsgi_log("WARNING: you are not using libuuid to generate Legions UUID\n");
#endif

	if (pthread_create(&legion_loop_t, NULL, legion_loop, NULL)) {
		upsgi_error("pthread_create()");
		upsgi_log("unable to run the legion server !!!\n");
	}
	else {
		upsgi_log("legion manager thread enabled\n");
	}

}

void upsgi_legion_add(struct upsgi_legion *ul) {
	struct upsgi_legion *old_legion = NULL, *legion = upsgi.legions;
	while (legion) {
		old_legion = legion;
		legion = legion->next;
	}

	if (old_legion) {
		old_legion->next = ul;
	}
	else {
		upsgi.legions = ul;
	}
}

int upsgi_legion_announce(struct upsgi_legion *ul) {
	time_t now = upsgi_now();

	if (now <= ul->suspended_til) return 0;
	ul->suspended_til = 0;

	struct upsgi_buffer *ub = upsgi_buffer_new(4096);
	unsigned char *encrypted = NULL;

	if (upsgi_buffer_append_keyval(ub, "legion", 6, ul->legion, ul->legion_len))
		goto err;
	if (upsgi_buffer_append_keynum(ub, "valor", 5, ul->valor))
		goto err;
	if (upsgi_buffer_append_keynum(ub, "unix", 4, now))
		goto err;
	if (upsgi_buffer_append_keynum(ub, "lord", 4, ul->i_am_the_lord ? ul->i_am_the_lord : 0))
		goto err;
	if (upsgi_buffer_append_keyval(ub, "name", 4, upsgi.hostname, upsgi.hostname_len))
		goto err;
	if (upsgi_buffer_append_keynum(ub, "pid", 3, ul->pid))
		goto err;
	if (upsgi_buffer_append_keyval(ub, "uuid", 4, ul->uuid, 36))
		goto err;
	if (upsgi_buffer_append_keynum(ub, "checksum", 8, ul->checksum))
		goto err;
	if (upsgi_buffer_append_keynum(ub, "lord_valor", 10, ul->lord_valor))
		goto err;
	if (upsgi_buffer_append_keyval(ub, "lord_uuid", 9, ul->lord_uuid, 36))
		goto err;

	if (ul->scroll_len > 0) {
		if (upsgi_buffer_append_keyval(ub, "scroll", 6, ul->scroll, ul->scroll_len))
                	goto err;
	}

	if (ul->dead) {
		if (upsgi_buffer_append_keyval(ub, "dead", 4, "1", 1))
                	goto err;
	}

	encrypted = upsgi_malloc(ub->pos + 4 + EVP_MAX_BLOCK_LENGTH);
	if (EVP_EncryptInit_ex(ul->encrypt_ctx, NULL, NULL, NULL, NULL) <= 0) {
		upsgi_error("[upsgi-legion] EVP_EncryptInit_ex()");
		goto err;
	}

	int e_len = 0;

	if (EVP_EncryptUpdate(ul->encrypt_ctx, encrypted + 4, &e_len, (unsigned char *) ub->buf, ub->pos) <= 0) {
		upsgi_error("[upsgi-legion] EVP_EncryptUpdate()");
		goto err;
	}

	int tmplen = 0;
	if (EVP_EncryptFinal_ex(ul->encrypt_ctx, encrypted + 4 + e_len, &tmplen) <= 0) {
		upsgi_error("[upsgi-legion] EVP_EncryptFinal_ex()");
		goto err;
	}

	e_len += tmplen;
	uint16_t pktsize = ub->pos;
	encrypted[0] = 109;
	encrypted[1] = (unsigned char) (pktsize & 0xff);
	encrypted[2] = (unsigned char) ((pktsize >> 8) & 0xff);
	encrypted[3] = 0;

	struct upsgi_string_list *usl = ul->nodes;
	while (usl) {
		if (sendto(ul->socket, encrypted, e_len + 4, 0, usl->custom_ptr, usl->custom) != e_len + 4) {
			upsgi_error("[upsgi-legion] sendto()");
		}
		usl = usl->next;
	}

	upsgi_buffer_destroy(ub);
	free(encrypted);
	return 0;
err:
	upsgi_buffer_destroy(ub);
	free(encrypted);
	return -1;
}

void upsgi_opt_legion_mcast(char *opt, char *value, void *foobar) {
	upsgi_opt_legion(opt, value, foobar);
	char *legion = upsgi_str(value);
	char *space = strchr(legion, ' ');	
	// over engineering
	if (!space) exit(1);
	*space = 0;
	struct upsgi_legion *ul = upsgi_legion_get_by_name(legion);
        if (!ul) {
                upsgi_log("unknown legion: %s\n", legion);
                exit(1);
        }
	upsgi_legion_register_node(ul, upsgi_str(ul->addr));
	free(legion);
}

void upsgi_opt_legion_node(char *opt, char *value, void *foobar) {

	char *legion = upsgi_str(value);

	char *space = strchr(legion, ' ');
	if (!space) {
		upsgi_log("invalid legion-node syntax, must be <legion> <addr>\n");
		exit(1);
	}
	*space = 0;

	struct upsgi_legion *ul = upsgi_legion_get_by_name(legion);
	if (!ul) {
		upsgi_log("unknown legion: %s\n", legion);
		exit(1);
	}

	upsgi_legion_register_node(ul, space + 1);
	
}

void upsgi_legion_register_node(struct upsgi_legion *ul, char *addr) {
	struct upsgi_string_list *usl = upsgi_string_new_list(&ul->nodes, addr);
	char *port = strchr(addr, ':');
	if (!port) {
		upsgi_log("[upsgi-legion] invalid udp address: %s\n", addr);
		exit(1);
	}
	// no need to zero the memory, socket_to_in_addr will do that
	struct sockaddr_in *sin = upsgi_malloc(sizeof(struct sockaddr_in));
	usl->custom = socket_to_in_addr(addr, port, 0, sin);
	usl->custom_ptr = sin;
}

void upsgi_opt_legion_quorum(char *opt, char *value, void *foobar) {

        char *legion = upsgi_str(value);

        char *space = strchr(legion, ' ');
        if (!space) {
                upsgi_log("invalid legion-quorum syntax, must be <legion> <quorum>\n");
                exit(1);
        }
        *space = 0;

        struct upsgi_legion *ul = upsgi_legion_get_by_name(legion);
        if (!ul) {
                upsgi_log("unknown legion: %s\n", legion);
                exit(1);
        }

	ul->quorum = atoi(space+1);
	free(legion);
}

void upsgi_opt_legion_scroll(char *opt, char *value, void *foobar) {

        char *legion = upsgi_str(value);

        char *space = strchr(legion, ' ');
        if (!space) {
                upsgi_log("invalid legion-scroll syntax, must be <legion> <scroll>\n");
                exit(1);
        }
        *space = 0;

        struct upsgi_legion *ul = upsgi_legion_get_by_name(legion);
        if (!ul) {
                upsgi_log("unknown legion: %s\n", legion);
                exit(1);
        }

        ul->scroll = space+1;
	ul->scroll_len = strlen(ul->scroll);
	// DO NOT FREE IT !!!
        //free(legion);
}



void upsgi_opt_legion_hook(char *opt, char *value, void *foobar) {

	char *event = strchr(opt, '-');
	if (!event) {
		upsgi_log("[upsgi-legion] invalid option name (%s), this should not happen (possible bug)\n", opt);
		exit(1);
	}
  
	char *legion = upsgi_str(value);
	
	char *space = strchr(legion, ' ');
	if (!space) {
		upsgi_log("[upsgi-legion] invalid %s syntax, must be <legion> <action>\n", opt);
		exit(1);
	}
	*space = 0;

	struct upsgi_legion *ul = upsgi_legion_get_by_name(legion);
	if (!ul) {
		upsgi_log("[upsgi-legion] unknown legion: %s\n", legion);
		exit(1);
	}

	upsgi_legion_register_hook(ul, event + 1, space + 1);
}

void upsgi_legion_register_hook(struct upsgi_legion *ul, char *event, char *action) {

	struct upsgi_string_list *usl = NULL;

	if (!strcmp(event, "lord")) {
		usl = upsgi_string_new_list(&ul->lord_hooks, action);
	}
	else if (!strcmp(event, "unlord")) {
		usl = upsgi_string_new_list(&ul->unlord_hooks, action);
	}
	else if (!strcmp(event, "setup")) {
		usl = upsgi_string_new_list(&ul->setup_hooks, action);
	}
	else if (!strcmp(event, "death")) {
		usl = upsgi_string_new_list(&ul->death_hooks, action);
	}
	else if (!strcmp(event, "join")) {
		usl = upsgi_string_new_list(&ul->join_hooks, action);
	}
	else if (!strcmp(event, "node-joined")) {
		usl = upsgi_string_new_list(&ul->node_joined_hooks, action);
	}
	else if (!strcmp(event, "node-left")) {
		usl = upsgi_string_new_list(&ul->node_left_hooks, action);
	}

	else {
		upsgi_log("[upsgi-legion] invalid event: %s\n", event);
		exit(1);
	}

	if (!usl)
		return;

	char *hook = strchr(action, ':');
	if (!hook) {
		upsgi_log("[upsgi-legion] invalid %s action: %s\n", event, action);
		exit(1);
	}

	// pointer to action plugin
	usl->custom_ptr = upsgi_concat2n(action, hook - action, "", 0);;
	// add that to check the plugin value
	usl->custom = hook - action + 1;

}

void upsgi_opt_legion(char *opt, char *value, void *foobar) {

	// legion addr valor algo:secret
	char *legion = upsgi_str(value);
	char *space = strchr(legion, ' ');
	if (!space) {
		upsgi_log("invalid legion syntax, must be <legion> <addr> <valor> <algo:secret>\n");
		exit(1);
	}
	*space = 0;
	char *addr = space + 1;

	space = strchr(addr, ' ');
	if (!space) {
		upsgi_log("invalid legion syntax, must be <legion> <addr> <valor> <algo:secret>\n");
		exit(1);
	}
	*space = 0;
	char *valor = space + 1;

	space = strchr(valor, ' ');
	if (!space) {
		upsgi_log("invalid legion syntax, must be <legion> <addr> <valor> <algo:secret>\n");
		exit(1);
	}
	*space = 0;
	char *algo_secret = space + 1;

	char *colon = strchr(algo_secret, ':');
	if (!colon) {
		upsgi_log("invalid legion syntax, must be <legion> <addr> <valor> <algo:secret>\n");
		exit(1);
	}
	*colon = 0;
	char *secret = colon + 1;

	upsgi_legion_register(legion, addr, valor, algo_secret, secret);
}

struct upsgi_legion *upsgi_legion_register(char *legion, char *addr, char *valor, char *algo, char *secret) {
	char *iv = strchr(secret, ' ');
	if (iv) {
		*iv = 0;
		iv++;
	}

	if (!upsgi.ssl_initialized) {
		upsgi_ssl_init();
	}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	EVP_CIPHER_CTX *ctx = upsgi_malloc(sizeof(EVP_CIPHER_CTX));
	EVP_CIPHER_CTX_init(ctx);
#else
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
#endif

	const EVP_CIPHER *cipher = EVP_get_cipherbyname(algo);
	if (!cipher) {
		upsgi_log("[upsgi-legion] unable to find algorithm/cipher %s\n", algo);
		exit(1);
	}

	int cipher_len = EVP_CIPHER_key_length(cipher);
	size_t s_len = strlen(secret);
	if ((unsigned int) cipher_len > s_len) {
		char *secret_tmp = upsgi_malloc(cipher_len);
		memcpy(secret_tmp, secret, s_len);
		memset(secret_tmp + s_len, 0, cipher_len - s_len);
		secret = secret_tmp;
	}

	int iv_len = EVP_CIPHER_iv_length(cipher);
	size_t s_iv_len = 0;
	if (iv) {
		s_iv_len = strlen(iv);
	}
	if ((unsigned int) iv_len > s_iv_len) {
                char *secret_tmp = upsgi_malloc(iv_len);
                memcpy(secret_tmp, iv, s_iv_len);
                memset(secret_tmp + s_iv_len, '0', iv_len - s_iv_len);
                iv = secret_tmp;
        }

	if (EVP_EncryptInit_ex(ctx, cipher, NULL, (const unsigned char *) secret, (const unsigned char *) iv) <= 0) {
		upsgi_error("EVP_EncryptInit_ex()");
		exit(1);
	}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	EVP_CIPHER_CTX *ctx2 = upsgi_malloc(sizeof(EVP_CIPHER_CTX));
	EVP_CIPHER_CTX_init(ctx2);
#else
	EVP_CIPHER_CTX *ctx2 = EVP_CIPHER_CTX_new();
#endif

	if (EVP_DecryptInit_ex(ctx2, cipher, NULL, (const unsigned char *) secret, (const unsigned char *) iv) <= 0) {
		upsgi_error("EVP_DecryptInit_ex()");
		exit(1);
	}

	// we use shared memory, as we want to export legion status to the api
	struct upsgi_legion *ul = upsgi_calloc_shared(sizeof(struct upsgi_legion));
	ul->legion = legion;
	ul->legion_len = strlen(ul->legion);

	ul->valor = strtol(valor, (char **) NULL, 10);
	ul->addr = addr;

	ul->encrypt_ctx = ctx;
	ul->decrypt_ctx = ctx2;

	if (!upsgi.legion_scroll_max_size) {
		upsgi.legion_scroll_max_size = 4096;
	}

	if (!upsgi.legion_scroll_list_max_size) {
		upsgi.legion_scroll_list_max_size = 32768;
	}

	ul->lord_scroll_size = upsgi.legion_scroll_max_size;
	ul->lord_scroll = upsgi_calloc_shared(ul->lord_scroll_size);
	ul->scrolls_max_size = upsgi.legion_scroll_list_max_size;
	ul->scrolls = upsgi_calloc_shared(ul->scrolls_max_size);

	upsgi_legion_add(ul);

	return ul;
}

struct upsgi_legion_action *upsgi_legion_action_get(char *name) {
	struct upsgi_legion_action *ula = upsgi.legion_actions;
	while (ula) {
		if (!strcmp(name, ula->name)) {
			return ula;
		}
		ula = ula->next;
	}
	return NULL;
}

struct upsgi_legion_action *upsgi_legion_action_register(char *name, int (*func) (struct upsgi_legion *, char *)) {
	struct upsgi_legion_action *found_ula = upsgi_legion_action_get(name);
	if (found_ula) {
		upsgi_log("[upsgi-legion] action \"%s\" is already registered !!!\n", name);
		return found_ula;
	}

	struct upsgi_legion_action *old_ula = NULL, *ula = upsgi.legion_actions;
	while (ula) {
		old_ula = ula;
		ula = ula->next;
	}

	ula = upsgi_calloc(sizeof(struct upsgi_legion_action));
	ula->name = name;
	ula->func = func;

	if (old_ula) {
		old_ula->next = ula;
	}
	else {
		upsgi.legion_actions = ula;
	}

	return ula;
}

void upsgi_legion_announce_death(void) {
	struct upsgi_legion *legion = upsgi.legions;
        while (legion) {
                legion->dead = 1;
                upsgi_legion_announce(legion);
                legion = legion->next;
        }
}

void upsgi_legion_atexit(void) {
	struct upsgi_legion *legion = upsgi.legions;
	while (legion) {
		if (getpid() != legion->pid)
			goto next;
		struct upsgi_string_list *usl = legion->death_hooks;
		while (usl) {
			int ret = upsgi_legion_action_call("death", legion, usl);
			if (ret) {
				upsgi_log("[upsgi-legion] ERROR, death hook returned: %d\n", ret);
			}
			usl = usl->next;
		}
next:
		legion = legion->next;
	}

	// this must be called only by the master !!!
	if (!upsgi.workers) return;
	if (upsgi.workers[0].pid != getpid()) return;
	upsgi_legion_announce_death();
}

int upsgi_legion_i_am_the_lord(char *name) {
	struct upsgi_legion *legion = upsgi_legion_get_by_name(name);
	if (!legion) return 0;
	if (legion->i_am_the_lord) {
		return 1;
	}
	return 0;
}

char *upsgi_legion_lord_scroll(char *name, uint16_t *rlen) {
	char *buf = NULL;
	struct upsgi_legion *legion = upsgi_legion_get_by_name(name);
        if (!legion) return 0;
	upsgi_rlock(legion->lock);
	if (legion->lord_scroll_len > 0) {
		buf = upsgi_malloc(legion->lord_scroll_len);
		memcpy(buf, legion->lord_scroll, legion->lord_scroll_len);
		*rlen = legion->lord_scroll_len;
	}
	upsgi_rwunlock(legion->lock);
	return buf;
}

char *upsgi_legion_scrolls(char *name, uint64_t *rlen) {
	char *buf = NULL;
        struct upsgi_legion *legion = upsgi_legion_get_by_name(name);
        if (!legion) return NULL;
	upsgi_rlock(legion->lock);
	buf = upsgi_malloc(legion->scrolls_len);
	memcpy(buf, legion->scrolls, legion->scrolls_len);
	*rlen = legion->scrolls_len;
	upsgi_rwunlock(legion->lock);
	return buf;
}
