#include "upsgi.h"
void upsgi_validate_runtime_tunables(void);

struct upsgi_server upsgi;
pid_t masterpid;

#if defined(__APPLE__) && defined(UPSGI_AS_SHARED_LIBRARY)
#include <crt_externs.h>
char **environ;
#else
extern char **environ;
#endif

UPSGI_DECLARE_EMBEDDED_PLUGINS;

/*
 * upsgi keeps the shared upstream option table, but classifies a narrower
 * v1 product surface in-place:
 * - baseline supported: PSGI + HTTP socket + static-map + first-class logging
 * - advanced/reference: retained shared-core controls that still matter in v1
 * - compatibility-only: legacy flags that must still parse but stay runtime inert
 *
 * The comments below mark the upsgi-specific support boundary without
 * rewriting the whole upstream option catalog in one pass.
 */
static struct upsgi_option upsgi_base_options[] = {
	{"socket", required_argument, 's', "bind to the specified UNIX/TCP socket using default protocol", upsgi_opt_add_socket, NULL, 0},
	{"upsgi-socket", required_argument, 's', "bind to the specified UNIX/TCP socket using upsgi protocol", upsgi_opt_add_socket, "upsgi", 0},
#ifdef UPSGI_SSL
	{"supsgi-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using upsgi protocol over SSL", upsgi_opt_add_ssl_socket, "supsgi", 0},
	{"ssl-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using upsgi protocol over SSL", upsgi_opt_add_ssl_socket, "supsgi", 0},
#endif

	/*
	 * upsgi baseline front-door transport:
	 * - http-socket is first-class
	 * - legacy HTTP modifier flags remain parse-only compatibility shims
	 * - all upsgi compatibility-only flags route through upsgi_opt_compat_noop()
	 */
	{"http-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using HTTP protocol", upsgi_opt_add_socket, "http", 0},
	{"http-socket-modifier1", required_argument, 0, "accepted for migration compatibility; no runtime effect in upsgi", upsgi_opt_compat_noop, NULL, 0},
	{"http-socket-modifier2", required_argument, 0, "accepted for migration compatibility; no runtime effect in upsgi", upsgi_opt_compat_noop, NULL, 0},
	{"http-modifier1", required_argument, 0, "accepted for migration compatibility; no runtime effect in upsgi", upsgi_opt_compat_noop, NULL, 0},
	{"http-modifier2", required_argument, 0, "accepted for migration compatibility; no runtime effect in upsgi", upsgi_opt_compat_noop, NULL, 0},

	{"http11-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using HTTP 1.1 (Keep-Alive) protocol", upsgi_opt_add_socket, "http11", 0},

	{"http-path-info-no-decode-slashes", required_argument, 0, "don't URL decode encoded slashes in PATH_INFO when using HTTP(S) protocol", upsgi_opt_true, &upsgi.http_path_info_no_decode_slashes, 0},

#ifdef UPSGI_SSL
	{"https-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using HTTPS protocol", upsgi_opt_add_ssl_socket, "https", 0},
	{"https-socket-modifier1", required_argument, 0, "accepted for migration compatibility; no runtime effect in upsgi", upsgi_opt_compat_noop, NULL, 0},
	{"https-socket-modifier2", required_argument, 0, "accepted for migration compatibility; no runtime effect in upsgi", upsgi_opt_compat_noop, NULL, 0},
#endif

	/*
	 * The protocol-specific surfaces below are still part of the shared core,
	 * but they are not part of the primary upsgi v1 baseline. They remain in
	 * place as advanced/reference or deferred upstream breadth until a later
	 * deeper trim.
	 */
	{"fastcgi-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using FastCGI protocol", upsgi_opt_add_socket, "fastcgi", 0},
	{"fastcgi-nph-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using FastCGI protocol (nph mode)", upsgi_opt_add_socket, "fastcgi-nph", 0},
	{"fastcgi-modifier1", required_argument, 0, "force the specified modifier1 when using FastCGI protocol", upsgi_opt_set_64bit, &upsgi.fastcgi_modifier1, 0},
	{"fastcgi-modifier2", required_argument, 0, "force the specified modifier2 when using FastCGI protocol", upsgi_opt_set_64bit, &upsgi.fastcgi_modifier2, 0},

	{"scgi-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using SCGI protocol", upsgi_opt_add_socket, "scgi", 0},
	{"scgi-nph-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using SCGI protocol (nph mode)", upsgi_opt_add_socket, "scgi-nph", 0},
	{"scgi-modifier1", required_argument, 0, "force the specified modifier1 when using SCGI protocol", upsgi_opt_set_64bit, &upsgi.scgi_modifier1, 0},
	{"scgi-modifier2", required_argument, 0, "force the specified modifier2 when using SCGI protocol", upsgi_opt_set_64bit, &upsgi.scgi_modifier2, 0},

	{"raw-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using RAW protocol", upsgi_opt_add_socket_no_defer, "raw", 0},
	{"raw-modifier1", required_argument, 0, "force the specified modifier1 when using RAW protocol", upsgi_opt_set_64bit, &upsgi.raw_modifier1, 0},
	{"raw-modifier2", required_argument, 0, "force the specified modifier2 when using RAW protocol", upsgi_opt_set_64bit, &upsgi.raw_modifier2, 0},

	{"pupsgi-socket", required_argument, 0, "bind to the specified UNIX/TCP socket using persistent upsgi protocol (pupsgi)", upsgi_opt_add_socket, "pupsgi", 0},

	{"protocol", required_argument, 0, "force the specified protocol for default sockets", upsgi_opt_set_str, &upsgi.protocol, 0},
	{"socket-protocol", required_argument, 0, "force the specified protocol for default sockets", upsgi_opt_set_str, &upsgi.protocol, 0},
	{"shared-socket", required_argument, 0, "create a shared socket for advanced jailing or ipc", upsgi_opt_add_shared_socket, NULL, 0},
	{"undeferred-shared-socket", required_argument, 0, "create a shared socket for advanced jailing or ipc (undeferred mode)", upsgi_opt_add_shared_socket, NULL, 0},
	{"processes", required_argument, 'p', "spawn the specified number of workers/processes", upsgi_opt_set_int, &upsgi.numproc, 0},
	{"workers", required_argument, 'p', "spawn the specified number of workers/processes", upsgi_opt_set_int, &upsgi.numproc, 0},
	{"thunder-lock", no_argument, 0, "enable shared-listener accept serialization (enabled by default)", upsgi_opt_true, &upsgi.use_thunder_lock, 0},
	{"disable-thunder-lock", no_argument, 0, "disable shared-listener accept serialization", upsgi_opt_false, &upsgi.use_thunder_lock, 0},
	{"thunder-lock-watchdog", no_argument, 0, "diagnostic-only watchdog flag for thunder-lock troubleshooting", upsgi_opt_true, &upsgi.use_thunder_lock_watchdog, 0},
	{"thunder-lock-backend", required_argument, 0, "set thunder-lock backend: auto|fdlock", upsgi_opt_set_str, &upsgi.thunder_lock_backend_request, 0},
	{"harakiri", required_argument, 't', "set harakiri timeout", upsgi_opt_set_int, &upsgi.harakiri_options.workers, 0},
	{"harakiri-verbose", no_argument, 0, "enable verbose mode for harakiri", upsgi_opt_true, &upsgi.harakiri_verbose, 0},
	{"harakiri-graceful-timeout", required_argument, 0, "interval between graceful harakiri attempt and a sigkill", upsgi_opt_set_int, &upsgi.harakiri_graceful_timeout, 0},
	{"harakiri-graceful-signal", required_argument, 0, "use this signal instead of sigterm for graceful harakiri attempts", upsgi_opt_set_int, &upsgi.harakiri_graceful_signal, 0},
	{"harakiri-queue-threshold", required_argument, 0, "only trigger harakiri if queue is greater than this threshold", upsgi_opt_set_int, &upsgi.harakiri_queue_threshold, 0},
	{"harakiri-no-arh", no_argument, 0, "do not enable harakiri during after-request-hook", upsgi_opt_true, &upsgi.harakiri_no_arh, 0},
	{"no-harakiri-arh", no_argument, 0, "do not enable harakiri during after-request-hook", upsgi_opt_true, &upsgi.harakiri_no_arh, 0},
	{"no-harakiri-after-req-hook", no_argument, 0, "do not enable harakiri during after-request-hook", upsgi_opt_true, &upsgi.harakiri_no_arh, 0},
	{"backtrace-depth", required_argument, 0, "set backtrace depth", upsgi_opt_set_int, &upsgi.backtrace_depth, 0},
#ifdef UPSGI_XML
	{"xmlconfig", required_argument, 'x', "load config from xml file", upsgi_opt_load_xml, NULL, UPSGI_OPT_IMMEDIATE},
	{"xml", required_argument, 'x', "load config from xml file", upsgi_opt_load_xml, NULL, UPSGI_OPT_IMMEDIATE},
#endif
	{"config", required_argument, 0, "load configuration from a supported file type", upsgi_opt_load, NULL, UPSGI_OPT_IMMEDIATE},
	{"fallback-config", required_argument, 0, "re-exec upsgi with the specified config when exit code is 1", upsgi_opt_set_str, &upsgi.fallback_config, UPSGI_OPT_IMMEDIATE},
	{"strict", no_argument, 0, "enable strict mode (placeholder cannot be used)", upsgi_opt_true, &upsgi.strict, UPSGI_OPT_IMMEDIATE},

	{"skip-zero", no_argument, 0, "skip check of file descriptor 0", upsgi_opt_true, &upsgi.skip_zero, 0},
	{"skip-atexit", no_argument, 0, "skip atexit hooks (ignored by the master)", upsgi_opt_true, &upsgi.skip_atexit, 0},
	{"skip-atexit-teardown", no_argument, 0, "skip atexit teardown (ignored by the master)", upsgi_opt_true, &upsgi.skip_atexit_teardown, 0},

	{"set", required_argument, 'S', "set a placeholder or an option", upsgi_opt_set_placeholder, NULL, UPSGI_OPT_IMMEDIATE},
	{"set-placeholder", required_argument, 0, "set a placeholder", upsgi_opt_set_placeholder, (void *) 1, UPSGI_OPT_IMMEDIATE},
	{"set-ph", required_argument, 0, "set a placeholder", upsgi_opt_set_placeholder, (void *) 1, UPSGI_OPT_IMMEDIATE},
	{"get", required_argument, 0, "print the specified option value and exit", upsgi_opt_add_string_list, &upsgi.get_list, UPSGI_OPT_NO_INITIAL},
	{"declare-option", required_argument, 0, "declare a new upsgi custom option", upsgi_opt_add_custom_option, NULL, UPSGI_OPT_IMMEDIATE},
	{"declare-option2", required_argument, 0, "declare a new upsgi custom option (non-immediate)", upsgi_opt_add_custom_option, NULL, 0},

	{"resolve", required_argument, 0, "place the result of a dns query in the specified placeholder, syntax: placeholder=name (immediate option)", upsgi_opt_resolve, NULL, UPSGI_OPT_IMMEDIATE},

	{"for", required_argument, 0, "(opt logic) for cycle", upsgi_opt_logic, (void *) upsgi_logic_opt_for, UPSGI_OPT_IMMEDIATE},
	{"for-glob", required_argument, 0, "(opt logic) for cycle (expand glob)", upsgi_opt_logic, (void *) upsgi_logic_opt_for_glob, UPSGI_OPT_IMMEDIATE},
	{"for-times", required_argument, 0, "(opt logic) for cycle (expand the specified num to a list starting from 1)", upsgi_opt_logic, (void *) upsgi_logic_opt_for_times, UPSGI_OPT_IMMEDIATE},
	{"for-readline", required_argument, 0, "(opt logic) for cycle (expand the specified file to a list of lines)", upsgi_opt_logic, (void *) upsgi_logic_opt_for_readline, UPSGI_OPT_IMMEDIATE},
	{"endfor", optional_argument, 0, "(opt logic) end for cycle", upsgi_opt_noop, NULL, UPSGI_OPT_IMMEDIATE},
	{"end-for", optional_argument, 0, "(opt logic) end for cycle", upsgi_opt_noop, NULL, UPSGI_OPT_IMMEDIATE},

	{"if-opt", required_argument, 0, "(opt logic) check for option", upsgi_opt_logic, (void *) upsgi_logic_opt_if_opt, UPSGI_OPT_IMMEDIATE},
	{"if-not-opt", required_argument, 0, "(opt logic) check for option", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_opt, UPSGI_OPT_IMMEDIATE},

	{"if-env", required_argument, 0, "(opt logic) check for environment variable", upsgi_opt_logic, (void *) upsgi_logic_opt_if_env, UPSGI_OPT_IMMEDIATE},
	{"if-not-env", required_argument, 0, "(opt logic) check for environment variable", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_env, UPSGI_OPT_IMMEDIATE},
	{"ifenv", required_argument, 0, "(opt logic) check for environment variable", upsgi_opt_logic, (void *) upsgi_logic_opt_if_env, UPSGI_OPT_IMMEDIATE},

	{"if-reload", no_argument, 0, "(opt logic) check for reload", upsgi_opt_logic, (void *) upsgi_logic_opt_if_reload, UPSGI_OPT_IMMEDIATE},
	{"if-not-reload", no_argument, 0, "(opt logic) check for reload", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_reload, UPSGI_OPT_IMMEDIATE},

	{"if-hostname", required_argument, 0, "(opt logic) check for hostname", upsgi_opt_logic, (void *) upsgi_logic_opt_if_hostname, UPSGI_OPT_IMMEDIATE},
	{"if-not-hostname", required_argument, 0, "(opt logic) check for hostname", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_hostname, UPSGI_OPT_IMMEDIATE},

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	{"if-hostname-match", required_argument, 0, "(opt logic) try to match hostname against a regular expression", upsgi_opt_logic, (void *) upsgi_logic_opt_if_hostname_match, UPSGI_OPT_IMMEDIATE},
	{"if-not-hostname-match", required_argument, 0, "(opt logic) try to match hostname against a regular expression", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_hostname_match, UPSGI_OPT_IMMEDIATE},
#endif

