#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	Exceptions management

	generally exceptions are printed in the logs, but if you enable
	an exception manager they will be stored in a (relatively big) upsgi packet
	with the following structure.

	"vars" -> keyval of request vars
	"backtrace" -> list of backtrace lines. Each line is a list of 5 elements filename,line,function,text,custom
	"unix" -> seconds since the epoch
	"class" -> the exception class
	"msg" -> a text message mapped to the extension
	"repr" -> a text message mapped to the extension in language-specific gergo
	"wid" -> worker id
	"core" -> the core generating the exception
	"pid" -> pid of the worker
	"node" -> hostname

	Other vars can be added, but you cannot be sure they will be used by exceptions handler.

	The exception-upsgi packet is passed "as is" to the exception handler

	Exceptions hooks:
	
		a request plugin can export that hooks:
	
		struct upsgi_buffer *backtrace(struct wsgi_request *);
		struct upsgi_buffer *exception_class(struct wsgi_request *);
		struct upsgi_buffer *exception_msg(struct wsgi_request *);
		struct upsgi_buffer *exception_repr(struct wsgi_request *);
		void exception_log(struct wsgi_request *);

		Remember to reset the exception status (if possible) after each call

	Exceptions catcher:

		This is a special development-mode in which exceptions are printed
		to the HTTP client.

*/

struct upsgi_buffer *upsgi_exception_handler_object(struct wsgi_request *wsgi_req) {
	struct upsgi_buffer *ub = upsgi_buffer_new(4096);
	if (upsgi_buffer_append_keyval(ub, "vars", 4, wsgi_req->buffer, wsgi_req->len)) goto error;
	if (upsgi.p[wsgi_req->uh->modifier1]->backtrace) {
                struct upsgi_buffer *bt = upsgi.p[wsgi_req->uh->modifier1]->backtrace(wsgi_req);
		if (bt) {
			if (upsgi_buffer_append_keyval(ub, "backtrace", 9, bt->buf, bt->pos)) {
				upsgi_buffer_destroy(bt);
				goto error;
			} 
			upsgi_buffer_destroy(bt);
		}
	}
	
	if (upsgi.p[wsgi_req->uh->modifier1]->exception_class) {
		struct upsgi_buffer *ec = upsgi.p[wsgi_req->uh->modifier1]->exception_class(wsgi_req);
		if (ec) {
			if (upsgi_buffer_append_keyval(ub, "class", 5, ec->buf, ec->pos)) {
				upsgi_buffer_destroy(ec);
				goto error;
			}
			upsgi_buffer_destroy(ec);
		}
	}

	if (upsgi.p[wsgi_req->uh->modifier1]->exception_msg) {
                struct upsgi_buffer *em = upsgi.p[wsgi_req->uh->modifier1]->exception_msg(wsgi_req);
                if (em) {
                        if (upsgi_buffer_append_keyval(ub, "msg", 3, em->buf, em->pos)) {
                                upsgi_buffer_destroy(em);
                                goto error;
                        }
                        upsgi_buffer_destroy(em);
                }
        }

	if (upsgi.p[wsgi_req->uh->modifier1]->exception_repr) {
                struct upsgi_buffer *er = upsgi.p[wsgi_req->uh->modifier1]->exception_repr(wsgi_req);
                if (er) {
                        if (upsgi_buffer_append_keyval(ub, "repr", 4, er->buf, er->pos)) {
                                upsgi_buffer_destroy(er);
                                goto error;
                        }
                        upsgi_buffer_destroy(er);
                }
        }

	if (upsgi_buffer_append_keynum(ub, "unix", 4, upsgi_now())) goto error;
	if (upsgi_buffer_append_keynum(ub, "wid", 3, upsgi.mywid)) goto error;
	if (upsgi_buffer_append_keynum(ub, "pid", 3, upsgi.mypid)) goto error;
	if (upsgi_buffer_append_keynum(ub, "core", 4, wsgi_req->async_id)) goto error;
	if (upsgi_buffer_append_keyval(ub, "node", 4, upsgi.hostname, upsgi.hostname_len)) goto error;

