#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	upsgi metrics subsystem

	a metric is a node in a linked list reachable via a numeric id (OID, in SNMP way) or a simple string:

	upsgi.worker.1.requests
	upsgi.custom.foo.bar

	the oid representation:

		1.3.6.1.4.1.35156.17 = iso.org.dod.internet.private.enterprise.unbit.upsgi
		1.3.6.1.4.1.35156.17.3.1.1 = iso.org.dod.internet.private.enterprise.unbit.upsgi.worker.1.requests
		1.3.6.1.4.1.35156.17.3.1.1 = iso.org.dod.internet.private.enterprise.unbit.upsgi.worker.1.requests
		1.3.6.1.4.1.35156.17.3.1.2.1.1 = iso.org.dod.internet.private.enterprise.unbit.upsgi.worker.1.core.1.requests
		...

	each metric is a collected value with a specific frequency
	metrics are meant for numeric values signed 64 bit, but they can be exposed as:

	gauge
	counter
	absolute

	metrics are managed by a dedicated thread (in the master) holding a linked list of all the items. For few metrics it is a good (read: simple) approach,
	but you can cache lookups in a upsgi cache for really big list. (TODO)

	struct upsgi_metric *um = upsgi_register_metric("worker.1.requests", "3.1.1", UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[1].requests, 0, NULL);
	prototype: struct upsgi_metric *upsgi_register_metric(char *name, char *oid, uint8_t value_type, char *collector, void *ptr, uint32_t freq, void *custom);

	value_type = UPSGI_METRIC_COUNTER/UPSGI_METRIC_GAUGE/UPSGI_METRIC_ABSOLUTE
	collect_way = "ptr" -> get from a pointer / UPSGI_METRIC_FUNC -> get from a func with the prototype int64_t func(struct upsgi_metric *); / UPSGI_METRIC_FILE -> get the value from a file, ptr is the filename

	For some metric (or all ?) you may want to hold a value even after a server reload. For such a reason you can specify a directory in which the server (on startup/restart) will look for
	a file named like the metric and will read the initial value from it. It may look an old-fashioned and quite inefficient way, but it is the most versatile for a sysadmin (allowing him/her
	to even modify the values manually)

	When registering a metric with the same name of an already registered one, the new one will overwrite the previous one. This allows plugins writer to override default behaviours

	Applications are allowed to update metrics (but they cannot register new ones), with simple api funcs:

	upsgi.metric_set("worker.1.requests", N)
	upsgi.metric_inc("worker.1.requests", N=1)
	upsgi.metric_dec("worker.1.requests", N=1)
	upsgi.metric_mul("worker.1.requests", N=1)
	upsgi.metric_div("worker.1.requests", N=1)

	and obviously they can get values:

	upsgi.metric_get("worker.1.requests")

	Updating metrics from your app MUST BE ATOMIC, for such a reason a upsgi rwlock is initialized on startup and used for each operation (simple reading from a metric does not require locking)

	Metrics can be updated from the internal routing subsystem too:

		route-if = equal:${REQUEST_URI};/foobar metricinc:foobar.test 2

	and can be accessed as ${metric[foobar.test]}

	The stats server exports the metrics list in the "metrics" attribute (obviously some info could be redundant)

*/


int64_t upsgi_metric_collector_file(struct upsgi_metric *metric) {
	char *filename = metric->arg1;
	if (!filename) return 0;
	int split_pos = metric->arg1n;

	char buf[4096];
	int64_t ret = 0;
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		upsgi_error_open(filename);
		return 0;
	}

	ssize_t rlen = read(fd, buf, 4096);
	if (rlen <= 0) goto end;

	char *ptr = buf;
	ssize_t i;
	int pos = 0;
	int found = 0;
	for(i=0;i<rlen;i++) {
		if (!found) {
			if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == 0 || buf[i] == '\n') {
				if (pos == split_pos) goto found;
				found = 1;
			}
		}
		else {
			if (!(buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == 0 || buf[i] == '\n')) {
				found = 0;
				ptr = buf + i;
				pos++;
			}
		}
	}

	if (pos == split_pos) goto found;
	goto end;
found:
	ret = strtoll(ptr, NULL, 10);
end:
	close(fd);
	return ret;

}



/*

	allowed chars for metrics name

	0-9
	a-z
	A-Z
	.
	-
	_

*/

static int upsgi_validate_metric_name(char *buf) {
	size_t len = strlen(buf);
	size_t i;
	for(i=0;i<len;i++) {
		if ( !(
			(buf[i] >= '0' && buf[i] <= '9') ||
			(buf[i] >= 'a' && buf[i] <= 'z') ||
			(buf[i] >= 'A' && buf[i] <= 'Z') ||
			buf[i] == '.' || buf[i] == '-' || buf[i] == '_'
		)) {

			return 0;
		
		}
	}	

	return 1;
}

/*

	allowed chars for metrics oid

	0-9
	.

	oids can be null
*/

static int upsgi_validate_metric_oid(char *buf) {
	if (!buf) return 1;
        size_t len = strlen(buf);
        size_t i;
        for(i=0;i<len;i++) {
                if ( !(
                        (buf[i] >= '0' && buf[i] <= '9') ||
                        buf[i] == '.'
                )) {
                
                        return 0;
                
                }
        }

        return 1;
}