	{"if-exists", required_argument, 0, "(opt logic) check for file/directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_exists, UPSGI_OPT_IMMEDIATE},
	{"if-not-exists", required_argument, 0, "(opt logic) check for file/directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_exists, UPSGI_OPT_IMMEDIATE},
	{"ifexists", required_argument, 0, "(opt logic) check for file/directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_exists, UPSGI_OPT_IMMEDIATE},

	{"if-plugin", required_argument, 0, "(opt logic) check for plugin", upsgi_opt_logic, (void *) upsgi_logic_opt_if_plugin, UPSGI_OPT_IMMEDIATE},
	{"if-not-plugin", required_argument, 0, "(opt logic) check for plugin", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_plugin, UPSGI_OPT_IMMEDIATE},
	{"ifplugin", required_argument, 0, "(opt logic) check for plugin", upsgi_opt_logic, (void *) upsgi_logic_opt_if_plugin, UPSGI_OPT_IMMEDIATE},

	{"if-file", required_argument, 0, "(opt logic) check for file existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_file, UPSGI_OPT_IMMEDIATE},
	{"if-not-file", required_argument, 0, "(opt logic) check for file existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_file, UPSGI_OPT_IMMEDIATE},
	{"if-dir", required_argument, 0, "(opt logic) check for directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_dir, UPSGI_OPT_IMMEDIATE},
	{"if-not-dir", required_argument, 0, "(opt logic) check for directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_not_dir, UPSGI_OPT_IMMEDIATE},

	{"ifdir", required_argument, 0, "(opt logic) check for directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_dir, UPSGI_OPT_IMMEDIATE},
	{"if-directory", required_argument, 0, "(opt logic) check for directory existence", upsgi_opt_logic, (void *) upsgi_logic_opt_if_dir, UPSGI_OPT_IMMEDIATE},

	{"endif", optional_argument, 0, "(opt logic) end if", upsgi_opt_noop, NULL, UPSGI_OPT_IMMEDIATE},
	{"end-if", optional_argument, 0, "(opt logic) end if", upsgi_opt_noop, NULL, UPSGI_OPT_IMMEDIATE},

	{"else", required_argument, 0, "(opt logic) else after condition", upsgi_opt_logic, (void *) upsgi_logic_opt_else, UPSGI_OPT_IMMEDIATE},

	{"blacklist", required_argument, 0, "set options blacklist context", upsgi_opt_set_str, &upsgi.blacklist_context, UPSGI_OPT_IMMEDIATE},
	{"end-blacklist", no_argument, 0, "clear options blacklist context", upsgi_opt_set_null, &upsgi.blacklist_context, UPSGI_OPT_IMMEDIATE},

	{"whitelist", required_argument, 0, "set options whitelist context", upsgi_opt_set_str, &upsgi.whitelist_context, UPSGI_OPT_IMMEDIATE},
	{"end-whitelist", no_argument, 0, "clear options whitelist context", upsgi_opt_set_null, &upsgi.whitelist_context, UPSGI_OPT_IMMEDIATE},

	{"ignore-sigpipe", no_argument, 0, "do not report (annoying) SIGPIPE", upsgi_opt_true, &upsgi.ignore_sigpipe, 0},
	{"ignore-write-errors", no_argument, 0, "do not report (annoying) write()/writev() errors", upsgi_opt_true, &upsgi.ignore_write_errors, 0},
	{"write-errors-tolerance", required_argument, 0, "set the maximum number of allowed write errors (default: no tolerance)", upsgi_opt_set_64bit, &upsgi.write_errors_tolerance, 0},
	{"write-errors-exception-only", no_argument, 0, "only raise an exception on write errors giving control to the app itself", upsgi_opt_true, &upsgi.write_errors_exception_only, 0},
	{"disable-write-exception", no_argument, 0, "disable exception generation on write()/writev()", upsgi_opt_true, &upsgi.disable_write_exception, 0},

	{"inherit", required_argument, 0, "use the specified file as config template", upsgi_opt_load, NULL, 0},
	{"include", required_argument, 0, "include the specified file as immediate configuration", upsgi_opt_load, NULL, UPSGI_OPT_IMMEDIATE},
	{"inject-before", required_argument, 0, "inject a text file before the config file (advanced templating)", upsgi_opt_add_string_list, &upsgi.inject_before, UPSGI_OPT_IMMEDIATE},
	{"inject-after", required_argument, 0, "inject a text file after the config file (advanced templating)", upsgi_opt_add_string_list, &upsgi.inject_after, UPSGI_OPT_IMMEDIATE},
	{"daemonize", required_argument, 'd', "daemonize upsgi", upsgi_opt_set_str, &upsgi.daemonize, 0},
	{"daemonize2", required_argument, 0, "daemonize upsgi after app loading", upsgi_opt_set_str, &upsgi.daemonize2, 0},
	{"stop", required_argument, 0, "stop an instance", upsgi_opt_pidfile_signal, (void *) SIGINT, UPSGI_OPT_IMMEDIATE},
	{"reload", required_argument, 0, "reload an instance", upsgi_opt_pidfile_signal, (void *) SIGHUP, UPSGI_OPT_IMMEDIATE},
	{"pause", required_argument, 0, "pause an instance", upsgi_opt_pidfile_signal, (void *) SIGTSTP, UPSGI_OPT_IMMEDIATE},
	{"suspend", required_argument, 0, "suspend an instance", upsgi_opt_pidfile_signal, (void *) SIGTSTP, UPSGI_OPT_IMMEDIATE},
	{"resume", required_argument, 0, "resume an instance", upsgi_opt_pidfile_signal, (void *) SIGTSTP, UPSGI_OPT_IMMEDIATE},

	{"connect-and-read", required_argument, 0, "connect to a socket and wait for data from it", upsgi_opt_connect_and_read, NULL, UPSGI_OPT_IMMEDIATE},
	{"extract", required_argument, 0, "fetch/dump any supported address to stdout", upsgi_opt_extract, NULL, UPSGI_OPT_IMMEDIATE},

	{"listen", required_argument, 'l', "set the socket listen queue size", upsgi_opt_set_int, &upsgi.listen_queue, UPSGI_OPT_IMMEDIATE},
	{"max-vars", required_argument, 'v', "set the amount of internal iovec/vars structures", upsgi_opt_max_vars, NULL, 0},
	{"max-apps", required_argument, 0, "set the maximum number of per-worker applications", upsgi_opt_set_int, &upsgi.max_apps, 0},
	{"buffer-size", required_argument, 'b', "set internal buffer size", upsgi_opt_set_64bit, &upsgi.buffer_size, 0},
	{"memory-report", optional_argument, 'm', "enable memory report. 1 for basic (default), 2 for uss/pss (Linux only)", upsgi_opt_set_int, &upsgi.logging_options.memory_report, 0},
	{"profiler", required_argument, 0, "enable the specified profiler", upsgi_opt_set_str, &upsgi.profiler, 0},
	{"cgi-mode", no_argument, 'c', "force CGI-mode for plugins supporting it", upsgi_opt_true, &upsgi.cgi_mode, 0},
	{"abstract-socket", no_argument, 'a', "force UNIX socket in abstract mode (Linux only)", upsgi_opt_true, &upsgi.abstract_socket, 0},
	{"chmod-socket", optional_argument, 'C', "chmod-socket", upsgi_opt_chmod_socket, NULL, 0},
	{"chmod", optional_argument, 'C', "chmod-socket", upsgi_opt_chmod_socket, NULL, 0},
	{"chown-socket", required_argument, 0, "chown unix sockets", upsgi_opt_set_str, &upsgi.chown_socket, 0},
	{"umask", required_argument, 0, "set umask", upsgi_opt_set_umask, NULL, UPSGI_OPT_IMMEDIATE},
#ifdef __linux__
	{"freebind", no_argument, 0, "put socket in freebind mode", upsgi_opt_true, &upsgi.freebind, 0},
#endif
	{"map-socket", required_argument, 0, "map sockets to specific workers", upsgi_opt_add_string_list, &upsgi.map_socket, 0},
	{"enable-threads", no_argument, 'T', "enable threads (stub option this is true by default)", upsgi_opt_true, &upsgi.has_threads, 0},
	{"no-threads-wait", no_argument, 0, "do not wait for threads cancellation on quit/reload", upsgi_opt_true, &upsgi.no_threads_wait, 0},

	{"auto-procname", no_argument, 0, "automatically set processes name to something meaningful", upsgi_opt_true, &upsgi.auto_procname, 0},
	{"procname-prefix", required_argument, 0, "add a prefix to the process names", upsgi_opt_set_str, &upsgi.procname_prefix, UPSGI_OPT_PROCNAME},
	{"procname-prefix-spaced", required_argument, 0, "add a spaced prefix to the process names", upsgi_opt_set_str_spaced, &upsgi.procname_prefix, UPSGI_OPT_PROCNAME},
	{"procname-append", required_argument, 0, "append a string to process names", upsgi_opt_set_str, &upsgi.procname_append, UPSGI_OPT_PROCNAME},
	{"procname", required_argument, 0, "set process names", upsgi_opt_set_str, &upsgi.procname, UPSGI_OPT_PROCNAME},
	{"procname-master", required_argument, 0, "set master process name", upsgi_opt_set_str, &upsgi.procname_master, UPSGI_OPT_PROCNAME},

	{"single-interpreter", no_argument, 'i', "do not use multiple interpreters (where available)", upsgi_opt_true, &upsgi.single_interpreter, 0},
	{"need-app", optional_argument, 0, "exit if no app can be loaded", upsgi_opt_true, &upsgi.need_app, 0},
	{"dynamic-apps", no_argument, 0, "allows apps to be dynamically loaded via upsgi protocol", upsgi_opt_true, &upsgi.dynamic_apps, 0},
	{"master", no_argument, 'M', "enable master process", upsgi_opt_true, &upsgi.master_process, 0},
	{"honour-stdin", no_argument, 0, "do not remap stdin to /dev/null", upsgi_opt_true, &upsgi.honour_stdin, 0},
	{"emperor", required_argument, 0, "run the Emperor", upsgi_opt_add_string_list, &upsgi.emperor, 0},
	{"emperor-proxy-socket", required_argument, 0, "force the vassal to became an Emperor proxy", upsgi_opt_set_str, &upsgi.emperor_proxy, 0},
	{"emperor-wrapper", required_argument, 0, "set a binary wrapper for vassals", upsgi_opt_set_str, &upsgi.emperor_wrapper, 0},
	{"emperor-wrapper-override", required_argument, 0, "set a binary wrapper for vassals to try before the default one", upsgi_opt_add_string_list, &upsgi.emperor_wrapper_override, 0},
	{"emperor-wrapper-fallback", required_argument, 0, "set a binary wrapper for vassals to try as a last resort", upsgi_opt_add_string_list, &upsgi.emperor_wrapper_fallback, 0},
	{"emperor-nofollow", no_argument, 0, "do not follow symlinks when checking for mtime", upsgi_opt_true, &upsgi.emperor_nofollow, 0},
	{"emperor-procname", required_argument, 0, "set the Emperor process name", upsgi_opt_set_str, &upsgi.emperor_procname, 0},
	{"emperor-freq", required_argument, 0, "set the Emperor scan frequency (default 3 seconds)", upsgi_opt_set_int, &upsgi.emperor_freq, 0},
	{"emperor-required-heartbeat", required_argument, 0, "set the Emperor tolerance about heartbeats", upsgi_opt_set_int, &upsgi.emperor_heartbeat, 0},
	{"emperor-curse-tolerance", required_argument, 0, "set the Emperor tolerance about cursed vassals", upsgi_opt_set_int, &upsgi.emperor_curse_tolerance, 0},
	{"emperor-pidfile", required_argument, 0, "write the Emperor pid in the specified file", upsgi_opt_set_str, &upsgi.emperor_pidfile, 0},
	{"emperor-tyrant", no_argument, 0, "put the Emperor in Tyrant mode", upsgi_opt_true, &upsgi.emperor_tyrant, 0},
	{"emperor-tyrant-nofollow", no_argument, 0, "do not follow symlinks when checking for uid/gid in Tyrant mode", upsgi_opt_true, &upsgi.emperor_tyrant_nofollow, 0},
	{"emperor-tyrant-initgroups", no_argument, 0, "add additional groups set via initgroups() in Tyrant mode", upsgi_opt_true, &upsgi.emperor_tyrant_initgroups, 0},
	{"emperor-stats", required_argument, 0, "run the Emperor stats server", upsgi_opt_set_str, &upsgi.emperor_stats, 0},
	{"emperor-stats-server", required_argument, 0, "run the Emperor stats server", upsgi_opt_set_str, &upsgi.emperor_stats, 0},
	{"emperor-trigger-socket", required_argument, 0, "enable the Emperor trigger socket", upsgi_opt_set_str, &upsgi.emperor_trigger_socket, 0},

	{"early-emperor", no_argument, 0, "spawn the emperor as soon as possible", upsgi_opt_true, &upsgi.early_emperor, 0},

	{"emperor-command-socket", required_argument, 0, "enable the Emperor command socket", upsgi_opt_set_str, &upsgi.emperor_command_socket, 0},
	{"emperor-wait-for-command", no_argument, 0, "always wait for a 'spawn' Emperor command before starting a vassal", upsgi_opt_true, &upsgi.emperor_wait_for_command, 0},
	{"emperor-wait-for-command-ignore", required_argument, 0, "ignore the emperor-wait-for-command directive for the specified vassal", upsgi_opt_add_string_list, &upsgi.emperor_wait_for_command_ignore, 0},
	{"early-emperor", no_argument, 0, "spawn the emperor as soon as possible", upsgi_opt_true, &upsgi.early_emperor, 0},
	{"emperor-broodlord", required_argument, 0, "run the emperor in BroodLord mode", upsgi_opt_set_int, &upsgi.emperor_broodlord, 0},
	{"emperor-throttle", required_argument, 0, "set throttling level (in milliseconds) for bad behaving vassals (default 1000)", upsgi_opt_set_int, &upsgi.emperor_throttle, 0},
	{"emperor-max-throttle", required_argument, 0, "set max throttling level (in milliseconds) for bad behaving vassals (default 3 minutes)", upsgi_opt_set_int, &upsgi.emperor_max_throttle, 0},
	{"emperor-magic-exec", no_argument, 0, "prefix vassals config files with exec:// if they have the executable bit", upsgi_opt_true, &upsgi.emperor_magic_exec, 0},
	{"emperor-on-demand-extension", required_argument, 0, "search for text file (vassal name + extension) containing the on demand socket name", upsgi_opt_set_str, &upsgi.emperor_on_demand_extension, 0},
	{"emperor-on-demand-ext", required_argument, 0, "search for text file (vassal name + extension) containing the on demand socket name", upsgi_opt_set_str, &upsgi.emperor_on_demand_extension, 0},
	{"emperor-on-demand-directory", required_argument, 0, "enable on demand mode binding to the unix socket in the specified directory named like the vassal + .socket", upsgi_opt_set_str, &upsgi.emperor_on_demand_directory, 0},
	{"emperor-on-demand-dir", required_argument, 0, "enable on demand mode binding to the unix socket in the specified directory named like the vassal + .socket", upsgi_opt_set_str, &upsgi.emperor_on_demand_directory, 0},
	{"emperor-on-demand-exec", required_argument, 0, "use the output of the specified command as on demand socket name (the vassal name is passed as the only argument)", upsgi_opt_set_str, &upsgi.emperor_on_demand_exec, 0},
	{"emperor-extra-extension", required_argument, 0, "allows the specified extension in the Emperor (vassal will be called with --config)", upsgi_opt_add_string_list, &upsgi.emperor_extra_extension, 0},
	{"emperor-extra-ext", required_argument, 0, "allows the specified extension in the Emperor (vassal will be called with --config)", upsgi_opt_add_string_list, &upsgi.emperor_extra_extension, 0},
	{"emperor-no-blacklist", no_argument, 0, "disable Emperor blacklisting subsystem", upsgi_opt_true, &upsgi.emperor_no_blacklist, 0},
#if defined(__linux__) && !defined(OBSOLETE_LINUX_KERNEL)
	{"emperor-use-clone", required_argument, 0, "use clone() instead of fork() passing the specified unshare() flags", upsgi_opt_set_unshare, &upsgi.emperor_clone, 0},
#endif
	{"emperor-use-fork-server", required_argument, 0, "connect to the specified fork server instead of using plain fork() for new vassals", upsgi_opt_set_str, &upsgi.emperor_use_fork_server, 0},
	{"vassal-fork-base", required_argument, 0, "use plain fork() for the specified vassal (instead of a fork-server)", upsgi_opt_add_string_list, &upsgi.vassal_fork_base, 0},
	{"emperor-subreaper", no_argument, 0, "force the Emperor to be a sub-reaper (if supported)", upsgi_opt_true, &upsgi.emperor_subreaper, 0},
	{"emperor-graceful-shutdown", no_argument, 0, "use vassals graceful shutdown during ragnarok", upsgi_opt_true, &upsgi.emperor_graceful_shutdown, 0},
#ifdef UPSGI_CAP
	{"emperor-cap", required_argument, 0, "set vassals capability", upsgi_opt_set_emperor_cap, NULL, 0},
	{"vassals-cap", required_argument, 0, "set vassals capability", upsgi_opt_set_emperor_cap, NULL, 0},
	{"vassal-cap", required_argument, 0, "set vassals capability", upsgi_opt_set_emperor_cap, NULL, 0},
#endif
	{"emperor-collect-attribute", required_argument, 0, "collect the specified vassal attribute from imperial monitors", upsgi_opt_add_string_list, &upsgi.emperor_collect_attributes, 0},
	{"emperor-collect-attr", required_argument, 0, "collect the specified vassal attribute from imperial monitors", upsgi_opt_add_string_list, &upsgi.emperor_collect_attributes, 0},
	{"emperor-fork-server-attr", required_argument, 0, "set the vassal's attribute to get when checking for fork-server", upsgi_opt_set_str, &upsgi.emperor_fork_server_attr, 0},
	{"emperor-wrapper-attr", required_argument, 0, "set the vassal's attribute to get when checking for fork-wrapper", upsgi_opt_set_str, &upsgi.emperor_wrapper_attr, 0},
	{"emperor-chdir-attr", required_argument, 0, "set the vassal's attribute to get when checking for chdir", upsgi_opt_set_str, &upsgi.emperor_chdir_attr, 0},
	{"imperial-monitor-list", no_argument, 0, "list enabled imperial monitors", upsgi_opt_true, &upsgi.imperial_monitor_list, 0},
	{"imperial-monitors-list", no_argument, 0, "list enabled imperial monitors", upsgi_opt_true, &upsgi.imperial_monitor_list, 0},
	{"vassals-inherit", required_argument, 0, "add config templates to vassals config (uses --inherit)", upsgi_opt_add_string_list, &upsgi.vassals_templates, 0},
	{"vassals-include", required_argument, 0, "include config templates to vassals config (uses --include instead of --inherit)", upsgi_opt_add_string_list, &upsgi.vassals_includes, 0},
	{"vassals-inherit-before", required_argument, 0, "add config templates to vassals config (uses --inherit, parses before the vassal file)", upsgi_opt_add_string_list, &upsgi.vassals_templates_before, 0},
	{"vassals-include-before", required_argument, 0, "include config templates to vassals config (uses --include instead of --inherit, parses before the vassal file)", upsgi_opt_add_string_list, &upsgi.vassals_includes_before, 0},
	{"vassals-start-hook", required_argument, 0, "run the specified command before each vassal starts", upsgi_opt_set_str, &upsgi.vassals_start_hook, 0},
	{"vassals-stop-hook", required_argument, 0, "run the specified command after vassal's death", upsgi_opt_set_str, &upsgi.vassals_stop_hook, 0},
	{"vassal-sos", required_argument, 0, "ask emperor for reinforcement when overloaded", upsgi_opt_set_int, &upsgi.vassal_sos, 0},
	{"vassal-sos-backlog", required_argument, 0, "ask emperor for sos if backlog queue has more items than the value specified", upsgi_opt_set_int, &upsgi.vassal_sos_backlog, 0},
	{"vassals-set", required_argument, 0, "automatically set the specified option (via --set) for every vassal", upsgi_opt_add_string_list, &upsgi.vassals_set, 0},
	{"vassal-set", required_argument, 0, "automatically set the specified option (via --set) for every vassal", upsgi_opt_add_string_list, &upsgi.vassals_set, 0},

	{"heartbeat", required_argument, 0, "announce healthiness to the emperor", upsgi_opt_set_int, &upsgi.heartbeat, 0},

	{"zeus", required_argument, 0, "enable Zeus mode", upsgi_opt_set_str, &upsgi.zeus, 0},

	{"reload-mercy", required_argument, 0, "set the maximum time (in seconds) we wait for workers and other processes to die during reload/shutdown", upsgi_opt_set_int, &upsgi.reload_mercy, 0},
	{"worker-reload-mercy", required_argument, 0, "set the maximum time (in seconds) a worker can take to reload/shutdown (default is 60)", upsgi_opt_set_int, &upsgi.worker_reload_mercy, 0},
	{"chain-reload-delay", required_argument, 0, "wait the specified seconds between chain reload worker turnovers", upsgi_opt_set_int, &upsgi.chain_reload_delay, UPSGI_OPT_MASTER},
	{"exit-on-reload", no_argument, 0, "force exit even if a reload is requested", upsgi_opt_true, &upsgi.exit_on_reload, 0},
	{"die-on-term", no_argument, 0, "exit instead of brutal reload on SIGTERM (no more needed)", upsgi_opt_deprecated, &upsgi.die_on_term, 0},
	{"force-gateway", no_argument, 0, "force the spawn of the first registered gateway without a master", upsgi_opt_true, &upsgi.force_gateway, 0},
	{"help", no_argument, 'h', "show this help", upsgi_help, NULL, UPSGI_OPT_IMMEDIATE},
	{"usage", no_argument, 'h', "show this help", upsgi_help, NULL, UPSGI_OPT_IMMEDIATE},

	{"print-sym", required_argument, 0, "print content of the specified binary symbol", upsgi_print_sym, NULL, UPSGI_OPT_IMMEDIATE},
	{"print-symbol", required_argument, 0, "print content of the specified binary symbol", upsgi_print_sym, NULL, UPSGI_OPT_IMMEDIATE},

	{"reaper", no_argument, 'r', "call waitpid(-1,...) after each request to get rid of zombies", upsgi_opt_true, &upsgi.reaper, 0},
	{"max-requests", required_argument, 'R', "reload workers after the specified amount of managed requests", upsgi_opt_set_64bit, &upsgi.max_requests, 0},
	{"max-requests-delta", required_argument, 0, "add (worker_id * delta) to the max_requests value of each worker", upsgi_opt_set_64bit, &upsgi.max_requests_delta, 0},
	{"min-worker-lifetime", required_argument, 0, "number of seconds worker must run before being reloaded (default is 10)", upsgi_opt_set_64bit, &upsgi.min_worker_lifetime, 0},
	{"max-worker-lifetime", required_argument, 0, "reload workers after the specified amount of seconds (default is disabled)", upsgi_opt_set_64bit, &upsgi.max_worker_lifetime, 0},
	{"max-worker-lifetime-delta", required_argument, 0, "add (worker_id * delta) seconds to the max_worker_lifetime value of each worker", upsgi_opt_set_int, &upsgi.max_worker_lifetime_delta, 0},

	{"socket-timeout", required_argument, 'z', "set internal sockets timeout", upsgi_opt_set_int, &upsgi.socket_timeout, 0},
	{"no-fd-passing", no_argument, 0, "disable file descriptor passing", upsgi_opt_true, &upsgi.no_fd_passing, 0},
	{"locks", required_argument, 0, "create the specified number of shared locks", upsgi_opt_set_int, &upsgi.locks, 0},
	{"lock-engine", required_argument, 0, "set the lock engine", upsgi_opt_set_str, &upsgi.lock_engine, 0},
	{"ftok", required_argument, 0, "set the ipcsem key via ftok() for avoiding duplicates", upsgi_opt_set_str, &upsgi.ftok, 0},
	{"persistent-ipcsem", no_argument, 0, "do not remove ipcsem's on shutdown", upsgi_opt_true, &upsgi.persistent_ipcsem, 0},

	{"safe-fd", required_argument, 0, "do not close the specified file descriptor", upsgi_opt_safe_fd, NULL, 0},
	{"fd-safe", required_argument, 0, "do not close the specified file descriptor", upsgi_opt_safe_fd, NULL, 0},

	{"cache2", required_argument, 0, "define a retained local cache for internal runtime consumers", upsgi_opt_add_string_list, &upsgi.cache2, 0},






	{"signal", required_argument, 0, "send a upsgi signal to a server", upsgi_opt_signal, NULL, UPSGI_OPT_IMMEDIATE},
	{"signal-bufsize", required_argument, 0, "set buffer size for signal queue", upsgi_opt_set_int, &upsgi.signal_bufsize, 0},
	{"signals-bufsize", required_argument, 0, "set buffer size for signal queue", upsgi_opt_set_int, &upsgi.signal_bufsize, 0},

	{"signal-timer", required_argument, 0, "add a timer (syntax: <signal> <seconds>)", upsgi_opt_add_string_list, &upsgi.signal_timers, UPSGI_OPT_MASTER},
	{"timer", required_argument, 0, "add a timer (syntax: <signal> <seconds>)", upsgi_opt_add_string_list, &upsgi.signal_timers, UPSGI_OPT_MASTER},

	{"signal-rbtimer", required_argument, 0, "add a redblack timer (syntax: <signal> <seconds>)", upsgi_opt_add_string_list, &upsgi.rb_signal_timers, UPSGI_OPT_MASTER},
	{"rbtimer", required_argument, 0, "add a redblack timer (syntax: <signal> <seconds>)", upsgi_opt_add_string_list, &upsgi.rb_signal_timers, UPSGI_OPT_MASTER},

	{"rpc-max", required_argument, 0, "maximum number of rpc slots (default: 64)", upsgi_opt_set_64bit, &upsgi.rpc_max, 0},

	{"disable-logging", no_argument, 'L', "disable request logging", upsgi_opt_false, &upsgi.logging_options.enabled, 0},

	{"flock", required_argument, 0, "lock the specified file before starting, exit if locked", upsgi_opt_flock, NULL, UPSGI_OPT_IMMEDIATE},
	{"flock-wait", required_argument, 0, "lock the specified file before starting, wait if locked", upsgi_opt_flock_wait, NULL, UPSGI_OPT_IMMEDIATE},

	{"flock2", required_argument, 0, "lock the specified file after logging/daemon setup, exit if locked", upsgi_opt_set_str, &upsgi.flock2, UPSGI_OPT_IMMEDIATE},
	{"flock-wait2", required_argument, 0, "lock the specified file after logging/daemon setup, wait if locked", upsgi_opt_set_str, &upsgi.flock_wait2, UPSGI_OPT_IMMEDIATE},

	{"pidfile", required_argument, 0, "create pidfile (before privileges drop)", upsgi_opt_set_str, &upsgi.pidfile, 0},
	{"pidfile2", required_argument, 0, "create pidfile (after privileges drop)", upsgi_opt_set_str, &upsgi.pidfile2, 0},
	{"safe-pidfile", required_argument, 0, "create safe pidfile (before privileges drop)", upsgi_opt_set_str, &upsgi.safe_pidfile, 0},
	{"safe-pidfile2", required_argument, 0, "create safe pidfile (after privileges drop)", upsgi_opt_set_str, &upsgi.safe_pidfile2, 0},
	{"chroot", required_argument, 0, "chroot() to the specified directory", upsgi_opt_set_str, &upsgi.chroot, 0},
#ifdef __linux__
	{"pivot-root", required_argument, 0, "pivot_root() to the specified directories (new_root and put_old must be separated with a space)", upsgi_opt_set_str, &upsgi.pivot_root, 0},
	{"pivot_root", required_argument, 0, "pivot_root() to the specified directories (new_root and put_old must be separated with a space)", upsgi_opt_set_str, &upsgi.pivot_root, 0},
#endif

	{"uid", required_argument, 0, "setuid to the specified user/uid", upsgi_opt_set_uid, NULL, 0},
	{"gid", required_argument, 0, "setgid to the specified group/gid", upsgi_opt_set_gid, NULL, 0},
	{"add-gid", required_argument, 0, "add the specified group id to the process credentials", upsgi_opt_add_string_list, &upsgi.additional_gids, 0},
	{"immediate-uid", required_argument, 0, "setuid to the specified user/uid IMMEDIATELY", upsgi_opt_set_immediate_uid, NULL, UPSGI_OPT_IMMEDIATE},
	{"immediate-gid", required_argument, 0, "setgid to the specified group/gid IMMEDIATELY", upsgi_opt_set_immediate_gid, NULL, UPSGI_OPT_IMMEDIATE},
	{"no-initgroups", no_argument, 0, "disable additional groups set via initgroups()", upsgi_opt_true, &upsgi.no_initgroups, 0},
#ifdef UPSGI_CAP
	{"cap", required_argument, 0, "set process capability", upsgi_opt_set_cap, NULL, 0},
#endif
#ifdef __linux__
	{"unshare", required_argument, 0, "unshare() part of the processes and put it in a new namespace", upsgi_opt_set_unshare, &upsgi.unshare, 0},
	{"unshare2", required_argument, 0, "unshare() part of the processes and put it in a new namespace after rootfs change", upsgi_opt_set_unshare, &upsgi.unshare2, 0},
	{"setns-socket", required_argument, 0, "expose a unix socket returning namespace fds from /proc/self/ns", upsgi_opt_set_str, &upsgi.setns_socket, UPSGI_OPT_MASTER},
	{"setns-socket-skip", required_argument, 0, "skip the specified entry when sending setns file descriptors", upsgi_opt_add_string_list, &upsgi.setns_socket_skip, 0},
	{"setns-skip", required_argument, 0, "skip the specified entry when sending setns file descriptors", upsgi_opt_add_string_list, &upsgi.setns_socket_skip, 0},
	{"setns", required_argument, 0, "join a namespace created by an external upsgi instance", upsgi_opt_set_str, &upsgi.setns, 0},
	{"setns-preopen", no_argument, 0, "open /proc/self/ns as soon as possible and cache fds", upsgi_opt_true, &upsgi.setns_preopen, 0},
	{"fork-socket", required_argument, 0, "suspend the execution after early initialization and fork() at every unix socket connection", upsgi_opt_set_str, &upsgi.fork_socket, 0},
	{"fork-server", required_argument, 0, "suspend the execution after early initialization and fork() at every unix socket connection", upsgi_opt_set_str, &upsgi.fork_socket, 0},
#endif
	{"jailed", no_argument, 0, "mark the instance as jailed (force the execution of post_jail hooks)", upsgi_opt_true, &upsgi.jailed, 0},
#if defined(__FreeBSD__) || defined(__GNU_kFreeBSD__)
	{"jail", required_argument, 0, "put the instance in a FreeBSD jail", upsgi_opt_set_str, &upsgi.jail, 0},
	{"jail-ip4", required_argument, 0, "add an ipv4 address to the FreeBSD jail", upsgi_opt_add_string_list, &upsgi.jail_ip4, 0},
	{"jail-ip6", required_argument, 0, "add an ipv6 address to the FreeBSD jail", upsgi_opt_add_string_list, &upsgi.jail_ip6, 0},
	{"jidfile", required_argument, 0, "save the jid of a FreeBSD jail in the specified file", upsgi_opt_set_str, &upsgi.jidfile, 0},
	{"jid-file", required_argument, 0, "save the jid of a FreeBSD jail in the specified file", upsgi_opt_set_str, &upsgi.jidfile, 0},
#ifdef UPSGI_HAS_FREEBSD_LIBJAIL
	{"jail2", required_argument, 0, "add an option to the FreeBSD jail", upsgi_opt_add_string_list, &upsgi.jail2, 0},
	{"libjail", required_argument, 0, "add an option to the FreeBSD jail", upsgi_opt_add_string_list, &upsgi.jail2, 0},
	{"jail-attach", required_argument, 0, "attach to the FreeBSD jail", upsgi_opt_set_str, &upsgi.jail_attach, 0},
#endif
#endif
	{"refork", no_argument, 0, "fork() again after privileges drop. Useful for jailing systems", upsgi_opt_true, &upsgi.refork, 0},
	{"re-fork", no_argument, 0, "fork() again after privileges drop. Useful for jailing systems", upsgi_opt_true, &upsgi.refork, 0},
	{"refork-as-root", no_argument, 0, "fork() again before privileges drop. Useful for jailing systems", upsgi_opt_true, &upsgi.refork_as_root, 0},
	{"re-fork-as-root", no_argument, 0, "fork() again before privileges drop. Useful for jailing systems", upsgi_opt_true, &upsgi.refork_as_root, 0},
	{"refork-post-jail", no_argument, 0, "fork() again after jailing. Useful for jailing systems", upsgi_opt_true, &upsgi.refork_post_jail, 0},
	{"re-fork-post-jail", no_argument, 0, "fork() again after jailing. Useful for jailing systems", upsgi_opt_true, &upsgi.refork_post_jail, 0},

	{"hook-asap", required_argument, 0, "run the specified hook as soon as possible", upsgi_opt_add_string_list, &upsgi.hook_asap, 0},
	{"hook-pre-jail", required_argument, 0, "run the specified hook before jailing", upsgi_opt_add_string_list, &upsgi.hook_pre_jail, 0},
        {"hook-post-jail", required_argument, 0, "run the specified hook after jailing", upsgi_opt_add_string_list, &upsgi.hook_post_jail, 0},
        {"hook-in-jail", required_argument, 0, "run the specified hook in jail after initialization", upsgi_opt_add_string_list, &upsgi.hook_in_jail, 0},
        {"hook-as-root", required_argument, 0, "run the specified hook before privileges drop", upsgi_opt_add_string_list, &upsgi.hook_as_root, 0},
        {"hook-as-user", required_argument, 0, "run the specified hook after privileges drop", upsgi_opt_add_string_list, &upsgi.hook_as_user, 0},
        {"hook-as-user-atexit", required_argument, 0, "run the specified hook before app exit and reload", upsgi_opt_add_string_list, &upsgi.hook_as_user_atexit, 0},
        {"hook-pre-app", required_argument, 0, "run the specified hook before app loading", upsgi_opt_add_string_list, &upsgi.hook_pre_app, 0},
        {"hook-post-app", required_argument, 0, "run the specified hook after app loading", upsgi_opt_add_string_list, &upsgi.hook_post_app, 0},
        {"hook-post-fork", required_argument, 0, "run the specified hook after each fork", upsgi_opt_add_string_list, &upsgi.hook_post_fork, 0},
        {"hook-accepting", required_argument, 0, "run the specified hook after each worker enter the accepting phase", upsgi_opt_add_string_list, &upsgi.hook_accepting, 0},
        {"hook-accepting1", required_argument, 0, "run the specified hook after the first worker enters the accepting phase", upsgi_opt_add_string_list, &upsgi.hook_accepting1, 0},
        {"hook-accepting-once", required_argument, 0, "run the specified hook after each worker enter the accepting phase (once per-instance)", upsgi_opt_add_string_list, &upsgi.hook_accepting_once, 0},
        {"hook-accepting1-once", required_argument, 0, "run the specified hook after the first worker enters the accepting phase (once per instance)", upsgi_opt_add_string_list, &upsgi.hook_accepting1_once, 0},

        {"hook-master-start", required_argument, 0, "run the specified hook when the Master starts", upsgi_opt_add_string_list, &upsgi.hook_master_start, 0},

        {"hook-touch", required_argument, 0, "run the specified hook when the specified file is touched (syntax: <file> <action>)", upsgi_opt_add_string_list, &upsgi.hook_touch, 0},

        {"hook-emperor-start", required_argument, 0, "run the specified hook when the Emperor starts", upsgi_opt_add_string_list, &upsgi.hook_emperor_start, 0},
        {"hook-emperor-stop", required_argument, 0, "run the specified hook when the Emperor send a stop message", upsgi_opt_add_string_list, &upsgi.hook_emperor_stop, 0},
        {"hook-emperor-reload", required_argument, 0, "run the specified hook when the Emperor send a reload message", upsgi_opt_add_string_list, &upsgi.hook_emperor_reload, 0},
        {"hook-emperor-lost", required_argument, 0, "run the specified hook when the Emperor connection is lost", upsgi_opt_add_string_list, &upsgi.hook_emperor_lost, 0},

        {"hook-as-vassal", required_argument, 0, "run the specified hook before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.hook_as_vassal, 0},
        {"hook-as-emperor", required_argument, 0, "run the specified hook in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.hook_as_emperor, 0},

        {"hook-as-on-demand-vassal", required_argument, 0, "run the specified hook whenever a vassal enters on-demand mode", upsgi_opt_add_string_list, &upsgi.hook_as_on_demand_vassal, 0},
	{"hook-as-on-config-vassal", required_argument, 0, "run the specified hook whenever the emperor detects a config change for an on-demand vassal", upsgi_opt_add_string_list, &upsgi.hook_as_on_config_vassal, 0},

        {"hook-as-emperor-before-vassal", required_argument, 0, "run the specified hook before the new vassal is spawned", upsgi_opt_add_string_list, &upsgi.hook_as_emperor_before_vassal, 0},
        {"hook-as-vassal-before-drop", required_argument, 0, "run the specified hook into vassal, before dropping its privileges", upsgi_opt_add_string_list, &upsgi.hook_as_vassal_before_drop, 0},

        {"hook-as-emperor-setns", required_argument, 0, "run the specified hook in the emperor entering vassal namespace", upsgi_opt_add_string_list, &upsgi.hook_as_emperor_setns, 0},


        {"hook-as-gateway", required_argument, 0, "run the specified hook in each gateway", upsgi_opt_add_string_list, &upsgi.hook_as_gateway, 0},

        {"after-request-hook", required_argument, 0, "run the specified function/symbol after each request", upsgi_opt_add_string_list, &upsgi.after_request_hooks, 0},
        {"after-request-call", required_argument, 0, "run the specified function/symbol after each request", upsgi_opt_add_string_list, &upsgi.after_request_hooks, 0},

	{"exec-asap", required_argument, 0, "run the specified command as soon as possible", upsgi_opt_add_string_list, &upsgi.exec_asap, 0},
	{"exec-pre-jail", required_argument, 0, "run the specified command before jailing", upsgi_opt_add_string_list, &upsgi.exec_pre_jail, 0},
	{"exec-post-jail", required_argument, 0, "run the specified command after jailing", upsgi_opt_add_string_list, &upsgi.exec_post_jail, 0},
	{"exec-in-jail", required_argument, 0, "run the specified command in jail after initialization", upsgi_opt_add_string_list, &upsgi.exec_in_jail, 0},
	{"exec-as-root", required_argument, 0, "run the specified command before privileges drop", upsgi_opt_add_string_list, &upsgi.exec_as_root, 0},
	{"exec-as-user", required_argument, 0, "run the specified command after privileges drop", upsgi_opt_add_string_list, &upsgi.exec_as_user, 0},
	{"exec-as-user-atexit", required_argument, 0, "run the specified command before app exit and reload", upsgi_opt_add_string_list, &upsgi.exec_as_user_atexit, 0},
	{"exec-pre-app", required_argument, 0, "run the specified command before app loading", upsgi_opt_add_string_list, &upsgi.exec_pre_app, 0},
	{"exec-post-app", required_argument, 0, "run the specified command after app loading", upsgi_opt_add_string_list, &upsgi.exec_post_app, 0},

	{"exec-as-vassal", required_argument, 0, "run the specified command before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.exec_as_vassal, 0},
	{"exec-as-emperor", required_argument, 0, "run the specified command in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.exec_as_emperor, 0},

	{"mount-asap", required_argument, 0, "mount filesystem as soon as possible", upsgi_opt_add_string_list, &upsgi.mount_asap, 0},
	{"mount-pre-jail", required_argument, 0, "mount filesystem before jailing", upsgi_opt_add_string_list, &upsgi.mount_pre_jail, 0},
        {"mount-post-jail", required_argument, 0, "mount filesystem after jailing", upsgi_opt_add_string_list, &upsgi.mount_post_jail, 0},
        {"mount-in-jail", required_argument, 0, "mount filesystem in jail after initialization", upsgi_opt_add_string_list, &upsgi.mount_in_jail, 0},
        {"mount-as-root", required_argument, 0, "mount filesystem before privileges drop", upsgi_opt_add_string_list, &upsgi.mount_as_root, 0},

        {"mount-as-vassal", required_argument, 0, "mount filesystem before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.mount_as_vassal, 0},
        {"mount-as-emperor", required_argument, 0, "mount filesystem in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.mount_as_emperor, 0},

	{"umount-asap", required_argument, 0, "unmount filesystem as soon as possible", upsgi_opt_add_string_list, &upsgi.umount_asap, 0},
	{"umount-pre-jail", required_argument, 0, "unmount filesystem before jailing", upsgi_opt_add_string_list, &upsgi.umount_pre_jail, 0},
        {"umount-post-jail", required_argument, 0, "unmount filesystem after jailing", upsgi_opt_add_string_list, &upsgi.umount_post_jail, 0},
        {"umount-in-jail", required_argument, 0, "unmount filesystem in jail after initialization", upsgi_opt_add_string_list, &upsgi.umount_in_jail, 0},
        {"umount-as-root", required_argument, 0, "unmount filesystem before privileges drop", upsgi_opt_add_string_list, &upsgi.umount_as_root, 0},

        {"umount-as-vassal", required_argument, 0, "unmount filesystem before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.umount_as_vassal, 0},
        {"umount-as-emperor", required_argument, 0, "unmount filesystem in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.umount_as_emperor, 0},

	{"wait-for-interface", required_argument, 0, "wait for the specified network interface to come up before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_interface, 0},
	{"wait-for-interface-timeout", required_argument, 0, "set the timeout for wait-for-interface", upsgi_opt_set_int, &upsgi.wait_for_interface_timeout, 0},

	{"wait-interface", required_argument, 0, "wait for the specified network interface to come up before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_interface, 0},
	{"wait-interface-timeout", required_argument, 0, "set the timeout for wait-for-interface", upsgi_opt_set_int, &upsgi.wait_for_interface_timeout, 0},

	{"wait-for-iface", required_argument, 0, "wait for the specified network interface to come up before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_interface, 0},
	{"wait-for-iface-timeout", required_argument, 0, "set the timeout for wait-for-interface", upsgi_opt_set_int, &upsgi.wait_for_interface_timeout, 0},

	{"wait-iface", required_argument, 0, "wait for the specified network interface to come up before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_interface, 0},
	{"wait-iface-timeout", required_argument, 0, "set the timeout for wait-for-interface", upsgi_opt_set_int, &upsgi.wait_for_interface_timeout, 0},

	{"wait-for-fs", required_argument, 0, "wait for the specified filesystem item to appear before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_fs, 0},
	{"wait-for-file", required_argument, 0, "wait for the specified file to appear before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_fs, 0},
	{"wait-for-dir", required_argument, 0, "wait for the specified directory to appear before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_fs, 0},
	{"wait-for-mountpoint", required_argument, 0, "wait for the specified mountpoint to appear before running root hooks", upsgi_opt_add_string_list, &upsgi.wait_for_mountpoint, 0},
	{"wait-for-fs-timeout", required_argument, 0, "set the timeout for wait-for-fs/file/dir", upsgi_opt_set_int, &upsgi.wait_for_fs_timeout, 0},

	{"wait-for-socket", required_argument, 0, "wait for the specified socket to be ready before loading apps", upsgi_opt_add_string_list, &upsgi.wait_for_socket, 0},
	{"wait-for-socket-timeout", required_argument, 0, "set the timeout for wait-for-socket", upsgi_opt_set_int, &upsgi.wait_for_socket_timeout, 0},

	{"call-asap", required_argument, 0, "call the specified function as soon as possible", upsgi_opt_add_string_list, &upsgi.call_asap, 0},
	{"call-pre-jail", required_argument, 0, "call the specified function before jailing", upsgi_opt_add_string_list, &upsgi.call_pre_jail, 0},
	{"call-post-jail", required_argument, 0, "call the specified function after jailing", upsgi_opt_add_string_list, &upsgi.call_post_jail, 0},
	{"call-in-jail", required_argument, 0, "call the specified function in jail after initialization", upsgi_opt_add_string_list, &upsgi.call_in_jail, 0},
	{"call-as-root", required_argument, 0, "call the specified function before privileges drop", upsgi_opt_add_string_list, &upsgi.call_as_root, 0},
	{"call-as-user", required_argument, 0, "call the specified function after privileges drop", upsgi_opt_add_string_list, &upsgi.call_as_user, 0},
	{"call-as-user-atexit", required_argument, 0, "call the specified function before app exit and reload", upsgi_opt_add_string_list, &upsgi.call_as_user_atexit, 0},
	{"call-pre-app", required_argument, 0, "call the specified function before app loading", upsgi_opt_add_string_list, &upsgi.call_pre_app, 0},
	{"call-post-app", required_argument, 0, "call the specified function after app loading", upsgi_opt_add_string_list, &upsgi.call_post_app, 0},

	{"call-as-vassal", required_argument, 0, "call the specified function() before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.call_as_vassal, 0},
	{"call-as-vassal1", required_argument, 0, "call the specified function(char *) before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.call_as_vassal1, 0},
	{"call-as-vassal3", required_argument, 0, "call the specified function(char *, uid_t, gid_t) before exec()ing the vassal", upsgi_opt_add_string_list, &upsgi.call_as_vassal3, 0},

	{"call-as-emperor", required_argument, 0, "call the specified function() in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.call_as_emperor, 0},
	{"call-as-emperor1", required_argument, 0, "call the specified function(char *) in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.call_as_emperor1, 0},
	{"call-as-emperor2", required_argument, 0, "call the specified function(char *, pid_t) in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.call_as_emperor2, 0},
	{"call-as-emperor4", required_argument, 0, "call the specified function(char *, pid_t, uid_t, gid_t) in the emperor after the vassal has been started", upsgi_opt_add_string_list, &upsgi.call_as_emperor4, 0},



	{"ini", required_argument, 0, "load config from ini file", upsgi_opt_load_ini, NULL, UPSGI_OPT_IMMEDIATE},
#ifdef UPSGI_YAML
	{"yaml", required_argument, 'y', "load config from yaml file", upsgi_opt_load_yml, NULL, UPSGI_OPT_IMMEDIATE},
	{"yml", required_argument, 'y', "load config from yaml file", upsgi_opt_load_yml, NULL, UPSGI_OPT_IMMEDIATE},
#endif
#ifdef UPSGI_JSON
	{"json", required_argument, 'j', "load config from json file", upsgi_opt_load_json, NULL, UPSGI_OPT_IMMEDIATE},
	{"js", required_argument, 'j', "load config from json file", upsgi_opt_load_json, NULL, UPSGI_OPT_IMMEDIATE},
#endif
	{"weight", required_argument, 0, "weight of the instance (used by clustering/lb/subscriptions)", upsgi_opt_set_64bit, &upsgi.weight, 0},
	{"auto-weight", required_argument, 0, "set weight of the instance (used by clustering/lb/subscriptions) automatically", upsgi_opt_true, &upsgi.auto_weight, 0},
	{"no-server", no_argument, 0, "force no-server mode", upsgi_opt_true, &upsgi.no_server, 0},
	{"command-mode", no_argument, 0, "force command mode", upsgi_opt_true, &upsgi.command_mode, UPSGI_OPT_IMMEDIATE},
	{"no-defer-accept", no_argument, 0, "disable deferred-accept on sockets", upsgi_opt_true, &upsgi.no_defer_accept, 0},
	{"tcp-nodelay", no_argument, 0, "enable TCP NODELAY on each request", upsgi_opt_true, &upsgi.tcp_nodelay, 0},
	{"so-keepalive", no_argument, 0, "enable TCP KEEPALIVEs", upsgi_opt_true, &upsgi.so_keepalive, 0},
	{"so-send-timeout", no_argument, 0, "set SO_SNDTIMEO", upsgi_opt_set_int, &upsgi.so_send_timeout, 0},
	{"socket-send-timeout", no_argument, 0, "set SO_SNDTIMEO", upsgi_opt_set_int, &upsgi.so_send_timeout, 0},
	{"so-write-timeout", no_argument, 0, "set SO_SNDTIMEO", upsgi_opt_set_int, &upsgi.so_send_timeout, 0},
	{"socket-write-timeout", no_argument, 0, "set SO_SNDTIMEO", upsgi_opt_set_int, &upsgi.so_send_timeout, 0},
	{"socket-sndbuf", required_argument, 0, "set SO_SNDBUF", upsgi_opt_set_64bit, &upsgi.so_sndbuf, 0},
	{"socket-rcvbuf", required_argument, 0, "set SO_RCVBUF", upsgi_opt_set_64bit, &upsgi.so_rcvbuf, 0},
	{"limit-as", required_argument, 0, "limit processes address space/vsz", upsgi_opt_set_megabytes, &upsgi.rl.rlim_max, 0},
	{"limit-nproc", required_argument, 0, "limit the number of spawnable processes", upsgi_opt_set_int, &upsgi.rl_nproc.rlim_max, 0},
	{"reload-on-as", required_argument, 0, "reload if address space is higher than specified megabytes", upsgi_opt_set_megabytes, &upsgi.reload_on_as, UPSGI_OPT_MEMORY},
	{"reload-on-rss", required_argument, 0, "reload if rss memory is higher than specified megabytes", upsgi_opt_set_megabytes, &upsgi.reload_on_rss, UPSGI_OPT_MEMORY},
#ifdef __linux__
	{"reload-on-uss", required_argument, 0, "reload if uss memory is higher than specified megabytes", upsgi_opt_set_megabytes, &upsgi.reload_on_uss, UPSGI_OPT_MEMORY},
	{"reload-on-pss", required_argument, 0, "reload if pss memory is higher than specified megabytes", upsgi_opt_set_megabytes, &upsgi.reload_on_pss, UPSGI_OPT_MEMORY},
#endif
	{"evil-reload-on-as", required_argument, 0, "force the master to reload a worker if its address space is higher than specified megabytes", upsgi_opt_set_megabytes, &upsgi.evil_reload_on_as, UPSGI_OPT_MASTER | UPSGI_OPT_MEMORY},
	{"evil-reload-on-rss", required_argument, 0, "force the master to reload a worker if its rss memory is higher than specified megabytes", upsgi_opt_set_megabytes, &upsgi.evil_reload_on_rss, UPSGI_OPT_MASTER | UPSGI_OPT_MEMORY},
	{"mem-collector-freq", required_argument, 0, "set the memory collector frequency when evil reloads are in place", upsgi_opt_set_int, &upsgi.mem_collector_freq, 0},

	{"reload-on-fd", required_argument, 0, "reload if the specified file descriptor is ready", upsgi_opt_add_string_list, &upsgi.reload_on_fd, UPSGI_OPT_MASTER},
	{"brutal-reload-on-fd", required_argument, 0, "brutal reload if the specified file descriptor is ready", upsgi_opt_add_string_list, &upsgi.brutal_reload_on_fd, UPSGI_OPT_MASTER},

#ifdef __linux__
#ifdef MADV_MERGEABLE
	{"ksm", optional_argument, 0, "enable Linux KSM", upsgi_opt_set_int, &upsgi.linux_ksm, 0},
#endif
#endif
#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	{"pcre-jit", no_argument, 0, "enable pcre jit (if available)", upsgi_opt_pcre_jit, NULL, UPSGI_OPT_IMMEDIATE},
#endif
	{"never-swap", no_argument, 0, "lock all memory pages avoiding swapping", upsgi_opt_true, &upsgi.never_swap, 0},
	{"touch-reload", required_argument, 0, "reload upsgi if the specified file is modified/touched", upsgi_opt_add_string_list, &upsgi.touch_reload, UPSGI_OPT_MASTER},
	{"touch-workers-reload", required_argument, 0, "trigger reload of (only) workers if the specified file is modified/touched", upsgi_opt_add_string_list, &upsgi.touch_workers_reload, UPSGI_OPT_MASTER},
	{"touch-chain-reload", required_argument, 0, "trigger chain reload if the specified file is modified/touched", upsgi_opt_add_string_list, &upsgi.touch_chain_reload, UPSGI_OPT_MASTER},
	{"touch-logrotate", required_argument, 0, "trigger logrotation if the specified file is modified/touched", upsgi_opt_add_string_list, &upsgi.touch_logrotate, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"touch-logreopen", required_argument, 0, "trigger log reopen if the specified file is modified/touched", upsgi_opt_add_string_list, &upsgi.touch_logreopen, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"touch-exec", required_argument, 0, "run command when the specified file is modified/touched (syntax: file command)", upsgi_opt_add_string_list, &upsgi.touch_exec, UPSGI_OPT_MASTER},
	{"touch-signal", required_argument, 0, "signal when the specified file is modified/touched (syntax: file signal)", upsgi_opt_add_string_list, &upsgi.touch_signal, UPSGI_OPT_MASTER},

	{"fs-reload", required_argument, 0, "graceful reload when the specified filesystem object is modified", upsgi_opt_add_string_list, &upsgi.fs_reload, UPSGI_OPT_MASTER},
	{"fs-brutal-reload", required_argument, 0, "brutal reload when the specified filesystem object is modified", upsgi_opt_add_string_list, &upsgi.fs_brutal_reload, UPSGI_OPT_MASTER},
	{"fs-signal", required_argument, 0, "raise a upsgi signal when the specified filesystem object is modified (syntax: file signal)", upsgi_opt_add_string_list, &upsgi.fs_signal, UPSGI_OPT_MASTER},

	{"check-mountpoint", required_argument, 0, "destroy the instance if a filesystem is no more reachable (useful for reliable Fuse management)", upsgi_opt_add_string_list, &upsgi.mountpoints_check, UPSGI_OPT_MASTER},
	{"mountpoint-check", required_argument, 0, "destroy the instance if a filesystem is no more reachable (useful for reliable Fuse management)", upsgi_opt_add_string_list, &upsgi.mountpoints_check, UPSGI_OPT_MASTER},
	{"check-mount", required_argument, 0, "destroy the instance if a filesystem is no more reachable (useful for reliable Fuse management)", upsgi_opt_add_string_list, &upsgi.mountpoints_check, UPSGI_OPT_MASTER},
	{"mount-check", required_argument, 0, "destroy the instance if a filesystem is no more reachable (useful for reliable Fuse management)", upsgi_opt_add_string_list, &upsgi.mountpoints_check, UPSGI_OPT_MASTER},

	{"propagate-touch", no_argument, 0, "over-engineering option for system with flaky signal management", upsgi_opt_true, &upsgi.propagate_touch, 0},
	{"limit-post", required_argument, 0, "limit request body", upsgi_opt_set_64bit, &upsgi.limit_post, 0},
	{"no-orphans", no_argument, 0, "automatically kill workers if master dies (can be dangerous for availability)", upsgi_opt_true, &upsgi.no_orphans, 0},
	{"prio", required_argument, 0, "set processes/threads priority", upsgi_opt_set_rawint, &upsgi.prio, 0},
	{"cpu-affinity", required_argument, 0, "set cpu affinity", upsgi_opt_set_int, &upsgi.cpu_affinity, 0},
	{"post-buffering", required_argument, 0, "set size in bytes after which request bodies spill to disk instead of staying fully in memory", upsgi_opt_set_64bit, &upsgi.post_buffering, 0},
	{"post-buffering-bufsize", required_argument, 0, "set read chunk buffer size used while post buffering; independent from the in-memory spill threshold", upsgi_opt_set_64bit, &upsgi.post_buffering_bufsize, 0},
	{"body-read-warning", required_argument, 0, "set the amount of allowed memory allocation (in megabytes) for request body before starting printing a warning", upsgi_opt_set_64bit, &upsgi.body_read_warning, 0},
	{"body-scheduler", no_argument, 0, "enable request-body scheduler fairness (enabled by default)", upsgi_opt_true, &upsgi.body_scheduler, 0},
	{"disable-body-scheduler", no_argument, 0, "disable request-body scheduler fairness", upsgi_opt_false, &upsgi.body_scheduler, 0},
	{"upload-progress", required_argument, 0, "enable creation of .json files in the specified directory during a file upload", upsgi_opt_set_str, &upsgi.upload_progress, 0},
	{"no-default-app", no_argument, 0, "do not fallback to default app", upsgi_opt_true, &upsgi.no_default_app, 0},
	{"manage-script-name", no_argument, 0, "automatically rewrite SCRIPT_NAME and PATH_INFO", upsgi_opt_true, &upsgi.manage_script_name, 0},
	{"ignore-script-name", no_argument, 0, "ignore SCRIPT_NAME", upsgi_opt_true, &upsgi.ignore_script_name, 0},

	{"catch-exceptions", no_argument, 0, "report exception as http output (discouraged, use only for testing)", upsgi_opt_true, &upsgi.catch_exceptions, 0},
	{"reload-on-exception", no_argument, 0, "reload a worker when an exception is raised", upsgi_opt_true, &upsgi.reload_on_exception, 0},
	{"reload-on-exception-type", required_argument, 0, "reload a worker when a specific exception type is raised", upsgi_opt_add_string_list, &upsgi.reload_on_exception_type, 0},
	{"reload-on-exception-value", required_argument, 0, "reload a worker when a specific exception value is raised", upsgi_opt_add_string_list, &upsgi.reload_on_exception_value, 0},
	{"reload-on-exception-repr", required_argument, 0, "reload a worker when a specific exception type+value (language-specific) is raised", upsgi_opt_add_string_list, &upsgi.reload_on_exception_repr, 0},
	{"exception-handler", required_argument, 0, "add an exception handler", upsgi_opt_add_string_list, &upsgi.exception_handlers_instance, UPSGI_OPT_MASTER},

	{"enable-metrics", no_argument, 0, "enable metrics subsystem", upsgi_opt_true, &upsgi.has_metrics, UPSGI_OPT_MASTER},
	{"metric", required_argument, 0, "add a custom metric", upsgi_opt_add_string_list, &upsgi.additional_metrics, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metric-threshold", required_argument, 0, "add a metric threshold/alarm", upsgi_opt_add_string_list, &upsgi.metrics_threshold, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metric-alarm", required_argument, 0, "add a metric threshold/alarm", upsgi_opt_add_string_list, &upsgi.metrics_threshold, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"alarm-metric", required_argument, 0, "add a metric threshold/alarm", upsgi_opt_add_string_list, &upsgi.metrics_threshold, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metrics-dir", required_argument, 0, "export metrics as text files to the specified directory", upsgi_opt_set_str, &upsgi.metrics_dir, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metrics-dir-restore", no_argument, 0, "restore last value taken from the metrics dir", upsgi_opt_true, &upsgi.metrics_dir_restore, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metric-dir", required_argument, 0, "export metrics as text files to the specified directory", upsgi_opt_set_str, &upsgi.metrics_dir, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metric-dir-restore", no_argument, 0, "restore last value taken from the metrics dir", upsgi_opt_true, &upsgi.metrics_dir_restore, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},
	{"metrics-no-cores", no_argument, 0, "disable generation of cores-related metrics", upsgi_opt_true, &upsgi.metrics_no_cores, UPSGI_OPT_METRICS|UPSGI_OPT_MASTER},

	{"udp", required_argument, 0, "run the udp server on the specified address", upsgi_opt_set_str, &upsgi.udp_socket, UPSGI_OPT_MASTER},
	{"stats", required_argument, 0, "enable the stats server on the specified address", upsgi_opt_set_str, &upsgi.stats, UPSGI_OPT_MASTER},
	{"stats-server", required_argument, 0, "enable the stats server on the specified address", upsgi_opt_set_str, &upsgi.stats, UPSGI_OPT_MASTER},
	{"stats-http", no_argument, 0, "prefix stats server json output with http headers", upsgi_opt_true, &upsgi.stats_http, UPSGI_OPT_MASTER},
	{"stats-minified", no_argument, 0, "minify statistics json output", upsgi_opt_true, &upsgi.stats_minified, UPSGI_OPT_MASTER},
	{"stats-min", no_argument, 0, "minify statistics json output", upsgi_opt_true, &upsgi.stats_minified, UPSGI_OPT_MASTER},
	{"stats-push", required_argument, 0, "push the stats json to the specified destination", upsgi_opt_add_string_list, &upsgi.requested_stats_pushers, UPSGI_OPT_MASTER|UPSGI_OPT_METRICS},
	{"stats-pusher-default-freq", required_argument, 0, "set the default frequency of stats pushers", upsgi_opt_set_int, &upsgi.stats_pusher_default_freq, UPSGI_OPT_MASTER},
	{"stats-pushers-default-freq", required_argument, 0, "set the default frequency of stats pushers", upsgi_opt_set_int, &upsgi.stats_pusher_default_freq, UPSGI_OPT_MASTER},
	{"stats-no-cores", no_argument, 0, "disable generation of cores-related stats", upsgi_opt_true, &upsgi.stats_no_cores, UPSGI_OPT_MASTER},
	{"stats-no-metrics", no_argument, 0, "do not include metrics in stats output", upsgi_opt_true, &upsgi.stats_no_metrics, UPSGI_OPT_MASTER},
	{"multicast", required_argument, 0, "subscribe to specified multicast group", upsgi_opt_set_str, &upsgi.multicast_group, UPSGI_OPT_MASTER},
	{"multicast-ttl", required_argument, 0, "set multicast ttl", upsgi_opt_set_int, &upsgi.multicast_ttl, 0},
	{"multicast-loop", required_argument, 0, "set multicast loop (default 1)", upsgi_opt_set_int, &upsgi.multicast_loop, 0},

	{"master-fifo", required_argument, 0, "enable the master fifo", upsgi_opt_add_string_list, &upsgi.master_fifo, UPSGI_OPT_MASTER},

	{"notify-socket", required_argument, 0, "enable the notification socket", upsgi_opt_set_str, &upsgi.notify_socket, UPSGI_OPT_MASTER},



	{"snmp", optional_argument, 0, "enable the embedded snmp server", upsgi_opt_snmp, NULL, 0},
	{"snmp-community", required_argument, 0, "set the snmp community string", upsgi_opt_snmp_community, NULL, 0},
#ifdef UPSGI_SSL
	{"ssl-verbose", no_argument, 0, "be verbose about SSL errors", upsgi_opt_true, &upsgi.ssl_verbose, 0},
	{"ssl-verify-depth", optional_argument, 0, "set maximum certificate verification depth", upsgi_opt_set_int, &upsgi.ssl_verify_depth, 0},
#ifdef UPSGI_SSL_SESSION_CACHE
	// force master, as ssl sessions caching initialize locking early
	{"ssl-sessions-use-cache", optional_argument, 0, "use upsgi cache for ssl sessions storage", upsgi_opt_set_str, &upsgi.ssl_sessions_use_cache, UPSGI_OPT_MASTER},
	{"ssl-session-use-cache", optional_argument, 0, "use upsgi cache for ssl sessions storage", upsgi_opt_set_str, &upsgi.ssl_sessions_use_cache, UPSGI_OPT_MASTER},
	{"ssl-sessions-timeout", required_argument, 0, "set SSL sessions timeout (default: 300 seconds)", upsgi_opt_set_int, &upsgi.ssl_sessions_timeout, 0},
	{"ssl-session-timeout", required_argument, 0, "set SSL sessions timeout (default: 300 seconds)", upsgi_opt_set_int, &upsgi.ssl_sessions_timeout, 0},
#endif
	{"sni", required_argument, 0, "add an SNI-governed SSL context", upsgi_opt_sni, NULL, 0},
	{"sni-dir", required_argument, 0, "check for cert/key/client_ca file in the specified directory and create a sni/ssl context on demand", upsgi_opt_set_str, &upsgi.sni_dir, 0},
	{"sni-dir-ciphers", required_argument, 0, "set ssl ciphers for sni-dir option", upsgi_opt_set_str, &upsgi.sni_dir_ciphers, 0},
	{"ssl-enable3", no_argument, 0, "enable SSLv3 (insecure)", upsgi_opt_true, &upsgi.sslv3, 0},
	{"ssl-enable-sslv3", no_argument, 0, "enable SSLv3 (insecure)", upsgi_opt_true, &upsgi.sslv3, 0},
	{"ssl-enable-tlsv1", no_argument, 0, "enable TLSv1 (insecure)", upsgi_opt_true, &upsgi.tlsv1, 0},
	{"ssl-option", required_argument, 0, "set a raw ssl option (numeric value)", upsgi_opt_add_string_list, &upsgi.ssl_options, 0},
#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	{"sni-regexp", required_argument, 0, "add an SNI-governed SSL context (the key is a regexp)", upsgi_opt_sni, NULL, 0},
#endif
	{"ssl-tmp-dir", required_argument, 0, "store ssl-related temp files in the specified directory", upsgi_opt_set_str, &upsgi.ssl_tmp_dir, 0},
#endif
	{"check-interval", required_argument, 0, "set the interval (in seconds) of master checks", upsgi_opt_set_int, &upsgi.master_interval, UPSGI_OPT_MASTER},
	{"forkbomb-delay", required_argument, 0, "sleep for the specified number of seconds when a forkbomb is detected", upsgi_opt_set_int, &upsgi.forkbomb_delay, UPSGI_OPT_MASTER},
	{"binary-path", required_argument, 0, "force binary path", upsgi_opt_set_str, &upsgi.binary_path, 0},
	{"privileged-binary-patch", required_argument, 0, "patch the upsgi binary with a new command (before privileges drop)", upsgi_opt_set_str, &upsgi.privileged_binary_patch, 0},
	{"unprivileged-binary-patch", required_argument, 0, "patch the upsgi binary with a new command (after privileges drop)", upsgi_opt_set_str, &upsgi.unprivileged_binary_patch, 0},
	{"privileged-binary-patch-arg", required_argument, 0, "patch the upsgi binary with a new command and arguments (before privileges drop)", upsgi_opt_set_str, &upsgi.privileged_binary_patch_arg, 0},
	{"unprivileged-binary-patch-arg", required_argument, 0, "patch the upsgi binary with a new command and arguments (after privileges drop)", upsgi_opt_set_str, &upsgi.unprivileged_binary_patch_arg, 0},
	{"async", required_argument, 0, "enable async mode with specified cores", upsgi_opt_set_int, &upsgi.async, 0},
	{"disable-async-warn-on-queue-full", no_argument, 0, "Disable printing 'async queue is full' warning messages.", upsgi_opt_false, &upsgi.async_warn_if_queue_full, 0},
	{"max-fd", required_argument, 0, "set maximum number of file descriptors (requires root privileges)", upsgi_opt_set_int, &upsgi.requested_max_fd, 0},
	{"logto", required_argument, 0, "set logfile/udp address", upsgi_opt_set_str, &upsgi.logfile, 0},
	{"logto2", required_argument, 0, "log to specified file or udp address after privileges drop", upsgi_opt_set_str, &upsgi.logto2, 0},
	{"log-format", required_argument, 0, "set advanced format for request logging", upsgi_opt_set_str, &upsgi.logformat, 0},
	{"logformat", required_argument, 0, "set advanced format for request logging", upsgi_opt_set_str, &upsgi.logformat, 0},
	{"logformat-strftime", no_argument, 0, "apply strftime to logformat output", upsgi_opt_true, &upsgi.logformat_strftime, 0},
	{"log-format-strftime", no_argument, 0, "apply strftime to logformat output", upsgi_opt_true, &upsgi.logformat_strftime, 0},
	{"logfile-chown", no_argument, 0, "chown logfiles", upsgi_opt_true, &upsgi.logfile_chown, 0},
	{"logfile-chmod", required_argument, 0, "chmod logfiles", upsgi_opt_logfile_chmod, NULL, 0},
	{"log-syslog", optional_argument, 0, "log to syslog", upsgi_opt_set_logger, "syslog", UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"log-socket", required_argument, 0, "send logs to the specified socket", upsgi_opt_set_logger, "socket", UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"req-logger", required_argument, 0, "set/append a request logger", upsgi_opt_set_req_logger, NULL, UPSGI_OPT_REQ_LOG_MASTER},
	{"logger-req", required_argument, 0, "set/append a request logger", upsgi_opt_set_req_logger, NULL, UPSGI_OPT_REQ_LOG_MASTER},
	{"logger", required_argument, 0, "set/append a logger", upsgi_opt_set_logger, NULL, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"worker-logger", required_argument, 0, "set/append a logger in single-worker setup", upsgi_opt_set_worker_logger, NULL, 0},
	{"worker-logger-req", required_argument, 0, "set/append a request logger in single-worker setup", upsgi_opt_set_req_logger, NULL, 0},
	{"logger-list", no_argument, 0, "list enabled loggers", upsgi_opt_true, &upsgi.loggers_list, 0},
	{"loggers-list", no_argument, 0, "list enabled loggers", upsgi_opt_true, &upsgi.loggers_list, 0},
	{"threaded-logger", no_argument, 0, "offload log writing to a thread", upsgi_opt_true, &upsgi.threaded_logger, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},


	{"log-encoder", required_argument, 0, "add an item in the log encoder chain", upsgi_opt_add_string_list, &upsgi.requested_log_encoders, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"log-req-encoder", required_argument, 0, "add an item in the log req encoder chain", upsgi_opt_add_string_list, &upsgi.requested_log_req_encoders, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},

	{"worker-log-encoder", required_argument, 0, "add an item in the log encoder chain", upsgi_opt_add_string_list, &upsgi.requested_log_encoders, 0},
	{"worker-log-req-encoder", required_argument, 0, "add an item in the log req encoder chain", upsgi_opt_add_string_list, &upsgi.requested_log_req_encoders, 0},
	

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	{"log-drain", required_argument, 0, "drain (do not show) log lines matching the specified regexp", upsgi_opt_add_regexp_list, &upsgi.log_drain_rules, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"log-filter", required_argument, 0, "show only log lines matching the specified regexp", upsgi_opt_add_regexp_list, &upsgi.log_filter_rules, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"log-route", required_argument, 0, "log to the specified named logger if regexp applied on logline matches", upsgi_opt_add_regexp_custom_list, &upsgi.log_route, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"log-req-route", required_argument, 0, "log requests to the specified named logger if regexp applied on logline matches", upsgi_opt_add_regexp_custom_list, &upsgi.log_req_route, UPSGI_OPT_REQ_LOG_MASTER},
#endif

	{"use-abort", no_argument, 0, "call abort() on segfault/fpe, could be useful for generating a core dump", upsgi_opt_true, &upsgi.use_abort, 0},

	{"alarm", required_argument, 0, "create a new alarm, syntax: <alarm> <plugin:args>", upsgi_opt_add_string_list, &upsgi.alarm_list, UPSGI_OPT_MASTER},
	{"alarm-cheap", required_argument, 0, "use main alarm thread rather than create dedicated threads for curl-based alarms", upsgi_opt_true, &upsgi.alarm_cheap, 0},
	{"alarm-freq", required_argument, 0, "tune the anti-loop alarm system (default 3 seconds)", upsgi_opt_set_int, &upsgi.alarm_freq, 0},
	{"alarm-fd", required_argument, 0, "raise the specified alarm when an fd is ready for read (by default it reads 1 byte, set 8 for eventfd)", upsgi_opt_add_string_list, &upsgi.alarm_fd_list, UPSGI_OPT_MASTER},
	{"alarm-segfault", required_argument, 0, "raise the specified alarm when the segmentation fault handler is executed", upsgi_opt_add_string_list, &upsgi.alarm_segfault, UPSGI_OPT_MASTER},
	{"segfault-alarm", required_argument, 0, "raise the specified alarm when the segmentation fault handler is executed", upsgi_opt_add_string_list, &upsgi.alarm_segfault, UPSGI_OPT_MASTER},
	{"alarm-backlog", required_argument, 0, "raise the specified alarm when the socket backlog queue is full", upsgi_opt_add_string_list, &upsgi.alarm_backlog, UPSGI_OPT_MASTER},
	{"backlog-alarm", required_argument, 0, "raise the specified alarm when the socket backlog queue is full", upsgi_opt_add_string_list, &upsgi.alarm_backlog, UPSGI_OPT_MASTER},
	{"lq-alarm", required_argument, 0, "raise the specified alarm when the socket backlog queue is full", upsgi_opt_add_string_list, &upsgi.alarm_backlog, UPSGI_OPT_MASTER},
	{"alarm-lq", required_argument, 0, "raise the specified alarm when the socket backlog queue is full", upsgi_opt_add_string_list, &upsgi.alarm_backlog, UPSGI_OPT_MASTER},
	{"alarm-listen-queue", required_argument, 0, "raise the specified alarm when the socket backlog queue is full", upsgi_opt_add_string_list, &upsgi.alarm_backlog, UPSGI_OPT_MASTER},
	{"listen-queue-alarm", required_argument, 0, "raise the specified alarm when the socket backlog queue is full", upsgi_opt_add_string_list, &upsgi.alarm_backlog, UPSGI_OPT_MASTER},
#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	{"log-alarm", required_argument, 0, "raise the specified alarm when a log line matches the specified regexp, syntax: <alarm>[,alarm...] <regexp>", upsgi_opt_add_string_list, &upsgi.alarm_logs_list, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"alarm-log", required_argument, 0, "raise the specified alarm when a log line matches the specified regexp, syntax: <alarm>[,alarm...] <regexp>", upsgi_opt_add_string_list, &upsgi.alarm_logs_list, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"not-log-alarm", required_argument, 0, "skip the specified alarm when a log line matches the specified regexp, syntax: <alarm>[,alarm...] <regexp>", upsgi_opt_add_string_list_custom, &upsgi.alarm_logs_list, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
	{"not-alarm-log", required_argument, 0, "skip the specified alarm when a log line matches the specified regexp, syntax: <alarm>[,alarm...] <regexp>", upsgi_opt_add_string_list_custom, &upsgi.alarm_logs_list, UPSGI_OPT_MASTER | UPSGI_OPT_LOG_MASTER},
#endif
	{"alarm-list", no_argument, 0, "list enabled alarms", upsgi_opt_true, &upsgi.alarms_list, 0},
	{"alarms-list", no_argument, 0, "list enabled alarms", upsgi_opt_true, &upsgi.alarms_list, 0},
	{"alarm-msg-size", required_argument, 0, "set the max size of an alarm message (default 8192)", upsgi_opt_set_64bit, &upsgi.alarm_msg_size, 0},
	{"log-master", no_argument, 0, "delegate logging to master process", upsgi_opt_true, &upsgi.log_master, UPSGI_OPT_MASTER|UPSGI_OPT_LOG_MASTER},
	{"log-master-bufsize", required_argument, 0, "set the buffer size for the master logger. bigger log messages will be truncated", upsgi_opt_set_64bit, &upsgi.log_master_bufsize, 0},
	{"log-drain-burst", required_argument, 0, "set the maximum number of log records drained per wake before yielding back to the event loop", upsgi_opt_log_drain_burst, &upsgi.log_drain_burst, 0},
	{"log-queue-enabled", no_argument, 0, "explicitly enable the bounded logger queues (enabled by default)", upsgi_opt_true, &upsgi.log_queue_enabled, 0},
	{"disable-log-queue", no_argument, 0, "disable the bounded logger queues for troubleshooting", upsgi_opt_false, &upsgi.log_queue_enabled, 0},
	{"log-queue-records", required_argument, 0, "set the maximum number of records buffered by each lossless logger queue", upsgi_opt_set_64bit, &upsgi.log_queue_records, 0},
	{"log-queue-bytes", required_argument, 0, "set the maximum total bytes buffered by each lossless logger queue", upsgi_opt_set_64bit, &upsgi.log_queue_bytes, 0},
	{"log-master-stream", no_argument, 0, "create the master logpipe as SOCK_STREAM", upsgi_opt_true, &upsgi.log_master_stream, 0},
	{"log-master-req-stream", no_argument, 0, "create the master requests logpipe as SOCK_STREAM", upsgi_opt_true, &upsgi.log_master_req_stream, 0},
	{"log-reopen", no_argument, 0, "reopen log after reload", upsgi_opt_true, &upsgi.log_reopen, 0},
	{"log-truncate", no_argument, 0, "truncate log on startup", upsgi_opt_true, &upsgi.log_truncate, 0},
	{"log-maxsize", required_argument, 0, "set maximum logfile size", upsgi_opt_set_64bit, &upsgi.log_maxsize, UPSGI_OPT_MASTER|UPSGI_OPT_LOG_MASTER},
	{"log-backupname", required_argument, 0, "set logfile name after rotation", upsgi_opt_set_str, &upsgi.log_backupname, 0},

	{"logdate", optional_argument, 0, "prefix logs with date or a strftime string", upsgi_opt_log_date, NULL, 0},
	{"log-date", optional_argument, 0, "prefix logs with date or a strftime string", upsgi_opt_log_date, NULL, 0},
	{"log-prefix", optional_argument, 0, "prefix logs with a string", upsgi_opt_log_date, NULL, 0},

	/*
	 * upsgi baseline request logging keeps request identity handling in the
	 * core path. log-x-forwarded-for is part of the supported baseline.
	 */
	{"log-zero", no_argument, 0, "log responses without body", upsgi_opt_true, &upsgi.logging_options.zero, 0},
	{"log-slow", required_argument, 0, "log requests slower than the specified number of milliseconds", upsgi_opt_set_int, &upsgi.logging_options.slow, 0},
	{"log-4xx", no_argument, 0, "log requests with a 4xx response", upsgi_opt_true, &upsgi.logging_options._4xx, 0},
	{"log-5xx", no_argument, 0, "log requests with a 5xx response", upsgi_opt_true, &upsgi.logging_options._5xx, 0},
	{"log-big", required_argument, 0, "log requests bigger than the specified size", upsgi_opt_set_64bit,  &upsgi.logging_options.big, 0},
	{"log-sendfile", required_argument, 0, "log sendfile requests", upsgi_opt_true, &upsgi.logging_options.sendfile, 0},
	{"log-ioerror", required_argument, 0, "log requests with io errors", upsgi_opt_true, &upsgi.logging_options.ioerror, 0},
	{"log-micros", no_argument, 0, "report response time in microseconds instead of milliseconds", upsgi_opt_true, &upsgi.log_micros, 0},
	{"log-x-forwarded-for", no_argument, 0, "log the client IP from X-Forwarded-For instead of REMOTE_ADDR", upsgi_opt_true, &upsgi.logging_options.log_x_forwarded_for, 0},
	{"master-as-root", no_argument, 0, "leave master process running as root", upsgi_opt_true, &upsgi.master_as_root, 0},

	{"drop-after-init", no_argument, 0, "run privileges drop after plugin initialization, superseded by drop-after-apps", upsgi_opt_true, &upsgi.drop_after_init, 0},
	{"drop-after-apps", no_argument, 0, "run privileges drop after apps loading, superseded by master-as-root", upsgi_opt_true, &upsgi.drop_after_apps, 0},

	{"force-cwd", required_argument, 0, "force the initial working directory to the specified value", upsgi_opt_set_str, &upsgi.force_cwd, 0},
	{"binsh", required_argument, 0, "override /bin/sh (used by exec hooks, it always fallback to /bin/sh)", upsgi_opt_add_string_list, &upsgi.binsh, 0},
	{"chdir", required_argument, 0, "chdir to specified directory before apps loading", upsgi_opt_set_str, &upsgi.chdir, 0},
	{"chdir2", required_argument, 0, "chdir to specified directory after apps loading", upsgi_opt_set_str, &upsgi.chdir2, 0},
	{"lazy", no_argument, 0, "set lazy mode (load apps in workers instead of master)", upsgi_opt_true, &upsgi.lazy, 0},
	{"lazy-apps", no_argument, 0, "load apps in each worker instead of the master", upsgi_opt_true, &upsgi.lazy_apps, 0},
	{"cheap", no_argument, 0, "set cheap mode (spawn workers only after the first request)", upsgi_opt_true, &upsgi.status.is_cheap, UPSGI_OPT_MASTER},
	{"cheaper", required_argument, 0, "set cheaper mode (adaptive process spawning)", upsgi_opt_set_int, &upsgi.cheaper_count, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"cheaper-initial", required_argument, 0, "set the initial number of processes to spawn in cheaper mode", upsgi_opt_set_int, &upsgi.cheaper_initial, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"cheaper-algo", required_argument, 0, "choose to algorithm used for adaptive process spawning", upsgi_opt_set_str, &upsgi.requested_cheaper_algo, UPSGI_OPT_MASTER},
	{"cheaper-step", required_argument, 0, "number of additional processes to spawn at each overload", upsgi_opt_set_int, &upsgi.cheaper_step, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"cheaper-overload", required_argument, 0, "increase workers after specified overload", upsgi_opt_set_64bit, &upsgi.cheaper_overload, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"cheaper-idle", required_argument, 0, "decrease workers after specified idle (algo: spare2) (default: 10)", upsgi_opt_set_int, &upsgi.cheaper_idle, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"cheaper-algo-list", no_argument, 0, "list enabled cheapers algorithms", upsgi_opt_true, &upsgi.cheaper_algo_list, 0},
	{"cheaper-algos-list", no_argument, 0, "list enabled cheapers algorithms", upsgi_opt_true, &upsgi.cheaper_algo_list, 0},
	{"cheaper-list", no_argument, 0, "list enabled cheapers algorithms", upsgi_opt_true, &upsgi.cheaper_algo_list, 0},
	{"cheaper-rss-limit-soft", required_argument, 0, "don't spawn new workers if total resident memory usage of all workers is higher than this limit", upsgi_opt_set_64bit, &upsgi.cheaper_rss_limit_soft, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"cheaper-rss-limit-hard", required_argument, 0, "if total workers resident memory usage is higher try to stop workers", upsgi_opt_set_64bit, &upsgi.cheaper_rss_limit_hard, UPSGI_OPT_MASTER | UPSGI_OPT_CHEAPER},
	{"idle", required_argument, 0, "set idle mode (put upsgi in cheap mode after inactivity)", upsgi_opt_set_int, &upsgi.idle, UPSGI_OPT_MASTER},
	{"die-on-idle", no_argument, 0, "shutdown upsgi when idle", upsgi_opt_true, &upsgi.die_on_idle, 0},
	{"die-on-no-workers", no_argument, 0, "shutdown upsgi when no workers are running", upsgi_opt_true, &upsgi.die_on_no_workers, 0},
	{"mount", required_argument, 0, "load application under mountpoint", upsgi_opt_add_string_list, &upsgi.mounts, 0},
	{"worker-mount", required_argument, 0, "load application under mountpoint in the specified worker or after workers spawn", upsgi_opt_add_string_list, &upsgi.mounts, 0},

	{"threads", required_argument, 0, "run each worker in prethreaded mode with the specified number of threads", upsgi_opt_set_int, &upsgi.threads, UPSGI_OPT_THREADS},
	{"thread-stacksize", required_argument, 0, "set threads stacksize", upsgi_opt_set_int, &upsgi.threads_stacksize, UPSGI_OPT_THREADS},
	{"threads-stacksize", required_argument, 0, "set threads stacksize", upsgi_opt_set_int, &upsgi.threads_stacksize, UPSGI_OPT_THREADS},
	{"thread-stack-size", required_argument, 0, "set threads stacksize", upsgi_opt_set_int, &upsgi.threads_stacksize, UPSGI_OPT_THREADS},
	{"threads-stack-size", required_argument, 0, "set threads stacksize", upsgi_opt_set_int, &upsgi.threads_stacksize, UPSGI_OPT_THREADS},

	{"vhost", no_argument, 0, "enable virtualhosting mode (based on SERVER_NAME variable)", upsgi_opt_true, &upsgi.vhost, 0},
	{"vhost-host", no_argument, 0, "enable virtualhosting mode (based on HTTP_HOST variable)", upsgi_opt_true, &upsgi.vhost_host, UPSGI_OPT_VHOST},
#ifdef UPSGI_ROUTING
	{"route", required_argument, 0, "add a route", upsgi_opt_add_route, "path_info", 0},
	{"route-host", required_argument, 0, "add a route based on Host header", upsgi_opt_add_route, "http_host", 0},
	{"route-uri", required_argument, 0, "add a route based on REQUEST_URI", upsgi_opt_add_route, "request_uri", 0},
	{"route-qs", required_argument, 0, "add a route based on QUERY_STRING", upsgi_opt_add_route, "query_string", 0},
	{"route-remote-addr", required_argument, 0, "add a route based on REMOTE_ADDR", upsgi_opt_add_route, "remote_addr", 0},
	{"route-user-agent", required_argument, 0, "add a route based on HTTP_USER_AGENT", upsgi_opt_add_route, "user_agent", 0},
	{"route-remote-user", required_argument, 0, "add a route based on REMOTE_USER", upsgi_opt_add_route, "remote_user", 0},
	{"route-referer", required_argument, 0, "add a route based on HTTP_REFERER", upsgi_opt_add_route, "referer", 0},
	{"route-label", required_argument, 0, "add a routing label (for use with goto)", upsgi_opt_add_route, NULL, 0},
	{"route-if", required_argument, 0, "add a route based on condition", upsgi_opt_add_route, "if", 0},
	{"route-if-not", required_argument, 0, "add a route based on condition (negate version)", upsgi_opt_add_route, "if-not", 0},
	{"route-run", required_argument, 0, "always run the specified route action", upsgi_opt_add_route, "run", 0},



	{"final-route", required_argument, 0, "add a final route", upsgi_opt_add_route, "path_info", 0},
	{"final-route-status", required_argument, 0, "add a final route for the specified status", upsgi_opt_add_route, "status", 0},
        {"final-route-host", required_argument, 0, "add a final route based on Host header", upsgi_opt_add_route, "http_host", 0},
        {"final-route-uri", required_argument, 0, "add a final route based on REQUEST_URI", upsgi_opt_add_route, "request_uri", 0},
        {"final-route-qs", required_argument, 0, "add a final route based on QUERY_STRING", upsgi_opt_add_route, "query_string", 0},
        {"final-route-remote-addr", required_argument, 0, "add a final route based on REMOTE_ADDR", upsgi_opt_add_route, "remote_addr", 0},
        {"final-route-user-agent", required_argument, 0, "add a final route based on HTTP_USER_AGENT", upsgi_opt_add_route, "user_agent", 0},
        {"final-route-remote-user", required_argument, 0, "add a final route based on REMOTE_USER", upsgi_opt_add_route, "remote_user", 0},
        {"final-route-referer", required_argument, 0, "add a final route based on HTTP_REFERER", upsgi_opt_add_route, "referer", 0},
        {"final-route-label", required_argument, 0, "add a final routing label (for use with goto)", upsgi_opt_add_route, NULL, 0},
        {"final-route-if", required_argument, 0, "add a final route based on condition", upsgi_opt_add_route, "if", 0},
        {"final-route-if-not", required_argument, 0, "add a final route based on condition (negate version)", upsgi_opt_add_route, "if-not", 0},
        {"final-route-run", required_argument, 0, "always run the specified final route action", upsgi_opt_add_route, "run", 0},

	{"error-route", required_argument, 0, "add an error route", upsgi_opt_add_route, "path_info", 0},
	{"error-route-status", required_argument, 0, "add an error route for the specified status", upsgi_opt_add_route, "status", 0},
        {"error-route-host", required_argument, 0, "add an error route based on Host header", upsgi_opt_add_route, "http_host", 0},
        {"error-route-uri", required_argument, 0, "add an error route based on REQUEST_URI", upsgi_opt_add_route, "request_uri", 0},
        {"error-route-qs", required_argument, 0, "add an error route based on QUERY_STRING", upsgi_opt_add_route, "query_string", 0},
        {"error-route-remote-addr", required_argument, 0, "add an error route based on REMOTE_ADDR", upsgi_opt_add_route, "remote_addr", 0},
        {"error-route-user-agent", required_argument, 0, "add an error route based on HTTP_USER_AGENT", upsgi_opt_add_route, "user_agent", 0},
        {"error-route-remote-user", required_argument, 0, "add an error route based on REMOTE_USER", upsgi_opt_add_route, "remote_user", 0},
        {"error-route-referer", required_argument, 0, "add an error route based on HTTP_REFERER", upsgi_opt_add_route, "referer", 0},
        {"error-route-label", required_argument, 0, "add an error routing label (for use with goto)", upsgi_opt_add_route, NULL, 0},
        {"error-route-if", required_argument, 0, "add an error route based on condition", upsgi_opt_add_route, "if", 0},
        {"error-route-if-not", required_argument, 0, "add an error route based on condition (negate version)", upsgi_opt_add_route, "if-not", 0},
        {"error-route-run", required_argument, 0, "always run the specified error route action", upsgi_opt_add_route, "run", 0},

	{"response-route", required_argument, 0, "add a response route", upsgi_opt_add_route, "path_info", 0},
        {"response-route-status", required_argument, 0, "add a response route for the specified status", upsgi_opt_add_route, "status", 0},
        {"response-route-host", required_argument, 0, "add a response route based on Host header", upsgi_opt_add_route, "http_host", 0},
        {"response-route-uri", required_argument, 0, "add a response route based on REQUEST_URI", upsgi_opt_add_route, "request_uri", 0},
        {"response-route-qs", required_argument, 0, "add a response route based on QUERY_STRING", upsgi_opt_add_route, "query_string", 0},
        {"response-route-remote-addr", required_argument, 0, "add a response route based on REMOTE_ADDR", upsgi_opt_add_route, "remote_addr", 0},
        {"response-route-user-agent", required_argument, 0, "add a response route based on HTTP_USER_AGENT", upsgi_opt_add_route, "user_agent", 0},
        {"response-route-remote-user", required_argument, 0, "add a response route based on REMOTE_USER", upsgi_opt_add_route, "remote_user", 0},
        {"response-route-referer", required_argument, 0, "add a response route based on HTTP_REFERER", upsgi_opt_add_route, "referer", 0},
        {"response-route-label", required_argument, 0, "add a response routing label (for use with goto)", upsgi_opt_add_route, NULL, 0},
        {"response-route-if", required_argument, 0, "add a response route based on condition", upsgi_opt_add_route, "if", 0},
        {"response-route-if-not", required_argument, 0, "add a response route based on condition (negate version)", upsgi_opt_add_route, "if-not", 0},
        {"response-route-run", required_argument, 0, "always run the specified response route action", upsgi_opt_add_route, "run", 0},

	{"router-list", no_argument, 0, "list enabled routers", upsgi_opt_true, &upsgi.router_list, 0},
	{"routers-list", no_argument, 0, "list enabled routers", upsgi_opt_true, &upsgi.router_list, 0},
#endif


	{"error-page-403", required_argument, 0, "add an error page (html) for managed 403 response", upsgi_opt_add_string_list, &upsgi.error_page_403, 0},
	{"error-page-404", required_argument, 0, "add an error page (html) for managed 404 response", upsgi_opt_add_string_list, &upsgi.error_page_404, 0},
	{"error-page-500", required_argument, 0, "add an error page (html) for managed 500 response", upsgi_opt_add_string_list, &upsgi.error_page_500, 0},

	{"websockets-ping-freq", required_argument, 0, "set the frequency (in seconds) of websockets automatic ping packets", upsgi_opt_set_int, &upsgi.websockets_ping_freq, 0},
	{"websocket-ping-freq", required_argument, 0, "set the frequency (in seconds) of websockets automatic ping packets", upsgi_opt_set_int, &upsgi.websockets_ping_freq, 0},

	{"websockets-pong-tolerance", required_argument, 0, "set the tolerance (in seconds) of websockets ping/pong subsystem", upsgi_opt_set_int, &upsgi.websockets_pong_tolerance, 0},
	{"websocket-pong-tolerance", required_argument, 0, "set the tolerance (in seconds) of websockets ping/pong subsystem", upsgi_opt_set_int, &upsgi.websockets_pong_tolerance, 0},

	{"websockets-max-size", required_argument, 0, "set the max allowed size of websocket messages (in Kbytes, default 1024)", upsgi_opt_set_64bit, &upsgi.websockets_max_size, 0},
	{"websocket-max-size", required_argument, 0, "set the max allowed size of websocket messages (in Kbytes, default 1024)", upsgi_opt_set_64bit, &upsgi.websockets_max_size, 0},

	{"chunked-input-limit", required_argument, 0, "set the max size of a chunked input part (default 1MB, in bytes)", upsgi_opt_set_64bit, &upsgi.chunked_input_limit, 0},
	{"chunked-input-timeout", required_argument, 0, "set default timeout for chunked input", upsgi_opt_set_int, &upsgi.chunked_input_timeout, 0},

	{"clock", required_argument, 0, "set a clock source", upsgi_opt_set_str, &upsgi.requested_clock, 0},

	{"clock-list", no_argument, 0, "list enabled clocks", upsgi_opt_true, &upsgi.clock_list, 0},
	{"clocks-list", no_argument, 0, "list enabled clocks", upsgi_opt_true, &upsgi.clock_list, 0},

	{"add-header", required_argument, 0, "automatically add HTTP headers to response", upsgi_opt_add_string_list, &upsgi.additional_headers, 0},
	{"rem-header", required_argument, 0, "automatically remove specified HTTP header from the response", upsgi_opt_add_string_list, &upsgi.remove_headers, 0},
	{"del-header", required_argument, 0, "automatically remove specified HTTP header from the response", upsgi_opt_add_string_list, &upsgi.remove_headers, 0},
	{"collect-header", required_argument, 0, "store the specified response header in a request var (syntax: header var)", upsgi_opt_add_string_list, &upsgi.collect_headers, 0},
	{"response-header-collect", required_argument, 0, "store the specified response header in a request var (syntax: header var)", upsgi_opt_add_string_list, &upsgi.collect_headers, 0},

	{"pull-header", required_argument, 0, "store the specified response header in a request var and remove it from the response (syntax: header var)", upsgi_opt_add_string_list, &upsgi.pull_headers, 0},

	/*
	 * upsgi baseline static serving stays in the core request path:
	 * - interception lives in core/protocol.c
	 * - file serving lives in core/static.c
	 * - router_static is a separate routing feature and is not required for
	 *   static-map support
	 */
	{"check-static", required_argument, 0, "check for static files in the specified directory", upsgi_opt_check_static, NULL, UPSGI_OPT_MIME},
	{"check-static-docroot", no_argument, 0, "check for static files in the requested DOCUMENT_ROOT", upsgi_opt_true, &upsgi.check_static_docroot, UPSGI_OPT_MIME},
	{"static-check", required_argument, 0, "check for static files in the specified directory", upsgi_opt_check_static, NULL, UPSGI_OPT_MIME},
	{"static-map", required_argument, 0, "map mountpoint to static directory (or file); baseline static serving path", upsgi_opt_static_map, &upsgi.static_maps, UPSGI_OPT_MIME},
	{"static-map2", required_argument, 0, "like static-map but completely appending the requested resource to the docroot", upsgi_opt_static_map, &upsgi.static_maps2, UPSGI_OPT_MIME},
	{"static-skip-ext", required_argument, 0, "skip specified extension from staticfile checks", upsgi_opt_add_string_list, &upsgi.static_skip_ext, UPSGI_OPT_MIME},
	{"static-index", required_argument, 0, "search for specified file if a directory is requested", upsgi_opt_add_string_list, &upsgi.static_index, UPSGI_OPT_MIME},
	{"static-safe", required_argument, 0, "skip security checks if the file is under the specified path", upsgi_opt_add_string_list, &upsgi.static_safe, UPSGI_OPT_MIME},
	{"static-cache-paths", required_argument, 0, "put resolved paths in the upsgi cache for the specified amount of seconds", upsgi_opt_set_int, &upsgi.use_static_cache_paths, UPSGI_OPT_MIME|UPSGI_OPT_MASTER},
	{"static-cache-paths-name", required_argument, 0, "use the specified cache for static paths", upsgi_opt_set_str, &upsgi.static_cache_paths_name, UPSGI_OPT_MIME|UPSGI_OPT_MASTER},
#ifdef __APPLE__
	{"mimefile", required_argument, 0, "set mime types file path (default /etc/apache2/mime.types)", upsgi_opt_add_string_list, &upsgi.mime_file, UPSGI_OPT_MIME},
	{"mime-file", required_argument, 0, "set mime types file path (default /etc/apache2/mime.types)", upsgi_opt_add_string_list, &upsgi.mime_file, UPSGI_OPT_MIME},
#else
	{"mimefile", required_argument, 0, "set mime types file path (default /etc/mime.types)", upsgi_opt_add_string_list, &upsgi.mime_file, UPSGI_OPT_MIME},
	{"mime-file", required_argument, 0, "set mime types file path (default /etc/mime.types)", upsgi_opt_add_string_list, &upsgi.mime_file, UPSGI_OPT_MIME},
#endif

	{"static-expires-type", required_argument, 0, "set the Expires header based on content type", upsgi_opt_add_dyn_dict, &upsgi.static_expires_type, UPSGI_OPT_MIME},
	{"static-expires-type-mtime", required_argument, 0, "set the Expires header based on content type and file mtime", upsgi_opt_add_dyn_dict, &upsgi.static_expires_type_mtime, UPSGI_OPT_MIME},

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	{"static-expires", required_argument, 0, "set the Expires header based on filename regexp", upsgi_opt_add_regexp_dyn_dict, &upsgi.static_expires, UPSGI_OPT_MIME},
	{"static-expires-mtime", required_argument, 0, "set the Expires header based on filename regexp and file mtime", upsgi_opt_add_regexp_dyn_dict, &upsgi.static_expires_mtime, UPSGI_OPT_MIME},

	{"static-expires-uri", required_argument, 0, "set the Expires header based on REQUEST_URI regexp", upsgi_opt_add_regexp_dyn_dict, &upsgi.static_expires_uri, UPSGI_OPT_MIME},
	{"static-expires-uri-mtime", required_argument, 0, "set the Expires header based on REQUEST_URI regexp and file mtime", upsgi_opt_add_regexp_dyn_dict, &upsgi.static_expires_uri_mtime, UPSGI_OPT_MIME},

	{"static-expires-path-info", required_argument, 0, "set the Expires header based on PATH_INFO regexp", upsgi_opt_add_regexp_dyn_dict, &upsgi.static_expires_path_info, UPSGI_OPT_MIME},
	{"static-expires-path-info-mtime", required_argument, 0, "set the Expires header based on PATH_INFO regexp and file mtime", upsgi_opt_add_regexp_dyn_dict, &upsgi.static_expires_path_info_mtime, UPSGI_OPT_MIME},
	{"static-gzip", required_argument, 0, "if the supplied regexp matches the static file translation it will search for a gzip version", upsgi_opt_add_regexp_list, &upsgi.static_gzip, UPSGI_OPT_MIME},
#endif
	{"static-gzip-all", no_argument, 0, "check for a gzip version of all requested static files", upsgi_opt_true, &upsgi.static_gzip_all, UPSGI_OPT_MIME},
	{"static-gzip-dir", required_argument, 0, "check for a gzip version of all requested static files in the specified dir/prefix", upsgi_opt_add_string_list, &upsgi.static_gzip_dir, UPSGI_OPT_MIME},
	{"static-gzip-prefix", required_argument, 0, "check for a gzip version of all requested static files in the specified dir/prefix", upsgi_opt_add_string_list, &upsgi.static_gzip_dir, UPSGI_OPT_MIME},
	{"static-gzip-ext", required_argument, 0, "check for a gzip version of all requested static files with the specified ext/suffix", upsgi_opt_add_string_list, &upsgi.static_gzip_ext, UPSGI_OPT_MIME},
	{"static-gzip-suffix", required_argument, 0, "check for a gzip version of all requested static files with the specified ext/suffix", upsgi_opt_add_string_list, &upsgi.static_gzip_ext, UPSGI_OPT_MIME},

	{"honour-range", no_argument, 0, "enable support for the HTTP Range header", upsgi_opt_true, &upsgi.honour_range, 0},

	{"offload-threads", required_argument, 0, "set the number of offload threads to spawn (per-worker, default 0)", upsgi_opt_set_int, &upsgi.offload_threads, 0},
	{"offload-thread", required_argument, 0, "set the number of offload threads to spawn (per-worker, default 0)", upsgi_opt_set_int, &upsgi.offload_threads, 0},

	{"file-serve-mode", required_argument, 0, "set static file serving mode", upsgi_opt_fileserve_mode, NULL, UPSGI_OPT_MIME},
	{"fileserve-mode", required_argument, 0, "set static file serving mode", upsgi_opt_fileserve_mode, NULL, UPSGI_OPT_MIME},

	{"disable-sendfile", no_argument, 0, "disable sendfile() and rely on boring read()/write()", upsgi_opt_true, &upsgi.disable_sendfile, 0},

	{"close-on-exec", no_argument, 0, "set close-on-exec on connection sockets (could be required for spawning processes in requests)", upsgi_opt_true, &upsgi.close_on_exec, 0},
	{"close-on-exec2", no_argument, 0, "set close-on-exec on server sockets (could be required for spawning processes in requests)", upsgi_opt_true, &upsgi.close_on_exec2, 0},
	{"mode", required_argument, 0, "set upsgi custom mode", upsgi_opt_set_str, &upsgi.mode, 0},
	{"env", required_argument, 0, "set environment variable", upsgi_opt_set_env, NULL, 0},
	{"ienv", required_argument, 0, "set environment variable (IMMEDIATE version)", upsgi_opt_set_env, NULL, UPSGI_OPT_IMMEDIATE},
	{"envdir", required_argument, 0, "load a daemontools compatible envdir", upsgi_opt_add_string_list, &upsgi.envdirs, 0},
	{"early-envdir", required_argument, 0, "load a daemontools compatible envdir ASAP", upsgi_opt_envdir, NULL, UPSGI_OPT_IMMEDIATE},
	{"unenv", required_argument, 0, "unset environment variable", upsgi_opt_unset_env, NULL, 0},
	{"vacuum", no_argument, 0, "try to remove all of the generated file/sockets", upsgi_opt_true, &upsgi.vacuum, 0},
	{"file-write", required_argument, 0, "write the specified content to the specified file (syntax: file=value) before privileges drop", upsgi_opt_add_string_list, &upsgi.file_write_list, 0},
#ifdef __linux__
	{"cgroup", required_argument, 0, "put the processes in the specified cgroup", upsgi_opt_add_string_list, &upsgi.cgroup, 0},
	{"cgroup-opt", required_argument, 0, "set value in specified cgroup option", upsgi_opt_add_string_list, &upsgi.cgroup_opt, 0},
	{"cgroup-dir-mode", required_argument, 0, "set permission for cgroup directory (default is 700)", upsgi_opt_set_str, &upsgi.cgroup_dir_mode, 0},
	{"namespace", required_argument, 0, "run in a new namespace under the specified rootfs", upsgi_opt_set_str, &upsgi.ns, 0},
	{"namespace-keep-mount", required_argument, 0, "keep the specified mountpoint in your namespace", upsgi_opt_add_string_list, &upsgi.ns_keep_mount, 0},
	{"ns", required_argument, 0, "run in a new namespace under the specified rootfs", upsgi_opt_set_str, &upsgi.ns, 0},
	{"namespace-net", required_argument, 0, "add network namespace", upsgi_opt_set_str, &upsgi.ns_net, 0},
	{"ns-net", required_argument, 0, "add network namespace", upsgi_opt_set_str, &upsgi.ns_net, 0},
#endif
	{"enable-proxy-protocol", no_argument, 0, "enable PROXY1 protocol support (only for http parsers)", upsgi_opt_true, &upsgi.enable_proxy_protocol, 0},
	{"reuse-port", no_argument, 0, "enable REUSE_PORT flag on socket (BSD and Linux >3.9 only)", upsgi_opt_true, &upsgi.reuse_port, 0},
	{"tcp-fast-open", required_argument, 0, "enable TCP_FASTOPEN flag on TCP sockets with the specified qlen value", upsgi_opt_set_int, &upsgi.tcp_fast_open, 0},
	{"tcp-fastopen", required_argument, 0, "enable TCP_FASTOPEN flag on TCP sockets with the specified qlen value", upsgi_opt_set_int, &upsgi.tcp_fast_open, 0},
	{"tcp-fast-open-client", no_argument, 0, "use sendto(..., MSG_FASTOPEN, ...) instead of connect() if supported", upsgi_opt_true, &upsgi.tcp_fast_open_client, 0},
	{"tcp-fastopen-client", no_argument, 0, "use sendto(..., MSG_FASTOPEN, ...) instead of connect() if supported", upsgi_opt_true, &upsgi.tcp_fast_open_client, 0},
	{"zerg", required_argument, 0, "attach to a zerg server", upsgi_opt_add_string_list, &upsgi.zerg_node, 0},
	{"zerg-fallback", no_argument, 0, "fallback to normal sockets if the zerg server is not available", upsgi_opt_true, &upsgi.zerg_fallback, 0},
	{"zerg-server", required_argument, 0, "enable the zerg server on the specified UNIX socket", upsgi_opt_set_str, &upsgi.zerg_server, UPSGI_OPT_MASTER},

	{"cron", required_argument, 0, "add a cron task", upsgi_opt_add_cron, NULL, UPSGI_OPT_MASTER},
	{"cron2", required_argument, 0, "add a cron task (key=val syntax)", upsgi_opt_add_cron2, NULL, UPSGI_OPT_MASTER},
	{"unique-cron", required_argument, 0, "add a unique cron task", upsgi_opt_add_unique_cron, NULL, UPSGI_OPT_MASTER},
	{"cron-harakiri", required_argument, 0, "set the maximum time (in seconds) we wait for cron command to complete", upsgi_opt_set_int, &upsgi.cron_harakiri, 0},
	{"loop", required_argument, 0, "select the upsgi loop engine", upsgi_opt_set_str, &upsgi.loop, 0},
	{"loop-list", no_argument, 0, "list enabled loop engines", upsgi_opt_true, &upsgi.loop_list, 0},
	{"loops-list", no_argument, 0, "list enabled loop engines", upsgi_opt_true, &upsgi.loop_list, 0},
	{"worker-exec", required_argument, 0, "run the specified command as worker", upsgi_opt_set_str, &upsgi.worker_exec, 0},
	{"worker-exec2", required_argument, 0, "run the specified command as worker (after post_fork hook)", upsgi_opt_set_str, &upsgi.worker_exec2, 0},
	{"attach-daemon", required_argument, 0, "attach a command/daemon to the master process (the command has to not go in background)", upsgi_opt_add_daemon, NULL, UPSGI_OPT_MASTER},
	{"attach-control-daemon", required_argument, 0, "attach a command/daemon to the master process (the command has to not go in background), when the daemon dies, the master dies too", upsgi_opt_add_daemon, NULL, UPSGI_OPT_MASTER},
	{"smart-attach-daemon", required_argument, 0, "attach a command/daemon to the master process managed by a pidfile (the command has to daemonize)", upsgi_opt_add_daemon, NULL, UPSGI_OPT_MASTER},
	{"smart-attach-daemon2", required_argument, 0, "attach a command/daemon to the master process managed by a pidfile (the command has to NOT daemonize)", upsgi_opt_add_daemon, NULL, UPSGI_OPT_MASTER},
	{"daemons-honour-stdin", no_argument, 0, "do not change the stdin of external daemons to /dev/null", upsgi_opt_true, &upsgi.daemons_honour_stdin, UPSGI_OPT_MASTER},
	{"attach-daemon2", required_argument, 0, "attach-daemon keyval variant (supports smart modes too)", upsgi_opt_add_daemon2, NULL, UPSGI_OPT_MASTER},
	{"plugins", required_argument, 0, "load upsgi plugins", upsgi_opt_load_plugin, NULL, UPSGI_OPT_IMMEDIATE},
	{"plugin", required_argument, 0, "load upsgi plugins", upsgi_opt_load_plugin, NULL, UPSGI_OPT_IMMEDIATE},
	{"need-plugins", required_argument, 0, "load upsgi plugins (exit on error)", upsgi_opt_load_plugin, NULL, UPSGI_OPT_IMMEDIATE},
	{"need-plugin", required_argument, 0, "load upsgi plugins (exit on error)", upsgi_opt_load_plugin, NULL, UPSGI_OPT_IMMEDIATE},
	{"plugins-dir", required_argument, 0, "add a directory to upsgi plugin search path", upsgi_opt_add_string_list, &upsgi.plugins_dir, UPSGI_OPT_IMMEDIATE},
	{"plugin-dir", required_argument, 0, "add a directory to upsgi plugin search path", upsgi_opt_add_string_list, &upsgi.plugins_dir, UPSGI_OPT_IMMEDIATE},
	{"plugins-list", no_argument, 0, "list enabled plugins", upsgi_opt_true, &upsgi.plugins_list, 0},
	{"plugin-list", no_argument, 0, "list enabled plugins", upsgi_opt_true, &upsgi.plugins_list, 0},
	{"autoload", no_argument, 0, "try to automatically load plugins when unknown options are found", upsgi_opt_true, &upsgi.autoload, UPSGI_OPT_IMMEDIATE},
	{"dlopen", required_argument, 0, "blindly load a shared library", upsgi_opt_load_dl, NULL, UPSGI_OPT_IMMEDIATE},
	{"allowed-modifiers", required_argument, 0, "comma separated list of allowed modifiers", upsgi_opt_set_str, &upsgi.allowed_modifiers, 0},
	{"remap-modifier", required_argument, 0, "remap request modifier from one id to another", upsgi_opt_set_str, &upsgi.remap_modifier, 0},

	{"dump-options", no_argument, 0, "dump the full list of available options", upsgi_opt_true, &upsgi.dump_options, 0},
	{"show-config", no_argument, 0, "show the current config reformatted as ini", upsgi_opt_true, &upsgi.show_config, 0},
	{"binary-append-data", required_argument, 0, "return the content of a resource to stdout for appending to a upsgi binary (for data:// usage)", upsgi_opt_binary_append_data, NULL, UPSGI_OPT_IMMEDIATE},
	{"print", required_argument, 0, "simple print", upsgi_opt_print, NULL, 0},
	{"iprint", required_argument, 0, "simple print (immediate version)", upsgi_opt_print, NULL, UPSGI_OPT_IMMEDIATE},
	{"exit", optional_argument, 0, "force exit() of the instance", upsgi_opt_exit, NULL, UPSGI_OPT_IMMEDIATE},
	{"cflags", no_argument, 0, "report core build CFLAGS (useful for building external plugins)", upsgi_opt_cflags, NULL, UPSGI_OPT_IMMEDIATE},
	{"dot-h", no_argument, 0, "dump the upsgi.h used for building the core (useful for building external plugins)", upsgi_opt_dot_h, NULL, UPSGI_OPT_IMMEDIATE},
	{"config-py", no_argument, 0, "dump the upsgiconfig.py used for building the core (useful for building external plugins)", upsgi_opt_config_py, NULL, UPSGI_OPT_IMMEDIATE},
	{"build-plugin", required_argument, 0, "build a plugin for the current binary", upsgi_opt_build_plugin, NULL, UPSGI_OPT_IMMEDIATE},
	{"version", no_argument, 0, "print upsgi version", upsgi_opt_print, UPSGI_VERSION, 0},
	{"response-headers-limit", required_argument, 0, "set response header maximum size (default: 64k)", upsgi_opt_set_int, &upsgi.response_header_limit, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

void show_config(void) {
	int i;
	upsgi_log("\n;upsgi instance configuration\n[upsgi]\n");
	for (i = 0; i < upsgi.exported_opts_cnt; i++) {
		if (upsgi.exported_opts[i]->value) {
			upsgi_log("%s = %s\n", upsgi.exported_opts[i]->key, upsgi.exported_opts[i]->value);
		}
		else {
			upsgi_log("%s = true\n", upsgi.exported_opts[i]->key);
		}
	}
	upsgi_log(";end of configuration\n\n");

}

void config_magic_table_fill(char *filename, char **magic_table) {

	char *tmp = NULL;
	char *fullname = filename;

	magic_table['o'] = filename;

	if (upsgi_check_scheme(filename) || !strcmp(filename, "-")) {
		return;
	}

        char *section = upsgi_get_last_char(filename, ':');
        if (section) {
                *section = 0;
		if (section == filename) {
			goto reuse;
		}
	}


	// we have a special case for symlinks
	if (upsgi_is_link(filename)) {
		if (filename[0] != '/') {
			fullname = upsgi_concat3(upsgi.cwd, "/", filename);
		}
	}
	else {

		fullname = upsgi_expand_path(filename, strlen(filename), NULL);
		if (!fullname) {
			exit(1);
		}
		char *minimal_name = upsgi_malloc(strlen(fullname) + 1);
		memcpy(minimal_name, fullname, strlen(fullname));
		minimal_name[strlen(fullname)] = 0;
		free(fullname);
		fullname = minimal_name;
	}

	magic_table['b'] = upsgi.binary_path;
	magic_table['p'] = fullname;

	// compute filename hash
	uint32_t hash = djb33x_hash(magic_table['p'], strlen(magic_table['p']));
	char *hex = upsgi_str_to_hex((char *)&hash, 4);
	magic_table['j'] = upsgi_concat2n(hex, 8, "", 0);
	free(hex);

	struct stat st;
	if (!lstat(fullname, &st)) {
		magic_table['i'] = upsgi_num2str(st.st_ino);
	}

	magic_table['s'] = upsgi_get_last_char(fullname, '/') + 1;

	magic_table['d'] = upsgi_concat2n(magic_table['p'], magic_table['s'] - magic_table['p'], "", 0);
	if (magic_table['d'][strlen(magic_table['d']) - 1] == '/') {
		tmp = magic_table['d'] + (strlen(magic_table['d']) - 1);
#ifdef UPSGI_DEBUG
		upsgi_log("tmp = %c\n", *tmp);
#endif
		*tmp = 0;
	}

	// clear optional vars
	magic_table['c'] = "";
	magic_table['e'] = "";
	magic_table['n'] = magic_table['s'];

	magic_table['0'] = "";
	magic_table['1'] = "";
	magic_table['2'] = "";
	magic_table['3'] = "";
	magic_table['4'] = "";
	magic_table['5'] = "";
	magic_table['6'] = "";
	magic_table['7'] = "";
	magic_table['8'] = "";
	magic_table['9'] = "";

	if (upsgi_get_last_char(magic_table['d'], '/')) {
		magic_table['c'] = upsgi_str(upsgi_get_last_char(magic_table['d'], '/') + 1);
		if (magic_table['c'][strlen(magic_table['c']) - 1] == '/') {
			magic_table['c'][strlen(magic_table['c']) - 1] = 0;
		}
	}

	int base = '0';
	char *to_split = upsgi_str(magic_table['d']);
	char *p, *ctx = NULL;
	upsgi_foreach_token(to_split, "/", p, ctx) {
		if (base <= '9') {
			magic_table[base] = p;
			base++;
		}
		else {
			break;
		}
	}

	if (tmp)
		*tmp = '/';

	if (upsgi_get_last_char(magic_table['s'], '.'))
		magic_table['e'] = upsgi_get_last_char(magic_table['s'], '.') + 1;
	if (upsgi_get_last_char(magic_table['s'], '.'))
		magic_table['n'] = upsgi_concat2n(magic_table['s'], upsgi_get_last_char(magic_table['s'], '.') - magic_table['s'], "", 0);

reuse:
	magic_table['x'] = "";
	if (section) {
		magic_table['x'] = section+1;
		*section = ':';
	}

	// first round ?
	if (!upsgi.magic_table_first_round) { 
		magic_table['O'] = magic_table['o'];
                magic_table['D'] = magic_table['d'];
                magic_table['S'] = magic_table['s'];
                magic_table['P'] = magic_table['p'];
                magic_table['C'] = magic_table['c'];
                magic_table['E'] = magic_table['e'];
                magic_table['N'] = magic_table['n'];
                magic_table['X'] = magic_table['x'];
                magic_table['I'] = magic_table['i'];
                magic_table['J'] = magic_table['j'];
		upsgi.magic_table_first_round = 1;
        }

}

int find_worker_id(pid_t pid) {
	int i;
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid == pid)
			return i;
	}

	return -1;
}


void warn_pipe() {
	struct wsgi_request *wsgi_req = current_wsgi_req();

	if (upsgi.threads < 2 && wsgi_req->uri_len > 0) {
		upsgi_log_verbose("SIGPIPE: writing to a closed pipe/socket/fd (probably the client disconnected) on request %.*s (ip %.*s) !!!\n", wsgi_req->uri_len, wsgi_req->uri, wsgi_req->remote_addr_len, wsgi_req->remote_addr);
	}
	else {
		upsgi_log_verbose("SIGPIPE: writing to a closed pipe/socket/fd (probably the client disconnected) !!!\n");
	}
}

// This function is called from signal handler or main thread to wait worker threads.
// `upsgi.workers[upsgi.mywid].manage_next_request` should be set to 0 to stop worker threads.
static void wait_for_threads() {
	int i, ret;

	// This option was added because we used pthread_cancel().
	// thread cancellation is REALLY flaky
	if (upsgi.no_threads_wait) return;

	// wait for thread termination
	for (i = 0; i < upsgi.threads; i++) {
		if (!pthread_equal(upsgi.workers[upsgi.mywid].cores[i].thread_id, pthread_self())) {
			ret = pthread_join(upsgi.workers[upsgi.mywid].cores[i].thread_id, NULL);
			if (ret) {
				upsgi_log("pthread_join() = %d\n", ret);
			}
			else {
				// upsgi_worker_is_busy() should not consider this thread as busy.
				upsgi.workers[upsgi.mywid].cores[i].in_request = 0;
			}
		}
	}
}


void gracefully_kill(int signum) {

	upsgi_log("Gracefully killing worker %d (pid: %d)...\n", upsgi.mywid, upsgi.mypid);
	upsgi.workers[upsgi.mywid].manage_next_request = 0;

	if (upsgi.threads > 1) {
		// Stop event_queue_wait() in other threads.
		// We use loop_stop_pipe only in threaded workers to avoid
		// unintensional behavior changes in single threaded workers.
		int fd;
		if ((fd = upsgi.loop_stop_pipe[1]) > 0) {
			close(fd);
			upsgi.loop_stop_pipe[1] = 0;
		}
		return;
	}

	// still not found a way to gracefully reload in async mode
	if (upsgi.async > 0) {
		if (upsgi.workers[upsgi.mywid].shutdown_sockets)
			upsgi_shutdown_all_sockets();
		exit(UPSGI_RELOAD_CODE);
	}

	if (!upsgi.workers[upsgi.mywid].cores[0].in_request) {
		if (upsgi.workers[upsgi.mywid].shutdown_sockets)
			upsgi_shutdown_all_sockets();
		exit(UPSGI_RELOAD_CODE);
	}
}

void end_me(int signum) {
	if (getpid() != masterpid && upsgi.skip_atexit) {
		_exit(UPSGI_END_CODE);
		// never here
	}
	exit(UPSGI_END_CODE);
}

static void simple_goodbye_cruel_world(const char *reason) {
	int prev = upsgi.workers[upsgi.mywid].manage_next_request;
	upsgi.workers[upsgi.mywid].manage_next_request = 0;
	if (prev) {
		// Avoid showing same message from all threads.
		upsgi_log("...The work of process %d is done (%s). Seeya!\n", getpid(), (reason != NULL ? reason : "no reason given"));
	}

	if (upsgi.threads > 1) {
		// Stop event_queue_wait() in other threads.
		// We use loop_stop_pipe only in threaded workers to avoid
		// unintensional behavior changes in single threaded workers.
		int fd;
		if ((fd = upsgi.loop_stop_pipe[1]) > 0) {
			close(fd);
			upsgi.loop_stop_pipe[1] = 0;
		}
	}
}

void goodbye_cruel_world(const char *reason_fmt, ...) {
	char reason[1024];
	va_list args;
	va_start(args, reason_fmt);
	vsnprintf(reason, 1024, reason_fmt, args);
	va_end(args);

	upsgi_curse(upsgi.mywid, 0);

	if (!upsgi.gbcw_hook) {
		simple_goodbye_cruel_world(reason);
	}
	else {
		upsgi.gbcw_hook(reason);
	}
}

// brutally destroy
void kill_them_all(int signum) {

	if (upsgi_instance_is_dying) return;
	upsgi.status.brutally_destroying = 1;

	upsgi_log("SIGINT/SIGTERM received...killing workers...\n");

	int i;
	for (i = 1; i <= upsgi.numproc; i++) {
                if (upsgi.workers[i].pid > 0) {
                        upsgi_curse(i, SIGINT);
                }
        }
	upsgi_destroy_processes();
}

// gracefully destroy
void gracefully_kill_them_all(int signum) {
	if (upsgi_instance_is_dying) return;
	upsgi.status.gracefully_destroying = 1;

	upsgi_log_verbose("graceful shutdown triggered...\n");

	int i;
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid > 0) {
			upsgi.workers[i].shutdown_sockets = 1;
			upsgi_curse(i, SIGHUP);
		}
	}

	// avoid breaking other child process signal handling logic by doing nohang checks on the workers
	// until they are all done.
	int keep_waiting = 1;
	while (keep_waiting == 1) {
		int still_running = 0;
		int errors = 0;
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].pid > 0) {
				pid_t rval = waitpid(upsgi.workers[i].pid, NULL, WNOHANG);
				if (rval == upsgi.workers[i].pid) {
					upsgi.workers[i].pid = 0;
				} else if (rval == 0) {
					still_running++;
				} else if (rval < 0) {
					errors++;
				}
			}
		}

		// exit out if everything is done or we got errors as we can't do much about the errors at this point
		if (still_running == 0 || errors > 0) {
			keep_waiting = 0;
			break;
		}
		sleep(1);
	}

	upsgi_destroy_processes();
}


// graceful reload
void grace_them_all(int signum) {
	if (upsgi_instance_is_reloading || upsgi_instance_is_dying)
		return;

	int i;

	if (upsgi.lazy) {
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].pid > 0) {
				upsgi_curse(i, SIGHUP);
			}
		}
		return;
	}
	

	upsgi.status.gracefully_reloading = 1;

	upsgi_destroy_processes();

	upsgi_log("...gracefully killing workers...\n");


	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid > 0) {
			upsgi_curse(i, SIGHUP);
		}
	}
}

void upsgi_nuclear_blast() {

	// the Emperor (as an example) cannot nuke itself
	if (upsgi.disable_nuclear_blast) return;

	if (!upsgi.workers) {
		reap_them_all(0);
	}
	else if (upsgi.master_process) {
		if (getpid() == upsgi.workers[0].pid) {
			reap_them_all(0);
		}
	}

	exit(1);
}

// brutally reload
void reap_them_all(int signum) {

	// avoid race condition in lazy mode
	if (upsgi_instance_is_reloading)
		return;

	upsgi.status.brutally_reloading = 1;

	if (!upsgi.workers) return;

	upsgi_destroy_processes();

	upsgi_log("...brutally killing workers...\n");


	int i;
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid > 0)
			upsgi_curse(i, SIGTERM);
	}
}

void harakiri() {

	upsgi_log("\nKilling the current process (pid: %d app_id: %d)...\n", upsgi.mypid, upsgi.wsgi_req->app_id);

	if (!upsgi.master_process) {
		upsgi_log("*** if you want your workers to be automatically respawned consider enabling the upsgi master process ***\n");
	}
	exit(0);
}

void stats(int signum) {
	//fix this for better logging(this cause races)
	struct upsgi_app *ua = NULL;
	int i, j;

	if (upsgi.mywid == 0) {
		show_config();
		upsgi_log("\tworkers total requests: %lu\n", upsgi.workers[0].requests);
		upsgi_log("-----------------\n");
		for (j = 1; j <= upsgi.numproc; j++) {
			for (i = 0; i < upsgi.workers[j].apps_cnt; i++) {
				ua = &upsgi.workers[j].apps[i];
				if (ua) {
					upsgi_log("\tworker %d app %d [%.*s] requests: %lu exceptions: %lu\n", j, i, ua->mountpoint_len, ua->mountpoint, ua->requests, ua->exceptions);
				}
			}
			upsgi_log("-----------------\n");
		}
	}
	else {
		upsgi_log("worker %d total requests: %lu\n", upsgi.mywid, upsgi.workers[0].requests);
		for (i = 0; i < upsgi.workers[upsgi.mywid].apps_cnt; i++) {
			ua = &upsgi.workers[upsgi.mywid].apps[i];
			if (ua) {
				upsgi_log("\tapp %d [%.*s] requests: %lu exceptions: %lu\n", i, ua->mountpoint_len, ua->mountpoint, ua->requests, ua->exceptions);
			}
		}
		upsgi_log("-----------------\n");
	}
	upsgi_log("\n");
}

void what_i_am_doing() {

	struct wsgi_request *wsgi_req;
	int i;
	char ctime_storage[26];

	upsgi_backtrace(upsgi.backtrace_depth);

	if (upsgi.cores > 1) {
		for (i = 0; i < upsgi.cores; i++) {
			wsgi_req = &upsgi.workers[upsgi.mywid].cores[i].req;
			if (wsgi_req->uri_len > 0) {
#if defined(__sun__) && !defined(__clang__)
				ctime_r((const time_t *) &wsgi_req->start_of_request_in_sec, ctime_storage, 26);
#else
				ctime_r((const time_t *) &wsgi_req->start_of_request_in_sec, ctime_storage);
#endif
				if (upsgi.harakiri_options.workers > 0 && upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri < upsgi_now()) {
					upsgi_log("HARAKIRI: --- upsgi worker %d core %d (pid: %d) WAS managing request %.*s since %.*s ---\n", (int) upsgi.mywid, i, (int) upsgi.mypid, wsgi_req->uri_len, wsgi_req->uri, 24, ctime_storage);
				}
				else {
					upsgi_log("SIGUSR2: --- upsgi worker %d core %d (pid: %d) is managing request %.*s since %.*s ---\n", (int) upsgi.mywid, i, (int) upsgi.mypid, wsgi_req->uri_len, wsgi_req->uri, 24, ctime_storage);
				}
			}
		}
	}
	else {
		wsgi_req = &upsgi.workers[upsgi.mywid].cores[0].req;
		if (wsgi_req->uri_len > 0) {
#if defined(__sun__) && !defined(__clang__)
			ctime_r((const time_t *) &wsgi_req->start_of_request_in_sec, ctime_storage, 26);
#else
			ctime_r((const time_t *) &wsgi_req->start_of_request_in_sec, ctime_storage);
#endif
			if (upsgi.harakiri_options.workers > 0 && upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri < upsgi_now()) {
				upsgi_log("HARAKIRI: --- upsgi worker %d (pid: %d) WAS managing request %.*s since %.*s ---\n", (int) upsgi.mywid, (int) upsgi.mypid, wsgi_req->uri_len, wsgi_req->uri, 24, ctime_storage);
			}
			else {
				upsgi_log("SIGUSR2: --- upsgi worker %d (pid: %d) is managing request %.*s since %.*s ---\n", (int) upsgi.mywid, (int) upsgi.mypid, wsgi_req->uri_len, wsgi_req->uri, 24, ctime_storage);
			}
		}
		else if (upsgi.harakiri_options.workers > 0 && upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri < upsgi_now() && upsgi.workers[upsgi.mywid].sig) {
			upsgi_log("HARAKIRI: --- upsgi worker %d (pid: %d) WAS handling signal %d ---\n", (int) upsgi.mywid, (int) upsgi.mypid, upsgi.workers[upsgi.mywid].signum);
		}
	}
}



int unconfigured_hook(struct wsgi_request *wsgi_req) {
	if (wsgi_req->uh->modifier1 == 0 && !upsgi.no_default_app) {
		if (upsgi_apps_cnt > 0 && upsgi.default_app > -1) {
			struct upsgi_app *ua = &upsgi_apps[upsgi.default_app];
			if (upsgi.p[ua->modifier1]->request != unconfigured_hook) {
				wsgi_req->uh->modifier1 = ua->modifier1;
				return upsgi.p[ua->modifier1]->request(wsgi_req);
			}
		}
	}
	upsgi_log("-- unavailable modifier requested: %d --\n", wsgi_req->uh->modifier1);
	return -1;
}

static void unconfigured_after_hook(struct wsgi_request *wsgi_req) {
	return;
}

struct upsgi_plugin unconfigured_plugin = {

	.name = "unconfigured",
	.request = unconfigured_hook,
	.after_request = unconfigured_after_hook,
};

void upsgi_exec_atexit(void) {
	if (getpid() == masterpid) {
	
		upsgi_hooks_run(upsgi.hook_as_user_atexit, "atexit", 0);
		// now run exit scripts needed by the user
		struct upsgi_string_list *usl;

		upsgi_foreach(usl, upsgi.exec_as_user_atexit) {
			upsgi_log("running \"%s\" (as uid: %d gid: %d) ...\n", usl->value, (int) getuid(), (int) getgid());
			int ret = upsgi_run_command_and_wait(NULL, usl->value);
			if (ret != 0) {
				upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
			}
		}

		upsgi_foreach(usl, upsgi.call_as_user_atexit) {
                	if (upsgi_call_symbol(usl->value)) {
                        	upsgi_log("unable to call function \"%s\"\n", usl->value);
                	}
        	}
	}
}

static void vacuum(void) {

	struct upsgi_socket *upsgi_sock = upsgi.sockets;

	if (upsgi.restore_tc) {
		if (getpid() == masterpid) {
			if (tcsetattr(0, TCSANOW, &upsgi.termios)) {
				upsgi_error("vacuum()/tcsetattr()");
			}
		}
	}

	if (upsgi.vacuum) {
		if (getpid() == masterpid) {
			if (chdir(upsgi.cwd)) {
				upsgi_error("chdir()");
			}
			if (upsgi.pidfile && !upsgi.uid) {
				if (unlink(upsgi.pidfile)) {
					upsgi_error("unlink()");
				}
				else {
					upsgi_log("VACUUM: pidfile removed.\n");
				}
			}
			if (upsgi.pidfile2) {
				if (unlink(upsgi.pidfile2)) {
					upsgi_error("unlink()");
				}
				else {
					upsgi_log("VACUUM: pidfile2 removed.\n");
				}
			}
			if (upsgi.safe_pidfile && !upsgi.uid) {
				if (unlink(upsgi.safe_pidfile)) {
					upsgi_error("unlink()");
				}
				else {
					upsgi_log("VACUUM: safe pidfile removed.\n");
				}
			}
			if (upsgi.safe_pidfile2) {
				if (unlink(upsgi.safe_pidfile2)) {
					upsgi_error("unlink()");
				}
				else {
					upsgi_log("VACUUM: safe pidfile2 removed.\n");
				}
			}
			if (upsgi.chdir) {
				if (chdir(upsgi.chdir)) {
					upsgi_error("chdir()");
				}
			}
			while (upsgi_sock) {
				if (upsgi_sock->family == AF_UNIX && upsgi_sock->name[0] != '@') {
					struct stat st;
					if (!stat(upsgi_sock->name, &st)) {
						if (st.st_ino != upsgi_sock->inode) {
							upsgi_log("VACUUM WARNING: unix socket %s changed inode. Skip removal\n", upsgi_sock->name);
							goto next;
						}
					}
					if (unlink(upsgi_sock->name)) {
						upsgi_error("unlink()");
					}
					else {
						upsgi_log("VACUUM: unix socket %s removed.\n", upsgi_sock->name);
					}
				}
next:
				upsgi_sock = upsgi_sock->next;
			}
			if (upsgi.stats) {
				// is a unix socket ?
				if (!strchr(upsgi.stats, ':') && upsgi.stats[0] != '@') {
					if (unlink(upsgi.stats)) {
                                                upsgi_error("unlink()");
                                        }
                                        else {
                                                upsgi_log("VACUUM: unix socket %s (stats) removed.\n", upsgi.stats);
                                        }
				}
			}
			if (upsgi.master_fifo) {
				// also remove all master fifo
				struct upsgi_string_list *usl;
				upsgi_foreach(usl, upsgi.master_fifo) {
					if (unlink(usl->value)) {
						upsgi_error("unlink()");
					}
					else {
						upsgi_log("VACUUM: master fifo %s removed.\n", usl->value);
					}
				}
			}
		}
	}
}

int signal_pidfile(int sig, char *filename) {

	int ret = 0;
	size_t size = 0;

	char *buffer = upsgi_open_and_read(filename, &size, 1, NULL);

	if (size > 0) {
		if (kill((pid_t) atoi(buffer), sig)) {
			upsgi_error("signal_pidfile()/kill()");
			ret = -1;
		}
	}
	else {
		upsgi_log("error: invalid pidfile\n");
		ret = -1;
	}
	free(buffer);
	return ret;
}

/*static*/ void upsgi_command_signal(char *opt) {

	int tmp_signal;
	char *colon = strchr(opt, ',');
	if (!colon) {
		upsgi_log("invalid syntax for signal, must be addr,signal\n");
		exit(1);
	}

	colon[0] = 0;
	tmp_signal = atoi(colon + 1);

	if (tmp_signal < 0 || tmp_signal > 255) {
		upsgi_log("invalid signal number\n");
		exit(3);
	}

	uint8_t upsgi_signal = tmp_signal;
	int ret = upsgi_remote_signal_send(opt, upsgi_signal);

	if (ret < 0) {
		upsgi_log("unable to deliver signal %d to node %s\n", upsgi_signal, opt);
		exit(1);
	}

	if (ret == 0) {
		upsgi_log("node %s rejected signal %d\n", opt, upsgi_signal);
		exit(2);
	}

	upsgi_log("signal %d delivered to node %s\n", upsgi_signal, opt);
	exit(0);
}

static void fixup_argv_and_environ(int argc, char **argv, char **environ, char **envp) {

	upsgi.orig_argv = argv;
	upsgi.argv = argv;
	upsgi.argc = argc;
	upsgi.environ = environ;

	// avoid messing with fake environ
	if (envp && *environ != *envp) return;
	

#if defined(__linux__) || defined(__sun__)

	int i;
	int env_count = 0;

	upsgi.argv = upsgi_malloc(sizeof(char *) * (argc + 1));

	for (i = 0; i < argc; i++) {
		if (i == 0 || argv[0] + upsgi.argv_len == argv[i]) {
			upsgi.argv_len += strlen(argv[i]) + 1;
		}
		upsgi.argv[i] = strdup(argv[i]);
	}

	// required by execve
	upsgi.argv[i] = NULL;

	for (i = 0; environ[i] != NULL; i++) {
		if (environ[0] + upsgi.environ_len == environ[i]) {
			upsgi.environ_len += strlen(environ[i]) + 1;
		}
		env_count++;
	}

	upsgi.environ = upsgi_malloc(sizeof(char *) * (env_count+1));
	for (i = 0; i < env_count; i++) {
		upsgi.environ[i] = strdup(environ[i]);
#ifdef UPSGI_DEBUG
		upsgi_log("ENVIRON: %s\n", upsgi.environ[i]);
#endif
		environ[i] = upsgi.environ[i];
	}
	upsgi.environ[env_count] = NULL;

#ifdef UPSGI_DEBUG
	upsgi_log("total length of argv = %u\n", (unsigned int)upsgi.argv_len);
	upsgi_log("total length of environ = %u\n", (unsigned int)upsgi.environ_len);
#endif
	//environ = upsgi.environ;

#endif
}


void upsgi_plugins_atexit(void) {

	int j;

	if (!upsgi.workers)
		return;

	// the master cannot run atexit handlers...
	if (upsgi.master_process && upsgi.workers[0].pid == getpid())
		return;

	for (j = 0; j < upsgi.gp_cnt; j++) {
		if (upsgi.gp[j]->atexit) {
			upsgi.gp[j]->atexit();
		}
	}

	for (j = 0; j < 256; j++) {
		if (upsgi.p[j]->atexit) {
			upsgi.p[j]->atexit();
		}
	}

}

void upsgi_backtrace(int depth) {

#if (!defined(__UCLIBC__) && defined(__GLIBC__)) || (defined(__APPLE__) && !defined(NO_EXECINFO)) || defined(UPSGI_HAS_EXECINFO)

#include <execinfo.h>

	void **btrace = upsgi_malloc(sizeof(void *) * depth);
	size_t bt_size, i;
	char **bt_strings;

	bt_size = backtrace(btrace, depth);

	bt_strings = backtrace_symbols(btrace, bt_size);

	struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	upsgi_buffer_append(ub, "*** backtrace of ",17);
	upsgi_buffer_num64(ub, (int64_t) getpid());
	upsgi_buffer_append(ub, " ***\n", 5);
	for (i = 0; i < bt_size; i++) {
		upsgi_buffer_append(ub, bt_strings[i], strlen(bt_strings[i]));
		upsgi_buffer_append(ub, "\n", 1);
	}

	free(btrace);

	upsgi_buffer_append(ub, "*** end of backtrace ***\n", 25);

	upsgi_log("%.*s", ub->pos, ub->buf);

	struct upsgi_string_list *usl = upsgi.alarm_segfault;
	while(usl) {
		upsgi_alarm_trigger(usl->value, ub->buf, ub->pos);	
		usl = usl->next;
	}	

	upsgi_buffer_destroy(ub);
#endif

}

void upsgi_segfault(int signum) {

	upsgi_log("!!! upsgi process %d got Segmentation Fault !!!\n", (int) getpid());
	upsgi_backtrace(upsgi.backtrace_depth);

	if (upsgi.use_abort) abort();

	// restore default handler to generate core
	signal(signum, SIG_DFL);
	kill(getpid(), signum);

	// never here...
	exit(1);
}

void upsgi_fpe(int signum) {

	upsgi_log("!!! upsgi process %d got Floating Point Exception !!!\n", (int) getpid());
	upsgi_backtrace(upsgi.backtrace_depth);

	if (upsgi.use_abort) abort();

	// restore default handler to generate core
	signal(signum, SIG_DFL);
	kill(getpid(), signum);

	// never here...
	exit(1);
}

void upsgi_flush_logs() {

	struct pollfd pfd;

	if (!upsgi.master_process)
		return;
	if (!upsgi.log_master)
		return;

	if (upsgi.workers) {
		if (upsgi.workers[0].pid == getpid()) {
			goto check;
		}
	}


	if (upsgi.mywid == 0)
		goto check;

	return;

check:
	// this buffer could not be initialized !!!
	if (upsgi.log_master) {
		upsgi.log_master_buf = upsgi_malloc(upsgi.log_master_bufsize);
	}

	// check for data in logpipe
	pfd.events = POLLIN;
	pfd.fd = upsgi.shared->worker_log_pipe[0];
	if (pfd.fd == -1)
		pfd.fd = 2;

	while (poll(&pfd, 1, 0) > 0) {
		if (!upsgi_master_log_drain(upsgi.log_drain_burst)) {
			break;
		}
	}
}

static void plugins_list(void) {
	int i;
	upsgi_log("\n*** upsgi loaded generic plugins ***\n");
	for (i = 0; i < upsgi.gp_cnt; i++) {
		upsgi_log("%s\n", upsgi.gp[i]->name);
	}

	upsgi_log("\n*** upsgi loaded request plugins ***\n");
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i] == &unconfigured_plugin)
			continue;
		upsgi_log("%d: %s\n", i, upsgi.p[i]->name);
	}

	upsgi_log("--- end of plugins list ---\n\n");
}

static void loggers_list(void) {
	struct upsgi_logger *ul = upsgi.loggers;
	upsgi_log("\n*** upsgi loaded loggers ***\n");
	while (ul) {
		upsgi_log("%s\n", ul->name);
		ul = ul->next;
	}
	upsgi_log("--- end of loggers list ---\n\n");
}

static void cheaper_algo_list(void) {
	struct upsgi_cheaper_algo *uca = upsgi.cheaper_algos;
	upsgi_log("\n*** upsgi loaded cheaper algorithms ***\n");
	while (uca) {
		upsgi_log("%s\n", uca->name);
		uca = uca->next;
	}
	upsgi_log("--- end of cheaper algorithms list ---\n\n");
}

#ifdef UPSGI_ROUTING
static void router_list(void) {
	struct upsgi_router *ur = upsgi.routers;
	upsgi_log("\n*** upsgi loaded routers ***\n");
	while (ur) {
		upsgi_log("%s\n", ur->name);
		ur = ur->next;
	}
	upsgi_log("--- end of routers list ---\n\n");
}
#endif

static void loop_list(void) {
	struct upsgi_loop *loop = upsgi.loops;
	upsgi_log("\n*** upsgi loaded loop engines ***\n");
	while (loop) {
		upsgi_log("%s\n", loop->name);
		loop = loop->next;
	}
	upsgi_log("--- end of loop engines list ---\n\n");
}

static void imperial_monitor_list(void) {
	struct upsgi_imperial_monitor *uim = upsgi.emperor_monitors;
	upsgi_log("\n*** upsgi loaded imperial monitors ***\n");
	while (uim) {
		upsgi_log("%s\n", uim->scheme);
		uim = uim->next;
	}
	upsgi_log("--- end of imperial monitors list ---\n\n");
}

static void clocks_list(void) {
	struct upsgi_clock *clocks = upsgi.clocks;
	upsgi_log("\n*** upsgi loaded clocks ***\n");
	while (clocks) {
		upsgi_log("%s\n", clocks->name);
		clocks = clocks->next;
	}
	upsgi_log("--- end of clocks list ---\n\n");
}

static void alarms_list(void) {
	struct upsgi_alarm *alarms = upsgi.alarms;
	upsgi_log("\n*** upsgi loaded alarms ***\n");
	while (alarms) {
		upsgi_log("%s\n", alarms->name);
		alarms = alarms->next;
	}
	upsgi_log("--- end of alarms list ---\n\n");
}

static time_t upsgi_unix_seconds() {
	return time(NULL);
}

static uint64_t upsgi_unix_microseconds() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((uint64_t) tv.tv_sec * 1000000) + tv.tv_usec;
}

static struct upsgi_clock upsgi_unix_clock = {
	.name = "unix",
	.seconds = upsgi_unix_seconds,
	.microseconds = upsgi_unix_microseconds,
};

void upsgi_init_random() {
        srand((unsigned int) (upsgi.start_tv.tv_usec * upsgi.start_tv.tv_sec));
}

#ifdef UPSGI_AS_SHARED_LIBRARY
int upsgi_init(int argc, char *argv[], char *envp[]) {
#else
int main(int argc, char *argv[], char *envp[]) {
#endif
	upsgi_setup(argc, argv, envp);
	return upsgi_run();
}

static char *upsgi_at_file_read(char *filename) {
	size_t size = 0;
	char *buffer = upsgi_open_and_read(filename, &size, 1, NULL);
	if (size > 1) {
		if (buffer[size-2] == '\n' || buffer[size-2] == '\r') {
			buffer[size-2] = 0;
		}
	}
	return buffer;
}

void upsgi_setup(int argc, char *argv[], char *envp[]) {

#ifdef UPSGI_AS_SHARED_LIBRARY
#ifdef __APPLE__
	char ***envPtr = _NSGetEnviron();
	environ = *envPtr;
#endif
#endif

	int i;


	// signal mask is inherited, and sme process manager could make a real mess...
	sigset_t smask;
        sigfillset(&smask);
        if (sigprocmask(SIG_UNBLOCK, &smask, NULL)) {
                upsgi_error("sigprocmask()");
        }

	signal(SIGCHLD, SIG_DFL);
	signal(SIGSEGV, upsgi_segfault);
	signal(SIGFPE, upsgi_fpe);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	//initialize masterpid with a default value
	masterpid = getpid();

	memset(&upsgi, 0, sizeof(struct upsgi_server));
	upsgi_proto_hooks_setup();
	upsgi.cwd = upsgi_get_cwd();

	init_magic_table(upsgi.magic_table);

	// initialize schemes
	upsgi_setup_schemes();

	// initialize the clock
	upsgi_register_clock(&upsgi_unix_clock);
	upsgi_set_clock("unix");

	// fallback config
	atexit(upsgi_fallback_config);
	// manage/flush logs
	atexit(upsgi_flush_logs);
	// clear sockets, pidfiles...
	atexit(vacuum);
	// call user scripts
	atexit(upsgi_exec_atexit);

	// allocate main shared memory
	upsgi.shared = (struct upsgi_shared *) upsgi_calloc_shared(sizeof(struct upsgi_shared));

	// initialize request plugin to void
	for (i = 0; i < 256; i++) {
		upsgi.p[i] = &unconfigured_plugin;
	}

	// set default values
	upsgi_init_default();

	// detect cpu cores
#if defined(_SC_NPROCESSORS_ONLN)
	upsgi.cpus = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_SC_NPROCESSORS_CONF)
	upsgi.cpus = sysconf(_SC_NPROCESSORS_CONF);
#endif
	// set default logit hook
	upsgi.logit = upsgi_logit_simple;

#ifdef UPSGI_BLACKLIST
	if (!upsgi_file_to_string_list(UPSGI_BLACKLIST, &upsgi.blacklist)) {
		upsgi_log("you cannot run this build of upsgi without a blacklist file\n");
		exit(1);
	}
#endif

#ifdef UPSGI_WHITELIST
	if (!upsgi_file_to_string_list(UPSGI_WHITELIST, &upsgi.whitelist)) {
		upsgi_log("you cannot run this build of upsgi without a whitelist file\n");
		exit(1);
	}
#endif

	// get startup time
	gettimeofday(&upsgi.start_tv, NULL);

	// initialize random engine
	upsgi_init_random();

	setlinebuf(stdout);

	upsgi.rl.rlim_cur = 0;
	upsgi.rl.rlim_max = 0;

	// are we under systemd ?
	char *notify_socket = getenv("NOTIFY_SOCKET");
	if (notify_socket) {
		upsgi_systemd_init(notify_socket);
	}

	upsgi_notify("initializing upsgi");

	// check if we are under the Emperor
	upsgi_check_emperor();

	char *screen_env = getenv("TERM");
	if (screen_env) {
		if (!strcmp(screen_env, "screen")) {
			upsgi.screen_session = getenv("STY");
		}
	}


	// count/set the current reload status
	upsgi_setup_reload();

#ifdef __CYGWIN__
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	upsgi.page_size = si.dwPageSize;
#else
	upsgi.page_size = getpagesize();
#endif
	upsgi.binary_path = upsgi_get_binary_path(argv[0]);

	if(upsgi.response_header_limit == 0)
		upsgi.response_header_limit = UMAX16;

	// ok we can now safely play with argv and environ
	fixup_argv_and_environ(argc, argv, environ, envp);

	if (gethostname(upsgi.hostname, 255)) {
		upsgi_error("gethostname()");
	}
	upsgi.hostname_len = strlen(upsgi.hostname);

#ifdef UPSGI_ROUTING
	upsgi_register_embedded_routers();
#endif

	// call here to allows plugin to override hooks
	upsgi_register_base_hooks();
	upsgi_register_logchunks();
	upsgi_log_encoders_register_embedded();

	// register base metrics (so plugins can override them)
	upsgi_metrics_collectors_setup();

	//initialize embedded plugins
	UPSGI_LOAD_EMBEDDED_PLUGINS
		// now a bit of magic, if the executable basename contains a 'upsgi_' string,
		// try to automatically load a plugin
#ifdef UPSGI_DEBUG
		upsgi_log("executable name: %s\n", upsgi.binary_path);
#endif
	upsgi_autoload_plugins_by_name(argv[0]);


	// build the options structure
	build_options();

	// set a couple of 'static' magic vars
	upsgi.magic_table['v'] = upsgi.cwd;
	upsgi.magic_table['h'] = upsgi.hostname;
	upsgi.magic_table['t'] = upsgi_64bit2str(upsgi_now());
	upsgi.magic_table['T'] = upsgi_64bit2str(upsgi_micros());
	upsgi.magic_table['V'] = UPSGI_VERSION;
	upsgi.magic_table['k'] = upsgi_num2str(upsgi.cpus);
	upsgi.magic_table['['] = "\033";
	upsgi.magic_table['u'] = upsgi_num2str((int)getuid());
	struct passwd *pw = getpwuid(getuid());
	upsgi.magic_table['U'] = pw ? pw->pw_name : upsgi.magic_table['u'];
	upsgi.magic_table['g'] = upsgi_num2str((int)getgid());
	struct group *gr = getgrgid(getgid());
	upsgi.magic_table['G'] = gr ? gr->gr_name : upsgi.magic_table['g'];

configure:

	// you can embed a ini file in the uWSGi binary with default options
#ifdef UPSGI_EMBED_CONFIG
	upsgi_ini_config("", upsgi.magic_table);
	// rebuild options if a custom ini is set
	build_options();
#endif
	//parse environ
	parse_sys_envs(environ);

	// parse commandline options
	upsgi_commandline_config();

	// second pass: ENVs
	upsgi_apply_config_pass('$', (char *(*)(char *)) upsgi_getenv_with_default);

	// third pass: FILEs
	upsgi_apply_config_pass('@', upsgi_at_file_read);

	// last pass: REFERENCEs
	upsgi_apply_config_pass('%', upsgi_manage_placeholder);

	// ok, the options dictionary is available, lets manage it
	upsgi_configure();

	// stop the execution until a connection arrives on the fork socket
	if (upsgi.fork_socket) {
		upsgi_log_verbose("waiting for fork-socket connections...\n");
		upsgi_fork_server(upsgi.fork_socket);
		// if we are here a new process has been spawned
		goto configure;
	}

	// fixup cwd
	if (upsgi.force_cwd) upsgi.cwd = upsgi.force_cwd;

	// run "asap" hooks
	upsgi_hooks_run(upsgi.hook_asap, "asap", 1);
        struct upsgi_string_list *usl = NULL;
        upsgi_foreach(usl, upsgi.mount_asap) {
        	upsgi_log("mounting \"%s\" (asap)...\n", usl->value);
                if (upsgi_mount_hook(usl->value)) exit(1);
	}
        upsgi_foreach(usl, upsgi.umount_asap) {
        	upsgi_log("un-mounting \"%s\" (asap)...\n", usl->value);
                if (upsgi_umount_hook(usl->value)) exit(1);
	}
        upsgi_foreach(usl, upsgi.exec_asap) {
        	upsgi_log("running \"%s\" (asap)...\n", usl->value);
                int ret = upsgi_run_command_and_wait(NULL, usl->value);
                if (ret != 0) {
                	upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
                        exit(1);
                }
	}
        upsgi_foreach(usl, upsgi.call_asap) {
        	if (upsgi_call_symbol(usl->value)) {
                	upsgi_log("unable to call function \"%s\"\n", usl->value);
                        exit(1);
                }
	}

	// manage envdirs ASAP
	upsgi_envdirs(upsgi.envdirs);

	// --get management
	struct upsgi_string_list *get_list = upsgi.get_list;
	while(get_list) {
		char *v = upsgi_get_exported_opt(get_list->value);
		if (v) {
			fprintf(stdout, "%s\n", v);
		}
		get_list = get_list->next;
	}

	if (upsgi.get_list) {
		exit(0);
	}


	// initial log setup (files and daemonization)
	upsgi_setup_log();

#ifndef __CYGWIN__
	// enable never-swap mode
	if (upsgi.never_swap) {
		if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
			upsgi_error("mlockall()");
		}
	}
#endif

	if (upsgi.flock2)
		upsgi_opt_flock(NULL, upsgi.flock2, NULL);

	if (upsgi.flock_wait2)
		upsgi_opt_flock(NULL, upsgi.flock_wait2, NULL);

	// setup master logging
	if (upsgi.log_master)
		upsgi_setup_log_master();
	else if (upsgi.numproc == 1 && upsgi.log_worker && upsgi.master_process == 0) {
		// hack for allowing request loggers
		if (upsgi.requested_req_logger)
			upsgi.req_log_master = 1;
                upsgi_setup_log_master();
                upsgi_threaded_logger_worker_spawn();
        }

	// setup offload engines
	upsgi_offload_engines_register_all();

	// setup main loops
	upsgi_register_loop("simple", simple_loop);
	upsgi_register_loop("async", async_loop);

	// setup cheaper algos
	upsgi_register_cheaper_algo("spare", upsgi_cheaper_algo_spare);
	upsgi_register_cheaper_algo("spare2", upsgi_cheaper_algo_spare2);
	upsgi_register_cheaper_algo("backlog", upsgi_cheaper_algo_backlog);
	upsgi_register_cheaper_algo("manual", upsgi_cheaper_algo_manual);

	// setup imperial monitors
	upsgi_register_imperial_monitor("dir", upsgi_imperial_monitor_directory_init, upsgi_imperial_monitor_directory);
	upsgi_register_imperial_monitor("glob", upsgi_imperial_monitor_glob_init, upsgi_imperial_monitor_glob);

	// setup stats pushers
	upsgi_stats_pusher_setup();

	// register embedded alarms
	upsgi_register_embedded_alarms();

	/* upsgi IS CONFIGURED !!! */

	if (upsgi.dump_options) {
		struct option *lopt = upsgi.long_options;
		while (lopt && lopt->name) {
			fprintf(stdout, "%s\n", lopt->name);
			lopt++;
		}
		exit(0);
	}

	if (upsgi.show_config)
		show_config();

	if (upsgi.plugins_list)
		plugins_list();

	if (upsgi.loggers_list)
		loggers_list();

	if (upsgi.cheaper_algo_list)
		cheaper_algo_list();


#ifdef UPSGI_ROUTING
	if (upsgi.router_list)
		router_list();
#endif


	if (upsgi.loop_list)
		loop_list();

	if (upsgi.imperial_monitor_list)
		imperial_monitor_list();

	if (upsgi.clock_list)
		clocks_list();

	if (upsgi.alarms_list)
		alarms_list();

	// set the clock
	if (upsgi.requested_clock)
		upsgi_set_clock(upsgi.requested_clock);

	if (upsgi.binary_path == upsgi.argv[0]) {
		upsgi.binary_path = upsgi_str(upsgi.argv[0]);
	}

	upsgi_log_initial("*** Starting upsgi %s (%dbit) on [%.*s] ***\n", UPSGI_VERSION, (int) (sizeof(void *)) * 8, 24, ctime((const time_t *) &upsgi.start_tv.tv_sec));

#ifdef UPSGI_DEBUG
	upsgi_log("***\n*** You are running a DEBUG version of upsgi, please disable debug in your build profile and recompile it ***\n***\n");
#endif

	upsgi_log_initial("compiled with version: %s on %s\n", __VERSION__, UPSGI_BUILD_DATE);

#ifdef __RUMP__
	upsgi_log_initial("Rump system detected\n");
#else
	struct utsname uuts;
#ifdef __sun__
	if (uname(&uuts) < 0) {
#else
	if (uname(&uuts)) {
#endif
		upsgi_error("uname()");
	}
	else {
		upsgi_log_initial("os: %s-%s %s\n", uuts.sysname, uuts.release, uuts.version);
		upsgi_log_initial("nodename: %s\n", uuts.nodename);
		upsgi_log_initial("machine: %s\n", uuts.machine);
	}
#endif

	upsgi_log_initial("clock source: %s\n", upsgi.clock->name);
#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	if (upsgi.pcre_jit) {
		upsgi_log_initial("pcre jit enabled\n");
	}
	else {
		upsgi_log_initial("pcre jit disabled\n");
	}
#endif

#ifdef __BIG_ENDIAN__
	upsgi_log_initial("*** big endian arch detected ***\n");
#endif

	upsgi_log_initial("detected number of CPU cores: %d\n", upsgi.cpus);


	upsgi_log_initial("current working directory: %s\n", upsgi.cwd);

	if (upsgi.screen_session) {
		upsgi_log("*** running under screen session %s ***\n", upsgi.screen_session);
	}

	if (upsgi.pidfile && !upsgi.is_a_reload) {
		upsgi_write_pidfile(upsgi.pidfile);
	}

	upsgi_log_initial("detected binary path: %s\n", upsgi.binary_path);

	if (upsgi.is_a_reload) {
		struct rlimit rl;
		if (!getrlimit(RLIMIT_NOFILE, &rl)) {
			upsgi.max_fd = rl.rlim_cur;
		}
	}

#ifdef UPSGI_ROUTING
	upsgi_routing_dump();
#else
	upsgi_log("!!! no internal routing support, rebuild with pcre support !!!\n");
#endif

	// initialize shared sockets
	upsgi_setup_shared_sockets();

#ifdef __linux__
	if (upsgi.setns_preopen) {
		upsgi_setns_preopen();
	}
	// eventually join a linux namespace
	if (upsgi.setns) {
		upsgi_setns(upsgi.setns);
	}
#endif

	// start the Emperor if needed
	if (upsgi.early_emperor && upsgi.emperor) {
		upsgi_emperor_start();
	}

	if (!upsgi.reloads) {
		upsgi_hooks_run(upsgi.hook_pre_jail, "pre-jail", 1);
		struct upsgi_string_list *usl = NULL;
		upsgi_foreach(usl, upsgi.mount_pre_jail) {
                                upsgi_log("mounting \"%s\" (pre-jail)...\n", usl->value);
                                if (upsgi_mount_hook(usl->value)) {
                                        exit(1);
                                }
                        }

                        upsgi_foreach(usl, upsgi.umount_pre_jail) {
                                upsgi_log("un-mounting \"%s\" (pre-jail)...\n", usl->value);
                                if (upsgi_umount_hook(usl->value)) {
                                        exit(1);
                                }
                        }
		// run the pre-jail scripts
		upsgi_foreach(usl, upsgi.exec_pre_jail) {
			upsgi_log("running \"%s\" (pre-jail)...\n", usl->value);
			int ret = upsgi_run_command_and_wait(NULL, usl->value);
			if (ret != 0) {
				upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
				exit(1);
			}
		}

		upsgi_foreach(usl, upsgi.call_pre_jail) {
			if (upsgi_call_symbol(usl->value)) {
				upsgi_log("unable to call function \"%s\"\n", usl->value);
				exit(1);
			}
		}
	}

	// we could now patch the binary
	if (upsgi.privileged_binary_patch) {
		upsgi.argv[0] = upsgi.privileged_binary_patch;
		execvp(upsgi.privileged_binary_patch, upsgi.argv);
		upsgi_error("execvp()");
		exit(1);
	}

	if (upsgi.privileged_binary_patch_arg) {
		upsgi_exec_command_with_args(upsgi.privileged_binary_patch_arg);
	}


	// call jail systems
	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->jail) {
			upsgi.gp[i]->jail(upsgi_start, upsgi.argv);
		}
	}

	// TODO pluginize basic Linux namespace support
#if defined(__linux__) && !defined(__ia64__)
	if (upsgi.ns) {
		linux_namespace_start((void *) upsgi.argv);
		// never here
	}
	else {
#endif
		upsgi_start((void *) upsgi.argv);
#if defined(__linux__) && !defined(__ia64__)
	}
#endif

	if (upsgi.safe_pidfile && !upsgi.is_a_reload) {
		upsgi_write_pidfile_explicit(upsgi.safe_pidfile, masterpid);
	}
}


int upsgi_start(void *v_argv) {

	int i, j;

#ifdef __linux__
	upsgi_set_cgroup();

#if !defined(__ia64__)
	if (upsgi.ns) {
		linux_namespace_jail();
	}
#endif
#endif

	upsgi_hooks_run(upsgi.hook_in_jail, "in-jail", 1);

	struct upsgi_string_list *usl;

	upsgi_foreach(usl, upsgi.mount_in_jail) {
                                upsgi_log("mounting \"%s\" (in-jail)...\n", usl->value);
                                if (upsgi_mount_hook(usl->value)) {
                                        exit(1);
                                }
                        }

                        upsgi_foreach(usl, upsgi.umount_in_jail) {
                                upsgi_log("un-mounting \"%s\" (in-jail)...\n", usl->value);
                                if (upsgi_umount_hook(usl->value)) {
                                        exit(1);
                                }
                        }

	upsgi_foreach(usl, upsgi.exec_in_jail) {
                upsgi_log("running \"%s\" (in-jail)...\n", usl->value);
                int ret = upsgi_run_command_and_wait(NULL, usl->value);
                if (ret != 0) {
                        upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
                        exit(1);
                }
        }

        upsgi_foreach(usl, upsgi.call_in_jail) {
                if (upsgi_call_symbol(usl->value)) {
                        upsgi_log("unable to call function \"%s\"\n", usl->value);
			exit(1);
                }
        }


	upsgi_file_write_do(upsgi.file_write_list);

	if (!upsgi.master_as_root && !upsgi.chown_socket && !upsgi.drop_after_init && !upsgi.drop_after_apps) {
		upsgi_log("dropping root privileges as early as possible\n");
		upsgi_as_root();
	}

	// wait for socket
	upsgi_foreach(usl, upsgi.wait_for_socket) {
                if (upsgi_wait_for_socket(usl->value)) exit(1);
        }

	if (upsgi.logto2) {
		if (!upsgi.is_a_reload || upsgi.log_reopen) {
			logto(upsgi.logto2);
		}
	}

	if (upsgi.chdir) {
		upsgi_log("chdir() to %s\n", upsgi.chdir);
		if (chdir(upsgi.chdir)) {
			upsgi_error("chdir()");
			exit(1);
		}
	}

	if (upsgi.pidfile2 && !upsgi.is_a_reload) {
		upsgi_write_pidfile(upsgi.pidfile2);
	}

#ifndef __RUMP__
	if (!upsgi.master_process && !upsgi.command_mode) {
		upsgi_log_initial("*** WARNING: you are running upsgi without its master process manager ***\n");
	}
#endif

#ifndef __RUMP__
#ifdef RLIMIT_NPROC
	if (upsgi.rl_nproc.rlim_max > 0) {
		upsgi.rl_nproc.rlim_cur = upsgi.rl_nproc.rlim_max;
		upsgi_log_initial("limiting number of processes to %d...\n", (int) upsgi.rl_nproc.rlim_max);
		if (setrlimit(RLIMIT_NPROC, &upsgi.rl_nproc)) {
			upsgi_error("setrlimit()");
		}
	}

	if (!getrlimit(RLIMIT_NPROC, &upsgi.rl_nproc)) {
		if (upsgi.rl_nproc.rlim_cur != RLIM_INFINITY) {
			upsgi_log_initial("your processes number limit is %d\n", (int) upsgi.rl_nproc.rlim_cur);
			if ((int) upsgi.rl_nproc.rlim_cur < upsgi.numproc + upsgi.master_process) {
				upsgi.numproc = upsgi.rl_nproc.rlim_cur - 1;
				upsgi_log_initial("!!! number of workers adjusted to %d due to system limits !!!\n", upsgi.numproc);
			}
		}
	}
#endif
#endif

#ifndef __OpenBSD__

	if (upsgi.rl.rlim_max > 0) {
		upsgi.rl.rlim_cur = upsgi.rl.rlim_max;
		upsgi_log_initial("limiting address space of processes...\n");
		if (setrlimit(RLIMIT_AS, &upsgi.rl)) {
			upsgi_error("setrlimit()");
		}
	}
	if (upsgi.prio != 0) {
#ifdef __HAIKU__
		if (set_thread_priority(find_thread(NULL), upsgi.prio) == B_BAD_THREAD_ID) {
			upsgi_error("set_thread_priority()");
#else
		if (setpriority(PRIO_PROCESS, 0, upsgi.prio)) {
			upsgi_error("setpriority()");
#endif

		}
		else {
			upsgi_log_initial("scheduler priority set to %d\n", upsgi.prio);
		}
	}
	if (!getrlimit(RLIMIT_AS, &upsgi.rl)) {
		//check for overflow
		if (upsgi.rl.rlim_max != (rlim_t) RLIM_INFINITY) {
			upsgi_log_initial("your process address space limit is %lld bytes (%lld MB)\n", (long long) upsgi.rl.rlim_max, (long long) upsgi.rl.rlim_max / 1024 / 1024);
		}
	}
#endif

	upsgi_log_initial("your memory page size is %d bytes\n", upsgi.page_size);

	// automatically fix options
	sanitize_args();


	if (upsgi.requested_max_fd) {
		upsgi.rl.rlim_cur = upsgi.requested_max_fd;
		upsgi.rl.rlim_max = upsgi.requested_max_fd;
		if (setrlimit(RLIMIT_NOFILE, &upsgi.rl)) {
			upsgi_error("setrlimit()");
		}
	}

	if (!getrlimit(RLIMIT_NOFILE, &upsgi.rl)) {
		upsgi.max_fd = upsgi.rl.rlim_cur;
		upsgi_log_initial("detected max file descriptor number: %lu\n", (unsigned long) upsgi.max_fd);
	}

	// start the Emperor if needed
	if (!upsgi.early_emperor && upsgi.emperor) {
		upsgi_emperor_start();
	}

	// end of generic initialization


	// build mime.types dictionary
	if (upsgi.build_mime_dict) {
		if (!upsgi.mime_file)
#ifdef __APPLE__
			upsgi_string_new_list(&upsgi.mime_file, "/etc/apache2/mime.types");
#else
			upsgi_string_new_list(&upsgi.mime_file, "/etc/mime.types");
#endif
		struct upsgi_string_list *umd = upsgi.mime_file;
		while (umd) {
			if (!access(umd->value, R_OK)) {
				upsgi_build_mime_dict(umd->value);
			}
			else {
				upsgi_log("!!! no %s file found !!!\n", umd->value);
			}
			umd = umd->next;
		}
	}

	if (upsgi.async > 0) {
		if ((unsigned long) upsgi.max_fd < (unsigned long) upsgi.async) {
			upsgi_log_initial("- your current max open files limit is %lu, this is lower than requested async cores !!! -\n", (unsigned long) upsgi.max_fd);
			upsgi.rl.rlim_cur = upsgi.async;
			upsgi.rl.rlim_max = upsgi.async;
			if (!setrlimit(RLIMIT_NOFILE, &upsgi.rl)) {
				upsgi_log("max open files limit raised to %lu\n", (unsigned long) upsgi.rl.rlim_cur);
				upsgi.async = upsgi.rl.rlim_cur;
				upsgi.max_fd = upsgi.rl.rlim_cur;
			}
			else {
				upsgi.async = (int) upsgi.max_fd;
			}
		}
		upsgi_log_initial("- async cores set to %d - fd table size: %d\n", upsgi.async, (int) upsgi.max_fd);
	}

#ifdef UPSGI_DEBUG
	upsgi_log("cores allocated...\n");
#endif

	if (upsgi.vhost) {
		upsgi_log_initial("VirtualHosting mode enabled.\n");
	}

	// setup locking
	upsgi_setup_locking();
	if (upsgi.use_thunder_lock) {
		const char *backend_name = "unknown";
		switch (upsgi.thunder_lock_backend) {
		case UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_ROBUST:
			backend_name = "robust-pthread";
			break;
		case UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_PLAIN:
			backend_name = "plain-pthread";
			break;
		case UPSGI_THUNDER_LOCK_BACKEND_IPCSEM:
			backend_name = "ipcsem";
			break;
		case UPSGI_THUNDER_LOCK_BACKEND_FDLOCK:
			backend_name = "fd-lock";
			break;
		default:
			break;
		}

		upsgi_log_initial("thunder lock: enabled\n");
		upsgi_log_initial("thunder lock backend: %s\n", backend_name);
		upsgi_log_initial("thunder lock robust recovery: %s\n", upsgi.thunder_lock_backend_has_owner_dead_recovery ? "yes" : "no");
		upsgi_log_initial("thunder lock watchdog diagnostics: %s\n", upsgi.use_thunder_lock_watchdog ? "enabled" : "disabled");
		if (upsgi.thunder_lock_backend_reason) {
			upsgi_log_initial("thunder lock backend note: %s\n", upsgi.thunder_lock_backend_reason);
		}
		if (upsgi.thunder_lock_backend == UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_PLAIN) {
			upsgi_log_initial("thunder lock warning: plain pthread fallback has no owner-death recovery; fd-lock compatibility backend is preferred for fragile environments\n");
		}
		if (upsgi.use_thunder_lock_watchdog) {
			upsgi_log_initial("thunder lock watchdog note: diagnostics only; no recovery thread is spawned\n");
		}
	}
	else {
		upsgi_log_initial("thunder lock: disabled by configuration\n");
	}

	// allocate rpc structures
        upsgi_rpc_init();

	upsgi.snmp_lock = upsgi_lock_init("snmp");

	upsgi_cache_create_all();

	if (upsgi.use_static_cache_paths) {
		if (upsgi.static_cache_paths_name) {
			upsgi.static_cache_paths = upsgi_cache_ensure_named_local(upsgi.static_cache_paths_name, 64, 4096);
		}
		else {
			upsgi.static_cache_paths = upsgi_cache_ensure_named_local("staticpaths", 64, 4096);
		}
        }

        // initialize the alarm subsystem
        upsgi_alarms_init();

	// initialize the exception handlers
	upsgi_exception_setup_handlers();

	// initialize socket protocols (do it after caching !!!)
	upsgi_protocols_register();

	/* plugin initialization */
	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->init) {
			upsgi.gp[i]->init();
		}
	}

	if (!upsgi.no_server) {

		// systemd/upstart/zerg socket activation
		if (!upsgi.is_a_reload) {
			upsgi_setup_systemd();
			upsgi_setup_upstart();
			upsgi_setup_zerg();
			upsgi_setup_emperor();
		}


		//check for inherited sockets
		if (upsgi.is_a_reload) {
			upsgi_setup_inherited_sockets();
		}


		//now bind all the unbound sockets
		upsgi_bind_sockets();

		if (!upsgi.master_as_root && !upsgi.drop_after_init && !upsgi.drop_after_apps) {
			upsgi_log("dropping root privileges after socket binding\n");
			upsgi_as_root();
		}

		// put listening socket in non-blocking state and set the protocol
		upsgi_set_sockets_protocols();

	}


	// initialize request plugin only if workers or master are available
	if (upsgi.sockets || upsgi.master_process || upsgi.no_server || upsgi.command_mode || upsgi.loop) {
		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->init) {
				upsgi.p[i]->init();
			}
		}
	}

	if (!upsgi.master_as_root && !upsgi.drop_after_apps) {
		upsgi_log("dropping root privileges after plugin initialization\n");
		upsgi_as_root();
	}


	/* gp/plugin initialization */
	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->post_init) {
			upsgi.gp[i]->post_init();
		}
	}

	// again check for workers/sockets...
	if (upsgi.sockets || upsgi.master_process || upsgi.no_server || upsgi.command_mode || upsgi.loop) {
		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->post_init) {
				upsgi.p[i]->post_init();
			}
		}
	}

	upsgi.current_wsgi_req = simple_current_wsgi_req;


	if (upsgi.has_threads) {
		if (upsgi.threads > 1)
			upsgi.current_wsgi_req = threaded_current_wsgi_req;
		(void) pthread_attr_init(&upsgi.threads_attr);
		if (upsgi.threads_stacksize) {
			if (pthread_attr_setstacksize(&upsgi.threads_attr, upsgi.threads_stacksize * 1024) == 0) {
				upsgi_log("threads stack size set to %luk\n", (unsigned long) upsgi.threads_stacksize);
			}
			else {
				upsgi_log("!!! unable to set requested threads stacksize !!!\n");
			}
		}

		pthread_mutex_init(&upsgi.lock_static, NULL);

		// again check for workers/sockets...
		if (upsgi.sockets || upsgi.master_process || upsgi.no_server || upsgi.command_mode || upsgi.loop) {
			for (i = 0; i < 256; i++) {
				if (upsgi.p[i]->enable_threads)
					upsgi.p[i]->enable_threads();
			}
		}
	}

	// users of the --loop option should know what they are doing... really...
#ifndef UPSGI_DEBUG
	if (upsgi.loop)
		goto unsafe;
#endif

	if (!upsgi.sockets &&
		!ushared->gateways_cnt &&
		!upsgi.no_server &&
		!upsgi.udp_socket &&
		!upsgi.emperor &&
		!upsgi.command_mode &&
		!upsgi.daemons_cnt &&
		!upsgi.crons &&
		!upsgi.emperor_proxy
#ifdef __linux__
		&& !upsgi.setns_socket
#endif
		) {
		upsgi_log("The -s/--socket option is missing and stdin is not a socket.\n");
		exit(1);
	}
	else if (!upsgi.sockets && ushared->gateways_cnt && !upsgi.no_server && !upsgi.master_process) {
		// here we will have a zombie... sorry
		upsgi_log("...you should enable the master process... really...\n");
		if (upsgi.force_gateway) {
			struct upsgi_gateway *ug = &ushared->gateways[0];
			ug->loop(0, ug->data);
			// when we are here the gateway is dead :(
		}
		exit(0);
	}

	if (!upsgi.sockets)
		upsgi.numproc = 0;

	if (upsgi.command_mode) {
		upsgi.sockets = NULL;
		upsgi.numproc = 1;
		// hack to destroy the instance after command exit
		upsgi.status.brutally_destroying = 1;
	}

#ifndef UPSGI_DEBUG
unsafe:
#endif

#ifdef UPSGI_DEBUG
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	int so_bufsize;
	socklen_t so_bufsize_len;
	while (upsgi_sock) {
		so_bufsize_len = sizeof(int);
		if (getsockopt(upsgi_sock->fd, SOL_SOCKET, SO_RCVBUF, &so_bufsize, &so_bufsize_len)) {
			upsgi_error("getsockopt()");
		}
		else {
			upsgi_debug("upsgi socket %d SO_RCVBUF size: %d\n", upsgi_sock->fd, so_bufsize);
		}

		so_bufsize_len = sizeof(int);
		if (getsockopt(upsgi_sock->fd, SOL_SOCKET, SO_SNDBUF, &so_bufsize, &so_bufsize_len)) {
			upsgi_error("getsockopt()");
		}
		else {
			upsgi_debug("upsgi socket %d SO_SNDBUF size: %d\n", upsgi_sock->fd, so_bufsize);
		}
		upsgi_sock = upsgi_sock->next;
	}
#endif


#ifndef UNBIT
	if (upsgi.sockets)
		upsgi_log("your server socket listen backlog is limited to %d connections\n", upsgi.listen_queue);
#endif


	upsgi_log("your mercy for graceful operations on workers is %d seconds\n", upsgi.worker_reload_mercy);

	upsgi_log("your request buffer size is %llu bytes\n", (unsigned long long) upsgi.buffer_size);
	if (!upsgi.__buffer_size) {
		if (upsgi.buffer_size > 0xffff) {
			upsgi_log("your request buffer size for old plugins has been limited to 64k\n");
			upsgi.__buffer_size = 0xffff;
		}
		else {
			upsgi.__buffer_size = upsgi.buffer_size;
		}
	}
	else {
		upsgi_log("your request buffer size for old plugins is %u bytes\n", upsgi.__buffer_size);
	}

	if (upsgi.crons) {
		struct upsgi_cron *ucron = upsgi.crons;
		while (ucron) {
			upsgi_log("[upsgi-cron] command \"%s\" registered as cron task\n", ucron->command);
			ucron = ucron->next;
		}
	}


	upsgi_validate_runtime_tunables();

	// initialize post buffering values
	if (upsgi.post_buffering > 0)
		upsgi_setup_post_buffering();

	// initialize workers/master shared memory segments
	upsgi_setup_workers();

	// create signal pipes if master is enabled
	if (upsgi.master_process) {
		for (i = 1; i <= upsgi.numproc; i++) {
			create_signal_pipe(upsgi.workers[i].signal_pipe);
		}
	}

	// set masterpid
	upsgi.mypid = getpid();
	masterpid = upsgi.mypid;
	upsgi.workers[0].pid = masterpid;


	if (upsgi.command_mode) {
		upsgi_log("*** Operational MODE: command ***\n");
	}
	else if (!upsgi.numproc) {
		upsgi_log("*** Operational MODE: no-workers ***\n");
	}
	else if (upsgi.threads > 1) {
		if (upsgi.numproc > 1) {
			upsgi_log("*** Operational MODE: preforking+threaded ***\n");
		}
		else {
			upsgi_log("*** Operational MODE: threaded ***\n");
		}
	}
	else if (upsgi.async > 0) {
		if (upsgi.numproc > 1) {
			upsgi_log("*** Operational MODE: preforking+async ***\n");
		}
		else {
			upsgi_log("*** Operational MODE: async ***\n");
		}
	}
	else if (upsgi.numproc > 1) {
		upsgi_log("*** Operational MODE: preforking ***\n");
	}
	else {
		upsgi_log("*** Operational MODE: single process ***\n");
	}

	// set a default request structure (for loading apps...)
	upsgi.wsgi_req = &upsgi.workers[0].cores[0].req;

	// ok, let's initialize the metrics subsystem
	upsgi_setup_metrics();

	// cores are allocated, lets allocate logformat (if required)
	if (upsgi.logformat) {
		upsgi_build_log_format(upsgi.logformat);
		upsgi.logit = upsgi_logit_lf;
		upsgi.logvectors = upsgi_malloc(sizeof(struct iovec *) * upsgi.cores);
		for (j = 0; j < upsgi.cores; j++) {
			upsgi.logvectors[j] = upsgi_malloc(sizeof(struct iovec) * upsgi.logformat_vectors);
			upsgi.logvectors[j][upsgi.logformat_vectors - 1].iov_base = "\n";
			upsgi.logvectors[j][upsgi.logformat_vectors - 1].iov_len = 1;
		}
	}


	// preinit apps (create the language environment)
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->preinit_apps) {
			upsgi.p[i]->preinit_apps();
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->preinit_apps) {
			upsgi.gp[i]->preinit_apps();
		}
	}

