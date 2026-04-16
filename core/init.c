#include "upsgi.h"

extern struct upsgi_server upsgi;

struct http_status_codes {
        const char      key[4];
        const char      *message;
        int             message_size;
};

/* statistically ordered */
struct http_status_codes hsc[] = {
        {"200", "OK"},
        {"302", "Found"},
        {"404", "Not Found"},
        {"500", "Internal Server Error"},
        {"301", "Moved Permanently"},
        {"304", "Not Modified"},
        {"303", "See Other"},
        {"403", "Forbidden"},
        {"307", "Temporary Redirect"},
        {"401", "Unauthorized"},
        {"400", "Bad Request"},
        {"405", "Method Not Allowed"},
        {"408", "Request Timeout"},

        {"100", "Continue"},
        {"101", "Switching Protocols"},
        {"103", "Early Hints"},
        {"201", "Created"},
        {"202", "Accepted"},
        {"203", "Non-Authoritative Information"},
        {"204", "No Content"},
        {"205", "Reset Content"},
        {"206", "Partial Content"},
        {"300", "Multiple Choices"},
        {"305", "Use Proxy"},
        {"402", "Payment Required"},
        {"406", "Not Acceptable"},
        {"407", "Proxy Authentication Required"},
        {"409", "Conflict"},
        {"410", "Gone"},
        {"411", "Length Required"},
        {"412", "Precondition Failed"},
        {"413", "Request Entity Too Large"},
        {"414", "Request-URI Too Long"},
        {"415", "Unsupported Media Type"},
        {"416", "Requested Range Not Satisfiable"},
        {"417", "Expectation Failed"},
        {"418", "I'm a teapot"},
        {"422", "Unprocessable Entity"},
        {"425", "Too Early"},
        {"426", "Upgrade Required"},
        {"428", "Precondition Required"},
        {"429", "Too Many Requests"},
        {"431", "Request Header Fields Too Large"},
        {"451", "Unavailable For Legal Reasons"},
        {"501", "Not Implemented"},
        {"502", "Bad Gateway"},
        {"503", "Service Unavailable"},
        {"504", "Gateway Timeout"},
        {"505", "HTTP Version Not Supported"},
        {"509", "Bandwidth Limit Exceeded"},
        {"511", "Network Authentication Required"},
        {"", NULL},
};