void upsgi_metric_append(struct upsgi_metric *um) {
	struct upsgi_metric *old_metric=NULL,*metric=upsgi.metrics;
	while(metric) {
		old_metric = metric;
                metric = metric->next;
	}

	if (old_metric) {
                       old_metric->next = um;
        }
        else {
        	upsgi.metrics = um;
        }

        upsgi.metrics_cnt++;
}

struct upsgi_metric_collector *upsgi_metric_collector_by_name(char *name) {
	if (!name) return NULL;
	struct upsgi_metric_collector *umc = upsgi.metric_collectors;
	while(umc) {
		if (!strcmp(name, umc->name)) return umc;
		umc = umc->next;
	}
	upsgi_log("unable to find metric collector \"%s\"\n", name);
	exit(1);
}

struct upsgi_metric *upsgi_register_metric_do(char *name, char *oid, uint8_t value_type, char *collector, void *ptr, uint32_t freq, void *custom, int do_not_push) {
	if (!upsgi.has_metrics) return NULL;
	struct upsgi_metric *old_metric=NULL,*metric=upsgi.metrics;

	if (!upsgi_validate_metric_name(name)) {
		upsgi_log("invalid metric name: %s\n", name);
		exit(1);
	}

	if (!upsgi_validate_metric_oid(oid)) {
		upsgi_log("invalid metric oid: %s\n", oid);
		exit(1);
	}

	while(metric) {
		if (!strcmp(metric->name, name)) {
			goto found;
		}
		old_metric = metric;
		metric = metric->next;
	}

	metric = upsgi_calloc(sizeof(struct upsgi_metric));
	// always make a copy of the name (so we can use stack for building strings)
	metric->name = upsgi_str(name);
	metric->name_len = strlen(metric->name);

	if (!do_not_push) {
		if (old_metric) {
			old_metric->next = metric;
		}
		else {
			upsgi.metrics = metric;
		}

		upsgi.metrics_cnt++;
	}

found:
	metric->oid = oid;
	if (metric->oid) {
		metric->oid_len = strlen(oid);
		metric->oid = upsgi_str(oid);
		char *p, *ctx = NULL;
		char *oid_tmp = upsgi_str(metric->oid);
		// slower but we save lot of memory
		struct upsgi_buffer *ub = upsgi_buffer_new(1);
                upsgi_foreach_token(oid_tmp, ".", p, ctx) {
			uint64_t l = strtoull(p, NULL, 10);	
			if (upsgi_base128(ub, l, 1)) {
				upsgi_log("unable to encode oid %s to asn/ber\n", metric->oid);
				exit(1);
			}
		}
		metric->asn = ub->buf;
		metric->asn_len = ub->pos;
		ub->buf = NULL;
		upsgi_buffer_destroy(ub);
		free(oid_tmp);
	}
	metric->type = value_type;
	metric->collector = upsgi_metric_collector_by_name(collector);
	metric->ptr = ptr;
	metric->freq = freq;
	if (!metric->freq) metric->freq = 1;
	metric->custom = custom;

	if (upsgi.metrics_dir) {
		char *filename = upsgi_concat3(upsgi.metrics_dir, "/", name);
		int fd = open(filename, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP);
		if (fd < 0) {
			upsgi_error_open(filename);
			exit(1);
		}
		// fill the file
		if (lseek(fd, upsgi.page_size-1, SEEK_SET) < 0) {
			upsgi_error("upsgi_register_metric()/lseek()");
			upsgi_log("unable to register metric: %s\n", name);
			exit(1);
		}
		if (write(fd, "\0", 1) != 1) {
			upsgi_error("upsgi_register_metric()/write()");
			upsgi_log("unable to register metric: %s\n", name);
			exit(1);
		}
		metric->map = mmap(NULL, upsgi.page_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (metric->map == MAP_FAILED) {
			upsgi_error("upsgi_register_metric()/mmap()");
			upsgi_log("unable to register metric: %s\n", name);
			exit(1);
		}
		
		// we can now safely close the file descriptor and update the file from memory
		close(fd);
		free(filename);
	}

	return metric;
}

struct upsgi_metric *upsgi_register_metric(char *name, char *oid, uint8_t value_type, char *collector, void *ptr, uint32_t freq, void *custom) {
	return upsgi_register_metric_do(name, oid, value_type, collector, ptr, freq, custom, 0);
}

struct upsgi_metric *upsgi_register_keyval_metric(char *arg) {
	char *m_name = NULL;
	char *m_oid = NULL;
	char *m_type = NULL;
	char *m_collector = NULL;
	char *m_freq = NULL;
	char *m_arg1 = NULL;
	char *m_arg2 = NULL;
	char *m_arg3 = NULL;
	char *m_arg1n = NULL;
	char *m_arg2n = NULL;
	char *m_arg3n = NULL;
	char *m_initial_value = NULL;
	char *m_children = NULL;
	char *m_alias = NULL;
	char *m_reset_after_push = NULL;

