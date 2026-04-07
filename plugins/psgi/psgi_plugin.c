#include "psgi.h"

extern char **environ;
extern struct upsgi_server upsgi;

struct upsgi_perl uperl;

struct upsgi_plugin psgi_plugin;

static void upsgi_opt_plshell(char *opt, char *value, void *foobar) {

        upsgi.honour_stdin = 1;
        if (value) {
                uperl.shell = value;
        }
        else {
                uperl.shell = "";
        }

        if (!strcmp("plshell-oneshot", opt)) {
                uperl.shell_oneshot = 1;
        }
}

EXTERN_C void xs_init (pTHX);
int upsgi_perl_init(void);

static void upsgi_opt_early_perl(char *opt, char *value, void *foobar) {
	// avoid duplicates
	if (uperl.early_interpreter) return;
	upsgi_perl_init();
	uperl.early_interpreter = uperl.main[0];

	// HACK the following allocations ensure correct xs initialization
	uperl.tmp_streaming_stash = upsgi_calloc(sizeof(HV *) * upsgi.threads);
        uperl.tmp_input_stash = upsgi_calloc(sizeof(HV *) * upsgi.threads);
        uperl.tmp_error_stash = upsgi_calloc(sizeof(HV *) * upsgi.threads);
        uperl.tmp_stream_responder = upsgi_calloc(sizeof(CV *) * upsgi.threads);
        uperl.tmp_psgix_logger = upsgi_calloc(sizeof(CV *) * upsgi.threads);
        uperl.tmp_psgix_informational = upsgi_calloc(sizeof(CV *) * upsgi.threads);

	char *perl_e_arg = upsgi_concat2("#line 0 ", value);
        char *perl_init_arg[] = { "", "-e", perl_e_arg };
        perl_parse(uperl.early_interpreter, xs_init, 3, perl_init_arg, NULL);
	free(perl_e_arg);
}

static void upsgi_opt_early_psgi(char *opt, char *value, void *foobar) {
	upsgi_perl_init();
	init_psgi_app(NULL, value, strlen(value), uperl.main);
	if (!uperl.early_psgi_callable) exit(1);
}

static void upsgi_opt_early_exec(char *opt, char *value, void *foobar) {
        upsgi_perl_init();
	if (!uperl.early_interpreter) {
        	perl_parse(uperl.main[0], xs_init, 3, uperl.embedding, NULL);
	}
        SV *dollar_zero = get_sv("0", GV_ADD);
        sv_setsv(dollar_zero, newSVpv(value, strlen(value)));
        upsgi_perl_exec(value);
}


/*
 * upsgi PSGI option classification:
 * - baseline supported: psgi, log-exceptions
 * - advanced/reference: psgi-enable-psgix-io
 * - compatibility-only: perl-no-die-catch
 * - Perl extras/supporting components remain available in v1 below
 */
struct upsgi_option upsgi_perl_options[] = {

        /* upsgi baseline PSGI options */
        {"psgi", required_argument, 0, "load a PSGI app", upsgi_opt_set_str, &uperl.psgi, 0},
        {"log-exceptions", no_argument, 0, "enable PSGI exception logging with Perl stack traces for debugging", upsgi_opt_true, &uperl.log_exceptions, 0},

        /* advanced/reference PSGI support kept in v1 */
        {"psgi-enable-psgix-io", no_argument, 0, "enable PSGIx.IO support for compatible PSGI apps", upsgi_opt_true, &uperl.enable_psgix_io, 0},

        /* compatibility-only: centralized silent shim for legacy PSGI configs */
        {"perl-no-die-catch", no_argument, 0, "accepted for migration compatibility; default PSGI exception logging is already disabled", upsgi_opt_compat_noop, NULL, 0},

        /* Perl extras and supporting components retained in v1 */
        {"perl-local-lib", required_argument, 0, "set perl locallib path", upsgi_opt_set_str, &uperl.locallib, 0},
#ifdef PERL_VERSION_STRING
        {"perl-version", no_argument, 0, "print perl version", upsgi_opt_print, PERL_VERSION_STRING, UPSGI_OPT_IMMEDIATE},
#endif
        {"perl-args", required_argument, 0, "add items (space separated) to @ARGV", upsgi_opt_set_str, &uperl.argv_items, 0},
        {"perl-arg", required_argument, 0, "add an item to @ARGV", upsgi_opt_add_string_list, &uperl.argv_item, 0},
        {"perl-exec", required_argument, 0, "exec the specified perl file before fork()", upsgi_opt_add_string_list, &uperl.exec, 0},
        {"perl-exec-post-fork", required_argument, 0, "exec the specified perl file after fork()", upsgi_opt_add_string_list, &uperl.exec_post_fork, 0},
        {"perl-auto-reload", required_argument, 0, "enable perl auto-reloader with the specified frequency", upsgi_opt_set_int, &uperl.auto_reload, UPSGI_OPT_MASTER},
        {"perl-auto-reload-ignore", required_argument, 0, "ignore the specified files when auto-reload is enabled", upsgi_opt_add_string_list, &uperl.auto_reload_ignore, UPSGI_OPT_MASTER},