	//init apps hook (if not lazy)
	if (!upsgi.lazy && !upsgi.lazy_apps) {
		upsgi_init_all_apps();
		// Register upsgi atexit plugin callbacks after all applications have
		// been loaded. This ensures plugin atexit callbacks are called prior
		// to application registered atexit callbacks.
		atexit(upsgi_plugins_atexit);
	}

	if (!upsgi.master_as_root) {
		upsgi_log("dropping root privileges after application loading\n");
		upsgi_as_root();
	}

	// postinit apps (setup specific features after app initialization)
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->postinit_apps) {
			upsgi.p[i]->postinit_apps();
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->postinit_apps) {
			upsgi.gp[i]->postinit_apps();
		}
	}

	// initialize after_request hooks
	upsgi_foreach(usl, upsgi.after_request_hooks) {
		usl->custom_ptr =  dlsym(RTLD_DEFAULT, usl->value);
		if (!usl->custom_ptr) {
			upsgi_log("unable to find symbol/function \"%s\"\n", usl->value);
			exit(1);
		}
		upsgi_log("added \"%s(struct wsgi_request *)\" to the after-request chain\n", usl->value);
	}

	if (upsgi.daemonize2) {
		masterpid = upsgi_daemonize2();
	}

	if (upsgi.no_server) {
		upsgi_log("no-server mode requested. Goodbye.\n");
		exit(0);
	}


	if (!upsgi.master_process && upsgi.numproc == 0) {
		exit(0);
	}

	if (!upsgi.single_interpreter && upsgi.numproc > 0) {
		upsgi_log("*** upsgi is running in multiple interpreter mode ***\n");
	}

	// check for request plugins, and eventually print a warning
	int rp_available = 0;
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i] != &unconfigured_plugin) {
			rp_available = 1;
			break;
		}
	}
	if (!rp_available && !ushared->gateways_cnt) {
		upsgi_log("!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!\n");
		upsgi_log("no request plugin is loaded, you will not be able to manage requests.\n");
		upsgi_log("you may need to install the package for your language of choice, or simply load it with --plugin.\n");
		upsgi_log("!!!!!!!!!!! END OF WARNING !!!!!!!!!!\n");
	}