	if (!strchr(arg, '=')) {
		m_name = upsgi_str(arg);
	}
	else if (upsgi_kvlist_parse(arg, strlen(arg), ',', '=',
		"name", &m_name,
		"oid", &m_oid,
		"type", &m_type,
		"initial_value", &m_initial_value,
		"collector", &m_collector,
		"freq", &m_freq,
		"arg1", &m_arg1,
		"arg2", &m_arg2,
		"arg3", &m_arg3,
		"arg1n", &m_arg1n,
		"arg2n", &m_arg2n,
		"arg3n", &m_arg3n,
		"children", &m_children,
		"alias", &m_alias,
		"reset_after_push", &m_reset_after_push,
		NULL)) {
		upsgi_log("invalid metric keyval syntax: %s\n", arg);
		exit(1);
	}

	if (!m_name) {
		upsgi_log("you need to specify a metric name: %s\n", arg);
		exit(1);
	}

	uint8_t type = UPSGI_METRIC_COUNTER;
	char *collector = NULL;
	uint32_t freq = 0;
	int64_t initial_value = 0;

	if (m_type) {
		if (!strcmp(m_type, "gauge")) {
			type = UPSGI_METRIC_GAUGE;
		}
		else if (!strcmp(m_type, "absolute")) {
			type = UPSGI_METRIC_ABSOLUTE;
		}
		else if (!strcmp(m_type, "alias")) {
			type = UPSGI_METRIC_ALIAS;
		}
	}

	if (m_collector) {
		collector = m_collector;	
	}

	if (m_freq) freq = strtoul(m_freq, NULL, 10);

	
	if (m_initial_value) {
		initial_value = strtoll(m_initial_value, NULL, 10);		
	}

	struct upsgi_metric* um =  upsgi_register_metric(m_name, m_oid, type, collector, NULL, freq, NULL);
	um->initial_value = initial_value;

	if (m_reset_after_push){
		um->reset_after_push = 1;
	}

	if (m_children) {
		char *p, *ctx = NULL;
        	upsgi_foreach_token(m_children, ";", p, ctx) {
			struct upsgi_metric *child = upsgi_metric_find_by_name(p);
			if (!child) {
				upsgi_log("unable to find metric \"%s\"\n", p);
				exit(1);
			}
			upsgi_metric_add_child(um, child);
                }
        }

	if (m_alias) {
		struct upsgi_metric *alias = upsgi_metric_find_by_name(m_alias);
		if (!alias) {
			upsgi_log("unable to find metric \"%s\"\n", m_alias);
                        exit(1);
		}
		um->ptr = (void *) alias;
	}

	um->arg1 = m_arg1;
	um->arg2 = m_arg2;
	um->arg3 = m_arg3;

	if (m_arg1n) um->arg1n = strtoll(m_arg1n, NULL, 10);
	if (m_arg2n) um->arg2n = strtoll(m_arg2n, NULL, 10);
	if (m_arg3n) um->arg3n = strtoll(m_arg3n, NULL, 10);

	free(m_name);
	if (m_oid) free(m_oid);
	if (m_type) free(m_type);
	if (m_collector) free(m_collector);
	if (m_freq) free(m_freq);
	/*
	DO NOT FREE THEM
	if (m_arg1) free(m_arg1);
	if (m_arg2) free(m_arg2);
	if (m_arg3) free(m_arg3);
	*/
	if (m_arg1n) free(m_arg1n);
	if (m_arg2n) free(m_arg2n);
	if (m_arg3n) free(m_arg3n);
	if (m_initial_value) free(m_initial_value);
	if (m_children) free(m_children);
	if (m_alias) free(m_alias);
	if (m_reset_after_push) free(m_reset_after_push);
	return um;
}

static void *upsgi_metrics_loop(void *arg) {

	// block signals on this thread
        sigset_t smask;
        sigfillset(&smask);
#ifndef UPSGI_DEBUG
        sigdelset(&smask, SIGSEGV);
#endif
        pthread_sigmask(SIG_BLOCK, &smask, NULL);

	for(;;) {
		struct upsgi_metric *metric = upsgi.metrics;
		// every second scan the whole metrics tree
		time_t now = upsgi_now();
		while(metric) {
			if (!metric->last_update) {
				metric->last_update = now;
			}
			else {
				if ((uint32_t) (now - metric->last_update) < metric->freq) goto next;
			}
			upsgi_wlock(upsgi.metrics_lock);
			int64_t value = *metric->value;
			// gather the new value based on the type of collection strategy
			if (metric->collector) {
				*metric->value = metric->initial_value + metric->collector->func(metric);
			}
			int64_t new_value = *metric->value;
			upsgi_rwunlock(upsgi.metrics_lock);

			metric->last_update = now;

			if (upsgi.metrics_dir && metric->map) {
				if (value != new_value) {
					int ret = snprintf(metric->map, upsgi.page_size, "%lld\n", (long long) new_value);
					if (ret > 0 && ret < upsgi.page_size) {
						memset(metric->map+ret, 0, 4096-ret);
					}
				}
			}

			// thresholds;
			struct upsgi_metric_threshold *umt = metric->thresholds;
			while(umt) {
				if (new_value >= umt->value) {
					if (umt->reset) {
						upsgi_wlock(upsgi.metrics_lock);
						*metric->value = umt->reset_value;
						upsgi_rwunlock(upsgi.metrics_lock);
					}

					if (umt->alarm) {
						if (umt->last_alarm + umt->rate <= now) {
							if (umt->msg) {
								upsgi_alarm_trigger(umt->alarm, umt->msg, umt->msg_len);
							}
							else {
								upsgi_alarm_trigger(umt->alarm, metric->name, metric->name_len);
							}
							umt->last_alarm = now;
						}
					}
				}
				umt = umt->next;
			}
next:
			metric = metric->next;
		}
		sleep(1);
	}

	return NULL;
	
}

void upsgi_metrics_start_collector() {
	if (!upsgi.has_metrics) return;
	pthread_t t;
        pthread_create(&t, NULL, upsgi_metrics_loop, NULL);
	upsgi_log("metrics collector thread started\n");
}

struct upsgi_metric *upsgi_metric_find_by_name(char *name) {
	struct upsgi_metric *um = upsgi.metrics;
	while(um) {
		if (!strcmp(um->name, name)) {
			return um;
		}	
		um = um->next;
	}