	{"plshell", optional_argument, 0, "run a perl interactive shell", upsgi_opt_plshell, NULL, 0},
        {"plshell-oneshot", no_argument, 0, "run a perl interactive shell (one shot)", upsgi_opt_plshell, NULL, 0},

        {"perl-no-plack", no_argument, 0, "force the use of do instead of Plack::Util::load_psgi", upsgi_opt_true, &uperl.no_plack, 0},
        {"early-perl", required_argument, 0, "initialize an early perl interpreter shared by all loaders", upsgi_opt_early_perl, NULL, UPSGI_OPT_IMMEDIATE},
        {"early-psgi", required_argument, 0, "load a psgi app soon after upsgi initialization", upsgi_opt_early_psgi, NULL, UPSGI_OPT_IMMEDIATE},
        {"early-perl-exec", required_argument, 0, "load a perl script soon after upsgi initialization", upsgi_opt_early_exec, NULL, UPSGI_OPT_IMMEDIATE},
        {0, 0, 0, 0, 0, 0, 0},

};

int upsgi_perl_check_mtime(time_t now, HV *list, SV *key) {
	// insert item with the current time
	if (!hv_exists_ent(list, key, 0)) {
		// useless if...
		if (hv_store_ent(list, key, newSViv(now), 0)) return 0;
	}
	else {
		// compare mtime
		struct stat st;
		if (stat(SvPV_nolen(key), &st)) return 0;
		HE *mtime = hv_fetch_ent(list, key, 0, 0);
		if (!mtime) return 0;
		if (st.st_mtime > SvIV(HeVAL(mtime))) {
			upsgi_log_verbose("[perl-auto-reloader] %s has been modified !!!\n", SvPV_nolen(key));
			kill(upsgi.workers[0].pid, SIGHUP);
			return 1;
		}
	}

	return 0;
}

void upsgi_perl_check_auto_reload() {
	time_t now = upsgi_now();
	HE *he;
	if (!uperl.auto_reload_hash) {
		uperl.auto_reload_hash = newHV();
		// useless return value
		if (!SvREFCNT_inc(uperl.auto_reload_hash)) return;
	}
	GV *gv_inc = gv_fetchpv("INC", TRUE, SVt_PV);
	if (!gv_inc) return;
	HV *inc = GvHV(gv_inc);
	hv_iterinit(inc);
	while((he = hv_iternext(inc))) {
		SV *filename = hv_iterval(inc, he);
		struct upsgi_string_list *usl;
		int found = 0;
		upsgi_foreach(usl, uperl.auto_reload_ignore) {
			if (!strcmp(usl->value, SvPV_nolen(filename))) {
				found = 1; break;
			}
		}	
		if (found) continue;
		if (upsgi_perl_check_mtime(now, uperl.auto_reload_hash, filename)) return;
	}
}

SV *upsgi_perl_obj_new(char *class, size_t class_len) {

	SV *newobj;

	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVpv( class, class_len)));
	PUTBACK;

	call_method( "new", G_SCALAR);

	SPAGAIN;

	newobj = SvREFCNT_inc(POPs);
	PUTBACK;
	FREETMPS;
	LEAVE;

	return newobj;
	
}

static SV *upsgi_perl_new_blessed_object(HV *stash) {
	return sv_bless(newRV_noinc(newSV(0)), stash);
}

SV *upsgi_perl_obj_new_from_fd(char *class, size_t class_len, int fd) {
	SV *newobj;

        dSP;

        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVpv( class, class_len)));
        XPUSHs(sv_2mortal(newSViv( fd )));
        XPUSHs(sv_2mortal(newSVpv( "w", 1 )));
        PUTBACK;

        call_method( "new_from_fd", G_SCALAR);

        SPAGAIN;

        newobj = SvREFCNT_inc(POPs);
        PUTBACK;
        FREETMPS;
        LEAVE;

        return newobj;
}