#ifdef __linux__
#ifdef MADV_MERGEABLE
	if (upsgi.linux_ksm > 0) {
		upsgi_log("[upsgi-KSM] enabled with frequency: %d\n", upsgi.linux_ksm);
	}
#endif
#endif

	if (upsgi.master_process) {
		// initialize threads with shared state
		upsgi_alarm_thread_start();
        	upsgi_exceptions_handler_thread_start();
		// initialize a mutex to avoid glibc problem with pthread+fork()
		if (upsgi.threaded_logger) {
			pthread_mutex_init(&upsgi.threaded_logger_lock, NULL);
		}

		if (upsgi.is_a_reload) {
			upsgi_log("gracefully (RE)spawned upsgi master process (pid: %d)\n", upsgi.mypid);
		}
		else {
			upsgi_log("spawned upsgi master process (pid: %d)\n", upsgi.mypid);
		}
	}



	// security in multiuser environment: allow only a subset of modifiers
	if (upsgi.allowed_modifiers) {
		for (i = 0; i < 256; i++) {
			if (!upsgi_list_has_num(upsgi.allowed_modifiers, i)) {
				upsgi.p[i]->request = unconfigured_hook;
				upsgi.p[i]->after_request = unconfigured_after_hook;
			}
		}
	}

	// master fixup
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->master_fixup) {
			upsgi.p[i]->master_fixup(0);
		}
	}



	if (!upsgi.master_process) {
		if (upsgi.numproc == 1) {
			upsgi_log("spawned upsgi worker 1 (and the only) (pid: %d, cores: %d)\n", masterpid, upsgi.cores);
		}
		else {
			upsgi_log("spawned upsgi worker 1 (pid: %d, cores: %d)\n", masterpid, upsgi.cores);
		}
		upsgi.workers[1].pid = masterpid;
		upsgi.workers[1].id = 1;
		upsgi.workers[1].last_spawn = upsgi_now();
		upsgi.workers[1].manage_next_request = 1;
		upsgi.mywid = 1;
		upsgi.respawn_delta = upsgi_now();
	}
	else {
		// setup internal signalling system
		create_signal_pipe(upsgi.shared->worker_signal_pipe);
		upsgi.signal_socket = upsgi.shared->worker_signal_pipe[1];
	}

	// upsgi is ready
	upsgi_notify_ready();
	upsgi.current_time = upsgi_now();

	// here we spawn the workers...
	if (!upsgi.status.is_cheap) {
		if (upsgi.cheaper && upsgi.cheaper_count) {
			int nproc = upsgi.cheaper_initial;
			if (!nproc)
				nproc = upsgi.cheaper_count;
			for (i = 1; i <= upsgi.numproc; i++) {
				if (i <= nproc) {
					if (upsgi_respawn_worker(i))
						break;
					upsgi.respawn_delta = upsgi_now();
				}
				else {
					upsgi.workers[i].cheaped = 1;
				}
			}
		}
		else {
			for (i = 2 - upsgi.master_process; i < upsgi.numproc + 1; i++) {
				if (upsgi_respawn_worker(i))
					break;
				upsgi.respawn_delta = upsgi_now();
			}
		}
	}

	if (upsgi.safe_pidfile2 && !upsgi.is_a_reload) {
		upsgi_write_pidfile_explicit(upsgi.safe_pidfile2, masterpid);
	}

	// END OF INITIALIZATION
	return 0;

}