	return NULL;
}

struct upsgi_metric *upsgi_metric_find_by_namen(char *name, size_t len) {
        struct upsgi_metric *um = upsgi.metrics;
        while(um) {
                if (!upsgi_strncmp(um->name, um->name_len, name, len)) {
                        return um;
                }
                um = um->next;
        }

        return NULL;
}

struct upsgi_metric_child *upsgi_metric_add_child(struct upsgi_metric *parent, struct upsgi_metric *child) {
	struct upsgi_metric_child *umc = parent->children, *old_umc = NULL;
	while(umc) {
		old_umc = umc;
		umc = umc->next;
	}

	umc = upsgi_calloc(sizeof(struct upsgi_metric_child));
	umc->um = child;
	if (old_umc) {
		old_umc->next = umc;
	}
	else {
		parent->children = umc;
	}
	return umc;
}

struct upsgi_metric *upsgi_metric_find_by_oid(char *oid) {
        struct upsgi_metric *um = upsgi.metrics;
        while(um) {
                if (um->oid && !strcmp(um->oid, oid)) {
                        return um;
                }
                um = um->next;
        }

        return NULL;
}

struct upsgi_metric *upsgi_metric_find_by_oidn(char *oid, size_t len) {
        struct upsgi_metric *um = upsgi.metrics;
        while(um) {
                if (um->oid && !upsgi_strncmp(um->oid, um->oid_len, oid, len)) {
                        return um;
                }
                um = um->next;
        }

        return NULL;
}

struct upsgi_metric *upsgi_metric_find_by_asn(char *asn, size_t len) {
        struct upsgi_metric *um = upsgi.metrics;
        while(um) {
                if (um->oid && um->asn && !upsgi_strncmp(um->asn, um->asn_len, asn, len)) {
                        return um;
                }
                um = um->next;
        }