SV *upsgi_perl_call_stream(SV *func) {

	SV *ret = NULL;
	struct wsgi_request *wsgi_req = current_wsgi_req();
	struct upsgi_app *wi = &upsgi_apps[wsgi_req->app_id];

        dSP;
        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
	if (upsgi.threads > 1) {
        	XPUSHs( sv_2mortal(newRV((SV*) ((SV **)wi->responder0)[wsgi_req->async_id])));
	}
	else {
        	XPUSHs( sv_2mortal(newRV((SV*) ((SV **)wi->responder0)[0])));
	}
        PUTBACK;

	call_sv( func, G_SCALAR | G_EVAL);

	SPAGAIN;
        if(SvTRUE(ERRSV)) {
                upsgi_log("[upsgi-perl error] %s", SvPV_nolen(ERRSV));
        }
        else {
                ret = SvREFCNT_inc(POPs);
        }

        PUTBACK;
        FREETMPS;
        LEAVE;

        return ret;
}

int upsgi_perl_obj_can(SV *obj, char *method, size_t len) {

	int ret;

        dSP;

        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
        XPUSHs(obj);
        XPUSHs(sv_2mortal(newSVpv(method, len)));
        PUTBACK;

        call_method( "can", G_SCALAR|G_EVAL);

        SPAGAIN;
	if(SvTRUE(ERRSV)) {
		upsgi_log("%s", SvPV_nolen(ERRSV));
	}

        ret = SvROK(POPs);
        PUTBACK;
        FREETMPS;
        LEAVE;

        return ret;

}

SV *upsgi_perl_obj_call(SV *obj, char *method) {

        SV *ret = NULL;

        dSP;

        ENTER;
        SAVETMPS;
        PUSHMARK(SP);

        XPUSHs(obj);

        PUTBACK;

        call_method( method, G_SCALAR | G_EVAL);

        SPAGAIN;
	if(SvTRUE(ERRSV)) {
        	upsgi_log("%s", SvPV_nolen(ERRSV));
        }
	else {
        	ret = SvREFCNT_inc(POPs);
	}

        PUTBACK;
        FREETMPS;
        LEAVE;

        return ret;

}


AV *psgi_call(struct wsgi_request *wsgi_req, SV *psgi_func, SV *env) {

	AV *ret = NULL;

        dSP;

        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
        XPUSHs(env);
	PUTBACK;

	call_sv(psgi_func, G_SCALAR | G_EVAL);

	SPAGAIN;

        if(SvTRUE(ERRSV)) {
                upsgi_500(wsgi_req);
                upsgi_log("[upsgi-perl error] %s", SvPV_nolen(ERRSV));
        }
	else {
		SV *r = POPs;
		if (SvROK(r)) {
			ret = (AV *) SvREFCNT_inc(SvRV(r));
		}
	}

	PUTBACK;
        FREETMPS;
        LEAVE;

        return (AV *)ret;
	
}

