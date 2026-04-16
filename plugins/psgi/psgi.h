#undef __USE_GNU
#include <upsgi.h>

#ifdef __APPLE__
#define HAS_BOOL 1
#endif
#include <EXTERN.h>
#include <perl.h>
#include "XSUB.h"

enum upsgi_psgi_output_mode {
	UPSGI_PSGI_OUTPUT_NONE = 0,
	UPSGI_PSGI_OUTPUT_ASYNC_GETLINE = 1,
	UPSGI_PSGI_OUTPUT_XS_STREAMING = 2,
	UPSGI_PSGI_OUTPUT_LEGACY_OTHER = 3,
};

enum upsgi_psgi_output_owner_kind {
	UPSGI_PSGI_OWNER_NONE = 0,
	UPSGI_PSGI_OWNER_PERL_SV = 1,
	UPSGI_PSGI_OWNER_REQUEST_BUFFER = 2,
};

enum upsgi_psgi_output_resume_kind {
	UPSGI_PSGI_RESUME_NONE = 0,
	UPSGI_PSGI_RESUME_CHUNK_BOUNDARY = 1,
	UPSGI_PSGI_RESUME_OWNED_BUFFER = 2,
};

enum upsgi_psgi_step_result {
	UPSGI_PSGI_STEP_HARD_FAIL = -1,
	UPSGI_PSGI_STEP_CHUNK_DONE = 0,
	UPSGI_PSGI_STEP_CHUNK_PARTIAL = 1,
};

#define upsgi_psgi_output_active(wsgi_req) ((wsgi_req)->psgi_output_len > 0)

static inline void upsgi_psgi_output_state_reset(struct wsgi_request *wsgi_req) {
	wsgi_req->psgi_output_mode = UPSGI_PSGI_OUTPUT_NONE;
	wsgi_req->psgi_output_flags = 0;
	wsgi_req->psgi_chunk_owner_kind = UPSGI_PSGI_OWNER_NONE;
	wsgi_req->psgi_output_resume_kind = UPSGI_PSGI_RESUME_NONE;
	wsgi_req->psgi_output_owner_ref = NULL;
	wsgi_req->psgi_output_pos = 0;
	wsgi_req->psgi_output_len = 0;
	if (wsgi_req->psgi_output_buf) {
		wsgi_req->psgi_output_buf->pos = 0;
	}
}

static inline void upsgi_psgi_output_state_release(struct wsgi_request *wsgi_req) {
	if (wsgi_req->psgi_output_buf) {
		upsgi_buffer_destroy(wsgi_req->psgi_output_buf);
		wsgi_req->psgi_output_buf = NULL;
	}
	upsgi_psgi_output_state_reset(wsgi_req);
}

#define upsgi_pl_check_write_errors if (wsgi_req->write_errors > 0 && upsgi.write_errors_exception_only) {\
                        croak("error writing to client");\
                }\
                else if (wsgi_req->write_errors > upsgi.write_errors_tolerance)\


/*
 * upsgi PSGI subtree boundary:
 * - psgi_plugin.c: option parsing, compatibility shims, request dispatch
 * - psgi_response.c: PSGI response marshalling and body emission
 * - psgi_loader.c: Perl bridge helpers, XS setup, interpreter/app loading
 * - upsgi_plmodule.c: retained Perl extras and supporting helpers
 */
struct upsgi_perl {

	// path of the statically loaded main app
        char *psgi;
	// locallib path
	char *locallib;

	// perl argv for initialization
	char *embedding[3];

	// opt-in PSGI exception visibility for debugging
	int log_exceptions;
	int stacktrace_available;

	char *argv_items;
	struct upsgi_string_list *argv_item;

	// this is a pointer to the main list of interpreters (required for signals, rpc....);
        PerlInterpreter **main;

	// a lock for dynamic apps
	pthread_mutex_t lock_loader;

	// this fields must be heavy protected in threaded modes
	int tmp_current_i;
	HV **tmp_streaming_stash;
	HV **tmp_input_stash;
	HV **tmp_error_stash;

	CV **tmp_psgix_logger;
	CV **tmp_psgix_informational;
	CV **tmp_stream_responder;
	
	SV *postfork;
	SV *atexit;

	/* Cached immutable PSGI scalar values reused across request env builds. */
	SV *psgi_scheme_http;
	SV *psgi_scheme_https;

	int loaded;

	struct upsgi_string_list *exec;
	struct upsgi_string_list *exec_post_fork;

	int auto_reload;
	time_t last_auto_reload;
	struct upsgi_string_list *auto_reload_ignore;
	HV *auto_reload_hash;

	int enable_psgix_io;

	char *shell;
	int shell_oneshot;


	int no_plack;

	SV **early_psgi_callable;
	char *early_psgi_app_name;

	PerlInterpreter *early_interpreter;
};

void init_perl_embedded_module(void);

/* response layer */
int psgi_response(struct wsgi_request *, AV*);
int psgi_response_stream_start(struct wsgi_request *, AV*);
int psgi_informational_response(struct wsgi_request *, int, AV *);

/* Perl bridge and app loading layer */
void upsgi_psgi_app(void);
HV *upsgi_psgi_build_base_env(void);
HV **upsgi_psgi_build_slot_env_bases(SV **logger_refs, SV **informational_refs, int include_informational);
SV *upsgi_perl_obj_call(SV *, char *);
int upsgi_perl_obj_can(SV *, char *, size_t);
int init_psgi_app(struct wsgi_request *, char *, uint16_t, PerlInterpreter **);
PerlInterpreter *upsgi_perl_new_interpreter(void);
void upsgi_perl_run_hook(SV *);
void upsgi_perl_exec(char *);
void upsgi_perl_check_auto_reload(void);
void upsgi_psgi_preinit_apps(void);
int upsgi_perl_add_app(struct wsgi_request *, char *, PerlInterpreter **, SV **, time_t);

#define psgi_xs(func) newXS("upsgi::" #func, XS_##func, "upsgi")
#define psgi_check_args(x) if (items < x) Perl_croak(aTHX_ "Usage: upsgi::%s takes %d arguments", __FUNCTION__ + 3, x)

extern struct upsgi_perl uperl;