void upsgi_init_default() {

	upsgi.cpus = 1;
	upsgi.new_argc = -1;
	upsgi.binary_argc = 1;

	upsgi.backtrace_depth = 64;
	upsgi.max_apps = 64;

	upsgi.master_queue = -1;

	upsgi.signal_socket = -1;
	upsgi.my_signal_socket = -1;
	upsgi.stats_fd = -1;

	upsgi.stats_pusher_default_freq = 3;

	upsgi.original_log_fd = 2;

	upsgi.emperor_fd_config = -1;
	upsgi.emperor_fd_proxy = -1;
	// default emperor scan frequency
	upsgi.emperor_freq = 3;
	upsgi.emperor_throttle = 1000;
	upsgi.emperor_heartbeat = 30;
	upsgi.emperor_curse_tolerance = 30;
	// max 3 minutes throttling
	upsgi.emperor_max_throttle = 1000 * 180;
	upsgi.emperor_pid = -1;


	upsgi.cores = 1;
	upsgi.threads = 1;

	upsgi.need_app = 1;

	// default max number of rpc slot
	upsgi.rpc_max = 64;

	upsgi.offload_threads_events = 64;

	upsgi.default_app = -1;

	upsgi.buffer_size = 4096;
	upsgi.body_read_warning = 8;
	upsgi.numproc = 1;

	upsgi.forkbomb_delay = 2;

	upsgi.async = 0;
	upsgi.async_warn_if_queue_full = 1;
	upsgi.listen_queue = 100;

	upsgi.cheaper_overload = 3;
	upsgi.cheaper_idle = 10;

	upsgi.log_master_bufsize = 8192;
	upsgi.log_drain_burst = 8;
	upsgi.log_queue_enabled = 1;
	upsgi.log_queue_records = 512;
	upsgi.log_queue_bytes = 512 * 1024;
	/* default-on request handling posture */
	upsgi.use_thunder_lock = 1;
	upsgi.body_scheduler = 1;
	upsgi.logger_queue.records_cap = upsgi.log_queue_records;
	upsgi.logger_queue.bytes_cap = upsgi.log_queue_bytes;
	upsgi.req_logger_queue.records_cap = upsgi.log_queue_records;
	upsgi.req_logger_queue.bytes_cap = upsgi.log_queue_bytes;

	upsgi.worker_reload_mercy = 60;

	upsgi.max_vars = MAX_VARS;
	upsgi.vec_size = 4 + 1 + (4 * MAX_VARS);

	upsgi.socket_timeout = 4;
	upsgi.logging_options.enabled = 1;

	// a workers hould be running for at least 10 seconds
	upsgi.min_worker_lifetime = 10;





	upsgi.shared->worker_log_pipe[0] = -1;
	upsgi.shared->worker_log_pipe[1] = -1;

	upsgi.shared->worker_req_log_pipe[0] = -1;
	upsgi.shared->worker_req_log_pipe[1] = -1;

	upsgi.req_log_fd = 2;

#ifdef UPSGI_SSL
	// 1 day of tolerance
	upsgi.subscriptions_sign_check_tolerance = 3600 * 24;
	upsgi.ssl_sessions_timeout = 300;
	upsgi.ssl_verify_depth = 1;
#endif

	upsgi.alarm_freq = 3;
	upsgi.alarm_msg_size = 8192;

	upsgi.exception_handler_msg_size = 65536;

	upsgi.multicast_ttl = 1;
	upsgi.multicast_loop = 1;

	// filling http status codes
	struct http_status_codes *http_sc;
        for (http_sc = hsc; http_sc->message != NULL; http_sc++) {
                http_sc->message_size = strlen(http_sc->message);
        }

	upsgi.empty = "";

#ifdef __linux__
	upsgi.cgroup_dir_mode = "0700";
#endif

	upsgi.wait_read_hook = upsgi_simple_wait_read_hook;
	upsgi.wait_write_hook = upsgi_simple_wait_write_hook;
	upsgi.wait_milliseconds_hook = upsgi_simple_wait_milliseconds_hook;
	upsgi.wait_read2_hook = upsgi_simple_wait_read2_hook;

	upsgi_websockets_init();
	
	// 1 MB default limit
	upsgi.chunked_input_limit = 1024*1024;

	// clear reforked status
	upsgi.master_is_reforked = 0;

	upsgi.master_fifo_fd = -1;
	upsgi_master_fifo_prepare();

	upsgi.notify_socket_fd = -1;


	upsgi.harakiri_graceful_signal = SIGTERM;
}

void upsgi_setup_reload() {

	char env_reload_buf[11];

	char *env_reloads = getenv("UPSGI_RELOADS");
	if (env_reloads) {
		//convert env value to int
		upsgi.reloads = atoi(env_reloads);
		upsgi.reloads++;
		//convert reloads to string
		int rlen = snprintf(env_reload_buf, 10, "%u", upsgi.reloads);
		if (rlen > 0 && rlen < 10) {
			env_reload_buf[rlen] = 0;
			if (setenv("UPSGI_RELOADS", env_reload_buf, 1)) {
				upsgi_error("setenv()");
			}
		}
		upsgi.is_a_reload = 1;
	}
	else {
		if (setenv("UPSGI_RELOADS", "0", 1)) {
			upsgi_error("setenv()");
		}
	}

}