SV *build_psgi_env(struct wsgi_request *wsgi_req) {
	int i;
	int slot = 0;
	struct upsgi_app *wi = &upsgi_apps[wsgi_req->app_id];
	HV *env = newHV();
	HV *input_stash = NULL;
	HV *error_stash = NULL;
	SV *bool_yes = &PL_sv_yes;
	SV *bool_no = &PL_sv_no;
	if (upsgi.threads > 1) slot = wsgi_req->async_id;
	input_stash = ((HV **) wi->input)[slot];
	error_stash = ((HV **) wi->error)[slot];
	hv_ksplit(env, (U32) (wsgi_req->var_cnt + 16));
	for(i=0;i<wsgi_req->var_cnt;i++) {
		char *key = wsgi_req->hvec[i].iov_base;
		STRLEN key_len = wsgi_req->hvec[i].iov_len;
		char *value = wsgi_req->hvec[i+1].iov_base;
		STRLEN value_len = wsgi_req->hvec[i+1].iov_len;
		if ((key_len == 19 && !memcmp(key, "HTTP_CONTENT_LENGTH", 19)) || (key_len == 17 && !memcmp(key, "HTTP_CONTENT_TYPE", 17))) {
			i++;
			continue;
		}
		if (value_len > 0) {
			if (!hv_store(env, key, key_len, newSVpvn(value, value_len), 0)) goto clear;
		}
		else {
			if (!hv_store(env, key, key_len, newSVpvs(""), 0)) goto clear;
		}
		i++;
	}
        AV *av = newAV();
        av_store( av, 0, newSViv(1));
        av_store( av, 1, newSViv(1));
        if (!hv_store(env, "psgi.version", 12, newRV_noinc((SV *)av ), 0)) goto clear;
        if (!hv_store(env, "psgi.multiprocess", 17, upsgi.numproc > 1    ? bool_yes : bool_no, 0)) goto clear;
        if (!hv_store(env, "psgi.multithread",  16, upsgi.threads > 1    ? bool_yes : bool_no, 0)) goto clear;
        if (!hv_store(env, "psgi.nonblocking",  16, upsgi.async   > 0    ? bool_yes : bool_no, 0)) goto clear;
        if (!hv_store(env, "psgix.harakiri",    14, upsgi.master_process ? bool_yes : bool_no, 0)) goto clear;
        if (!hv_store(env, "psgi.run_once",  13, bool_no,  0)) goto clear;
        if (!hv_store(env, "psgi.streaming", 14, bool_yes, 0)) goto clear;
        if (!hv_store(env, "psgix.cleanup",  13, bool_yes, 0)) goto clear;
	SV *us;
        if (wsgi_req->scheme_len > 0) us = newSVpvn(wsgi_req->scheme, wsgi_req->scheme_len);
        else if (wsgi_req->https_len > 0) {
                if (!strncasecmp(wsgi_req->https, "on", 2) || wsgi_req->https[0] == '1') us = newSVpvs("https");
                else us = newSVpvs("http");
        }
        else us = newSVpvs("http");
        if (!hv_store(env, "psgi.url_scheme", 15, us, 0)) goto clear;
	SV *pi = input_stash ? upsgi_perl_new_blessed_object(input_stash) : upsgi_perl_obj_new("upsgi::input", 12);
        if (!hv_store(env, "psgi.input", 10, pi, 0)) goto clear;
	if (!hv_store(env, "psgix.input.buffered", 20, upsgi.post_buffering > 0 ? bool_yes : bool_no, 0)) goto clear;
	if (!hv_store(env, "psgix.logger", 12, newRV((SV *) ((SV **) wi->responder1)[slot]), 0)) goto clear;
	if (wsgi_req->protocol_len == 8 && !upsgi_strncmp("HTTP/1.1", 8, wsgi_req->protocol, wsgi_req->protocol_len)) {
		if (!hv_store(env, "psgix.informational", 19, newRV((SV *) ((SV **) wi->responder2)[slot]), 0)) goto clear;
	}
	av = newAV();
	if (!hv_store(env, "psgix.cleanup.handlers", 22, newRV_noinc((SV *)av ), 0)) goto clear;
	if (uperl.enable_psgix_io) {
		SV *io = upsgi_perl_obj_new_from_fd("IO::Socket", 10, wsgi_req->fd);
		if (!hv_store(env, "psgix.io", 8, io, 0)) goto clear;
	}
	SV *pe = error_stash ? upsgi_perl_new_blessed_object(error_stash) : upsgi_perl_obj_new("upsgi::error", 12);
        if (!hv_store(env, "psgi.errors", 11, pe, 0)) goto clear;
	return newRV_noinc((SV *)env);
clear:
	SvREFCNT_dec((SV *)env);
	return NULL;
}

int upsgi_perl_init(){

	int argc;
	int i;

	if (uperl.main) {
		goto already_initialized;
	}

	uperl.embedding[0] = "";
	uperl.embedding[1] = "-e";
	uperl.embedding[2] = "0";

#ifndef USE_ITHREADS
	if (upsgi.threads > 1) {
		upsgi_log("your Perl environment does not support threads\n");
		exit(1);
	} 
#endif

	if (setenv("PLACK_ENV", "upsgi", 0)) {
		upsgi_error("setenv()");
	}

	if (setenv("PLACK_SERVER", "upsgi", 0)) {
		upsgi_error("setenv()");
	}

	argc = 3;

	PERL_SYS_INIT3(&argc, (char ***) &uperl.embedding, &environ);

	uperl.main = upsgi_calloc(sizeof(PerlInterpreter *) * upsgi.threads);

	uperl.main[0] = upsgi_perl_new_interpreter();
	if (!uperl.main[0]) {
		return -1;
	}

	for(i=1;i<upsgi.threads;i++) {
		uperl.main[i] = upsgi_perl_new_interpreter();
                if (!uperl.main[i]) {
                	upsgi_log("unable to create new perl interpreter for thread %d\n", i+1);
                        exit(1);
                }
	}

	PERL_SET_CONTEXT(uperl.main[0]);

already_initialized:
#ifdef PERL_VERSION_STRING
	upsgi_log_initial("initialized Perl %s main interpreter at %p\n", PERL_VERSION_STRING, uperl.main[0]);
#else
	upsgi_log_initial("initialized Perl main interpreter at %p\n", uperl.main[0]);
#endif

	return 1;

}