// this lives in a worker thread and periodically scans for memory usage
// when evil reloaders are in place
void *mem_collector(void *foobar) {
	// block all signals
        sigset_t smask;
        sigfillset(&smask);
        pthread_sigmask(SIG_BLOCK, &smask, NULL);
	upsgi_log_verbose("mem-collector thread started for worker %d\n", upsgi.mywid);
	for(;;) {
		sleep(upsgi.mem_collector_freq);
		uint64_t rss = 0, vsz = 0;
		get_memusage(&rss, &vsz);
		upsgi.workers[upsgi.mywid].rss_size = rss;
		upsgi.workers[upsgi.mywid].vsz_size = vsz;
	}
	return NULL;
}

int upsgi_run() {

	// !!! from now on, we could be in the master or in a worker !!!
	int i;

	if (getpid() == masterpid && upsgi.master_process == 1) {
#ifdef UPSGI_AS_SHARED_LIBRARY
		int ml_ret = master_loop(upsgi.argv, upsgi.environ);
		if (ml_ret == -1) {
			return 0;
		}
#else
		(void) master_loop(upsgi.argv, upsgi.environ);
#endif
		//from now on the process is a real worker
	}

#if defined(__linux__) && defined(PR_SET_PDEATHSIG)
	// avoid workers running without master at all costs !!! (dangerous)
	if (upsgi.master_process && upsgi.no_orphans) {
		if (prctl(PR_SET_PDEATHSIG, SIGKILL)) {
			upsgi_error("upsgi_run()/prctl()");
		}
	}
#endif

	if (upsgi.evil_reload_on_rss || upsgi.evil_reload_on_as) {
		pthread_t t;
		pthread_create(&t, NULL, mem_collector, NULL);
	}


	// eventually maps (or disable) sockets for the  worker
	upsgi_map_sockets();
	upsgi_configure_worker_accept_mode();

	// eventually set cpu affinity policies (OS-dependent)
	upsgi_set_cpu_affinity();

	if (upsgi.worker_exec) {
		char *w_argv[2];
		w_argv[0] = upsgi.worker_exec;
		w_argv[1] = NULL;

		upsgi.sockets->arg &= (~O_NONBLOCK);
		if (fcntl(upsgi.sockets->fd, F_SETFL, upsgi.sockets->arg) < 0) {
			upsgi_error("fcntl()");
			exit(1);
		}

		if (upsgi.sockets->fd != 0 && !upsgi.honour_stdin) {
			if (dup2(upsgi.sockets->fd, 0) < 0) {
				upsgi_error("dup2()");
			}
		}
		execvp(w_argv[0], w_argv);
		// never here
		upsgi_error("execvp()");
		exit(1);
	}

	if (upsgi.master_as_root) {
		upsgi_log("dropping root privileges after master thread creation\n");
		upsgi_as_root();
	}

	// set default wsgi_req (for loading apps);
	upsgi.wsgi_req = &upsgi.workers[upsgi.mywid].cores[0].req;

	if (upsgi.offload_threads > 0) {
		upsgi.offload_thread = upsgi_malloc(sizeof(struct upsgi_thread *) * upsgi.offload_threads);
		for(i=0;i<upsgi.offload_threads;i++) {
			upsgi.offload_thread[i] = upsgi_offload_thread_start();
			if (!upsgi.offload_thread[i]) {
				upsgi_log("unable to start offload thread %d for worker %d !!!\n", i, upsgi.mywid);
				upsgi.offload_threads = i;
				break;
			}
		}
		upsgi_log("spawned %d offload threads for upsgi worker %d\n", upsgi.offload_threads, upsgi.mywid);
	}

	// must be run before running apps
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->post_fork) {
			upsgi.p[i]->post_fork();
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
                if (upsgi.gp[i]->post_fork) {
                        upsgi.gp[i]->post_fork();
                }
        }

	upsgi_hooks_run(upsgi.hook_post_fork, "post-fork", 1);

	if (upsgi.worker_exec2) {
                char *w_argv[2];
                w_argv[0] = upsgi.worker_exec2;
                w_argv[1] = NULL;

                upsgi.sockets->arg &= (~O_NONBLOCK);
                if (fcntl(upsgi.sockets->fd, F_SETFL, upsgi.sockets->arg) < 0) {
                        upsgi_error("fcntl()");
                        exit(1);
                }

                if (upsgi.sockets->fd != 0 && !upsgi.honour_stdin) {
                        if (dup2(upsgi.sockets->fd, 0) < 0) {
                                upsgi_error("dup2()");
                        }
                }
                execvp(w_argv[0], w_argv);
                // never here
                upsgi_error("execvp()");
                exit(1);
        }

	// must be run before running apps

	// check for worker override
        for (i = 0; i < 256; i++) {
                if (upsgi.p[i]->worker) {
                        if (upsgi.p[i]->worker()) {
				_exit(0);
			}
                }
        }

        for (i = 0; i < upsgi.gp_cnt; i++) {
                if (upsgi.gp[i]->worker) {
                        if (upsgi.gp[i]->worker()) {
				_exit(0);
			}
                }
        }

	upsgi_worker_run();
	// never here
	_exit(0);

}