void upsgi_autoload_plugins_by_name(char *argv_zero) {

	char *plugins_requested = NULL;

	char *original_proc_name = getenv("UPSGI_ORIGINAL_PROC_NAME");
	if (!original_proc_name) {
		// here we use argv[0];
		original_proc_name = argv_zero;
		setenv("UPSGI_ORIGINAL_PROC_NAME", original_proc_name, 1);
	}
	char *p = strrchr(original_proc_name, '/');
	if (p == NULL)
		p = original_proc_name;
	p = strstr(p, "upsgi_");
	if (p != NULL) {
		char *ctx = NULL;
		upsgi_foreach_token(upsgi_str(p + 6), "_", plugins_requested, ctx) {
			upsgi_log("[upsgi] implicit plugin requested %s\n", plugins_requested);
			upsgi_load_plugin(-1, plugins_requested, NULL);
		}
	}

	plugins_requested = getenv("UPSGI_PLUGINS");
	if (plugins_requested) {
		plugins_requested = upsgi_concat2(plugins_requested, "");
		char *p, *ctx = NULL;
		upsgi_foreach_token(plugins_requested, ",", p, ctx) {
			upsgi_load_plugin(-1, p, NULL);
		}
	}

}

void upsgi_commandline_config() {
	int i;

	upsgi.option_index = -1;
	// required in case we want to call getopt_long from the beginning
	optind = 0;

	int argc = upsgi.argc;
	char **argv = upsgi.argv;

	// we might want to ignore some arguments not meant for us
	char binary_argv0_pretty[256] = {'\0'};
	char *binary_argv0_actual = NULL;

	if (upsgi.new_argc > -1 && upsgi.new_argv) {
		argc = upsgi.new_argc;
		argv = upsgi.new_argv;
	}

	if (upsgi.binary_argc > 1 && argc >= upsgi.binary_argc) {
		char *pretty = (char *)binary_argv0_pretty;
		strncat(pretty, argv[0], 255);

		for (i = 1; i < upsgi.binary_argc; i++) {
			if (strlen(pretty) + 1 + strlen(argv[i]) + 1 > 256)
				break;

			strcat(pretty, " ");
			strcat(pretty, argv[i]);
		}

		argc -= upsgi.binary_argc - 1;
		argv += upsgi.binary_argc - 1;

		binary_argv0_actual = argv[0];
		argv[0] = (char *)binary_argv0_pretty;
	}

	char *optname;
	while ((i = getopt_long(argc, argv, upsgi.short_options, upsgi.long_options, &upsgi.option_index)) != -1) {

		if (i == '?') {
			upsgi_log("getopt_long() error\n");
			exit(1);
		}

		if (upsgi.option_index > -1) {
			optname = (char *) upsgi.long_options[upsgi.option_index].name;
		}
		else {
			optname = upsgi_get_optname_by_index(i);
		}
		if (!optname) {
			upsgi_log("unable to parse command line options\n");
			exit(1);
		}
		upsgi.option_index = -1;
		add_exported_option(optname, optarg, 0);
	}

	if (binary_argv0_actual != NULL)
		argv[0] = binary_argv0_actual;

#ifdef UPSGI_DEBUG
	upsgi_log("optind:%d argc:%d\n", optind, upsgi.argc);
#endif

	if (optind < argc) {
		for (i = optind; i < argc; i++) {
			char *lazy = argv[i];
			if (lazy[0] != '[') {
				upsgi_opt_load(NULL, lazy, NULL);
				// manage magic mountpoint
				int magic = 0;
				int j;
				for (j = 0; j < upsgi.gp_cnt; j++) {
					if (upsgi.gp[j]->magic) {
						if (upsgi.gp[j]->magic(NULL, lazy)) {
							magic = 1;
							break;
						}
					}
				}
				if (!magic) {
					for (j = 0; j < 256; j++) {
						if (upsgi.p[j]->magic) {
							if (upsgi.p[j]->magic(NULL, lazy)) {
								magic = 1;
								break;
							}
						}
					}
				}
			}
		}
	}

}

