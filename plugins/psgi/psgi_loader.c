#include "psgi.h" 

extern struct uwsgi_server uwsgi;

extern struct uwsgi_plugin psgi_plugin;

/*
 * upsgi PSGI loader boundary:
 * - XS bootstrap for uwsgi::* Perl helpers
 * - Perl interpreter setup and PSGI app loading
 * - request-to-Perl bridge helpers
 *
 * Request dispatch still lives in psgi_plugin.c.
 * Response marshalling still lives in psgi_response.c.
 */

static int uwsgi_perl_sv_is_hashref(SV *sv) {
	return sv && SvOK(sv) && SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVHV;
}

static int uwsgi_perl_sv_is_arrayref(SV *sv) {
	return sv && SvOK(sv) && SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVAV;
}

static int uwsgi_perl_psgix_logger_level_allowed(const char *level, STRLEN level_len) {
	if (level_len == 5 && !memcmp(level, "debug", 5)) return 1;
	if (level_len == 4 && !memcmp(level, "info", 4)) return 1;
	if (level_len == 4 && !memcmp(level, "warn", 4)) return 1;
	if (level_len == 5 && !memcmp(level, "error", 5)) return 1;
	if (level_len == 5 && !memcmp(level, "fatal", 5)) return 1;
	return 0;
}

XS(XS_input_seek) {

        dXSARGS;
	struct wsgi_request *wsgi_req = current_wsgi_req();
	IV pos_iv = 0;
	IV whence_iv = 0;
	int seek_rc = -1;

        psgi_check_args(3);
	pos_iv = SvIV(ST(1));
	whence_iv = SvIV(ST(2));
	seek_rc = uwsgi_request_body_seek_do(wsgi_req, (off_t) pos_iv, (int) whence_iv);

	if (seek_rc == 0) {
		XSRETURN_YES;
	}

	XSRETURN_NO;
}

XS(XS_error) {
	dXSARGS;
	struct wsgi_request *wsgi_req = current_wsgi_req();
	struct uwsgi_app *wi = &uwsgi_apps[wsgi_req->app_id];

        psgi_check_args(0);

	if (uwsgi.threads > 1) {
        	ST(0) = sv_bless(newRV_noinc(newSV(0)), ((HV **)wi->error)[wsgi_req->async_id]);
	}
	else {
        	ST(0) = sv_bless(newRV_noinc(newSV(0)), ((HV **)wi->error)[0]);
	}
        sv_2mortal(ST(0));
        XSRETURN(1);
}

XS(XS_input) {

        dXSARGS;
	struct wsgi_request *wsgi_req = current_wsgi_req();
	struct uwsgi_app *wi = &uwsgi_apps[wsgi_req->app_id];
        psgi_check_args(0);

	if (uwsgi.threads > 1) {
        	ST(0) = sv_bless(newRV_noinc(newSV(0)), ((HV **)wi->input)[wsgi_req->async_id]);
	}
	else {
        	ST(0) = sv_bless(newRV_noinc(newSV(0)), ((HV **)wi->input)[0]);
	}
        sv_2mortal(ST(0));
        XSRETURN(1);
}

XS(XS_psgix_logger) {
	dXSARGS;
	STRLEN level_len = 0;
	char *level = NULL;
	char *message = NULL;
	psgi_check_args(1);
	if (!uwsgi_perl_sv_is_hashref(ST(0))) {
		Perl_croak(aTHX_ "psgix.logger requires a hash reference argument");
	}
	HV *hv_args = (HV *) SvRV(ST(0));
	SV **level_sv = hv_fetch(hv_args, "level", 5, 0);
	SV **message_sv = hv_fetch(hv_args, "message", 7, 0);
	if (!level_sv || !message_sv || !*level_sv || !*message_sv || !SvOK(*level_sv) || !SvOK(*message_sv)) {
		Perl_croak(aTHX_ "psgix.logger requires both level and message items");
	}
	level = SvPV(*level_sv, level_len);
	if (!uwsgi_perl_psgix_logger_level_allowed(level, level_len)) {
		Perl_croak(aTHX_ "psgix.logger level must be one of debug, info, warn, error, fatal");
	}
	message = SvPV_nolen(*message_sv);
	uwsgi_log("[uwsgi-perl %.*s] %s\n", (int) level_len, level, message);
	XSRETURN_YES;
}