        return NULL;
}


/*

	api functions

	metric_set
	metric_inc
	metric_dec
	metric_mul
	metric_div

*/

#define um_op struct upsgi_metric *um = NULL;\
	if (!upsgi.has_metrics) return -1;\
	if (name) {\
                um = upsgi_metric_find_by_name(name);\
        }\
        else if (oid) {\
                um = upsgi_metric_find_by_oid(oid);\
        }\
        if (!um) return -1;\
	if (um->collector || um->type == UPSGI_METRIC_ALIAS) return -1;\
	upsgi_wlock(upsgi.metrics_lock)

int upsgi_metric_set(char *name, char *oid, int64_t value) {
	um_op;
	*um->value = value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

int upsgi_metric_inc(char *name, char *oid, int64_t value) {
        um_op;
	*um->value += value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

int upsgi_metric_dec(char *name, char *oid, int64_t value) {
        um_op;
	*um->value -= value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

int upsgi_metric_mul(char *name, char *oid, int64_t value) {
        um_op;
	*um->value *= value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

int upsgi_metric_div(char *name, char *oid, int64_t value) {
	// avoid division by zero
	if (value == 0) return -1;
        um_op;
	*um->value /= value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

int64_t upsgi_metric_get(char *name, char *oid) {
	if (!upsgi.has_metrics) return 0;
	int64_t ret = 0;
	struct upsgi_metric *um = NULL;
	if (name) {
		um = upsgi_metric_find_by_name(name);
	}
	else if (oid) {
		um = upsgi_metric_find_by_oid(oid);
	}
	if (!um) return 0;

	// now (in rlocked context) we get the value from
	// the map
	upsgi_rlock(upsgi.metrics_lock);
	ret = *um->value;
	// unlock
	upsgi_rwunlock(upsgi.metrics_lock);
	return ret;
}

int64_t upsgi_metric_getn(char *name, size_t nlen, char *oid, size_t olen) {
        if (!upsgi.has_metrics) return 0;
        int64_t ret = 0;
        struct upsgi_metric *um = NULL;
        if (name) {
                um = upsgi_metric_find_by_namen(name, nlen);
        }
        else if (oid) {
                um = upsgi_metric_find_by_oidn(oid, olen);
        }
        if (!um) return 0;

        // now (in rlocked context) we get the value from
        // the map
        upsgi_rlock(upsgi.metrics_lock);
        ret = *um->value;
        // unlock
        upsgi_rwunlock(upsgi.metrics_lock);
        return ret;
}

int upsgi_metric_set_max(char *name, char *oid, int64_t value) {
	um_op;
	if (value > *um->value)
	        *um->value = value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

int upsgi_metric_set_min(char *name, char *oid, int64_t value) {
	um_op;
	if ((value > um->initial_value || 0) && value < *um->value)
		*um->value = value;
	upsgi_rwunlock(upsgi.metrics_lock);
	return 0;
}

#define upsgi_metric_name(f, n) ret = snprintf(buf, 4096, f, n); if (ret <= 1 || ret >= 4096) { upsgi_log("unable to register metric name %s\n", f); exit(1);}
#define upsgi_metric_name2(f, n, n2) ret = snprintf(buf, 4096, f, n, n2); if (ret <= 1 || ret >= 4096) { upsgi_log("unable to register metric name %s\n", f); exit(1);}

#define upsgi_metric_oid(f, n) ret = snprintf(buf2, 4096, f, n); if (ret <= 1 || ret >= 4096) { upsgi_log("unable to register metric oid %s\n", f); exit(1);}
#define upsgi_metric_oid2(f, n, n2) ret = snprintf(buf2, 4096, f, n, n2); if (ret <= 1 || ret >= 4096) { upsgi_log("unable to register metric oid %s\n", f); exit(1);}

void upsgi_setup_metrics() {

	if (!upsgi.has_metrics) return;

	char buf[4096];
	char buf2[4096];

	// create the main rwlock
	upsgi.metrics_lock = upsgi_rwlock_init("metrics");
	
	// get realpath of the storage dir
	if (upsgi.metrics_dir) {
		char *dir = upsgi_expand_path(upsgi.metrics_dir, strlen(upsgi.metrics_dir), NULL);
		if (!dir) {
			upsgi_error("upsgi_setup_metrics()/upsgi_expand_path()");
			exit(1);
		}
		upsgi.metrics_dir = dir;
	}

	// the 'core' namespace
	upsgi_register_metric("core.routed_signals", "5.1", UPSGI_METRIC_COUNTER, "ptr", &upsgi.shared->routed_signals, 0, NULL);
	upsgi_register_metric("core.unrouted_signals", "5.2", UPSGI_METRIC_COUNTER, "ptr", &upsgi.shared->unrouted_signals, 0, NULL);

	upsgi_register_metric("core.busy_workers", "5.3", UPSGI_METRIC_GAUGE, "ptr", &upsgi.shared->busy_workers, 0, NULL);
	upsgi_register_metric("core.idle_workers", "5.4", UPSGI_METRIC_GAUGE, "ptr", &upsgi.shared->idle_workers, 0, NULL);
	upsgi_register_metric("core.overloaded", "5.5", UPSGI_METRIC_COUNTER, "ptr", &upsgi.shared->overloaded, 0, NULL);
	// parents are appended only at the end
	struct upsgi_metric *total_tx = upsgi_register_metric_do("core.total_tx", "5.100", UPSGI_METRIC_COUNTER, "sum", NULL, 0, NULL, 1);
	struct upsgi_metric *total_rss = upsgi_register_metric_do("core.total_rss", "5.101", UPSGI_METRIC_GAUGE, "sum", NULL, 0, NULL, 1);
	struct upsgi_metric *total_vsz = upsgi_register_metric_do("core.total_vsz", "5.102", UPSGI_METRIC_GAUGE, "sum", NULL, 0, NULL, 1);
	struct upsgi_metric *total_avg_rt = upsgi_register_metric_do("core.avg_response_time", "5.103", UPSGI_METRIC_GAUGE, "avg", NULL, 0, NULL, 1);
	struct upsgi_metric *total_running_time = upsgi_register_metric_do("core.total_running_time", "5.104", UPSGI_METRIC_COUNTER, "sum", NULL, 0, NULL, 1);

	int ret;

	// create the 'worker' namespace
	int i;
	for(i=0;i<=upsgi.numproc;i++) {

		upsgi_metric_name("worker.%d.requests", i) ; upsgi_metric_oid("3.%d.1", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].requests, 0, NULL);

		upsgi_metric_name("worker.%d.delta_requests", i) ; upsgi_metric_oid("3.%d.2", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_ABSOLUTE, "ptr", &upsgi.workers[i].delta_requests, 0, NULL);

		upsgi_metric_name("worker.%d.failed_requests", i) ; upsgi_metric_oid("3.%d.13", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].failed_requests, 0, NULL);

		upsgi_metric_name("worker.%d.respawns", i) ; upsgi_metric_oid("3.%d.14", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].respawn_count, 0, NULL);

		upsgi_metric_name("worker.%d.avg_response_time", i) ; upsgi_metric_oid("3.%d.8", i);
		struct upsgi_metric *avg_rt = upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].avg_response_time, 0, NULL);		if (i > 0) upsgi_metric_add_child(total_avg_rt, avg_rt);

		upsgi_metric_name("worker.%d.total_tx", i) ; upsgi_metric_oid("3.%d.9", i);
		struct upsgi_metric *tx = upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].tx, 0, NULL);
		if (i > 0) upsgi_metric_add_child(total_tx, tx);

		upsgi_metric_name("worker.%d.rss_size", i) ; upsgi_metric_oid("3.%d.11", i);
		struct upsgi_metric *rss = upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].rss_size, 0, NULL);
		if (i > 0) upsgi_metric_add_child(total_rss, rss);

		upsgi_metric_name("worker.%d.vsz_size", i) ; upsgi_metric_oid("3.%d.12", i);
		struct upsgi_metric *vsz = upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].vsz_size, 0, NULL);
		if (i > 0) upsgi_metric_add_child(total_vsz, vsz);

		upsgi_metric_name("worker.%d.running_time", i) ; upsgi_metric_oid("3.%d.15", i);
		struct upsgi_metric *running_time = upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].running_time, 0, NULL);
		if (i > 0) upsgi_metric_add_child(total_running_time, running_time);

		upsgi_metric_name("worker.%d.thunder_lock_acquires", i) ; upsgi_metric_oid("3.%d.16", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].thunder_lock_acquires, 0, NULL);

		upsgi_metric_name("worker.%d.thunder_lock_contention_events", i) ; upsgi_metric_oid("3.%d.17", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].thunder_lock_contention_events, 0, NULL);

		upsgi_metric_name("worker.%d.thunder_lock_wait_us", i) ; upsgi_metric_oid("3.%d.18", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].thunder_lock_wait_us, 0, NULL);

		upsgi_metric_name("worker.%d.thunder_lock_hold_us", i) ; upsgi_metric_oid("3.%d.19", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].thunder_lock_hold_us, 0, NULL);

		upsgi_metric_name("worker.%d.thunder_lock_bypass_count", i) ; upsgi_metric_oid("3.%d.20", i);
		upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].thunder_lock_bypass_count, 0, NULL);


		// skip core metrics for worker 0
		if (i == 0) continue;

		if (upsgi.metrics_no_cores) continue;

		int j;
		for(j=0;j<upsgi.cores;j++) {
			upsgi_metric_name2("worker.%d.core.%d.requests", i, j) ; upsgi_metric_oid2("3.%d.2.%d.1", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].requests, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.write_errors", i, j) ; upsgi_metric_oid2("3.%d.2.%d.3", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].write_errors, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.routed_requests", i, j) ; upsgi_metric_oid2("3.%d.2.%d.4", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].routed_requests, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_requests", i, j) ; upsgi_metric_oid2("3.%d.2.%d.5", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_requests, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.offloaded_requests", i, j) ; upsgi_metric_oid2("3.%d.2.%d.6", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].offloaded_requests, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.exceptions", i, j) ; upsgi_metric_oid2("3.%d.2.%d.7", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].exceptions, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.read_errors", i, j) ; upsgi_metric_oid2("3.%d.2.%d.8", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].read_errors, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_path_cache_hits", i, j) ; upsgi_metric_oid2("3.%d.2.%d.9", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_path_cache_hits, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_path_cache_misses", i, j) ; upsgi_metric_oid2("3.%d.2.%d.10", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_path_cache_misses, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_realpath_calls", i, j) ; upsgi_metric_oid2("3.%d.2.%d.11", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_realpath_calls, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_stat_calls", i, j) ; upsgi_metric_oid2("3.%d.2.%d.12", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_stat_calls, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_open_calls", i, j) ; upsgi_metric_oid2("3.%d.2.%d.14", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_open_calls, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_open_failures", i, j) ; upsgi_metric_oid2("3.%d.2.%d.15", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_open_failures, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.static_index_checks", i, j) ; upsgi_metric_oid2("3.%d.2.%d.13", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].static_index_checks, 0, NULL);

			upsgi_metric_name2("worker.%d.core.%d.body_sched_rounds", i, j) ; upsgi_metric_oid2("3.%d.2.%d.16", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_rounds, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_interactive_turns", i, j) ; upsgi_metric_oid2("3.%d.2.%d.17", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_interactive_turns, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_bulk_turns", i, j) ; upsgi_metric_oid2("3.%d.2.%d.18", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_bulk_turns, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_requeues", i, j) ; upsgi_metric_oid2("3.%d.2.%d.19", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_requeues, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_promotions_to_bulk", i, j) ; upsgi_metric_oid2("3.%d.2.%d.20", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_promotions_to_bulk, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_no_credit_skips", i, j) ; upsgi_metric_oid2("3.%d.2.%d.21", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_no_credit_skips, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_empty_read_events", i, j) ; upsgi_metric_oid2("3.%d.2.%d.22", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_empty_read_events, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_eagain_events", i, j) ; upsgi_metric_oid2("3.%d.2.%d.23", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_eagain_events, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_completed_items", i, j) ; upsgi_metric_oid2("3.%d.2.%d.24", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_completed_items, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_bytes_interactive", i, j) ; upsgi_metric_oid2("3.%d.2.%d.25", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_bytes_interactive, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_bytes_bulk", i, j) ; upsgi_metric_oid2("3.%d.2.%d.26", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_bytes_bulk, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_bytes_total", i, j) ; upsgi_metric_oid2("3.%d.2.%d.27", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_bytes_total, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_credit_granted_bytes", i, j) ; upsgi_metric_oid2("3.%d.2.%d.28", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_credit_granted_bytes, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_credit_unused_bytes", i, j) ; upsgi_metric_oid2("3.%d.2.%d.29", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_credit_unused_bytes, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_active_items", i, j) ; upsgi_metric_oid2("3.%d.2.%d.30", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].cores[j].body_sched_active_items, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_interactive_depth_max", i, j) ; upsgi_metric_oid2("3.%d.2.%d.31", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].cores[j].body_sched_interactive_depth_max, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_bulk_depth_max", i, j) ; upsgi_metric_oid2("3.%d.2.%d.32", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].cores[j].body_sched_bulk_depth_max, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_residency_us_max", i, j) ; upsgi_metric_oid2("3.%d.2.%d.33", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].cores[j].body_sched_residency_us_max, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_residency_us_p50_sample", i, j) ; upsgi_metric_oid2("3.%d.2.%d.34", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].cores[j].body_sched_residency_us_p50_sample, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_residency_us_p95_sample", i, j) ; upsgi_metric_oid2("3.%d.2.%d.35", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi.workers[i].cores[j].body_sched_residency_us_p95_sample, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_items_promoted_by_bytes", i, j) ; upsgi_metric_oid2("3.%d.2.%d.36", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_items_promoted_by_bytes, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_items_promoted_by_rounds", i, j) ; upsgi_metric_oid2("3.%d.2.%d.37", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_items_promoted_by_rounds, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_items_promoted_by_residency", i, j) ; upsgi_metric_oid2("3.%d.2.%d.38", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_items_promoted_by_residency, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_near_complete_fastfinishes", i, j) ; upsgi_metric_oid2("3.%d.2.%d.39", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_near_complete_fastfinishes, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_overflow_protection_hits", i, j) ; upsgi_metric_oid2("3.%d.2.%d.40", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_overflow_protection_hits, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_queue_full_events", i, j) ; upsgi_metric_oid2("3.%d.2.%d.41", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_queue_full_events, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_disabled_fallbacks", i, j) ; upsgi_metric_oid2("3.%d.2.%d.42", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_disabled_fallbacks, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_full_budget_turns", i, j) ; upsgi_metric_oid2("3.%d.2.%d.43", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_full_budget_turns, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_wait_relief_events", i, j) ; upsgi_metric_oid2("3.%d.2.%d.44", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_wait_relief_events, 0, NULL);
			upsgi_metric_name2("worker.%d.core.%d.body_sched_yield_hints", i, j) ; upsgi_metric_oid2("3.%d.2.%d.45", i, j);
			upsgi_register_metric(buf, buf2, UPSGI_METRIC_COUNTER, "ptr", &upsgi.workers[i].cores[j].body_sched_yield_hints, 0, NULL);

		}
	}

	// append parents
	upsgi_metric_append(total_tx);
	upsgi_metric_append(total_rss);
	upsgi_metric_append(total_vsz);
	upsgi_metric_append(total_avg_rt);
	upsgi_metric_append(total_running_time);

	// sockets
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	int pos = 0;
	while(upsgi_sock) {
		upsgi_metric_name("socket.%d.listen_queue", pos) ; upsgi_metric_oid("7.%d.1", pos);
                upsgi_register_metric(buf, buf2, UPSGI_METRIC_GAUGE, "ptr", &upsgi_sock->queue, 0, NULL);
		pos++;
		upsgi_sock = upsgi_sock->next;
	}

	// create aliases
	upsgi_register_metric("rss_size", NULL, UPSGI_METRIC_ALIAS, NULL, total_rss, 0, NULL);
	upsgi_register_metric("vsz_size", NULL, UPSGI_METRIC_ALIAS, NULL, total_vsz, 0, NULL);

	// create custom/user-defined metrics
	struct upsgi_string_list *usl;
	upsgi_foreach(usl, upsgi.additional_metrics) {
		struct upsgi_metric *um = upsgi_register_keyval_metric(usl->value);
		if (um) {
			upsgi_log("added custom metric: %s\n", um->name);
		}
	}

	// allocate shared memory
	int64_t *values = upsgi_calloc_shared(sizeof(int64_t) * upsgi.metrics_cnt);
	pos = 0;

	struct upsgi_metric *metric = upsgi.metrics;
	while(metric) {
		metric->value = &values[pos];
		pos++;
		metric = metric->next;
	}

	// remap aliases
	metric = upsgi.metrics;
        while(metric) {
		if (metric->type == UPSGI_METRIC_ALIAS) {
			struct upsgi_metric *alias = (struct upsgi_metric *) metric->ptr;
			if (!alias) {
				upsgi_log("metric alias \"%s\" requires a mapping !!!\n", metric->name);
				exit(1);
			}
			metric->value = alias->value;
			metric->oid = alias->oid;
		}
		if (metric->initial_value) {
			*metric->value = metric->initial_value;
		}
		metric = metric->next;
	}

	// setup thresholds
	upsgi_foreach(usl, upsgi.metrics_threshold) {
		char *m_key = NULL;
		char *m_value = NULL;
		char *m_alarm = NULL;
		char *m_rate = NULL;
		char *m_reset = NULL;
		char *m_msg = NULL;
		if (upsgi_kvlist_parse(usl->value, usl->len, ',', '=',
                	"key", &m_key,
                	"value", &m_value,
                	"alarm", &m_alarm,
                	"rate", &m_rate,
                	"msg", &m_msg,
                	"reset", &m_reset,
                	NULL)) {
                		upsgi_log("invalid metric threshold keyval syntax: %s\n", usl->value);
                		exit(1);
		}

		if (!m_key || !m_value) {
			upsgi_log("metric's threshold requires a key and a value: %s\n", usl->value);
			exit(1);
		}

		struct upsgi_metric *um = upsgi_metric_find_by_name(m_key);
		if (!um) {
			upsgi_log("unable to find metric %s\n", m_key);
			exit(1);
		}

		struct upsgi_metric_threshold *umt = upsgi_calloc(sizeof(struct upsgi_metric_threshold));
		umt->value = strtoll(m_value, NULL, 10);
		if (m_reset) {
			umt->reset = 1;
			umt->reset_value = strtoll(m_reset, NULL, 10);
		}

		if (m_rate) {
			umt->rate = (int32_t) atoi(m_rate);
		}

		umt->alarm = m_alarm;

		if (m_msg) {
			umt->msg = m_msg;
			umt->msg_len = strlen(m_msg);
		}

		free(m_key);
		free(m_value);
		if (m_rate) free(m_rate);
		if (m_reset) free(m_reset);

		if (um->thresholds) {
			struct upsgi_metric_threshold *umt_list = um->thresholds;
			while(umt_list) {
				if (!umt_list->next) {
					umt_list->next = umt;
					break;
				}
				umt_list = umt_list->next;
			}
		}
		else {
			um->thresholds = umt;
		}

		upsgi_log("added threshold for metric %s (value: %lld)\n", um->name, umt->value);
	}

	upsgi_log("initialized %llu metrics\n", upsgi.metrics_cnt);

	if (upsgi.metrics_dir) {
		upsgi_log("memory allocated for metrics storage: %llu bytes (%llu MB)\n", upsgi.metrics_cnt * upsgi.page_size, (upsgi.metrics_cnt * upsgi.page_size)/1024/1024);
		if (upsgi.metrics_dir_restore) {
			metric = upsgi.metrics;
        		while(metric) {
				if (metric->map) {
					metric->initial_value = strtoll(metric->map, NULL, 10);
				}
				metric = metric->next;
			}
		}
	}
}