void upsgi_worker_run() {

	int i;

	if (upsgi.lazy || upsgi.lazy_apps) {
		upsgi_init_all_apps();

		// Register upsgi atexit plugin callbacks after all applications have
		// been loaded. This ensures plugin atexit callbacks are called prior
		// to application registered atexit callbacks.
		atexit(upsgi_plugins_atexit);
	}

	// some apps could be mounted only on specific workers
	upsgi_init_worker_mount_apps();

	if (upsgi.async > 0) {
		// a stack of unused cores
        	upsgi.async_queue_unused = upsgi_malloc(sizeof(struct wsgi_request *) * upsgi.async);

        	// fill it with default values
               for (i = 0; i < upsgi.async; i++) {
               	upsgi.async_queue_unused[i] = &upsgi.workers[upsgi.mywid].cores[i].req;
               }

                // the first available core is the last one
                upsgi.async_queue_unused_ptr = upsgi.async - 1;
	}

	// setup UNIX signals for the worker
	if (upsgi.harakiri_options.workers > 0 && !upsgi.master_process) {
		signal(SIGALRM, (void *) &harakiri);
	}
	upsgi_unix_signal(SIGHUP, gracefully_kill);
	upsgi_unix_signal(SIGINT, end_me);
	upsgi_unix_signal(SIGTERM, end_me);

	upsgi_unix_signal(SIGUSR1, stats);
	signal(SIGUSR2, (void *) &what_i_am_doing);
	if (!upsgi.ignore_sigpipe) {
		signal(SIGPIPE, (void *) &warn_pipe);
	}

	// worker initialization done

	// run fixup handler
	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->fixup) {
			upsgi.p[i]->fixup();
		}
	}

	if (upsgi.chdir2) {
		upsgi_log("chdir() to %s\n", upsgi.chdir2);
		if (chdir(upsgi.chdir2)) {
			upsgi_error("chdir()");
			exit(1);
		}
	}


	//re - initialize wsgi_req(can be full of init_upsgi_app data)
	for (i = 0; i < upsgi.cores; i++) {
		memset(&upsgi.workers[upsgi.mywid].cores[i].req, 0, sizeof(struct wsgi_request));
		upsgi.workers[upsgi.mywid].cores[i].req.async_id = i;
	}


	// eventually remap plugins
	if (upsgi.remap_modifier) {
		char *map, *ctx = NULL;
		upsgi_foreach_token(upsgi.remap_modifier, ",", map, ctx) {
			char *colon = strchr(map, ':');
			if (colon) {
				colon[0] = 0;
				int rm_src = atoi(map);
				int rm_dst = atoi(colon + 1);
				upsgi.p[rm_dst]->request = upsgi.p[rm_src]->request;
				upsgi.p[rm_dst]->after_request = upsgi.p[rm_src]->after_request;
			}
		}
	}


	if (upsgi.cores > 1) {
		upsgi.workers[upsgi.mywid].cores[0].thread_id = pthread_self();
	}

	upsgi_ignition();

	// never here
	exit(0);

}