	return ub;
	
error:
	upsgi_buffer_destroy(ub);
	return NULL;
}

static void append_vars_to_ubuf(char *key, uint16_t keylen, char *val, uint16_t vallen, void *data) {
	struct upsgi_buffer *ub = (struct upsgi_buffer *) data;

	if (upsgi_buffer_append(ub, key, keylen)) return;
	if (upsgi_buffer_append(ub, " = ", 3)) return;
	if (upsgi_buffer_append(ub, val, vallen)) return;
	if (upsgi_buffer_append(ub, "\n", 1)) return;
}

static void append_backtrace_to_ubuf(uint16_t pos, char *value, uint16_t len, void *data) {
        struct upsgi_buffer *ub = (struct upsgi_buffer *) data;

	uint16_t item = 0;
	if (pos > 0) {
		item = pos % 5;
	}

	switch(item) {
		// filename
		case 0:
			if (upsgi_buffer_append(ub, "filename: \"", 11)) return;
			if (upsgi_buffer_append(ub, value, len)) return;
			if (upsgi_buffer_append(ub, "\" ", 2)) return;
			break;
		// lineno
		case 1:
			if (upsgi_buffer_append(ub, "line: ", 6)) return;
			if (upsgi_buffer_append(ub, value, len)) return;
			if (upsgi_buffer_append(ub, " ", 1)) return;
			break;
		// function
		case 2:
			if (upsgi_buffer_append(ub, "function: \"", 11)) return;
			if (upsgi_buffer_append(ub, value, len)) return;
			if (upsgi_buffer_append(ub, "\" ", 2)) return;
			break;
		// text
		case 3:
			if (len > 0) {
				if (upsgi_buffer_append(ub, "text/code: \"", 12)) return;
				if (upsgi_buffer_append(ub, value, len)) return;
				if (upsgi_buffer_append(ub, "\" ", 2)) return;
			}
			break;
		// custom
		case 4:
			if (len > 0) {
				if (upsgi_buffer_append(ub, "custom: \"", 9)) return;
                        	if (upsgi_buffer_append(ub, value, len)) return;
                        	if (upsgi_buffer_append(ub, "\" ", 2)) return;
			}
			if (upsgi_buffer_append(ub, "\n", 1)) return;
			break;
		default:
			break;
	}

}