/*
 * PSGI request dispatch layer:
 * - parse request vars
 * - resolve or lazily load the target PSGI app
 * - build the PSGI env
 * - call into Perl
 * - hand the result to the response layer
 */
int upsgi_perl_request(struct wsgi_request *wsgi_req) {

	if (wsgi_req->async_status == UPSGI_AGAIN) {
		return psgi_response(wsgi_req, wsgi_req->async_placeholder);	
	}

	/* Standard PSGI request */
	if (!wsgi_req->len) {
		upsgi_log("Empty PSGI request. skip.\n");
		return -1;
	}

	if (upsgi_parse_vars(wsgi_req)) {
		return -1;
	}

	if (wsgi_req->dynamic) {
		if (upsgi.threads > 1) {
                	pthread_mutex_lock(&uperl.lock_loader);
                }
	}

	wsgi_req->app_id = upsgi_get_app_id(wsgi_req, wsgi_req->appid, wsgi_req->appid_len, psgi_plugin.modifier1);
	// if it is -1, try to load a dynamic app
	if (wsgi_req->app_id == -1) {
		if (wsgi_req->dynamic) {
			if (wsgi_req->script_len > 0) {
				wsgi_req->app_id = init_psgi_app(wsgi_req, wsgi_req->script, wsgi_req->script_len, NULL);	
			}
			else if (wsgi_req->file_len > 0) {
				wsgi_req->app_id = init_psgi_app(wsgi_req, wsgi_req->file, wsgi_req->file_len, NULL);	
			}
		}

			if (wsgi_req->app_id == -1 && !upsgi.no_default_app && upsgi.default_app > -1) {
				if (upsgi_apps[upsgi.default_app].modifier1 == psgi_plugin.modifier1) {
					wsgi_req->app_id = upsgi.default_app;
				}
			}

	}
	
	if (wsgi_req->dynamic) {
                if (upsgi.threads > 1) {
                        pthread_mutex_unlock(&uperl.lock_loader);
                }
        }

		if (wsgi_req->app_id == -1) {
			upsgi_500(wsgi_req);	
			upsgi_log("--- unable to find perl application ---\n");
			// nothing to clear/free
			return UPSGI_OK;
		}

	struct upsgi_app *wi = &upsgi_apps[wsgi_req->app_id];
	wi->requests++;


	if (upsgi.threads < 2) {
		if (((PerlInterpreter **)wi->interpreter)[0] != uperl.main[0]) {
			PERL_SET_CONTEXT(((PerlInterpreter **)wi->interpreter)[0]);
		}
	}
	else {
		if (((PerlInterpreter **)wi->interpreter)[wsgi_req->async_id] != uperl.main[wsgi_req->async_id]) {
			PERL_SET_CONTEXT(((PerlInterpreter **)wi->interpreter)[wsgi_req->async_id]);
		}
	}

	ENTER;
	SAVETMPS;

	wsgi_req->async_environ = build_psgi_env(wsgi_req);
	if (!wsgi_req->async_environ) goto clear;


	if (upsgi.threads > 1) {
		wsgi_req->async_result = psgi_call(wsgi_req, ((SV **)wi->callable)[wsgi_req->async_id], wsgi_req->async_environ);
	}
	else {
		wsgi_req->async_result = psgi_call(wsgi_req, ((SV **)wi->callable)[0], wsgi_req->async_environ);
	}
	if (!wsgi_req->async_result) goto clear;

	if (SvTYPE((AV *)wsgi_req->async_result) == SVt_PVCV) {
		SV *stream_result = upsgi_perl_call_stream((SV*)wsgi_req->async_result);		
		if (!stream_result) {
			upsgi_500(wsgi_req);
		}
		else {
			SvREFCNT_dec(stream_result);
		}
		goto clear2;
	}

	while (psgi_response(wsgi_req, wsgi_req->async_result) != UPSGI_OK) {
		if (upsgi.async > 0) {
			FREETMPS;
			LEAVE;
			return UPSGI_AGAIN;
		}
	}

clear2:
	// clear response
	SvREFCNT_dec(wsgi_req->async_result);
clear:

	FREETMPS;
	LEAVE;

	// restore main interpreter if needed
	if (upsgi.threads > 1) {
		if (((PerlInterpreter **)wi->interpreter)[wsgi_req->async_id] != uperl.main[wsgi_req->async_id]) {
			PERL_SET_CONTEXT(uperl.main[wsgi_req->async_id]);
		}
	}
	else {
		if (((PerlInterpreter **)wi->interpreter)[0] != uperl.main[0]) {
			PERL_SET_CONTEXT(uperl.main[0]);
		}
	}

	return UPSGI_OK;
}