XS(XS_psgix_informational) {
	dXSARGS;
	struct wsgi_request *wsgi_req = current_wsgi_req();
	IV status_iv = 0;
	int rc = -1;
	if (items < 2) {
		Perl_croak(aTHX_ "Usage: uwsgi::psgix_informational takes 2 arguments");
	}
	status_iv = SvIV(ST(0));
	if (status_iv < 100 || status_iv >= 200 || status_iv == 101) {
		Perl_croak(aTHX_ "psgix.informational status must be an informational code other than 101");
	}
	if (!uwsgi_perl_sv_is_arrayref(ST(1))) {
		Perl_croak(aTHX_ "psgix.informational requires an array reference of headers");
	}
	rc = psgi_informational_response(wsgi_req, (int) status_iv, (AV *) SvRV(ST(1)));
	if (rc == 0) {
		XSRETURN_YES;
	}
	XSRETURN_NO;
}


XS(XS_stream)
{
    dXSARGS;
    struct wsgi_request *wsgi_req = current_wsgi_req();
    struct uwsgi_app *wi = &uwsgi_apps[wsgi_req->app_id];

    psgi_check_args(1);

    if (!uwsgi_perl_sv_is_arrayref(ST(0))) {
	Perl_croak(aTHX_ "streaming responder requires an array reference");
    }

    AV *response = (AV* ) SvREFCNT_inc(SvRV(ST(0))) ;

	if (av_len(response) == 2) {
		while (psgi_response_stream_start(wsgi_req, response) != UWSGI_OK);
	}
	else if (av_len(response) == 1) {
		while (psgi_response_stream_start(wsgi_req, response) != UWSGI_OK);

		SvREFCNT_dec(response);
		if (uwsgi.threads > 1) {
                	ST(0) = sv_bless(newRV_noinc(newSV(0)), ((HV **)wi->stream)[wsgi_req->async_id]);
		}
		else {
                	ST(0) = sv_bless(newRV_noinc(newSV(0)), ((HV **)wi->stream)[0]);
		}
                sv_2mortal(ST(0));
                XSRETURN(1);
	}
	else {
		uwsgi_log("invalid PSGI response: array size %d\n", av_len(response));
	}

	SvREFCNT_dec(response);
	XSRETURN(0);

}


XS(XS_input_read) {

        dXSARGS;
        struct wsgi_request *wsgi_req = current_wsgi_req();

        psgi_check_args(3);

        SV *read_buf = ST(1);
        IV arg_len_iv = SvIV(ST(2));
        size_t arg_len = 0;
        if (arg_len_iv < 0) {
                Perl_croak(aTHX_ "psgi.input read length must be non-negative");
        }
        if ((UV) arg_len_iv > (UV) SSIZE_MAX) {
                Perl_croak(aTHX_ "psgi.input read length is too large");
        }
        arg_len = (size_t) arg_len_iv;

	IV offset_iv = 0;
	if (items > 3) {
		offset_iv = SvIV(ST(3));
		if (offset_iv == IV_MIN) {
			Perl_croak(aTHX_ "psgi.input read offset is out of range");
		}
	}


	ssize_t rlen = 0;

	char *buf = uwsgi_request_body_read(wsgi_req, arg_len, &rlen);
        if (buf) {
		if (rlen > 0 && offset_iv != 0) {
			STRLEN orig_len = 0;
			size_t read_len = (size_t) rlen;
			size_t final_len = 0;
			size_t write_offset = 0;
			size_t orig_offset = 0;
			char *pv = SvPV_force_nomg(read_buf, orig_len);

			if (offset_iv > 0) {
				write_offset = (size_t) offset_iv;
				if (read_len > SIZE_MAX - write_offset) {
					Perl_croak(aTHX_ "psgi.input read offset is too large");
				}
				final_len = write_offset + read_len;
				if (orig_len > final_len) final_len = orig_len;
			}
			else {
				size_t offset = (size_t) (-offset_iv);
				if (offset > orig_len) {
					orig_offset = offset - orig_len;
					write_offset = 0;
					final_len = offset;
				}
				else {
					write_offset = orig_len - offset;
					final_len = orig_len;
				}

				if (read_len > SIZE_MAX - write_offset) {
					Perl_croak(aTHX_ "psgi.input read offset is too large");
				}
				if (write_offset + read_len > final_len) {
					final_len = write_offset + read_len;
				}
			}


			SvGROW(read_buf, final_len + 1);
			pv = SvPVX(read_buf);

			if (orig_offset > 0) {
				memmove(pv + orig_offset, pv, orig_len);
				Zero(pv, orig_offset, char);
			}
			else if (write_offset > orig_len) {
				Zero(pv + orig_len, write_offset - orig_len, char);
			}

			memcpy(pv + write_offset, buf, read_len);
			SvCUR_set(read_buf, final_len);
			pv[final_len] = '\0';
			SvPOK_only(read_buf);
		}
		else {
			sv_setpvn(read_buf, buf, rlen);
		}
		XSRETURN_IV(rlen);
        }

        // error ?
        if (rlen < 0) {
		ST(0) = &PL_sv_undef;
		XSRETURN(1);
        }

	ST(0) = &PL_sv_undef;
	XSRETURN(1);
}