int upsgi_exceptions_catch(struct wsgi_request *wsgi_req) {

	if (upsgi_response_prepare_headers(wsgi_req, "500 Internal Server Error", 25)) {
		return -1;
	}

	if (upsgi_response_add_content_type(wsgi_req, "text/plain", 10)) {
		return -1;
	}

	struct upsgi_buffer *ub = upsgi_buffer_new(4096);
	if (upsgi_buffer_append(ub, "upsgi exceptions catcher for \"", 30)) goto error;
	if (upsgi_buffer_append(ub, wsgi_req->method, wsgi_req->method_len)) goto error;
	if (upsgi_buffer_append(ub, " ", 1)) goto error;
	if (upsgi_buffer_append(ub, wsgi_req->uri, wsgi_req->uri_len)) goto error;
	if (upsgi_buffer_append(ub, "\" (request plugin: \"", 20)) goto error;
	if (upsgi_buffer_append(ub, (char *) upsgi.p[wsgi_req->uh->modifier1]->name, strlen(upsgi.p[wsgi_req->uh->modifier1]->name))) goto error;
	if (upsgi_buffer_append(ub, "\", modifier1: ", 14 )) goto error;
	if (upsgi_buffer_num64(ub, wsgi_req->uh->modifier1)) goto error;
	if (upsgi_buffer_append(ub, ")\n\n", 3)) goto error;

	if (upsgi_buffer_append(ub, "Exception: ", 11)) goto error;
                
        if (upsgi.p[wsgi_req->uh->modifier1]->exception_repr) {
                struct upsgi_buffer *ub_exc_repr = upsgi.p[wsgi_req->uh->modifier1]->exception_repr(wsgi_req);
                if (ub_exc_repr) {
                        if (upsgi_buffer_append(ub, ub_exc_repr->buf, ub_exc_repr->pos)) {
                                upsgi_buffer_destroy(ub_exc_repr);
                                goto error;
                        }
                        upsgi_buffer_destroy(ub_exc_repr);
                }
                else {
                        goto notavail3;
                }
        }
        else {
notavail3:
                if (upsgi_buffer_append(ub, "-Not available-", 15)) goto error;
        }

        if (upsgi_buffer_append(ub, "\n\n", 2)) goto error;

	if (upsgi_buffer_append(ub, "Exception class: ", 17)) goto error;

	if (upsgi.p[wsgi_req->uh->modifier1]->exception_class) {
		struct upsgi_buffer *ub_exc_class = upsgi.p[wsgi_req->uh->modifier1]->exception_class(wsgi_req);
		if (ub_exc_class) {
			if (upsgi_buffer_append(ub, ub_exc_class->buf, ub_exc_class->pos)) {
				upsgi_buffer_destroy(ub_exc_class);
				goto error;
			}
			upsgi_buffer_destroy(ub_exc_class);
		}
		else {
			goto notavail;
		}
	}
	else {
notavail:
		if (upsgi_buffer_append(ub, "-Not available-", 15)) goto error;
	}

	if (upsgi_buffer_append(ub, "\n\n", 2)) goto error;

	if (upsgi_buffer_append(ub, "Exception message: ", 19)) goto error;

        if (upsgi.p[wsgi_req->uh->modifier1]->exception_msg) {
                struct upsgi_buffer *ub_exc_msg = upsgi.p[wsgi_req->uh->modifier1]->exception_msg(wsgi_req);
                if (ub_exc_msg) {
                        if (upsgi_buffer_append(ub, ub_exc_msg->buf, ub_exc_msg->pos)) {
                                upsgi_buffer_destroy(ub_exc_msg);
                                goto error;
                        }
                        upsgi_buffer_destroy(ub_exc_msg);
                }
                else {
                        goto notavail2;
                }
        }
        else {
notavail2:
                if (upsgi_buffer_append(ub, "-Not available-", 15)) goto error;
        }

	if (upsgi_buffer_append(ub, "\n\n", 2)) goto error;

	if (upsgi_buffer_append(ub, "Backtrace:\n", 11)) goto error;

        if (upsgi.p[wsgi_req->uh->modifier1]->backtrace) {
                struct upsgi_buffer *ub_exc_bt = upsgi.p[wsgi_req->uh->modifier1]->backtrace(wsgi_req);
                if (ub_exc_bt) {
			struct upsgi_buffer *parsed_bt = upsgi_buffer_new(4096);
			if (upsgi_hooked_parse_array(ub_exc_bt->buf, ub_exc_bt->pos, append_backtrace_to_ubuf, parsed_bt)) {
				upsgi_buffer_destroy(ub_exc_bt);
				upsgi_buffer_destroy(parsed_bt);
                                goto error;
			}
			upsgi_buffer_destroy(ub_exc_bt);
                        if (upsgi_buffer_append(ub, parsed_bt->buf, parsed_bt->pos)) {
                                upsgi_buffer_destroy(parsed_bt);
                                goto error;
                        }
                        upsgi_buffer_destroy(parsed_bt);
                }
                else {
                        goto notavail4;
                }
        }
        else {
notavail4:
                if (upsgi_buffer_append(ub, "-Not available-", 15)) goto error;
        }

        if (upsgi_buffer_append(ub, "\n\n", 2)) goto error;

	if (upsgi_hooked_parse(wsgi_req->buffer, wsgi_req->len, append_vars_to_ubuf, ub)) {
		goto error;
	}

	if (upsgi_response_write_body_do(wsgi_req, ub->buf, ub->pos)) {
		goto error;
	}

	upsgi_buffer_destroy(ub);
	return 0;

error:
	upsgi_buffer_destroy(ub);
	return -1;

}