void upsgi_setup_workers() {
	int i, j;
	// allocate shared memory for workers + master
	upsgi.workers = (struct upsgi_worker *) upsgi_calloc_shared(sizeof(struct upsgi_worker) * (upsgi.numproc + 1));

	for (i = 0; i <= upsgi.numproc; i++) {
		// allocate memory for apps
		upsgi.workers[i].apps = (struct upsgi_app *) upsgi_calloc_shared(sizeof(struct upsgi_app) * upsgi.max_apps);

		// allocate memory for cores
		upsgi.workers[i].cores = (struct upsgi_core *) upsgi_calloc_shared(sizeof(struct upsgi_core) * upsgi.cores);

		// this is a trick for avoiding too much memory areas
		void *ts = upsgi_calloc_shared(sizeof(void *) * upsgi.max_apps * upsgi.cores);
		// add 4 bytes for upsgi header
		void *buffers = upsgi_malloc_shared((upsgi.buffer_size+4) * upsgi.cores);
		void *hvec = upsgi_malloc_shared(sizeof(struct iovec) * upsgi.vec_size * upsgi.cores);
		void *post_buf = NULL;
		if (upsgi.post_buffering > 0)
			post_buf = upsgi_malloc_shared(upsgi.post_buffering_bufsize * upsgi.cores);


		for (j = 0; j < upsgi.cores; j++) {
			// allocate shared memory for thread states (required for some language, like python)
			upsgi.workers[i].cores[j].ts = ts + ((sizeof(void *) * upsgi.max_apps) * j);
			// raw per-request buffer (+4 bytes for upsgi header)
			upsgi.workers[i].cores[j].buffer = buffers + ((upsgi.buffer_size+4) * j);
			// iovec for upsgi vars
			upsgi.workers[i].cores[j].hvec = hvec + ((sizeof(struct iovec) * upsgi.vec_size) * j);
			if (post_buf)
				upsgi.workers[i].cores[j].post_buf = post_buf + (upsgi.post_buffering_bufsize * j);
		}

		// master does not need to following steps...
		if (i == 0)
			continue;
		upsgi.workers[i].signal_pipe[0] = -1;
		upsgi.workers[i].signal_pipe[1] = -1;
		snprintf(upsgi.workers[i].name, 0xff, "upsgi worker %d", i);
	}

	uint64_t total_memory = (sizeof(struct upsgi_app) * upsgi.max_apps) + (sizeof(struct upsgi_core) * upsgi.cores) + (sizeof(void *) * upsgi.max_apps * upsgi.cores) + (upsgi.buffer_size * upsgi.cores) + (sizeof(struct iovec) * upsgi.vec_size * upsgi.cores);
	if (upsgi.post_buffering > 0) {
		total_memory += (upsgi.post_buffering_bufsize * upsgi.cores);
	}

	total_memory *= (upsgi.numproc + upsgi.master_process);
	if (upsgi.numproc > 0)
		upsgi_log("mapped %llu bytes (%llu KB) for %d cores\n", (unsigned long long) total_memory, (unsigned long long) (total_memory / 1024), upsgi.cores * upsgi.numproc);

	// allocate signal table
        upsgi.shared->signal_table = upsgi_calloc_shared(sizeof(struct upsgi_signal_entry) * 256 * (upsgi.numproc + 1));

#ifdef UPSGI_ROUTING
	upsgi_fixup_routes(upsgi.routes);
	upsgi_fixup_routes(upsgi.error_routes);
	upsgi_fixup_routes(upsgi.response_routes);
	upsgi_fixup_routes(upsgi.final_routes);
#endif

}

pid_t upsgi_daemonize2() {
	if (upsgi.has_emperor) {
		logto(upsgi.daemonize2);
	}
	else {
		if (!upsgi.is_a_reload) {
			upsgi_log("*** daemonizing upsgi ***\n");
			daemonize(upsgi.daemonize2);
		}
		else if (upsgi.log_reopen) {
			logto(upsgi.daemonize2);
		}
	}
	upsgi.mypid = getpid();

	upsgi.workers[0].pid = upsgi.mypid;

	if (upsgi.pidfile && !upsgi.is_a_reload) {
		upsgi_write_pidfile(upsgi.pidfile);
	}

	if (upsgi.pidfile2 && !upsgi.is_a_reload) {
		upsgi_write_pidfile(upsgi.pidfile2);
	}

	if (upsgi.log_master) 
		upsgi_setup_log_master();

	return upsgi.mypid;
}