void upsgi_ignition() {

	int i;

	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->hijack_worker) {
			upsgi.p[i]->hijack_worker();
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->hijack_worker) {
			upsgi.gp[i]->hijack_worker();
		}
	}

	// create a pthread key, storing per-thread wsgi_request structure
	if (upsgi.threads > 1) {
		if (pthread_key_create(&upsgi.tur_key, NULL)) {
			upsgi_error("pthread_key_create()");
			exit(1);
		}
	}
	if (pipe(&upsgi.loop_stop_pipe[0])) {
		upsgi_error("pipe()")
		exit(1);
	}

	// mark the worker as "accepting" (this is a mark used by chain reloading)
	upsgi.workers[upsgi.mywid].accepting = 1;
	// ready to accept request, if i am a vassal signal Emperor about it
        if (upsgi.has_emperor && upsgi.mywid == 1) {
                char byte = 5;
                if (write(upsgi.emperor_fd, &byte, 1) != 1) {
                        upsgi_error("emperor-i-am-ready-to-accept/write()");
			upsgi_log_verbose("lost communication with the Emperor, goodbye...\n");
			gracefully_kill_them_all(0);
			exit(1);
                }
        }

	// run accepting hooks
	upsgi_hooks_run(upsgi.hook_accepting, "accepting", 1);
	if (upsgi.workers[upsgi.mywid].respawn_count == 1) {
		upsgi_hooks_run(upsgi.hook_accepting_once, "accepting-once", 1);
	}

	if (upsgi.mywid == 1) {
		upsgi_hooks_run(upsgi.hook_accepting1, "accepting1", 1);
		if (upsgi.workers[upsgi.mywid].respawn_count == 1) {
			upsgi_hooks_run(upsgi.hook_accepting1_once, "accepting1-once", 1);
		}
	}

	if (upsgi.loop) {
		void (*u_loop) (void) = upsgi_get_loop(upsgi.loop);
		if (!u_loop) {
			upsgi_log("unavailable loop engine !!!\n");
			exit(1);
		}
		if (upsgi.mywid == 1) {
			upsgi_log("*** running %s loop engine [addr:%p] ***\n", upsgi.loop, u_loop);
		}
		u_loop();
		upsgi_log("your loop engine died. R.I.P.\n");
	}
	else {
		if (upsgi.async < 1) {
			simple_loop();
		}
		else {
			async_loop();
		}
	}

	// main thread waits other threads.
	if (upsgi.threads > 1) {
		wait_for_threads();
	}

	// end of the process...
	end_me(0);
}

/*

what happens here ?

we transform the upsgi_option structure to a struct option
for passing it to getopt_long
A short options string is built.

This function could be called multiple times, so it will free previous areas

*/

void build_options() {

	int options_count = 0;
	int pos = 0;
	int i;
	// first count the base options

	struct upsgi_option *op = upsgi_base_options;
	while (op->name) {
		options_count++;
		op++;
	}

	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->options) {
			options_count += upsgi_count_options(upsgi.p[i]->options);
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->options) {
			options_count += upsgi_count_options(upsgi.gp[i]->options);
		}
	}

	// add custom options
	struct upsgi_custom_option *uco = upsgi.custom_options;
	while (uco) {
		options_count++;
		uco = uco->next;
	}

	if (upsgi.options)
		free(upsgi.options);


	// rebuild upsgi.options area
	upsgi.options = upsgi_calloc(sizeof(struct upsgi_option) * (options_count + 1));

	op = upsgi_base_options;
	while (op->name) {
		memcpy(&upsgi.options[pos], op, sizeof(struct upsgi_option));
		pos++;
		op++;
	}

	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->options) {
			int c = upsgi_count_options(upsgi.p[i]->options);
			memcpy(&upsgi.options[pos], upsgi.p[i]->options, sizeof(struct upsgi_option) * c);
			pos += c;
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->options) {
			int c = upsgi_count_options(upsgi.gp[i]->options);
			memcpy(&upsgi.options[pos], upsgi.gp[i]->options, sizeof(struct upsgi_option) * c);
			pos += c;
		}
	}

	uco = upsgi.custom_options;
        while (uco) {
                upsgi.options[pos].name = uco->name;
                if (uco->has_args) {
                        upsgi.options[pos].type = required_argument;
                }
                else {
                        upsgi.options[pos].type = no_argument;
                }
                // custom options should be immediate
                upsgi.options[pos].flags = UPSGI_OPT_IMMEDIATE;
                // help shows the option definition
                upsgi.options[pos].help = uco->value;
                upsgi.options[pos].data = uco;
                upsgi.options[pos].func = upsgi_opt_custom;

                pos++;
                uco = uco->next;
        }


	pos = 0;

	if (upsgi.long_options)
		free(upsgi.long_options);

	upsgi.long_options = upsgi_calloc(sizeof(struct option) * (options_count + 1));

	if (upsgi.short_options)
		free(upsgi.short_options);

	upsgi.short_options = upsgi_calloc((options_count * 3) + 1);

	// build long_options (this time with custom_options)
	op = upsgi.options;
	while (op->name) {
		upsgi.long_options[pos].name = op->name;
		upsgi.long_options[pos].has_arg = op->type;
		upsgi.long_options[pos].flag = 0;
		// add 1000 to avoid short_options collision
		upsgi.long_options[pos].val = 1000 + pos;
		if (op->shortcut) {
			char shortcut = (char) op->shortcut;
			// avoid duplicates in short_options
			if (!strchr(upsgi.short_options, shortcut)) {
				strncat(upsgi.short_options, &shortcut, 1);
				if (op->type == optional_argument) {
					strcat(upsgi.short_options, "::");
				}
				else if (op->type == required_argument) {
					strcat(upsgi.short_options, ":");
				}
			}
		}
		op++;
		pos++;
	}
}

/*

this function builds the help output from the upsgi.options structure

*/
void upsgi_help(char *opt, char *val, void *none) {

	size_t max_size = 0;

	fprintf(stdout, "Usage: %s [options...]\n", upsgi.binary_path);

	struct upsgi_option *op = upsgi.options;
	while (op && op->name) {
		if (strlen(op->name) > max_size) {
			max_size = strlen(op->name);
		}
		op++;
	}

	max_size++;

	op = upsgi.options;
	while (op && op->name) {
		if (op->shortcut) {
			fprintf(stdout, "    -%c|--%-*s %s\n", op->shortcut, (int) max_size - 3, op->name, op->help);
		}
		else {
			fprintf(stdout, "    --%-*s %s\n", (int) max_size, op->name, op->help);
		}
		op++;
	}

	exit(0);
}

/*

initialize all apps

*/
void upsgi_init_all_apps() {

	int i, j;

	upsgi_hooks_run(upsgi.hook_pre_app, "pre app", 1);

	// now run the pre-app scripts
	struct upsgi_string_list *usl = upsgi.exec_pre_app;
	while (usl) {
		upsgi_log("running \"%s\" (pre app)...\n", usl->value);
		int ret = upsgi_run_command_and_wait(NULL, usl->value);
		if (ret != 0) {
			upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
			exit(1);
		}
		usl = usl->next;
	}

	upsgi_foreach(usl, upsgi.call_pre_app) {
                if (upsgi_call_symbol(usl->value)) {
                        upsgi_log("unable to call function \"%s\"\n", usl->value);
			exit(1);
                }
        }


	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->init_apps) {
			upsgi.p[i]->init_apps();
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->init_apps) {
			upsgi.gp[i]->init_apps();
		}
	}

	struct upsgi_string_list *app_mps = upsgi.mounts;
	while (app_mps) {
		char *what = strchr(app_mps->value, '=');
		if (what) {
			what[0] = 0;
			what++;
			for (j = 0; j < 256; j++) {
				if (upsgi.p[j]->mount_app) {
					upsgi_log("mounting %s on %s\n", what, app_mps->value[0] == 0 ? "/" : app_mps->value);
					if (upsgi.p[j]->mount_app(app_mps->value[0] == 0 ? "/" : app_mps->value, what) != -1)
						break;
				}
			}
			what--;
			what[0] = '=';
		}
		else {
			upsgi_log("invalid mountpoint: %s\n", app_mps->value);
			exit(1);
		}
		app_mps = app_mps->next;
	}

	// no app initialized and virtualhosting enabled
	if (upsgi_apps_cnt == 0 && upsgi.numproc > 0 && !upsgi.command_mode) {
		if (upsgi.need_app) {
			if (!upsgi.lazy)
				upsgi_log("*** no app loaded. GAME OVER ***\n");
			exit(UPSGI_FAILED_APP_CODE);
		}
		else {
			upsgi_log("*** no app loaded. going in full dynamic mode ***\n");
		}
	}

	upsgi_hooks_run(upsgi.hook_post_app, "post app", 1);

	usl = upsgi.exec_post_app;
        while (usl) {
                upsgi_log("running \"%s\" (post app)...\n", usl->value);
                int ret = upsgi_run_command_and_wait(NULL, usl->value);
                if (ret != 0) {
                        upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
                        exit(1);
                }
                usl = usl->next;
        }

	upsgi_foreach(usl, upsgi.call_post_app) {
                if (upsgi_call_symbol(usl->value)) {
                        upsgi_log("unable to call function \"%s\"\n", usl->value);
                }
        }

}

void upsgi_init_worker_mount_apps() {
/*
	int i,j;
	for (i = 0; i < upsgi.mounts_cnt; i++) {
                char *what = strchr(upsgi.mounts[i], '=');
                if (what) {
                        what[0] = 0;
                        what++;
                        for (j = 0; j < 256; j++) {
                                if (upsgi.p[j]->mount_app) {
                                        if (!upsgi_startswith(upsgi.mounts[i], "worker://", 9)) {
                        			upsgi_log("mounting %s on %s\n", what, upsgi.mounts[i]+9);
                                                if (upsgi.p[j]->mount_app(upsgi.mounts[i] + 9, what, 1) != -1)
                                                        break;
                                        }
                                }
                        }
                        what--;
                        what[0] = '=';
                }
                else {
                        upsgi_log("invalid mountpoint: %s\n", upsgi.mounts[i]);
                        exit(1);
                }
        }
*/

}

void upsgi_opt_true(char *opt, char *value, void *key) {

	int *ptr = (int *) key;
	*ptr = 1;
	if (value) {
		if (!strcasecmp("false", value) || !strcasecmp("off", value) || !strcasecmp("no", value) || !strcmp("0", value)) {
			*ptr = 0;
		}
	}
}

void upsgi_opt_false(char *opt, char *value, void *key) {

        int *ptr = (int *) key;
        *ptr = 0;
        if (value) {
                if (!strcasecmp("false", value) || !strcasecmp("off", value) || !strcasecmp("no", value) || !strcmp("0", value)) {
                        *ptr = 1;
                }
        }
}

void upsgi_opt_set_immediate_gid(char *opt, char *value, void *none) {
        gid_t gid = 0;
	if (is_a_number(value)) gid = atoi(value);
	if (gid == 0) {
		struct group *ugroup = getgrnam(value);
                if (ugroup)
                	gid = ugroup->gr_gid;
	}
        if (gid <= 0) {
                upsgi_log("upsgi_opt_set_immediate_gid(): invalid gid %d\n", (int) gid);
                exit(1);
        }
        if (setgid(gid)) {
                upsgi_error("upsgi_opt_set_immediate_gid()/setgid()");
                exit(1);
        }

	if (setgroups(0, NULL)) {
        	upsgi_error("upsgi_opt_set_immediate_gid()/setgroups()");
                exit(1);
        }

	gid = getgid();
	if (!gid) {
		exit(1);
	}
	upsgi_log("immediate gid: %d\n", (int) gid);
}


void upsgi_opt_set_immediate_uid(char *opt, char *value, void *none) {
	uid_t uid = 0;
	if (is_a_number(value)) uid = atoi(value);
	if (uid == 0) {
		struct passwd *upasswd = getpwnam(value);
                if (upasswd)
                        uid = upasswd->pw_uid;
	}
	if (uid <= 0) {
		upsgi_log("upsgi_opt_set_immediate_uid(): invalid uid %d\n", uid);
		exit(1);
	}
	if (setuid(uid)) {
		upsgi_error("upsgi_opt_set_immediate_uid()/setuid()");
		exit(1);
	}

	uid = getuid();
	if (!uid) {
		exit(1);
	}
	upsgi_log("immediate uid: %d\n", (int) uid);
}

void upsgi_opt_safe_fd(char *opt, char *value, void *foobar) {
	int fd = atoi(value);
	if (fd < 0) {
		upsgi_log("invalid file descriptor: %d\n", fd);
		exit(1);
	}
	upsgi_add_safe_fd(fd);
}

void upsgi_opt_log_drain_burst(char *opt, char *value, void *key) {
	int *ptr = (int *) key;
	if (value) {
		char *endptr;
		*ptr = (int) strtol(value, &endptr, 10);
		if (*endptr) {
			upsgi_log("[WARNING] non-numeric value \"%s\" for option \"%s\" - using %d !\n", value, opt, *ptr);
		}
	}
	else {
		*ptr = 1;
	}

	if (*ptr < 1) {
		upsgi_log("invalid log-drain-burst %d, clamping to 1\n", *ptr);
		*ptr = 1;
	}
	else if (*ptr > 1024) {
		upsgi_log("excessive log-drain-burst %d, clamping to 1024\n", *ptr);
		*ptr = 1024;
	}
}