XS(XS_streaming_close) {

        dXSARGS;
        psgi_check_args(0);

        XSRETURN(0);
}

XS(XS_streaming_write) {

        dXSARGS;
        struct wsgi_request *wsgi_req = current_wsgi_req();
        STRLEN blen;
        char *body;

        psgi_check_args(2);

        body = SvPV(ST(1), blen);

	uwsgi_response_write_body_do(wsgi_req, body, blen);
	uwsgi_pl_check_write_errors {
		croak("error while streaming PSGI response");
	}

        XSRETURN(0);
}

XS(XS_error_print) {

	dXSARGS;
	int i;
        STRLEN blen;
        char *body;

        psgi_check_args(1);

	for (i = 1; i < items; i++) {
        	body = SvPV(ST(i), blen);
		uwsgi_log("%.*s", (int) blen, body);
	}

        XSRETURN_YES;
}


/* XS bridge/bootstrap registration */


static void uwsgi_perl_enable_exception_logging(void) {
	perl_eval_pv("use Devel::StackTrace; $SIG{__DIE__} = sub { print Devel::StackTrace->new()->as_string() };", 0);
	if (SvTRUE(ERRSV)) {
		uwsgi_log("%s", SvPV_nolen(ERRSV));
	}
}

EXTERN_C void xs_init (pTHX);

EXTERN_C void boot_DynaLoader (pTHX_ CV* cv);

        EXTERN_C void
xs_init(pTHX)
{
        char *file = __FILE__;
        dXSUB_SYS;
	HV *stash;

        /* DynaLoader is a special case */
        newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);

	if (!uperl.tmp_input_stash) goto nonworker;

        newXS("uwsgi::input::new", XS_input, "uwsgi::input");
        newXS("uwsgi::input::read", XS_input_read, "uwsgi::input");
        newXS("uwsgi::input::seek", XS_input_seek, "uwsgi::input");

        uperl.tmp_input_stash[uperl.tmp_current_i] = gv_stashpv("uwsgi::input", 0);

        newXS("uwsgi::error::new", XS_error, "uwsgi::error");
        newXS("uwsgi::error::print", XS_error_print, "uwsgi::print");
        uperl.tmp_error_stash[uperl.tmp_current_i] = gv_stashpv("uwsgi::error", 0);
	uperl.tmp_psgix_logger[uperl.tmp_current_i] = newXS("uwsgi::psgix_logger", XS_psgix_logger, "uwsgi");
	uperl.tmp_psgix_informational[uperl.tmp_current_i] = newXS("uwsgi::psgix_informational", XS_psgix_informational, "uwsgi");
        uperl.tmp_stream_responder[uperl.tmp_current_i] = newXS("uwsgi::stream", XS_stream, "uwsgi");

        newXS("uwsgi::streaming::write", XS_streaming_write, "uwsgi::streaming");
        newXS("uwsgi::streaming::close", XS_streaming_close, "uwsgi::streaming");

        uperl.tmp_streaming_stash[uperl.tmp_current_i] = gv_stashpv("uwsgi::streaming", 0);