static void psgi_call_cleanup_hook(SV *hook, SV *env) {
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(env);
	PUTBACK;
	call_sv(hook, G_DISCARD);
	if(SvTRUE(ERRSV)) {
                upsgi_log("[upsgi-perl error] %s", SvPV_nolen(ERRSV));
        }
	FREETMPS;
	LEAVE;
}

void upsgi_perl_after_request(struct wsgi_request *wsgi_req) {

	log_request(wsgi_req);

	// We may be called after an early exit in XS_coroae_accept_request, 
	// before the environ is set up.
	if (!wsgi_req->async_environ) return;

	// we need to restore the context in case of multiple interpreters
	struct upsgi_app *wi = &upsgi_apps[wsgi_req->app_id];
	if (upsgi.threads < 2) {
                if (((PerlInterpreter **)wi->interpreter)[0] != uperl.main[0]) {
                        PERL_SET_CONTEXT(((PerlInterpreter **)wi->interpreter)[0]);
                }
        }
        else {
                if (((PerlInterpreter **)wi->interpreter)[wsgi_req->async_id] != uperl.main[wsgi_req->async_id]) {
                        PERL_SET_CONTEXT(((PerlInterpreter **)wi->interpreter)[wsgi_req->async_id]);
                }
        }

	// dereference %env
	SV *env = SvRV((SV *) wsgi_req->async_environ);

	// check for cleanup handlers
	if (hv_exists((HV *)env, "psgix.cleanup.handlers", 22)) {
		SV **cleanup_handlers = hv_fetch((HV *)env, "psgix.cleanup.handlers", 22, 0);
		if (SvROK(*cleanup_handlers)) {
			if (SvTYPE(SvRV(*cleanup_handlers)) == SVt_PVAV) {
				I32 n = av_len((AV *)SvRV(*cleanup_handlers));
				I32 i;
				for(i=0;i<=n;i++) {
					SV **hook = av_fetch((AV *)SvRV(*cleanup_handlers), i, 0);
					psgi_call_cleanup_hook(*hook, (SV *) wsgi_req->async_environ);
				}
			}
		}
	}

	// check for psgix.harakiri
	if (hv_exists((HV *)env, "psgix.harakiri.commit", 21)) {
		SV **harakiri = hv_fetch((HV *)env, "psgix.harakiri.commit", 21, 0);
		if (SvTRUE(*harakiri)) wsgi_req->async_plagued = 1;
	}

	// Free the $env hash
	SvREFCNT_dec(wsgi_req->async_environ);

	// async plagued could be defined in other areas...
	if (wsgi_req->async_plagued) {
		upsgi_log("*** psgix.harakiri.commit requested ***\n");
		// Before we call exit(0) we'll run the
		// upsgi_perl_atexit() hook which'll properly tear
		// down the interpreter.

		// mark the request as ended (otherwise the atexit hook will be skipped)
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].in_request = 0;
		goodbye_cruel_world("async plagued");
	}

	// now we can check for changed files
        if (uperl.auto_reload) {
                time_t now = upsgi_now();
                if (now - uperl.last_auto_reload > uperl.auto_reload) {
                        upsgi_perl_check_auto_reload();
                }
        }

	// restore main interpreter if needed
        if (upsgi.threads > 1) {
                if (((PerlInterpreter **)wi->interpreter)[wsgi_req->async_id] != uperl.main[wsgi_req->async_id]) {
                        PERL_SET_CONTEXT(uperl.main[wsgi_req->async_id]);
                }
        }
        else {
                if (((PerlInterpreter **)wi->interpreter)[0] != uperl.main[0]) {
                        PERL_SET_CONTEXT(uperl.main[0]);
                }
        }


}

int upsgi_perl_magic(char *mountpoint, char *lazy) {

        if (!strcmp(lazy+strlen(lazy)-5, ".psgi")) {
                uperl.psgi = lazy;
                return 1;
        }
        else if (!strcmp(lazy+strlen(lazy)-3, ".pl")) {
                uperl.psgi = lazy;
                return 1;
        }

        return 0;

}