static void upsgi_exception_run_handlers(struct upsgi_buffer *ub) {
	struct upsgi_string_list *usl = upsgi.exception_handlers_instance;
	struct iovec iov[2];
        iov[1].iov_base = ub->buf;
        iov[1].iov_len = ub->pos;
	while(usl) {
		struct upsgi_exception_handler_instance *uehi = (struct upsgi_exception_handler_instance *) usl->custom_ptr;
        	iov[0].iov_base = &uehi;
        	iov[0].iov_len = sizeof(long);
        	// now send the message to the exception handler thread
        	if (writev(upsgi.exception_handler_thread->pipe[0], iov, 2) != (ssize_t) (ub->pos+sizeof(long))) {
                	upsgi_error("[upsgi-exception-handler-error] upsgi_exception_run_handlers()/writev()");
        	}
		usl = usl->next;
	}
}

void upsgi_manage_exception(struct wsgi_request *wsgi_req,int catch) {

	int do_exit = upsgi.reload_on_exception;

	if (!wsgi_req) goto log2;

	if (do_exit) goto check_catch;

	if (upsgi.exception_handlers_instance) {
		struct upsgi_buffer *ehi = upsgi_exception_handler_object(wsgi_req);
		if (ehi) {
			upsgi_exception_run_handlers(ehi);
			upsgi_buffer_destroy(ehi);
		}
	}

	upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].exceptions++;
	upsgi_apps[wsgi_req->app_id].exceptions++;

	if (upsgi.reload_on_exception_type && upsgi.p[wsgi_req->uh->modifier1]->exception_class) {
		struct upsgi_buffer *ub = upsgi.p[wsgi_req->uh->modifier1]->exception_msg(wsgi_req);
		if (ub) {
			struct upsgi_string_list *usl = upsgi.reload_on_exception_type;
			while (usl) {
				if (!upsgi_strncmp(usl->value, usl->len, ub->buf, ub->len)) {
					do_exit = 1;
					upsgi_buffer_destroy(ub);
					goto check_catch;
				}
				usl = usl->next;
			}
			upsgi_buffer_destroy(ub);
		}
	}

	if (upsgi.reload_on_exception_value && upsgi.p[wsgi_req->uh->modifier1]->exception_msg) {
                struct upsgi_buffer *ub = upsgi.p[wsgi_req->uh->modifier1]->exception_msg(wsgi_req);
		if (ub) {
			struct upsgi_string_list *usl = upsgi.reload_on_exception_value;
                        while (usl) {
                                if (!upsgi_strncmp(usl->value, usl->len, ub->buf, ub->len)) {
                                        do_exit = 1;
                                        upsgi_buffer_destroy(ub);
                                        goto check_catch;
                                }
                                usl = usl->next;
                        }
			upsgi_buffer_destroy(ub);
		}
        }

        if (upsgi.reload_on_exception_repr && upsgi.p[wsgi_req->uh->modifier1]->exception_repr) {
                struct upsgi_buffer *ub = upsgi.p[wsgi_req->uh->modifier1]->exception_msg(wsgi_req);
		if (ub) {
			struct upsgi_string_list *usl = upsgi.reload_on_exception_repr;
                        while (usl) {
                                if (!upsgi_strncmp(usl->value, usl->len, ub->buf, ub->len)) {
                                        do_exit = 1;
                                        upsgi_buffer_destroy(ub);
                                        goto check_catch;
                                }
                                usl = usl->next;
                        }
			upsgi_buffer_destroy(ub);
		}
        }