void upsgi_opt_set_int(char *opt, char *value, void *key) {
	int *ptr = (int *) key;
	if (value) {
		char *endptr;
		*ptr = (int)strtol(value, &endptr, 10);
		if (*endptr) {
			upsgi_log("[WARNING] non-numeric value \"%s\" for option \"%s\" - using %d !\n", value, opt, *ptr);
		}
	}
	else {
		*ptr = 1;
	}

	if (*ptr < 0) {
		upsgi_log("invalid value for option \"%s\": must be > 0\n", opt);
		exit(1);
	}
}

void upsgi_opt_uid(char *opt, char *value, void *key) {
	uid_t uid = 0;
	if (is_a_number(value)) uid = atoi(value);
	if (!uid) {
		struct passwd *p = getpwnam(value);
		if (p) {
			uid = p->pw_uid;	
		}
		else {
			upsgi_log("unable to find user %s\n", value);
			exit(1);
		}
	}
	if (key)  {
        	uid_t *ptr = (uid_t *) key;
        	*ptr = uid;
        }
}

void upsgi_opt_gid(char *opt, char *value, void *key) {
        gid_t gid = 0;
	if (is_a_number(value)) gid = atoi(value);
        if (!gid) {
                struct group *g = getgrnam(value);
                if (g) {
                        gid = g->gr_gid;
                }
                else {
                        upsgi_log("unable to find group %s\n", value);
			exit(1);
                }
        }       
        if (key)  {
                gid_t *ptr = (gid_t *) key;
                *ptr = gid;
        }       
}     

void upsgi_opt_set_rawint(char *opt, char *value, void *key) {
	int *ptr = (int *) key;
	if (value) {
		*ptr = atoi((char *) value);
	}
	else {
		*ptr = 1;
	}
}


void upsgi_opt_set_64bit(char *opt, char *value, void *key) {
	uint64_t *ptr = (uint64_t *) key;

	if (value) {
		*ptr = (strtoul(value, NULL, 10));
	}
	else {
		*ptr = 1;
	}
}

void upsgi_opt_set_16bit(char *opt, char *value, void *key) {
        uint16_t *ptr = (uint16_t *) key;

        if (value) {
		unsigned long n = strtoul(value, NULL, 10);
		if (n > 65535) n = 65535;
                *ptr = n;
        }
        else {
                *ptr = 1;
        }
}


void upsgi_opt_set_megabytes(char *opt, char *value, void *key) {
	uint64_t *ptr = (uint64_t *) key;
	if(strchr(value, '.') != NULL) {
		double megabytes = atof(value);
		*ptr = (uint64_t)(megabytes * 1024 * 1024);
		return;
	}
	*ptr = (uint64_t)strtoul(value, NULL, 10) * 1024 * 1024;
}

void upsgi_opt_set_str(char *opt, char *value, void *key) {
	char **ptr = (char **) key;
	if (!value) {
		*ptr = "";
		return;	
	}
	*ptr = (char *) value;
}

void upsgi_opt_set_null(char *opt, char *value, void *key) {
        char **ptr = (char **) key;
        *ptr = NULL;
}


void upsgi_opt_set_logger(char *opt, char *value, void *prefix) {

	if (!value)
		value = "";

	if (prefix) {
		upsgi_string_new_list(&upsgi.requested_logger, upsgi_concat3((char *) prefix, ":", value));
	}
	else {
		upsgi_string_new_list(&upsgi.requested_logger, upsgi_str(value));
	}
}

void upsgi_opt_set_worker_logger(char *opt, char *value, void *prefix) {
	upsgi_opt_set_logger(opt, value, prefix);
	upsgi.log_worker = 1;
}

void upsgi_opt_set_req_logger(char *opt, char *value, void *prefix) {

	if (!value)
		value = "";

	if (prefix) {
		upsgi_string_new_list(&upsgi.requested_req_logger, upsgi_concat3((char *) prefix, ":", value));
	}
	else {
		upsgi_string_new_list(&upsgi.requested_req_logger, upsgi_str(value));
	}
}

void upsgi_opt_set_str_spaced(char *opt, char *value, void *key) {
	char **ptr = (char **) key;
	*ptr = upsgi_concat2((char *) value, " ");
}

void upsgi_opt_add_string_list(char *opt, char *value, void *list) {
	struct upsgi_string_list **ptr = (struct upsgi_string_list **) list;
	upsgi_string_new_list(ptr, value);
}

void upsgi_opt_add_addr_list(char *opt, char *value, void *list) {
        struct upsgi_string_list **ptr = (struct upsgi_string_list **) list;
	int af = AF_INET;
#ifdef AF_INET6
	void *ip = upsgi_malloc(16);
	if (strchr(value, ':')) {
		af = AF_INET6;
	}
#else
	void *ip = upsgi_malloc(4);
#endif
	
	if (inet_pton(af, value, ip) <= 0) {
		upsgi_log("%s: invalid address\n", opt);
		upsgi_error("upsgi_opt_add_addr_list()");
		exit(1);
	}

        struct upsgi_string_list *usl = upsgi_string_new_list(ptr, ip);
	usl->custom = af;
	usl->custom_ptr = value;
}


void upsgi_opt_add_string_list_custom(char *opt, char *value, void *list) {
	struct upsgi_string_list **ptr = (struct upsgi_string_list **) list;
	struct upsgi_string_list *usl = upsgi_string_new_list(ptr, value);
	usl->custom = 1;
}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
void upsgi_opt_add_regexp_list(char *opt, char *value, void *list) {
	struct upsgi_regexp_list **ptr = (struct upsgi_regexp_list **) list;
	upsgi_regexp_new_list(ptr, value);
}

void upsgi_opt_add_regexp_custom_list(char *opt, char *value, void *list) {
	char *space = strchr(value, ' ');
	if (!space) {
		upsgi_log("invalid custom regexp syntax: must be <custom> <regexp>\n");
		exit(1);
	}
	char *custom = upsgi_concat2n(value, space - value, "", 0);
	struct upsgi_regexp_list **ptr = (struct upsgi_regexp_list **) list;
	upsgi_regexp_custom_new_list(ptr, space + 1, custom);
}
#endif

void upsgi_opt_add_shared_socket(char *opt, char *value, void *protocol) {
	struct upsgi_socket *us = upsgi_new_shared_socket(generate_socket_name(value));
	if (!strcmp(opt, "undeferred-shared-socket")) {
		us->no_defer = 1;
	}
}

void upsgi_opt_add_socket(char *opt, char *value, void *protocol) {
	struct upsgi_socket *upsgi_sock = upsgi_new_socket(generate_socket_name(value));
	upsgi_sock->name_len = strlen(upsgi_sock->name);
	upsgi_sock->proto_name = protocol;
}

#ifdef UPSGI_SSL
void upsgi_opt_add_ssl_socket(char *opt, char *value, void *protocol) {
	char *client_ca = NULL;

        // build socket, certificate and key file
        char *sock = upsgi_str(value);
        char *crt = strchr(sock, ',');
        if (!crt) {
                upsgi_log("invalid https-socket syntax must be socket,crt,key\n");
                exit(1);
        }
        *crt = '\0'; crt++;
        char *key = strchr(crt, ',');
        if (!key) {
                upsgi_log("invalid https-socket syntax must be socket,crt,key\n");
                exit(1);
        }
        *key = '\0'; key++;

        char *ciphers = strchr(key, ',');
        if (ciphers) {
                *ciphers = '\0'; ciphers++;
                client_ca = strchr(ciphers, ',');
                if (client_ca) {
                        *client_ca = '\0'; client_ca++;
                }
        }

	struct upsgi_socket *upsgi_sock = upsgi_new_socket(generate_socket_name(sock));
	upsgi_sock->name_len = strlen(upsgi_sock->name);
        upsgi_sock->proto_name = protocol;

        // ok we have the socket, initialize ssl if required
        if (!upsgi.ssl_initialized) {
                upsgi_ssl_init();
        }

        // initialize ssl context
        upsgi_sock->ssl_ctx = upsgi_ssl_new_server_context(upsgi_sock->name, crt, key, ciphers, client_ca);
        if (!upsgi_sock->ssl_ctx) {
                exit(1);
        }
}
#endif

void upsgi_opt_add_socket_no_defer(char *opt, char *value, void *protocol) {
        struct upsgi_socket *upsgi_sock = upsgi_new_socket(generate_socket_name(value));
        upsgi_sock->name_len = strlen(upsgi_sock->name);
        upsgi_sock->proto_name = protocol;
	upsgi_sock->no_defer = 1;
}

void upsgi_opt_add_lazy_socket(char *opt, char *value, void *protocol) {
	struct upsgi_socket *upsgi_sock = upsgi_new_socket(generate_socket_name(value));
	upsgi_sock->proto_name = protocol;
	upsgi_sock->bound = 1;
	upsgi_sock->lazy = 1;
}


void upsgi_opt_set_placeholder(char *opt, char *value, void *ph) {

	char *p = strchr(value, '=');
	if (!p) {
		upsgi_log("invalid placeholder/--set value\n");
		exit(1);
	}

	p[0] = 0;
	add_exported_option_do(upsgi_str(value), p + 1, 0, ph ? 1 : 0);
	p[0] = '=';

}

#ifdef UPSGI_SSL
void upsgi_opt_scd(char *opt, char *value, void *foobar) {
	// openssl could not be initialized
	if (!upsgi.ssl_initialized) {
		upsgi_ssl_init();
	}

	char *colon = strchr(value, ':');
	if (!colon) {
		upsgi_log("invalid syntax for '%s', must be: <digest>:<directory>\n", opt);
		exit(1);
	}

	char *algo = upsgi_concat2n(value, (colon - value), "", 0);
	upsgi.subscriptions_sign_check_md = EVP_get_digestbyname(algo);
	if (!upsgi.subscriptions_sign_check_md) {
		upsgi_log("unable to find digest algorithm: %s\n", algo);
		exit(1);
	}
	free(algo);

	upsgi.subscriptions_sign_check_dir = colon + 1;
}
#endif

void upsgi_opt_set_umask(char *opt, char *value, void *mode) {
	int error = 0;
	mode_t mask = upsgi_mode_t(value, &error);
	if (error) {
		upsgi_log("invalid umask: %s\n", value);
	}
	umask(mask);

	upsgi.do_not_change_umask = 1;
}

void upsgi_opt_exit(char *opt, char *value, void *none) {
	int exit_code = 1;
	if (value) {
		exit_code = atoi(value);
	}
	exit(exit_code);
}

void upsgi_opt_print(char *opt, char *value, void *str) {
	if (str) {
		fprintf(stdout, "%s\n", (char *) str);
		exit(0);
	}
	fprintf(stdout, "%s\n", value);
}

void upsgi_opt_set_uid(char *opt, char *value, void *none) {
	if (is_a_number(value)) upsgi.uid = atoi(value);
	if (!upsgi.uid)
		upsgi.uidname = value;
}

void upsgi_opt_set_gid(char *opt, char *value, void *none) {
	if (is_a_number(value)) upsgi.gid = atoi(value);
	if (!upsgi.gid)
		upsgi.gidname = value;
}

#ifdef UPSGI_CAP
void upsgi_opt_set_cap(char *opt, char *value, void *none) {
	upsgi.cap_count = upsgi_build_cap(value, &upsgi.cap);
	if (upsgi.cap_count == 0) {
		upsgi_log("[security] empty capabilities mask !!!\n");
		exit(1);
	}
}
void upsgi_opt_set_emperor_cap(char *opt, char *value, void *none) {
	upsgi.emperor_cap_count = upsgi_build_cap(value, &upsgi.emperor_cap);
	if (upsgi.emperor_cap_count == 0) {
		upsgi_log("[security] empty capabilities mask !!!\n");
		exit(1);
	}
}
#endif
#ifdef __linux__
void upsgi_opt_set_unshare(char *opt, char *value, void *mask) {
	upsgi_build_unshare(value, (int *) mask);
}
#endif

void upsgi_opt_set_env(char *opt, char *value, void *none) {
	if (putenv(value)) {
		upsgi_error("putenv()");
	}
}

void upsgi_opt_unset_env(char *opt, char *value, void *none) {
#ifdef UNSETENV_VOID
	unsetenv(value);
#else
	if (unsetenv(value)) {
		upsgi_error("unsetenv()");
	}
#endif
}

void upsgi_opt_pidfile_signal(char *opt, char *pidfile, void *sig) {

	long *signum_fake_ptr = (long *) sig;
	int signum = (long) signum_fake_ptr;
	exit(signal_pidfile(signum, pidfile));
}

void upsgi_opt_load_dl(char *opt, char *value, void *none) {
	if (!dlopen(value, RTLD_NOW | RTLD_GLOBAL)) {
		upsgi_log("%s\n", dlerror());
	}
}

void upsgi_opt_load_plugin(char *opt, char *value, void *none) {

	char *plugins_list = upsgi_concat2(value, "");
	char *p, *ctx = NULL;
	upsgi_foreach_token(plugins_list, ",", p, ctx) {
#ifdef UPSGI_DEBUG
		upsgi_debug("loading plugin %s\n", p);
#endif
		if (upsgi_load_plugin(-1, p, NULL)) {
			build_options();
		}
		else if (!upsgi_startswith(opt, "need-", 5)) {
			upsgi_log("unable to load plugin \"%s\"\n", p);
			exit(1);
		}
	}
	free(p);
	free(plugins_list);
}

void upsgi_opt_check_static(char *opt, char *value, void *foobar) {

	upsgi_dyn_dict_new(&upsgi.check_static, value, strlen(value), NULL, 0);
	upsgi_log("[upsgi-static] added check for %s\n", value);
	upsgi.build_mime_dict = 1;

}

void upsgi_opt_add_dyn_dict(char *opt, char *value, void *dict) {

	char *equal = strchr(value, '=');
	if (!equal) {
		upsgi_log("invalid dictionary syntax for %s\n", opt);
		exit(1);
	}

	struct upsgi_dyn_dict **udd = (struct upsgi_dyn_dict **) dict;

	upsgi_dyn_dict_new(udd, value, equal - value, equal + 1, strlen(equal + 1));

}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
void upsgi_opt_add_regexp_dyn_dict(char *opt, char *value, void *dict) {

	char *space = strchr(value, ' ');
	if (!space) {
		upsgi_log("invalid dictionary syntax for %s\n", opt);
		exit(1);
	}

	struct upsgi_dyn_dict **udd = (struct upsgi_dyn_dict **) dict;

	struct upsgi_dyn_dict *new_udd = upsgi_dyn_dict_new(udd, value, space - value, space + 1, strlen(space + 1));

	char *regexp = upsgi_concat2n(value, space - value, "", 0);

	if (upsgi_regexp_build(regexp, &new_udd->pattern)) {
		exit(1);
	}

	free(regexp);
}
#endif


void upsgi_opt_fileserve_mode(char *opt, char *value, void *foobar) {

	if (!strcasecmp("x-sendfile", value)) {
		upsgi.file_serve_mode = 2;
	}
	else if (!strcasecmp("xsendfile", value)) {
		upsgi.file_serve_mode = 2;
	}
	else if (!strcasecmp("x-accel-redirect", value)) {
		upsgi.file_serve_mode = 1;
	}
	else if (!strcasecmp("xaccelredirect", value)) {
		upsgi.file_serve_mode = 1;
	}
	else if (!strcasecmp("nginx", value)) {
		upsgi.file_serve_mode = 1;
	}

}

void upsgi_opt_static_map(char *opt, char *value, void *static_maps) {

	struct upsgi_dyn_dict **maps = (struct upsgi_dyn_dict **) static_maps;
	char *mountpoint = upsgi_str(value);

	char *docroot = strchr(mountpoint, '=');

	if (!docroot) {
		upsgi_log("invalid document root in static map, syntax mountpoint=docroot\n");
		exit(1);
	}
	docroot[0] = 0;
	docroot++;
	upsgi_dyn_dict_new(maps, mountpoint, strlen(mountpoint), docroot, strlen(docroot));
	upsgi_log_initial("[upsgi-static] added mapping for %s => %s\n", mountpoint, docroot);
	upsgi.build_mime_dict = 1;
}


int upsgi_zerg_attach(char *value) {

	int count = 8;
	int zerg_fd = upsgi_connect(value, 30, 0);
	if (zerg_fd < 0) {
		upsgi_log("--- unable to connect to zerg server %s ---\n", value);
		return -1;
	}

	int last_count = count;

	int *zerg = upsgi_attach_fd(zerg_fd, &count, "upsgi-zerg", 10);
	if (zerg == NULL) {
		if (last_count != count) {
			close(zerg_fd);
			zerg_fd = upsgi_connect(value, 30, 0);
			if (zerg_fd < 0) {
				upsgi_log("--- unable to connect to zerg server %s ---\n", value);
				return -1;
			}
			zerg = upsgi_attach_fd(zerg_fd, &count, "upsgi-zerg", 10);
		}
	}

	if (zerg == NULL) {
		upsgi_log("--- invalid data received from zerg-server ---\n");
		close(zerg_fd);
		return -1;
	}

	if (!upsgi.zerg) {
		upsgi.zerg = zerg;
	}
	else {
		int pos = 0;
		for (;;) {
			if (upsgi.zerg[pos] == -1) {
				upsgi.zerg = realloc(upsgi.zerg, (sizeof(int) * (pos)) + (sizeof(int) * count + 1));
				if (!upsgi.zerg) {
					upsgi_error("realloc()");
					exit(1);
				}
				memcpy(&upsgi.zerg[pos], zerg, (sizeof(int) * count + 1));
				break;
			}
			pos++;
		}
		free(zerg);
	}

	close(zerg_fd);
	return 0;
}

void upsgi_opt_signal(char *opt, char *value, void *foobar) {
	upsgi_command_signal(value);
}

void upsgi_opt_log_date(char *opt, char *value, void *foobar) {

	upsgi.logdate = 1;
	if (value) {
		if (strcasecmp("true", value) && strcasecmp("1", value) && strcasecmp("on", value) && strcasecmp("yes", value)) {
			upsgi.log_strftime = value;
		}
	}
}

void upsgi_opt_chmod_socket(char *opt, char *value, void *foobar) {

	int i;

	upsgi.chmod_socket = 1;
	if (value) {
		if (strlen(value) == 1 && *value == '1') {
			return;
		}
		if (strlen(value) != 3) {
			upsgi_log("invalid chmod value: %s\n", value);
			exit(1);
		}
		for (i = 0; i < 3; i++) {
			if (value[i] < '0' || value[i] > '7') {
				upsgi_log("invalid chmod value: %s\n", value);
				exit(1);
			}
		}

		upsgi.chmod_socket_value = (upsgi.chmod_socket_value << 3) + (value[0] - '0');
		upsgi.chmod_socket_value = (upsgi.chmod_socket_value << 3) + (value[1] - '0');
		upsgi.chmod_socket_value = (upsgi.chmod_socket_value << 3) + (value[2] - '0');
	}

}

void upsgi_opt_logfile_chmod(char *opt, char *value, void *foobar) {

	int i;

	if (strlen(value) != 3) {
		upsgi_log("invalid chmod value: %s\n", value);
		exit(1);
	}
	for (i = 0; i < 3; i++) {
		if (value[i] < '0' || value[i] > '7') {
			upsgi_log("invalid chmod value: %s\n", value);
			exit(1);
		}
	}

	upsgi.chmod_logfile_value = (upsgi.chmod_logfile_value << 3) + (value[0] - '0');
	upsgi.chmod_logfile_value = (upsgi.chmod_logfile_value << 3) + (value[1] - '0');
	upsgi.chmod_logfile_value = (upsgi.chmod_logfile_value << 3) + (value[2] - '0');

}

void upsgi_opt_max_vars(char *opt, char *value, void *foobar) {

	upsgi.max_vars = atoi(value);
	upsgi.vec_size = 4 + 1 + (4 * upsgi.max_vars);
}

void upsgi_opt_deprecated(char *opt, char *value, void *message) {
	upsgi_log("[WARNING] option \"%s\" is deprecated: %s\n", opt, (char *) message);
}

void upsgi_opt_load(char *opt, char *filename, void *none) {

	// here we need to avoid setting upper magic vars
	int orig_magic = upsgi.magic_table_first_round;
	upsgi.magic_table_first_round = 1;

	if (upsgi_endswith(filename, ".ini")) {
		upsgi_opt_load_ini(opt, filename, none);
		goto end;
	}
#ifdef UPSGI_XML
	if (upsgi_endswith(filename, ".xml")) {
		upsgi_opt_load_xml(opt, filename, none);
		goto end;
	}
#endif
#ifdef UPSGI_YAML
	if (upsgi_endswith(filename, ".yaml")) {
		upsgi_opt_load_yml(opt, filename, none);
		goto end;
	}
	if (upsgi_endswith(filename, ".yml")) {
		upsgi_opt_load_yml(opt, filename, none);
		goto end;
	}
#endif
#ifdef UPSGI_JSON
	if (upsgi_endswith(filename, ".json")) {
		upsgi_opt_load_json(opt, filename, none);
		goto end;
	}
	if (upsgi_endswith(filename, ".js")) {
		upsgi_opt_load_json(opt, filename, none);
		goto end;
	}
#endif

	// fallback to pluggable system
	upsgi_opt_load_config(opt, filename, none);
end:
	upsgi.magic_table_first_round = orig_magic;
}

void upsgi_opt_logic(char *opt, char *arg, void *func) {

	if (upsgi.logic_opt) {
		upsgi_log("recursive logic in options is not supported (option = %s)\n", opt);
		exit(1);
	}
	upsgi.logic_opt = (int (*)(char *, char *)) func;
	upsgi.logic_opt_cycles = 0;
	if (arg) {
		upsgi.logic_opt_arg = upsgi_str(arg);
	}
	else {
		upsgi.logic_opt_arg = NULL;
	}
}

void upsgi_opt_noop(char *opt, char *foo, void *bar) {
}

/*
 * upsgi parse-only compatibility shim.
 *
 * Use this for legacy options that must continue to parse for migration
 * safety while remaining runtime inert in the PSGI-only fork.
 *
 * Current users:
 * - http-modifier1/2
 * - http-socket-modifier1/2
 * - https-socket-modifier1/2
 * - perl-no-die-catch
 *
 * Keep this silent by default. If a future debug-only compatibility trace is
 * needed, add it here instead of scattering per-option warnings.
 */
void upsgi_opt_compat_noop(char *opt, char *foo, void *bar) {
	(void) opt;
	(void) foo;
	(void) bar;
}

void upsgi_opt_load_ini(char *opt, char *filename, void *none) {
	config_magic_table_fill(filename, upsgi.magic_table);
	upsgi_ini_config(filename, upsgi.magic_table);
}

void upsgi_opt_load_config(char *opt, char *filename, void *none) {
        struct upsgi_configurator *uc = upsgi.configurators;
        while(uc) {
                if (upsgi_endswith(filename, uc->name)) {
                        config_magic_table_fill(filename, upsgi.magic_table);
                        uc->func(filename, upsgi.magic_table);
                        return;
                }
                uc = uc->next;
        }

	upsgi_log("unable to load configuration from %s\n", filename);
	exit(1);
}

#ifdef UPSGI_XML
void upsgi_opt_load_xml(char *opt, char *filename, void *none) {
	config_magic_table_fill(filename, upsgi.magic_table);
	upsgi_xml_config(filename, upsgi.wsgi_req, upsgi.magic_table);
}
#endif

#ifdef UPSGI_YAML
void upsgi_opt_load_yml(char *opt, char *filename, void *none) {
	config_magic_table_fill(filename, upsgi.magic_table);
	upsgi_yaml_config(filename, upsgi.magic_table);
}
#endif

#ifdef UPSGI_JSON
void upsgi_opt_load_json(char *opt, char *filename, void *none) {
	config_magic_table_fill(filename, upsgi.magic_table);
	upsgi_json_config(filename, upsgi.magic_table);
}
#endif

void upsgi_opt_add_custom_option(char *opt, char *value, void *none) {

	struct upsgi_custom_option *uco = upsgi.custom_options, *old_uco;

	if (!uco) {
		upsgi.custom_options = upsgi_malloc(sizeof(struct upsgi_custom_option));
		uco = upsgi.custom_options;
	}
	else {
		while (uco) {
			old_uco = uco;
			uco = uco->next;
		}

		uco = upsgi_malloc(sizeof(struct upsgi_custom_option));
		old_uco->next = uco;
	}

	char *copy = upsgi_str(value);
	char *equal = strchr(copy, '=');
	if (!equal) {
		upsgi_log("invalid %s syntax, must be newoption=template\n", value);
		exit(1);
	}
	*equal = 0;

	uco->name = copy;
	uco->value = equal + 1;
	uco->has_args = 0;
	// a little hack, we allow the user to skip the first 2 arguments (yes.. it is silly...but users tend to make silly things...)
	if (strstr(uco->value, "$1") || strstr(uco->value, "$2") || strstr(uco->value, "$3")) {
		uco->has_args = 1;
	}
	uco->next = NULL;
	build_options();
}


void upsgi_opt_flock(char *opt, char *filename, void *none) {

	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		upsgi_error_open(filename);
		exit(1);
	}

	if (upsgi_fcntl_is_locked(fd)) {
		upsgi_log("upsgi ERROR: %s is locked by another instance\n", filename);
		exit(1);
	}
}

void upsgi_opt_flock_wait(char *opt, char *filename, void *none) {

	int fd = open(filename, O_RDWR);
	if (fd < 0) {
		upsgi_error_open(filename);
		exit(1);
	}

	if (upsgi_fcntl_lock(fd)) {
		exit(1);
	}
}

// report CFLAGS used for compiling the server
// use that values to build external plugins
void upsgi_opt_cflags(char *opt, char *filename, void *foobar) {
	fprintf(stdout, "%s\n", upsgi_get_cflags());
	exit(0);
}

char *upsgi_get_cflags() {
	size_t len = sizeof(UPSGI_CFLAGS) -1;
        char *src = UPSGI_CFLAGS;
        char *ptr = upsgi_malloc((len / 2) + 1);
        char *base = ptr;
        size_t i;
        unsigned int u;
        for (i = 0; i < len; i += 2) {
                sscanf(src + i, "%2x", &u);
                *ptr++ = (char) u;
        }
	*ptr ++= 0;
	return base;
}

// report upsgi.h used for compiling the server
// use that values to build external plugins
extern char *upsgi_dot_h;
char *upsgi_get_dot_h() {
	char *src = upsgi_dot_h;
	size_t len = strlen(src);
        char *ptr = upsgi_malloc((len / 2) + 1);
        char *base = ptr;
        size_t i;
        unsigned int u;
        for (i = 0; i < len; i += 2) {
                sscanf(src + i, "%2x", &u);
                *ptr++ = (char) u;
        }
#ifdef UPSGI_ZLIB
	struct upsgi_buffer *ub = upsgi_zlib_decompress(base, ptr-base);
	if (!ub) {
		free(base);
		return "";
	}
	// add final null byte
	upsgi_buffer_append(ub, "\0", 1);
	free(base);
	// base is the final blob
	base = ub->buf;
	ub->buf = NULL;
	upsgi_buffer_destroy(ub);
#else
        // add final null byte
        *ptr = '\0';
#endif
	return base;
}
void upsgi_opt_dot_h(char *opt, char *filename, void *foobar) {
        fprintf(stdout, "%s\n", upsgi_get_dot_h());
        exit(0);
}

extern char *upsgi_config_py;
char *upsgi_get_config_py() {
        char *src = upsgi_config_py;
        size_t len = strlen(src);
        char *ptr = upsgi_malloc((len / 2) + 1);
        char *base = ptr;
        size_t i;
        unsigned int u;
        for (i = 0; i < len; i += 2) {
                sscanf(src + i, "%2x", &u);
                *ptr++ = (char) u;
        }
#ifdef UPSGI_ZLIB
        struct upsgi_buffer *ub = upsgi_zlib_decompress(base, ptr-base);
        if (!ub) {
                free(base);
                return "";
        }
        // add final null byte
        upsgi_buffer_append(ub, "\0", 1);
        free(base);
        // base is the final blob
        base = ub->buf;
        ub->buf = NULL;
        upsgi_buffer_destroy(ub);
#else
        // add final null byte
        *ptr = '\0';
#endif
        return base;
}

void upsgi_opt_config_py(char *opt, char *filename, void *foobar) {
        fprintf(stdout, "%s\n", upsgi_get_config_py());
        exit(0);
}


void upsgi_opt_build_plugin(char *opt, char *directory, void *foobar) {
	upsgi_build_plugin(directory);
	exit(1);
}

void upsgi_opt_connect_and_read(char *opt, char *address, void *foobar) {

	char buf[8192];

	int fd = upsgi_connect(address, -1, 0);
	while (fd >= 0) {
		int ret = upsgi_waitfd(fd, -1);
		if (ret <= 0) {
			exit(0);
		}
		ssize_t len = read(fd, buf, 8192);
		if (len <= 0) {
			exit(0);
		}
		upsgi_log("%.*s", (int) len, buf);
	}
	upsgi_error("upsgi_connect()");
	exit(1);
}

void upsgi_opt_extract(char *opt, char *address, void *foobar) {

	size_t len = 0;
	char *buf;

	buf = upsgi_open_and_read(address, &len, 0, NULL);
	if (len > 0) {
		if (write(1, buf, len) != (ssize_t) len) {
			upsgi_error("write()");
			exit(1);
		};
	};
	exit(0);
}

void upsgi_print_sym(char *opt, char *symbol, void *foobar) {
	char **sym = dlsym(RTLD_DEFAULT, symbol);
	if (sym) {
		upsgi_log("%s", *sym);
		exit(0);
	}
	
	char *symbol_start = upsgi_concat2(symbol, "_start");
	char *symbol_end = upsgi_concat2(symbol, "_end");

	char *sym_s = dlsym(RTLD_DEFAULT, symbol_start);
	char *sym_e = dlsym(RTLD_DEFAULT, symbol_end);

	if (sym_s && sym_e) {
		upsgi_log("%.*s", sym_e - sym_s, sym_s);
	}

	exit(0);
}

void upsgi_update_pidfiles() {
	if (upsgi.pidfile) {
		upsgi_write_pidfile(upsgi.pidfile);
	}
	if (upsgi.pidfile2) {
		upsgi_write_pidfile(upsgi.pidfile2);
	}
	if (upsgi.safe_pidfile) {
		upsgi_write_pidfile(upsgi.safe_pidfile);
	}
	if (upsgi.safe_pidfile2) {
		upsgi_write_pidfile(upsgi.safe_pidfile2);
	}
}

void upsgi_opt_binary_append_data(char *opt, char *value, void *none) {

	size_t size;
	char *buf = upsgi_open_and_read(value, &size, 0, NULL);

	uint64_t file_len = size;

	if (write(1, buf, size) != (ssize_t) size) {
		upsgi_error("upsgi_opt_binary_append_data()/write()");
		exit(1);
	}

	if (write(1, &file_len, 8) != 8) {
		upsgi_error("upsgi_opt_binary_append_data()/write()");
		exit(1);
	}

	exit(0);
}