// taken from Torsten Foertsch AfterFork.xs
void upsgi_perl_post_fork() {

	GV *tmpgv = gv_fetchpv("$", TRUE, SVt_PV);
	if (tmpgv) {
		SvREADONLY_off(GvSV(tmpgv));
		sv_setiv(GvSV(tmpgv), (IV)getpid());
		SvREADONLY_on(GvSV(tmpgv));
	}

	struct upsgi_string_list *usl;
	upsgi_foreach(usl, uperl.exec_post_fork) {
		SV *dollar_zero = get_sv("0", GV_ADD);
                sv_setsv(dollar_zero, newSVpv(usl->value, usl->len));
		upsgi_perl_exec(usl->value);
	}

	if (uperl.postfork) {
		upsgi_perl_run_hook(uperl.postfork);
	}
}

int upsgi_perl_mount_app(char *mountpoint, char *app) {

	if (upsgi_endswith(app, ".pl") || upsgi_endswith(app, ".psgi")) {
        	upsgi.wsgi_req->appid = mountpoint;
        	upsgi.wsgi_req->appid_len = strlen(mountpoint);

        	return init_psgi_app(upsgi.wsgi_req, app, strlen(app), NULL);
	}
	return -1;

}

void upsgi_perl_init_thread(int core_id) {

#ifdef USE_ITHREADS
        PERL_SET_CONTEXT(uperl.main[core_id]);
#endif
}

void upsgi_perl_pthread_prepare(void) {
        pthread_mutex_lock(&uperl.lock_loader);
}

void upsgi_perl_pthread_parent(void) {
        pthread_mutex_unlock(&uperl.lock_loader);
}

void upsgi_perl_pthread_child(void) {
        pthread_mutex_init(&uperl.lock_loader, NULL);
}


void upsgi_perl_enable_threads(void) {
#ifdef USE_ITHREADS
	pthread_mutex_init(&uperl.lock_loader, NULL);
	pthread_atfork(upsgi_perl_pthread_prepare, upsgi_perl_pthread_parent, upsgi_perl_pthread_child);
#endif
}

static int upsgi_perl_signal_handler(uint8_t sig, void *handler) {

	int ret = 0;

	dSP;
        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
        XPUSHs( sv_2mortal(newSViv(sig)));
        PUTBACK;

        call_sv( SvRV((SV*)handler), G_DISCARD);

        SPAGAIN;
	if(SvTRUE(ERRSV)) {
                upsgi_log("[upsgi-perl error] %s", SvPV_nolen(ERRSV));
		ret = -1;
        }

        PUTBACK;
        FREETMPS;
        LEAVE;

	return ret;
}

void upsgi_perl_run_hook(SV *hook) {
	dSP;
        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
        PUTBACK;

        call_sv( SvRV(hook), G_DISCARD);

        SPAGAIN;
        if(SvTRUE(ERRSV)) {
                upsgi_log("[upsgi-perl error] %s", SvPV_nolen(ERRSV));
		return;
        }

        PUTBACK;
        FREETMPS;
        LEAVE;
}

static void upsgi_perl_atexit() {
	int i;

	if (upsgi.mywid == 0) goto realstuff;

        // if hijacked do not run atexit hooks -- TODO: explain why
        // not.
        if (upsgi.workers[upsgi.mywid].hijacked)
                goto destroyperl;

	// if busy do not run atexit hooks (as this part could be called in a signal handler
	// while a subroutine is running)
        if (upsgi_worker_is_busy(upsgi.mywid))
                return;

realstuff:

	if (uperl.atexit) {
		upsgi_perl_run_hook(uperl.atexit);
	}

	// For the reasons explained in
	// https://github.com/unbit/upsgi/issues/1384, tearing down
	// the interpreter can be very expensive.
	if (upsgi.skip_atexit_teardown)
		return;

destroyperl:

        // We must free our perl context(s) so any DESTROY hooks
        // etc. will run.
        for(i=0;i<upsgi.threads;i++) {
            PERL_SET_CONTEXT(uperl.main[i]);

            // Destroy the PerlInterpreter, see "perldoc perlembed"
            perl_destruct(uperl.main[i]);
            perl_free(uperl.main[i]);
        }
        PERL_SYS_TERM();
        free(uperl.main);
}

static uint64_t upsgi_perl_rpc(void *func, uint8_t argc, char **argv, uint16_t argvs[], char **buffer) {

	int i;
	uint64_t ret = 0;

        dSP;
        ENTER;
        SAVETMPS;
        PUSHMARK(SP);
	for(i=0;i<argc;i++) {
        	XPUSHs( sv_2mortal(newSVpv(argv[i], argvs[i])));
	}
        PUTBACK;

        call_sv( SvRV((SV*)func), G_SCALAR | G_EVAL);

        SPAGAIN;
        if(SvTRUE(ERRSV)) {
                upsgi_log("[upsgi-perl error] %s", SvPV_nolen(ERRSV));
        }
	else {
		STRLEN rlen;
		SV *response = POPs;
                char *value = SvPV(response, rlen );
		if (rlen > 0) {
			*buffer = upsgi_malloc(rlen);
			memcpy(*buffer, value, rlen);
			ret = rlen;
		}	
	}

        PUTBACK;
        FREETMPS;
        LEAVE;

        return ret;
}