struct upsgi_metric_collector *upsgi_register_metric_collector(char *name, int64_t (*func)(struct upsgi_metric *)) {
	struct upsgi_metric_collector *collector = upsgi.metric_collectors, *old_collector = NULL;

	while(collector) {
		if (!strcmp(collector->name, name)) goto found;
		old_collector = collector;
		collector = collector->next;
	}

	collector = upsgi_calloc(sizeof(struct upsgi_metric_collector));
	collector->name = name;
	if (old_collector) {
		old_collector->next = collector;
	}
	else {
		upsgi.metric_collectors = collector;
	}
found:
	collector->func = func;

	return collector;
}

static int64_t upsgi_metric_collector_ptr(struct upsgi_metric *um) {
	return *um->ptr;
}

static int64_t upsgi_metric_collector_sum(struct upsgi_metric *um) {
	int64_t total = 0;
        struct upsgi_metric_child *umc = um->children;
        while(umc) {
                struct upsgi_metric *c = umc->um;
                total += *c->value;
                umc = umc->next;
        }

        return total;
}

static int64_t upsgi_metric_collector_accumulator(struct upsgi_metric *um) {
        int64_t total = *um->value;
        struct upsgi_metric_child *umc = um->children;
        while(umc) {
                struct upsgi_metric *c = umc->um;
                total += *c->value;
                umc = umc->next;
        }

        return total;
}