nonworker:

	stash = gv_stashpv("uwsgi", 1);
	newCONSTSUB(stash, "VERSION", newSVpv(UWSGI_VERSION, 0));
	newCONSTSUB(stash, "SPOOL_OK", newSViv(-2));
	newCONSTSUB(stash, "SPOOL_RETRY", newSViv(-1));
	newCONSTSUB(stash, "SPOOL_IGNORE", newSViv(0));

	HV *_opts = newHV();

	int i;
	for (i = 0; i < uwsgi.exported_opts_cnt; i++) {
		if (hv_exists(_opts, uwsgi.exported_opts[i]->key, strlen(uwsgi.exported_opts[i]->key))) {
			SV **value = hv_fetch(_opts, uwsgi.exported_opts[i]->key, strlen(uwsgi.exported_opts[i]->key), 0);
			// last resort !!!
			if (!value) {
				uwsgi_log("[perl] WARNING !!! unable to build uwsgi::opt hash !!!\n");
				goto end;
			}
			if (SvROK(*value) && SvTYPE(SvRV(*value)) == SVt_PVAV) {
				if (uwsgi.exported_opts[i]->value == NULL) {
                                        av_push((AV *)SvRV(*value), newSViv(1));
                                }
                                else {
                                        av_push((AV *)SvRV(*value), newSVpv(uwsgi.exported_opts[i]->value, 0));
                                }
			}
			else {
				AV *_opt_a = newAV();
				av_push(_opt_a, SvREFCNT_inc(*value));
				if (uwsgi.exported_opts[i]->value == NULL) {
					av_push(_opt_a, newSViv(1));
				}
				else {
					av_push(_opt_a, newSVpv(uwsgi.exported_opts[i]->value, 0));
				}
				(void ) hv_store(_opts, uwsgi.exported_opts[i]->key, strlen(uwsgi.exported_opts[i]->key), newRV_inc((SV *) _opt_a), 0);
			}
		}
		else {
			if (uwsgi.exported_opts[i]->value == NULL) {
				(void )hv_store(_opts, uwsgi.exported_opts[i]->key, strlen(uwsgi.exported_opts[i]->key), newSViv(1), 0);
			}
			else {
				(void)hv_store(_opts, uwsgi.exported_opts[i]->key, strlen(uwsgi.exported_opts[i]->key), newSVpv(uwsgi.exported_opts[i]->value, 0), 0);
			}
		}
	}

	newCONSTSUB(stash, "opt", newRV_inc((SV *) _opts));

end:

        init_perl_embedded_module();

}

/* end of automagically generated part */

/* Perl interpreter construction */
PerlInterpreter *uwsgi_perl_new_interpreter(void) {

	PerlInterpreter *pi = perl_alloc();
        if (!pi) {
                uwsgi_log("unable to allocate perl interpreter\n");
                return NULL;
        }

	PERL_SET_CONTEXT(pi);

        PL_perl_destruct_level = 2;
        PL_origalen = 1;
        perl_construct(pi);
	// over-engineering
        PL_origalen = 1;

	return pi;
}

static void uwsgi_perl_free_stashes(void) {
        free(uperl.tmp_streaming_stash);
        free(uperl.tmp_input_stash);
        free(uperl.tmp_error_stash);
        free(uperl.tmp_stream_responder);
        free(uperl.tmp_psgix_logger);
        free(uperl.tmp_psgix_informational);
}