static void upsgi_perl_hijack(void) {
        if (uperl.shell_oneshot && upsgi.workers[upsgi.mywid].hijacked_count > 0) {
                upsgi.workers[upsgi.mywid].hijacked = 0;
                return;
        }
        if (uperl.shell && upsgi.mywid == 1) {
                upsgi.workers[upsgi.mywid].hijacked = 1;
                upsgi.workers[upsgi.mywid].hijacked_count++;
                // re-map stdin to stdout and stderr if we are logging to a file
                if (upsgi.logfile) {
                        if (dup2(0, 1) < 0) {
                                upsgi_error("dup2()");
                        }
                        if (dup2(0, 2) < 0) {
                                upsgi_error("dup2()");
                        }
                }

                if (uperl.shell[0] != 0) {
			perl_eval_pv(uperl.shell, 0);
                }
                else {
			perl_eval_pv("use Devel::REPL;my $repl = Devel::REPL->new;$repl->run;", 0);
                }
                if (uperl.shell_oneshot) {
                        exit(UPSGI_DE_HIJACKED_CODE);
                }
                exit(0);
        }

}

static void upsgi_perl_add_item(char *key, uint16_t keylen, char *val, uint16_t vallen, void *data) {

        HV *spool_dict = (HV*) data;

	(void)hv_store(spool_dict, key, keylen, newSVpv(val, vallen), 0);
}


static int upsgi_perl_spooler(char *filename, char *buf, uint16_t len, char *body, size_t body_len) {

        int ret = -1;

	if (!uperl.spooler) return 0;

	dSP;
        ENTER;
        SAVETMPS;
        PUSHMARK(SP);

	HV *spool_dict = newHV();	

	if (upsgi_hooked_parse(buf, len, upsgi_perl_add_item, (void *) spool_dict)) {
                return 0;
        }

        (void) hv_store(spool_dict, "spooler_task_name", 18, newSVpv(filename, 0), 0);

        if (body && body_len > 0) {
                (void) hv_store(spool_dict, "body", 4, newSVpv(body, body_len), 0);
        }

        XPUSHs( sv_2mortal((SV*)newRV_noinc((SV*)spool_dict)) );
        PUTBACK;

        call_sv( SvRV((SV*)uperl.spooler), G_SCALAR|G_EVAL);

        SPAGAIN;
        if(SvTRUE(ERRSV)) {
                upsgi_log("[upsgi-spooler-perl error] %s", SvPV_nolen(ERRSV));
		ret = -1;
        }
	else {
		ret = POPi;
	}

        PUTBACK;
        FREETMPS;
        LEAVE;

	return ret;
}

static int upsgi_perl_hook_perl(char *arg) {
	SV *ret = perl_eval_pv(arg, 0);
	if (!ret) return -1;
	return 0;
}

static void upsgi_perl_register_features() {
	upsgi_register_hook("perl", upsgi_perl_hook_perl);
}

/*
 * Plugin callback map for the PSGI subtree:
 * - option parsing and compatibility handling live in this file
 * - preinit/init app loading callbacks flow into psgi_loader.c
 * - request/after_request stay here as the dispatch boundary
 */
struct upsgi_plugin psgi_plugin = {

	.name = "psgi",
	.modifier1 = 5,
	.init = upsgi_perl_init,
	.options = upsgi_perl_options,

	.preinit_apps = upsgi_psgi_preinit_apps,
	.init_apps = upsgi_psgi_app,
	.mount_app = upsgi_perl_mount_app,

	.init_thread = upsgi_perl_init_thread,
	.signal_handler = upsgi_perl_signal_handler,
	.rpc = upsgi_perl_rpc,

	.mule = upsgi_perl_mule,

	.hijack_worker = upsgi_perl_hijack,

	.post_fork = upsgi_perl_post_fork,
	.request = upsgi_perl_request,
	.after_request = upsgi_perl_after_request,
	.enable_threads = upsgi_perl_enable_threads,

	.atexit = upsgi_perl_atexit,

	.magic = upsgi_perl_magic,

	.spooler = upsgi_perl_spooler,
	.on_load = upsgi_perl_register_features,
};