// fix/check related options
void sanitize_args() {

        if (upsgi.async > 0) {
                upsgi.cores = upsgi.async;
        }

        upsgi.has_threads = 1;
        if (upsgi.threads > 1) {
                upsgi.cores = upsgi.threads;
        }

        if (upsgi.harakiri_options.workers > 0) {
                if (!upsgi.post_buffering) {
                        upsgi_log(" *** WARNING: you have enabled harakiri without post buffering. Slow upload could be rejected on post-unbuffered webservers *** \n");
                }
        }

        if (upsgi.write_errors_exception_only) {
                upsgi.ignore_sigpipe = 1;
                upsgi.ignore_write_errors = 1;
        }

        if (upsgi.cheaper_count == 0) upsgi.cheaper = 0;

        if (upsgi.cheaper_count > 0 && upsgi.cheaper_count >= upsgi.numproc) {
                upsgi_log("invalid cheaper value: must be lower than processes\n");
                exit(1);
        }

        if (upsgi.cheaper && upsgi.cheaper_count) {
		if (upsgi.cheaper_initial) {
                	if (upsgi.cheaper_initial < upsgi.cheaper_count) {
                        	upsgi_log("warning: invalid cheaper-initial value (%d), must be equal or higher than cheaper (%d), using %d as initial number of workers\n",
                                	upsgi.cheaper_initial, upsgi.cheaper_count, upsgi.cheaper_count);
                        	upsgi.cheaper_initial = upsgi.cheaper_count;
                	}
                	else if (upsgi.cheaper_initial > upsgi.numproc) {
                        	upsgi_log("warning: invalid cheaper-initial value (%d), must be lower or equal than worker count (%d), using %d as initial number of workers\n",
                                	upsgi.cheaper_initial, upsgi.numproc, upsgi.numproc);
                        	upsgi.cheaper_initial = upsgi.numproc;
                	}
		}
		else {
                        upsgi.cheaper_initial = upsgi.cheaper_count;
		}
        }

	if (upsgi.max_worker_lifetime > 0 && upsgi.min_worker_lifetime >= upsgi.max_worker_lifetime) {
		upsgi_log("invalid min-worker-lifetime value (%d), must be lower than max-worker-lifetime (%d)\n",
			upsgi.min_worker_lifetime, upsgi.max_worker_lifetime);
		exit(1);
	}

	if (upsgi.cheaper_rss_limit_soft && !upsgi.logging_options.memory_report && upsgi.force_get_memusage != 1) {
		upsgi_log("enabling cheaper-rss-limit-soft requires enabling also memory-report\n");
		exit(1);
	}
	if (upsgi.cheaper_rss_limit_hard && !upsgi.cheaper_rss_limit_soft) {
		upsgi_log("enabling cheaper-rss-limit-hard requires setting also cheaper-rss-limit-soft\n");
		exit(1);
	}
	if ( upsgi.cheaper_rss_limit_hard && upsgi.cheaper_rss_limit_hard <= upsgi.cheaper_rss_limit_soft) {
		upsgi_log("cheaper-rss-limit-hard value must be higher than cheaper-rss-limit-soft value\n");
		exit(1);
	}

	if (upsgi.evil_reload_on_rss || upsgi.evil_reload_on_as) {
		if (!upsgi.mem_collector_freq) upsgi.mem_collector_freq = 3;
	}

	/* thunder lock defaults are now handled in upsgi_init_default();
	   explicit disable options must remain authoritative. */

}

const char *upsgi_http_status_msg(char *status, uint16_t *len) {
	struct http_status_codes *http_sc;
	for (http_sc = hsc; http_sc->message != NULL; http_sc++) {
                if (!strncmp(http_sc->key, status, 3)) {
                        *len = http_sc->message_size;
			return http_sc->message;
                }
        }
	return NULL;
}