/* PSGI app loading and Perl bridge setup */
int init_psgi_app(struct wsgi_request *wsgi_req, char *app, uint16_t app_len, PerlInterpreter **interpreters) {

	int i;
	SV **callables;

	time_t now = uwsgi_now();

	char *app_name = uwsgi_concat2n(app, app_len, "", 0);

	if (uwsgi_file_exists(app_name)) {
		// prepare for $0 (if the file is local)
		uperl.embedding[1] = app_name;
	}

	// the first (default) app, should always be loaded in the main interpreter
	if (interpreters == NULL) {
		if (uwsgi_apps_cnt) {
			interpreters = uwsgi_calloc(sizeof(PerlInterpreter *) * uwsgi.threads);
			interpreters[0] = uwsgi_perl_new_interpreter();
			if (!interpreters[0]) {
				uwsgi_log("unable to create new perl interpreter\n");
				free(interpreters);
				goto clear2;
			}
		}
		else {
			interpreters = uperl.main;
		}		
	}

	if (!interpreters) goto clear2;

	callables = uwsgi_calloc(sizeof(SV *) * uwsgi.threads);
	if (!uperl.early_interpreter) {
		uperl.tmp_streaming_stash = uwsgi_calloc(sizeof(HV *) * uwsgi.threads);
		uperl.tmp_input_stash = uwsgi_calloc(sizeof(HV *) * uwsgi.threads);
		uperl.tmp_error_stash = uwsgi_calloc(sizeof(HV *) * uwsgi.threads);
		uperl.tmp_stream_responder = uwsgi_calloc(sizeof(CV *) * uwsgi.threads);
		uperl.tmp_psgix_logger = uwsgi_calloc(sizeof(CV *) * uwsgi.threads);
		uperl.tmp_psgix_informational = uwsgi_calloc(sizeof(CV *) * uwsgi.threads);
	}

	for(i=0;i<uwsgi.threads;i++) {

		if (i > 0 && interpreters != uperl.main) {
		
			interpreters[i] = uwsgi_perl_new_interpreter();
			if (!interpreters[i]) {
				uwsgi_log("unable to create new perl interpreter\n");
				// what to do here ? i hope no-one will use threads with dynamic apps...but clear the whole stuff...
				free(callables);
				uwsgi_perl_free_stashes();
				while(i>=0) {
					perl_destruct(interpreters[i]);	
					perl_free(interpreters[i]);
					goto clear2;
				}
			}
		}

		PERL_SET_CONTEXT(interpreters[i]);

		uperl.tmp_current_i = i;

		// We need to initialize the interpreter to execute
		// our xs_init hook, but we're *not* calling it with
		// uperl.embedding as an argument so we won't execute
		// BEGIN blocks in app_name twice.
		if (!uperl.early_interpreter) {
			char *perl_e_arg = uwsgi_concat2("#line 0 ", app_name);
			char *perl_init_arg[] = { "", "-e", perl_e_arg };
			if (perl_parse(interpreters[i], xs_init, 3, perl_init_arg, NULL)) {
				// what to do here ? i hope no-one will use threads with dynamic apps... but clear the whole stuff...
				free(callables);
                                free(perl_e_arg);
				uwsgi_perl_free_stashes();
				goto clear;
			} else {
				free(perl_e_arg);
			}
		}

		if (uperl.locallib) {
			uwsgi_log("using %s as local::lib directory\n", uperl.locallib);
			char *local_lib_use = uwsgi_concat3("use local::lib qw(", uperl.locallib, ");");
			perl_eval_pv(local_lib_use, 1);
			free(local_lib_use);
		}
		perl_eval_pv("use IO::Handle;", 1);
		perl_eval_pv("use IO::File;", 1);
		perl_eval_pv("use IO::Socket;", 1);

		if (uperl.argv_items || uperl.argv_item) {
			AV *uperl_argv = GvAV(PL_argvgv);
			if (uperl.argv_items) {
				char *argv_list = uwsgi_str(uperl.argv_items);
				char *p, *ctx = NULL;
				uwsgi_foreach_token(argv_list, " ", p, ctx) {
					av_push(uperl_argv, newSVpv(p, 0));
				}
			}
			struct uwsgi_string_list *usl = uperl.argv_item;
			while(usl) {
				av_push(uperl_argv, newSVpv(usl->value, usl->len));
				usl = usl->next;
			}
		}
		
		SV *dollar_zero = get_sv("0", GV_ADD);
		sv_setsv(dollar_zero, newSVpv(app, app_len));

		SV *has_plack = NULL;
		if (!uperl.no_plack) {
			has_plack = perl_eval_pv("use Plack::Util;", 0);
		}

		if (!has_plack || SvTRUE(ERRSV)) {
			if (!uperl.no_plack) { 
				uwsgi_log("Plack::Util is not installed, using \"do\" instead of \"load_psgi\"\n");
			}
			perl_eval_pv("use File::Spec;", 1);
			char *code = uwsgi_concat3("my $app = do File::Spec->rel2abs('", app_name, "');  if ( !$app && ( my $error = $@ || $! )) { die $error; }; $app");
			callables[i] = perl_eval_pv(code, 0);
			free(code);
		}
		else {
			char *code = uwsgi_concat3("Plack::Util::load_psgi '", app_name , "';");
			callables[i] = perl_eval_pv(code, 0);
			free(code);
		}

		if (!callables[i] || SvTYPE(callables[i]) == SVt_NULL || SvTRUE(ERRSV)) {
			if (SvTRUE(ERRSV)) {
        			uwsgi_log("%s", SvPV_nolen(ERRSV));
			}
			uwsgi_log("unable to find PSGI function entry point.\n");
			// what to do here ? i hope no-one will use threads with dynamic apps...
			free(callables);
			uwsgi_perl_free_stashes();
                	goto clear;
		}

		if (uperl.log_exceptions) {
			uwsgi_perl_enable_exception_logging();
		}

		PERL_SET_CONTEXT(interpreters[0]);
	}

	// is it an early loading ?
	if (!uwsgi.workers) {
		uperl.early_psgi_app_name = app_name;
		uperl.early_psgi_callable = callables;
		return 0;
	}

	if (uwsgi_apps_cnt >= uwsgi.max_apps) {
		uwsgi_log("ERROR: you cannot load more than %d apps in a worker\n", uwsgi.max_apps);
		goto clear;
	}

	return uwsgi_perl_add_app(wsgi_req, app_name, interpreters, callables, now);

clear:
	if (interpreters != uperl.main) {
		for(i=0;i<uwsgi.threads;i++) {
			perl_destruct(interpreters[i]);
			perl_free(interpreters[i]);
		}
		free(interpreters);
	}

	PERL_SET_CONTEXT(uperl.main[0]);
clear2:
	free(app_name);
       	return -1; 
}