check_catch:
	if (catch && wsgi_req) {
		if (upsgi_exceptions_catch(wsgi_req)) {
			// for now, just goto, new features could be added
			goto log;		
		}
	}

log:
	if (upsgi.p[wsgi_req->uh->modifier1]->exception_log) {
		upsgi.p[wsgi_req->uh->modifier1]->exception_log(wsgi_req);
	}
	
log2:
	if (do_exit) {
		exit(UPSGI_EXCEPTION_CODE);		
	}
	
}

struct upsgi_exception_handler *upsgi_register_exception_handler(char *name, int (*func)(struct upsgi_exception_handler_instance *, char *, size_t)) {
	struct upsgi_exception_handler *old_ueh = NULL, *ueh = upsgi.exception_handlers;
	while(ueh) {
		if (!strcmp(name, ueh->name)) {
			return NULL;
		}
		old_ueh = ueh;
		ueh = ueh->next;
	}

	ueh = upsgi_calloc(sizeof(struct upsgi_exception_handler));
	ueh->name = name;
	ueh->func = func;

	if (old_ueh) {
		old_ueh->next = ueh;
	}
	else {
		upsgi.exception_handlers = ueh;
	}

	return ueh;
}

struct upsgi_exception_handler *upsgi_exception_handler_by_name(char *name) {
	struct upsgi_exception_handler *ueh = upsgi.exception_handlers;
	while(ueh) {
		if (!strcmp(name, ueh->name)) {
			return ueh;
		}
		ueh = ueh->next;
	}
	return NULL;
}

static void upsgi_exception_handler_thread_loop(struct upsgi_thread *ut) {
        char *buf = upsgi_malloc(upsgi.exception_handler_msg_size + sizeof(long));
        for (;;) {
                int interesting_fd = -1;
                int ret = event_queue_wait(ut->queue, -1, &interesting_fd);
                if (ret > 0) {
                        ssize_t len = read(ut->pipe[1], buf, upsgi.exception_handler_msg_size + sizeof(long));
                        if (len > (ssize_t)(sizeof(long) + 1)) {
                                size_t msg_size = len - sizeof(long);
                                char *msg = buf + sizeof(long);
                                long ptr = 0;
                                memcpy(&ptr, buf, sizeof(long));
                                struct upsgi_exception_handler_instance *uehi = (struct upsgi_exception_handler_instance *) ptr;
                                if (!uehi)
					break;
				if (uehi->handler->func(uehi, msg, msg_size)) {
                        		upsgi_log("[upsgi-exception] error running the handler \"%s\" args: \"%s\"\n", uehi->handler->name, uehi->arg ? uehi->arg : "");
                		}
                        }
                }
        }
	free(buf);
}

void upsgi_exception_setup_handlers() {

	struct upsgi_string_list *usl = upsgi.exception_handlers_instance;
	while(usl) {
		// do not free handler !!!
		char *handler = upsgi_str(usl->value);
		char *colon = strchr(handler, ':');
		if (colon) {
			*colon = 0;
		}
		struct upsgi_exception_handler *ueh = upsgi_exception_handler_by_name(handler);
		if (!ueh) {
			upsgi_log("unable to find exception handler: %s\n", handler);
			exit(1);
		}

		struct upsgi_exception_handler_instance *uehi = upsgi_calloc(sizeof(struct upsgi_exception_handler_instance));
		uehi->handler = ueh;
		if (colon) {
			uehi->arg = colon+1;
		}
		usl->custom_ptr = uehi;
		usl = usl->next;
	}
}

void upsgi_exceptions_handler_thread_start() {
	if (!upsgi.exception_handlers_instance) return;
	// start the exception_handler_thread
        upsgi.exception_handler_thread = upsgi_thread_new(upsgi_exception_handler_thread_loop);
        if (!upsgi.exception_handler_thread) {
                upsgi_log("unable to spawn exception handler thread\n");
                exit(1);
        }

}