static int64_t upsgi_metric_collector_multiplier(struct upsgi_metric *um) {
        int64_t total = 0;
        struct upsgi_metric_child *umc = um->children;
        while(umc) {
                struct upsgi_metric *c = umc->um;
                total += *c->value;
                umc = umc->next;
        }

        return total * um->arg1n;
}

static int64_t upsgi_metric_collector_adder(struct upsgi_metric *um) {
        int64_t total = 0;
        struct upsgi_metric_child *umc = um->children;
        while(umc) {
                struct upsgi_metric *c = umc->um;
                total += *c->value;
                umc = umc->next;
        }

        return total + um->arg1n;
}

static int64_t upsgi_metric_collector_avg(struct upsgi_metric *um) {
        int64_t total = 0;
	int64_t count = 0;
        struct upsgi_metric_child *umc = um->children;
        while(umc) {
                struct upsgi_metric *c = umc->um;
                total += *c->value;
		count++;
                umc = umc->next;
        }

	if (count == 0) return 0;

        return total/count;
}

static int64_t upsgi_metric_collector_func(struct upsgi_metric *um) {
	if (!um->arg1) return 0;
	int64_t (*func)(struct upsgi_metric *) = (int64_t (*)(struct upsgi_metric *)) um->custom;
	if (!func) {
		func = dlsym(RTLD_DEFAULT, um->arg1);
		um->custom = func;
	}
	if (!func) return 0;
        return func(um);
}

void upsgi_metrics_collectors_setup() {
	upsgi_register_metric_collector("ptr", upsgi_metric_collector_ptr);
	upsgi_register_metric_collector("file", upsgi_metric_collector_file);
	upsgi_register_metric_collector("sum", upsgi_metric_collector_sum);
	upsgi_register_metric_collector("accumulator", upsgi_metric_collector_accumulator);
	upsgi_register_metric_collector("adder", upsgi_metric_collector_adder);
	upsgi_register_metric_collector("multiplier", upsgi_metric_collector_multiplier);
	upsgi_register_metric_collector("avg", upsgi_metric_collector_avg);
	upsgi_register_metric_collector("func", upsgi_metric_collector_func);
}