/* Register a loaded PSGI app with the shared uWSGI app table. */
int uwsgi_perl_add_app(struct wsgi_request *wsgi_req, char *app_name, PerlInterpreter **interpreters, SV **callables, time_t now) {
	int id = uwsgi_apps_cnt;
        struct uwsgi_app *wi = NULL;

        if (wsgi_req) {
                // we need a copy of app_id
                wi = uwsgi_add_app(id, psgi_plugin.modifier1, uwsgi_concat2n(wsgi_req->appid, wsgi_req->appid_len, "", 0), wsgi_req->appid_len, interpreters, callables);
        }
        else {
                wi = uwsgi_add_app(id, psgi_plugin.modifier1, "", 0, interpreters, callables);
        }

        wi->started_at = now;
        wi->startup_time = uwsgi_now() - now;

        uwsgi_log("PSGI app %d (%s) loaded in %d seconds at %p (interpreter %p)\n", id, app_name, (int) wi->startup_time, callables[0], interpreters[0]);
        free(app_name);

        // copy global data to app-specific areas
        wi->stream = uperl.tmp_streaming_stash;
        wi->input = uperl.tmp_input_stash;
        wi->error = uperl.tmp_error_stash;
        wi->responder0 = uperl.tmp_stream_responder;
        wi->responder1 = uperl.tmp_psgix_logger;
        wi->responder2 = uperl.tmp_psgix_informational;

        uwsgi_emulate_cow_for_apps(id);


        // restore context if required
        if (interpreters != uperl.main) {
                PERL_SET_CONTEXT(uperl.main[0]);
        }

        uperl.loaded = 1;

        return id;
}

/* Early Perl-side hooks that run before worker app initialization. */
void uwsgi_psgi_preinit_apps() {
	if (uperl.exec) {
		PERL_SET_CONTEXT(uperl.main[0]);
                perl_parse(uperl.main[0], xs_init, 3, uperl.embedding, NULL);
		struct uwsgi_string_list *usl;
        	uwsgi_foreach(usl, uperl.exec) {
			SV *dollar_zero = get_sv("0", GV_ADD);
                	sv_setsv(dollar_zero, newSVpv(usl->value, usl->len));
                	uwsgi_perl_exec(usl->value);
        	}
	}
}

/* Main PSGI application initialization entry point for the plugin. */
void uwsgi_psgi_app() {

	if (uperl.early_psgi_callable) {
		uwsgi_perl_add_app(NULL, uperl.early_psgi_app_name, uperl.main, uperl.early_psgi_callable, uwsgi_now());	
	}

        if (uperl.psgi) {
		//load app in the main interpreter list
		init_psgi_app(NULL, uperl.psgi, strlen(uperl.psgi), uperl.main);
        }
	// create a perl environment (if needed)
	else if (!uperl.exec && uperl.shell) {
		PERL_SET_CONTEXT(uperl.main[0]);
                perl_parse(uperl.main[0], xs_init, 3, uperl.embedding, NULL);
	}


}

/* Retained Perl mule support kept in v1 with the PSGI subtree. */
int uwsgi_perl_mule(char *opt) {

        if (uwsgi_endswith(opt, ".pl")) {
                PERL_SET_CONTEXT(uperl.main[0]);
                uperl.embedding[1] = opt;
                if (perl_parse(uperl.main[0], xs_init, 3, uperl.embedding, NULL)) {
                        return 0;
                }
                perl_run(uperl.main[0]);
                return 1;
        }

        return 0;

}


/* Shared helper for executing retained Perl support scripts. */
void uwsgi_perl_exec(char *filename) {
	size_t size = 0;
        char *buf = uwsgi_open_and_read(filename, &size, 1, NULL);
        perl_eval_pv(buf, 1);
	free(buf);
}
