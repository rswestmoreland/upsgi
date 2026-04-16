#ifdef UPSGI_ROUTING
#include "upsgi.h"

extern struct upsgi_server upsgi;

struct upsgi_route_var *upsgi_register_route_var(char *name, char *(*func)(struct wsgi_request *, char *, uint16_t, uint16_t *)) {

	struct upsgi_route_var *old_urv = NULL,*urv = upsgi.route_vars;
        while(urv) {
                if (!strcmp(urv->name, name)) {
                        return urv;
                }
                old_urv = urv;
                urv = urv->next;
        }

        urv = upsgi_calloc(sizeof(struct upsgi_route_var));
        urv->name = name;
	urv->name_len = strlen(name);
        urv->func = func;

        if (old_urv) {
                old_urv->next = urv;
        }
        else {
                upsgi.route_vars = urv;
        }

        return urv;
}

struct upsgi_route_var *upsgi_get_route_var(char *name, uint16_t name_len) {
	struct upsgi_route_var *urv = upsgi.route_vars;
	while(urv) {
		if (!upsgi_strncmp(urv->name, urv->name_len, name, name_len)) {
			return urv;
		}
		urv = urv->next;
	}
	return NULL;
}

struct upsgi_buffer *upsgi_routing_translate(struct wsgi_request *wsgi_req, struct upsgi_route *ur, char *subject, uint16_t subject_len, char *data, size_t data_len) {

	char *pass1 = data;
	size_t pass1_len = data_len;

	if (ur->condition_ub[wsgi_req->async_id] && ur->ovn[wsgi_req->async_id] > 0) {
		pass1 = upsgi_regexp_apply_ovec(ur->condition_ub[wsgi_req->async_id]->buf, ur->condition_ub[wsgi_req->async_id]->pos, data, data_len, ur->ovector[wsgi_req->async_id], ur->ovn[wsgi_req->async_id]);
		pass1_len = strlen(pass1);
	}
	// cannot fail
	else if (subject) {
		pass1 = upsgi_regexp_apply_ovec(subject, subject_len, data, data_len, ur->ovector[wsgi_req->async_id], ur->ovn[wsgi_req->async_id]);
		pass1_len = strlen(pass1);
	}

	struct upsgi_buffer *ub = upsgi_buffer_new(pass1_len);
	size_t i;
	int status = 0;
	char *key = NULL;
	size_t keylen = 0;
	for(i=0;i<pass1_len;i++) {
		switch(status) {
			case 0:
				if (pass1[i] == '$') {
					status = 1;
					break;
				}
				if (upsgi_buffer_append(ub, pass1 + i, 1)) goto error;
				break;
			case 1:
				if (pass1[i] == '{') {
					status = 2;
					key = pass1+i+1;
					keylen = 0;
					break;
				}
				status = 0;
				key = NULL;
				keylen = 0;
				if (upsgi_buffer_append(ub, "$", 1)) goto error;
				if (upsgi_buffer_append(ub, pass1 + i, 1)) goto error;
				break;
			case 2:
				if (pass1[i] == '}') {
					uint16_t vallen = 0;
					int need_free = 0;
					char *value = NULL;
					char *bracket = memchr(key, '[', keylen);
					if (bracket && keylen > 0 && key[keylen-1] == ']') {
						struct upsgi_route_var *urv = upsgi_get_route_var(key, bracket - key);
						if (urv) {
							need_free = urv->need_free;
							value = urv->func(wsgi_req, bracket + 1, keylen - (urv->name_len+2), &vallen); 
						}
						else {
							value = upsgi_get_var(wsgi_req, key, keylen, &vallen);
						}
					}
					else {
						value = upsgi_get_var(wsgi_req, key, keylen, &vallen);
					}
					if (value) {
						if (upsgi_buffer_append(ub, value, vallen)) {
							if (need_free) {
								free(value);
							}
							goto error;
						}
						if (need_free) {
							free(value);
						}
					}
                                        status = 0;
					key = NULL;
					keylen = 0;
                                        break;
                                }
				keylen++;
				break;
			default:
				break;
		}
	}

	// fix the buffer
	if (status == 1) {
		if (upsgi_buffer_append(ub, "$", 1)) goto error;
	}
	else if (status == 2) {
		if (upsgi_buffer_append(ub, "${", 2)) goto error;
		if (keylen > 0) {
			if (upsgi_buffer_append(ub, key, keylen)) goto error;
		}
	}

	// add the final NULL byte (to simplify plugin work)
	if (upsgi_buffer_append(ub, "\0", 1)) goto error;
	// .. but came back of 1 position to avoid accounting it
	ub->pos--;

	if (pass1 != data) {
		free(pass1);
	}
	return ub;

error:
	upsgi_buffer_destroy(ub);
	return NULL;
}

static void upsgi_routing_reset_memory(struct wsgi_request *wsgi_req, struct upsgi_route *routes) {
	// free dynamic memory structures
	if (routes->if_func) {
                        routes->ovn[wsgi_req->async_id] = 0;
                        if (routes->ovector[wsgi_req->async_id]) {
                                free(routes->ovector[wsgi_req->async_id]);
                                routes->ovector[wsgi_req->async_id] = NULL;
                        }
                        if (routes->condition_ub[wsgi_req->async_id]) {
                                upsgi_buffer_destroy(routes->condition_ub[wsgi_req->async_id]);
                                routes->condition_ub[wsgi_req->async_id] = NULL;
                        }
	}

}

int upsgi_apply_routes_do(struct upsgi_route *routes, struct wsgi_request *wsgi_req, char *subject, uint16_t subject_len) {

	int n = -1;

	char *orig_subject = subject;
	uint16_t orig_subject_len = subject_len;

	uint32_t *r_goto = &wsgi_req->route_goto;
	uint32_t *r_pc = &wsgi_req->route_pc;

	if (routes == upsgi.error_routes) {
		r_goto = &wsgi_req->error_route_goto;
		r_pc = &wsgi_req->error_route_pc;
	}
	else if (routes == upsgi.response_routes) {
                r_goto = &wsgi_req->response_route_goto;
                r_pc = &wsgi_req->response_route_pc;
        }
	else if (routes == upsgi.final_routes) {
		r_goto = &wsgi_req->final_route_goto;
		r_pc = &wsgi_req->final_route_pc;
	}

	while (routes) {

		if (routes->label) goto next;

		if (*r_goto > 0 && *r_pc < *r_goto) {
			goto next;
		}

		*r_goto = 0;

		if (!routes->if_func) {
			// could be a "run"
			if (!routes->subject) {
				n = 0;
				goto run;
			}
			if (!subject) {
				char **subject2 = (char **) (((char *) (wsgi_req)) + routes->subject);
				uint16_t *subject_len2 = (uint16_t *) (((char *) (wsgi_req)) + routes->subject_len);
				subject = *subject2 ;
				subject_len = *subject_len2;
			}
			n = upsgi_regexp_match_ovec(routes->pattern, subject, subject_len, routes->ovector[wsgi_req->async_id], routes->ovn[wsgi_req->async_id]);
		}
		else {
			int ret = routes->if_func(wsgi_req, routes);
			// error
			if (ret < 0) {
				upsgi_routing_reset_memory(wsgi_req, routes);
				return UPSGI_ROUTE_BREAK;
			}
			// true
			if (!routes->if_negate) {
				if (ret == 0) {
					upsgi_routing_reset_memory(wsgi_req, routes);
					goto next;	
				}
				n = ret;
			}
			else {
				if (ret > 0) {
					upsgi_routing_reset_memory(wsgi_req, routes);
					goto next;	
				}
				n = 1;
			}
		}

run:
		if (n >= 0) {
			wsgi_req->is_routing = 1;
			int ret = routes->func(wsgi_req, routes);
			upsgi_routing_reset_memory(wsgi_req, routes);
			wsgi_req->is_routing = 0;
			if (ret == UPSGI_ROUTE_BREAK) {
				upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].routed_requests++;
				return ret;
			}
			if (ret == UPSGI_ROUTE_CONTINUE) {
				return ret;
			}
			
			if (ret == -1) {
				return UPSGI_ROUTE_BREAK;
			}
		}
next:
		subject = orig_subject;
		subject_len = orig_subject_len;
		routes = routes->next;
		if (routes) *r_pc = *r_pc+1;
	}

	return UPSGI_ROUTE_CONTINUE;
}

int upsgi_apply_routes(struct wsgi_request *wsgi_req) {

	if (!upsgi.routes)
		return UPSGI_ROUTE_CONTINUE;

	// avoid loops
	if (wsgi_req->is_routing)
		return UPSGI_ROUTE_CONTINUE;

	if (upsgi_parse_vars(wsgi_req)) {
		return UPSGI_ROUTE_BREAK;
	}

	// in case of static files serving previous rules could be applied
	if (wsgi_req->routes_applied) {
		return UPSGI_ROUTE_CONTINUE;
	}

	return upsgi_apply_routes_do(upsgi.routes, wsgi_req, NULL, 0);
}

void upsgi_apply_final_routes(struct wsgi_request *wsgi_req) {

        if (!upsgi.final_routes) return;

        // avoid loops
        if (wsgi_req->is_routing) return;

	wsgi_req->is_final_routing = 1;

        upsgi_apply_routes_do(upsgi.final_routes, wsgi_req, NULL, 0);
}

int upsgi_apply_error_routes(struct wsgi_request *wsgi_req) {

        if (!upsgi.error_routes) return 0;

	// do not forget to check it !!!
        if (wsgi_req->is_error_routing) return 0;

        wsgi_req->is_error_routing = 1;

	return upsgi_apply_routes_do(upsgi.error_routes, wsgi_req, NULL, 0);
}

int upsgi_apply_response_routes(struct wsgi_request *wsgi_req) {


        if (!upsgi.response_routes) return 0;
        if (wsgi_req->response_routes_applied) return 0;

        // do not forget to check it !!!
        if (wsgi_req->is_response_routing) return 0;

        wsgi_req->is_response_routing = 1;

        int ret = upsgi_apply_routes_do(upsgi.response_routes, wsgi_req, NULL, 0);
	wsgi_req->response_routes_applied = 1;
	return ret;
}

static void *upsgi_route_get_condition_func(char *name) {
	struct upsgi_route_condition *urc = upsgi.route_conditions;
	while(urc) {
		if (!strcmp(urc->name, name)) {
			return urc->func;
		}
		urc = urc->next;
	}
	return NULL;
}

static int upsgi_route_condition_status(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	if (wsgi_req->status == ur->if_status) {
		return 1;
	}
        return 0;
}


void upsgi_opt_add_route(char *opt, char *value, void *foobar) {

	char *space = NULL;
	char *command = NULL;
	struct upsgi_route *old_ur = NULL, *ur = upsgi.routes;
	if (!upsgi_starts_with(opt, strlen(opt), "final", 5)) {
		ur = upsgi.final_routes;
	} 
	else if (!upsgi_starts_with(opt, strlen(opt), "error", 5)) {
                ur = upsgi.error_routes;
        }
	else if (!upsgi_starts_with(opt, strlen(opt), "response", 8)) {
                ur = upsgi.response_routes;
        }
	uint64_t pos = 0;
	while(ur) {
		old_ur = ur;
		ur = ur->next;
		pos++;
	}
	ur = upsgi_calloc(sizeof(struct upsgi_route));
	if (old_ur) {
		old_ur->next = ur;
	}
	else {
		if (!upsgi_starts_with(opt, strlen(opt), "final", 5)) {
			upsgi.final_routes = ur;
		}
		else if (!upsgi_starts_with(opt, strlen(opt), "error", 5)) {
                        upsgi.error_routes = ur;
                }
		else if (!upsgi_starts_with(opt, strlen(opt), "response", 8)) {
                        upsgi.response_routes = ur;
                }
		else {
			upsgi.routes = ur;
		}
	}

	ur->pos = pos;

	// is it a label ?
	if (foobar == NULL) {
		ur->label = value;
		ur->label_len = strlen(value);
		return;
	}

	ur->orig_route = upsgi_str(value);

	if (!strcmp(foobar, "run")) {
		command = ur->orig_route;	
		goto done;
	}

	space = strchr(ur->orig_route, ' ');
	if (!space) {
		upsgi_log("invalid route syntax\n");
		exit(1);
	}

	*space = 0;

	if (!strcmp(foobar, "if") || !strcmp(foobar, "if-not")) {
		char *colon = strchr(ur->orig_route, ':');
		if (!colon) {
			upsgi_log("invalid route condition syntax\n");
                	exit(1);
		}
		*colon = 0;

		if (!strcmp(foobar, "if-not")) {
			ur->if_negate = 1;
		}

		foobar = colon+1;
		ur->if_func = upsgi_route_get_condition_func(ur->orig_route);
		if (!ur->if_func) {
			upsgi_log("unable to find \"%s\" route condition\n", ur->orig_route);
			exit(1);
		}
	}
	else if (!strcmp(foobar, "status")) {
		ur->if_status = atoi(ur->orig_route);
		foobar = ur->orig_route;
                ur->if_func = upsgi_route_condition_status;
        }

	else if (!strcmp(foobar, "http_host")) {
		ur->subject = offsetof(struct wsgi_request, host);
		ur->subject_len = offsetof(struct wsgi_request, host_len);
	}
	else if (!strcmp(foobar, "request_uri")) {
		ur->subject = offsetof(struct wsgi_request, uri);
		ur->subject_len = offsetof(struct wsgi_request, uri_len);
	}
	else if (!strcmp(foobar, "query_string")) {
		ur->subject = offsetof(struct wsgi_request, query_string);
		ur->subject_len = offsetof(struct wsgi_request, query_string_len);
	}
	else if (!strcmp(foobar, "remote_addr")) {
		ur->subject = offsetof(struct wsgi_request, remote_addr);
		ur->subject_len = offsetof(struct wsgi_request, remote_addr_len);
	}
	else if (!strcmp(foobar, "user_agent")) {
		ur->subject = offsetof(struct wsgi_request, user_agent);
		ur->subject_len = offsetof(struct wsgi_request, user_agent_len);
	}
	else if (!strcmp(foobar, "referer")) {
		ur->subject = offsetof(struct wsgi_request, referer);
		ur->subject_len = offsetof(struct wsgi_request, referer_len);
	}
	else if (!strcmp(foobar, "remote_user")) {
		ur->subject = offsetof(struct wsgi_request, remote_user);
		ur->subject_len = offsetof(struct wsgi_request, remote_user_len);
	}
	else {
		ur->subject = offsetof(struct wsgi_request, path_info);
		ur->subject_len = offsetof(struct wsgi_request, path_info_len);
	}

	ur->subject_str = foobar;
	ur->subject_str_len = strlen(ur->subject_str);
	ur->regexp = ur->orig_route;

	command = space + 1;
done:
	ur->action = upsgi_str(command);

	char *colon = strchr(command, ':');
	if (!colon) {
		upsgi_log("invalid route syntax\n");
		exit(1);
	}

	*colon = 0;

	struct upsgi_router *r = upsgi.routers;
	while (r) {
		if (!strcmp(r->name, command)) {
			if (r->func(ur, colon + 1) == 0) {
				return;
			}
			break;
		}
		r = r->next;
	}

	upsgi_log("unable to register route \"%s\". Missing router for: \"%s\"\n", value, command);
	exit(1);
}

void upsgi_fixup_routes(struct upsgi_route *ur) {
	while(ur) {
		// prepare the main pointers
		ur->ovn = upsgi_calloc(sizeof(int) * upsgi.cores);
		ur->ovector = upsgi_calloc(sizeof(int *) * upsgi.cores);
		ur->condition_ub = upsgi_calloc( sizeof(struct upsgi_buffer *) * upsgi.cores);

		// fill them if needed... (this is an optimization for route with a static subject)
		if (ur->subject && ur->subject_len) {
			if (upsgi_regexp_build(ur->orig_route, &ur->pattern)) {
                        	exit(1);
                	}

			int i;
			for(i=0;i<upsgi.cores;i++) {
				ur->ovn[i] = upsgi_regexp_ovector(ur->pattern);
                		if (ur->ovn[i] > 0) {
                        		ur->ovector[i] = upsgi_calloc(sizeof(int) * PCRE_OVECTOR_BYTESIZE(ur->ovn[i]));
                		}
			}
		}
		ur = ur->next;
        }
}

int upsgi_route_api_func(struct wsgi_request *wsgi_req, char *router, char *args) {
	struct upsgi_route *ur = NULL;
	struct upsgi_router *r = upsgi.routers;
	while(r) {
		if (!strcmp(router, r->name)) {
			goto found;
		}
		r = r->next;
	}
	free(args);
	return -1;
found:
	ur = upsgi_calloc(sizeof(struct upsgi_route));
	// initialize the virtual route
	if (r->func(ur, args)) {
		free(ur);
		free(args);
		return -1;
	}
	// call it
	int ret = ur->func(wsgi_req, ur);
	if (ur->free) {
		ur->free(ur);
	}
	free(ur);
	free(args);
	return ret;
}

// continue/last route

static int upsgi_router_continue_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
	return UPSGI_ROUTE_CONTINUE;	
}

static int upsgi_router_continue(struct upsgi_route *ur, char *arg) {
	ur->func = upsgi_router_continue_func;
	return 0;
}

// break route

static int upsgi_router_break_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
	if (route->data_len >= 3) {
		if (upsgi_response_prepare_headers(wsgi_req, route->data, route->data_len)) goto end;
		if (upsgi_response_add_connection_close(wsgi_req)) goto end;
		if (upsgi_response_add_content_type(wsgi_req, "text/plain", 10)) goto end;
		// no need to check for return value
		upsgi_response_write_headers_do(wsgi_req);
	}
end:
	return UPSGI_ROUTE_BREAK;	
}

static int upsgi_router_break(struct upsgi_route *ur, char *arg) {
	ur->func = upsgi_router_break_func;
	ur->data = arg;
        ur->data_len = strlen(arg);
	return 0;
}

static int upsgi_router_return_func(struct wsgi_request *wsgi_req, struct upsgi_route *route)
{

	if (route->data_len < 3)
		return UPSGI_ROUTE_BREAK;
	uint16_t status_msg_len = 0;
	const char *status_msg = upsgi_http_status_msg(route->data, &status_msg_len);

	if (!status_msg)
		return UPSGI_ROUTE_BREAK;

	char *buf = upsgi_concat3n(route->data, route->data_len, " ", 1, (char *) status_msg, status_msg_len);
	if (upsgi_response_prepare_headers(wsgi_req, buf, route->data_len + 1 + status_msg_len))
		goto end;
	if (upsgi_response_add_content_type(wsgi_req, "text/plain", 10))
		goto end;
	if (upsgi_response_add_content_length(wsgi_req, status_msg_len))
		goto end;
	upsgi_response_write_body_do(wsgi_req, (char *) status_msg, status_msg_len);

end:
	free(buf);
	return UPSGI_ROUTE_BREAK;
}

static int upsgi_router_return(struct upsgi_route *ur, char *arg)
{
	ur->func = upsgi_router_return_func;
	ur->data = arg;
	ur->data_len = strlen(arg);
	return 0;
}

// simple math router
static int upsgi_router_simple_math_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	uint16_t var_vallen = 0;
	char *var_value = upsgi_get_var(wsgi_req, ur->data, ur->data_len, &var_vallen);
	if (!var_value) return UPSGI_ROUTE_BREAK;

	int64_t base_value = upsgi_str_num(var_value, var_vallen);
	int64_t value = 1;

	if (ur->data2_len) {
        	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data2, ur->data2_len);
        	if (!ub) return UPSGI_ROUTE_BREAK;
		value = upsgi_str_num(ub->buf, ub->pos);
		upsgi_buffer_destroy(ub);
	}

	char out[sizeof(UMAX64_STR)+1];
	int64_t total = 0;

	switch(ur->custom) {
		// -
		case 1:
			total = base_value - value;
			break;
		// *
		case 2:
			total = base_value * value;
			break;
		// /
		case 3:
			if (value == 0) total = 0;
			else {
				total = base_value/value;
			}
			break;
		default:
			total = base_value + value;
			break;
	}

	int ret = upsgi_long2str2n(total, out, sizeof(UMAX64_STR)+1);
	if (ret <= 0) return UPSGI_ROUTE_BREAK;

        if (!upsgi_req_append(wsgi_req, ur->data, ur->data_len, out, ret)) {
                return UPSGI_ROUTE_BREAK;
        }
        return UPSGI_ROUTE_NEXT;
}

static int upsgi_router_simple_math_plus(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_simple_math_func;
	char *comma = strchr(arg, ',');
	if (comma) {
		ur->data = arg;
		ur->data_len = comma - arg;
		ur->data2 = comma+1;
		ur->data2_len = strlen(ur->data);
	}
	else {
		ur->data = arg;
		ur->data_len = strlen(arg);
	}
        return 0;
}

static int upsgi_router_simple_math_minus(struct upsgi_route *ur, char *arg) {
	ur->custom = 1;
	return upsgi_router_simple_math_plus(ur, arg);
}

static int upsgi_router_simple_math_multiply(struct upsgi_route *ur, char *arg) {
	ur->custom = 2;
	return upsgi_router_simple_math_plus(ur, arg);
}

static int upsgi_router_simple_math_divide(struct upsgi_route *ur, char *arg) {
	ur->custom = 2;
	return upsgi_router_simple_math_plus(ur, arg);
}

// harakiri router
static int upsgi_router_harakiri_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
	if (route->custom > 0) {	
		set_user_harakiri(wsgi_req, route->custom);
	}
	return UPSGI_ROUTE_NEXT;
}

static int upsgi_router_harakiri(struct upsgi_route *ur, char *arg) {
	ur->func = upsgi_router_harakiri_func;
	ur->custom = atoi(arg);
	return 0;
}

// flush response
static int transform_flush(struct wsgi_request *wsgi_req, struct upsgi_transformation *ut) {
	// avoid loops !!!
	if (ut->chunk->pos == 0) return 0;
	wsgi_req->transformed_chunk = ut->chunk->buf;
	wsgi_req->transformed_chunk_len = ut->chunk->pos;
	int ret = upsgi_response_write_body_do(wsgi_req, ut->chunk->buf, ut->chunk->pos);
	wsgi_req->transformed_chunk = NULL;
	wsgi_req->transformed_chunk_len = 0;
	ut->flushed = 1;
	return ret;
}
static int upsgi_router_flush_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
	struct upsgi_transformation *ut = upsgi_add_transformation(wsgi_req, transform_flush, NULL);
	ut->can_stream = 1;
	return UPSGI_ROUTE_NEXT;	
}
static int upsgi_router_flush(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_flush_func;
        return 0;
}

// fix content length
static int transform_fixcl(struct wsgi_request *wsgi_req, struct upsgi_transformation *ut) {
	char buf[sizeof(UMAX64_STR)+1];
        int ret = snprintf(buf, sizeof(UMAX64_STR)+1, "%llu", (unsigned long long) ut->chunk->pos);
        if (ret <= 0 || ret >= (int) (sizeof(UMAX64_STR)+1)) {
                wsgi_req->write_errors++;
                return -1;
        }
	// do not check for errors !!!
        upsgi_response_add_header(wsgi_req, "Content-Length", 14, buf, ret);
	return 0;
}
static int upsgi_router_fixcl_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
        upsgi_add_transformation(wsgi_req, transform_fixcl, NULL);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_fixcl(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_fixcl_func;
        return 0;
}

// force content length
static int transform_forcecl(struct wsgi_request *wsgi_req, struct upsgi_transformation *ut) {
        char buf[sizeof(UMAX64_STR)+1];
        int ret = snprintf(buf, sizeof(UMAX64_STR)+1, "%llu", (unsigned long long) ut->chunk->pos);
        if (ret <= 0 || ret >= (int) (sizeof(UMAX64_STR)+1)) {
                wsgi_req->write_errors++;
                return -1;
        }
        // do not check for errors !!!
        upsgi_response_add_header_force(wsgi_req, "Content-Length", 14, buf, ret);
        return 0;
}
static int upsgi_router_forcecl_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
        upsgi_add_transformation(wsgi_req, transform_forcecl, NULL);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_forcecl(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_forcecl_func;
        return 0;
}

// log route
static int upsgi_router_log_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

	char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
	if (!ub) return UPSGI_ROUTE_BREAK;

	upsgi_log("%.*s\n", ub->pos, ub->buf);
	upsgi_buffer_destroy(ub);
	return UPSGI_ROUTE_NEXT;	
}

static int upsgi_router_log(struct upsgi_route *ur, char *arg) {
	ur->func = upsgi_router_log_func;
	ur->data = arg;
	ur->data_len = strlen(arg);
	return 0;
}

// do not log !!!
static int upsgi_router_donotlog_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	wsgi_req->do_not_log = 1;
	return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_donotlog(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_donotlog_func;
        return 0;
}

// do not offload !!!
static int upsgi_router_donotoffload_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	wsgi_req->socket->can_offload = 0;
	return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_donotoffload(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_donotoffload_func;
        return 0;
}

// logvar route
static int upsgi_router_logvar_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data2, ur->data2_len);
	if (!ub) return UPSGI_ROUTE_BREAK;
	upsgi_logvar_add(wsgi_req, ur->data, ur->data_len, ub->buf, ub->pos);
	upsgi_buffer_destroy(ub);

        return UPSGI_ROUTE_NEXT;
}

static int upsgi_router_logvar(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_logvar_func;
	char *equal = strchr(arg, '=');
	if (!equal) {
		upsgi_log("invalid logvar syntax, must be key=value\n");
		exit(1);
	}
        ur->data = arg;
        ur->data_len = equal-arg;
	ur->data2 = equal+1;
	ur->data2_len = strlen(ur->data2);
        return 0;
}


// goto route 

static int upsgi_router_goto_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	// build the label (if needed)
	char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
	uint32_t *r_goto = &wsgi_req->route_goto;
	uint32_t *r_pc = &wsgi_req->route_pc;

	// find the label
	struct upsgi_route *routes = upsgi.routes;
	if (wsgi_req->is_error_routing) {
		routes = upsgi.error_routes;
		r_goto = &wsgi_req->error_route_goto;
		r_pc = &wsgi_req->error_route_pc;
	}
	else if (wsgi_req->is_final_routing) {
		routes = upsgi.final_routes;
		r_goto = &wsgi_req->final_route_goto;
		r_pc = &wsgi_req->final_route_pc;
	}
	else if (wsgi_req->is_response_routing) {
                routes = upsgi.response_routes;
                r_goto = &wsgi_req->response_route_goto;
                r_pc = &wsgi_req->response_route_pc;
        }
	while(routes) {
		if (!routes->label) goto next;
		if (!upsgi_strncmp(routes->label, routes->label_len, ub->buf, ub->pos)) {
			*r_goto = routes->pos;
			goto found;
		}
next:
		routes = routes->next;
	}

	*r_goto = ur->custom;
	
found:
	upsgi_buffer_destroy(ub);
	if (*r_goto <= *r_pc) {
		*r_goto = 0;
		upsgi_log("[upsgi-route] ERROR \"goto\" instruction can only jump forward (check your label !!!)\n");
		return UPSGI_ROUTE_BREAK;
	}
	return UPSGI_ROUTE_NEXT;	
}

static int upsgi_router_goto(struct upsgi_route *ur, char *arg) {
	ur->func = upsgi_router_goto_func;
	ur->data = arg;
	ur->data_len = strlen(arg);
	ur->custom = atoi(arg);
        return 0;
}

// addvar route
static int upsgi_router_addvar_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data2, ur->data2_len);
        if (!ub) return UPSGI_ROUTE_BREAK;

	if (!upsgi_req_append(wsgi_req, ur->data, ur->data_len, ub->buf, ub->pos)) {
		upsgi_buffer_destroy(ub);
        	return UPSGI_ROUTE_BREAK;
	}
	upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}


static int upsgi_router_addvar(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_addvar_func;
	char *equal = strchr(arg, '=');
	if (!equal) {
		upsgi_log("[upsgi-route] invalid addvar syntax, must be KEY=VAL\n");
		exit(1);
	}
	ur->data = arg;
	ur->data_len = equal-arg;
	ur->data2 = equal+1;
	ur->data2_len = strlen(ur->data2);
        return 0;
}


// addheader route
static int upsgi_router_addheader_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
	upsgi_additional_header_add(wsgi_req, ub->buf, ub->pos);
	upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}


static int upsgi_router_addheader(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_addheader_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// remheader route
static int upsgi_router_remheader_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        upsgi_remove_header(wsgi_req, ub->buf, ub->pos);
	upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}


static int upsgi_router_remheader(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_remheader_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// clearheaders route
static int upsgi_router_clearheaders_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;

	if (upsgi_response_prepare_headers(wsgi_req, ub->buf, ub->pos)) {
        	upsgi_buffer_destroy(ub);
        	return UPSGI_ROUTE_BREAK;
	}
	
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}


static int upsgi_router_clearheaders(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_clearheaders_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// disable headers
static int upsgi_router_disableheaders_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	wsgi_req->headers_sent = 1;
        return UPSGI_ROUTE_NEXT;
}

static int upsgi_router_disableheaders(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_disableheaders_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}



// signal route
static int upsgi_router_signal_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
	upsgi_signal_send(upsgi.signal_socket, route->custom);	
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_signal(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_signal_func;
	ur->custom = atoi(arg);
        return 0;
}

// chdir route
static int upsgi_router_chdir_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
	if (!ub) return UPSGI_ROUTE_BREAK;
	if (chdir(ub->buf)) {
		upsgi_req_error("upsgi_router_chdir_func()/chdir()");
		upsgi_buffer_destroy(ub);
		return UPSGI_ROUTE_BREAK;
	}
	upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_chdir(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_chdir_func;
        ur->data = arg;
	ur->data_len = strlen(arg);
        return 0;
}

// setapp route
static int upsgi_router_setapp_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
	char *ptr = upsgi_req_append(wsgi_req, "UPSGI_APPID", 11, ub->buf, ub->pos);
	if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
	wsgi_req->appid = ptr;
	wsgi_req->appid_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setapp(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setapp_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// setscriptname route
static int upsgi_router_setscriptname_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "SCRIPT_NAME", 11, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->script_name = ptr;
        wsgi_req->script_name_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setscriptname(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setscriptname_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// setmethod route
static int upsgi_router_setmethod_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "REQUEST_METHOD", 14, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->method = ptr;
        wsgi_req->method_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setmethod(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setmethod_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// seturi route
static int upsgi_router_seturi_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "REQUEST_URI", 11, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->uri = ptr;
        wsgi_req->uri_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_seturi(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_seturi_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// setremoteaddr route
static int upsgi_router_setremoteaddr_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "REMOTE_ADDR", 11, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->remote_addr = ptr;
        wsgi_req->remote_addr_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setremoteaddr(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setremoteaddr_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}


// setdocroot route
static int upsgi_router_setdocroot_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "DOCUMENT_ROOT", 13, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->document_root = ptr;
        wsgi_req->document_root_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setdocroot(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setdocroot_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}


// setpathinfo route
static int upsgi_router_setpathinfo_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "PATH_INFO", 9, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->path_info = ptr;
        wsgi_req->path_info_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setpathinfo(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setpathinfo_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// fixpathinfo route
static int upsgi_router_fixpathinfo_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	if (wsgi_req->script_name_len == 0)
		return UPSGI_ROUTE_NEXT;

        char *ptr = upsgi_req_append(wsgi_req, "PATH_INFO", 9, wsgi_req->path_info+wsgi_req->script_name_len, wsgi_req->path_info_len - wsgi_req->script_name_len);
        if (!ptr) {
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->path_info = wsgi_req->path_info+wsgi_req->script_name_len;
        wsgi_req->path_info_len = wsgi_req->path_info_len - wsgi_req->script_name_len;
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_fixpathinfo(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_fixpathinfo_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}


// setscheme route
static int upsgi_router_setscheme_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "UPSGI_SCHEME", 12, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->scheme = ptr;
        wsgi_req->scheme_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setscheme(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setscheme_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// setmodifiers
static int upsgi_router_setmodifier1_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	wsgi_req->uh->modifier1 = ur->custom;
	return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setmodifier1(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setmodifier1_func;
	ur->custom = atoi(arg);
        return 0;
}
static int upsgi_router_setmodifier2_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	wsgi_req->uh->modifier2 = ur->custom;
	return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setmodifier2(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setmodifier2_func;
	ur->custom = atoi(arg);
        return 0;
}


// setuser route
static int upsgi_router_setuser_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
	uint16_t user_len = ub->pos;
	// stop at the first colon (useful for various tricks)
	char *colon = memchr(ub->buf, ':', ub->pos);
	if (colon) {
		user_len = colon - ub->buf;
	}
        char *ptr = upsgi_req_append(wsgi_req, "REMOTE_USER", 11, ub->buf, user_len);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->remote_user = ptr;
        wsgi_req->remote_user_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setuser(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setuser_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}


// sethome route
static int upsgi_router_sethome_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "UPSGI_HOME", 10, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->home = ptr;
        wsgi_req->home_len = ub->pos;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_sethome(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_sethome_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// setfile route
static int upsgi_router_setfile_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
        char *ptr = upsgi_req_append(wsgi_req, "UPSGI_HOME", 10, ub->buf, ub->pos);
        if (!ptr) {
                upsgi_buffer_destroy(ub);
                return UPSGI_ROUTE_BREAK;
        }
        wsgi_req->file = ptr;
        wsgi_req->file_len = ub->pos;
	wsgi_req->dynamic = 1;
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setfile(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setfile_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}


// setprocname route
static int upsgi_router_setprocname_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub) return UPSGI_ROUTE_BREAK;
	upsgi_set_processname(ub->buf);
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_setprocname(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_setprocname_func;
        ur->data = arg;
        ur->data_len = strlen(arg);
        return 0;
}

// alarm route
static int upsgi_router_alarm_func(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

        struct upsgi_buffer *ub_alarm = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
        if (!ub_alarm) return UPSGI_ROUTE_BREAK;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data2, ur->data2_len);
        if (!ub) {
		upsgi_buffer_destroy(ub_alarm);
		return UPSGI_ROUTE_BREAK;
	}
	upsgi_alarm_trigger(ub_alarm->buf, ub->buf, ub->pos);	
        upsgi_buffer_destroy(ub_alarm);
        upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_alarm(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_alarm_func;
	char *space = strchr(arg, ' ');
	if (!space) {
		return -1;
	}
	*space = 0;
        ur->data = arg;
        ur->data_len = strlen(arg);
        ur->data2 = space+1;
        ur->data2_len = strlen(ur->data2);
        return 0;
}



// send route
static int upsgi_router_send_func(struct wsgi_request *wsgi_req, struct upsgi_route *route) {
	char **subject = (char **) (((char *)(wsgi_req))+route->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+route->subject_len);

	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, route, *subject, *subject_len, route->data, route->data_len);
        if (!ub) {
                return UPSGI_ROUTE_BREAK;
        }
	if (route->custom) {
		if (upsgi_buffer_append(ub, "\r\n", 2)) {
			upsgi_buffer_destroy(ub);
			return UPSGI_ROUTE_BREAK;
		}
	}
	upsgi_response_write_body_do(wsgi_req, ub->buf, ub->pos);
	upsgi_buffer_destroy(ub);
        return UPSGI_ROUTE_NEXT;
}
static int upsgi_router_send(struct upsgi_route *ur, char *arg) {
        ur->func = upsgi_router_send_func;
	ur->data = arg;
	ur->data_len = strlen(arg);
        return 0;
}
static int upsgi_router_send_crnl(struct upsgi_route *ur, char *arg) {
	upsgi_router_send(ur, arg);
        ur->custom = 1;
        return 0;
}

static int upsgi_route_condition_exists(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, ur->subject_str_len);
	if (!ub) return -1;
	if (upsgi_file_exists(ub->buf)) {
		upsgi_buffer_destroy(ub);
		return 1;
	}
	upsgi_buffer_destroy(ub);
	return 0;
}

static int upsgi_route_condition_isfile(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, ur->subject_str_len);
        if (!ub) return -1;
        if (upsgi_is_file(ub->buf)) {
                upsgi_buffer_destroy(ub);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        return 0;
}

static int upsgi_route_condition_regexp(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        ur->condition_ub[wsgi_req->async_id] = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ur->condition_ub[wsgi_req->async_id]) return -1;

	upsgi_pcre *pattern;
	char *re = upsgi_concat2n(semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str), "", 0);
	if (upsgi_regexp_build(re, &pattern)) {
		free(re);
		return -1;
	}
	free(re);

	// a condition has no initialized vectors, let's create them
	ur->ovn[wsgi_req->async_id] = upsgi_regexp_ovector(pattern);
        if (ur->ovn[wsgi_req->async_id] > 0) {
        	ur->ovector[wsgi_req->async_id] = upsgi_calloc(sizeof(int) * (3 * (ur->ovn[wsgi_req->async_id] + 1)));
        }

	if (upsgi_regexp_match_ovec(pattern, ur->condition_ub[wsgi_req->async_id]->buf, ur->condition_ub[wsgi_req->async_id]->pos, ur->ovector[wsgi_req->async_id], ur->ovn[wsgi_req->async_id] ) >= 0) {
#ifdef UPSGI_PCRE2
		pcre2_code_free(pattern);
#else
		pcre_free(pattern->p);
#ifdef PCRE_STUDY_JIT_COMPILE
		pcre_free_study(pattern->extra);
#else
		pcre_free(pattern->extra);
#endif
		free(pattern);
#endif
		return 1;
	}

#ifdef UPSGI_PCRE2
	pcre2_code_free(pattern);
#else
	pcre_free(pattern->p);
#ifdef PCRE_STUDY_JIT_COMPILE
	pcre_free_study(pattern->extra);
#else
	pcre_free(pattern->extra);
#endif
	free(pattern);
#endif
	return 0;
}

static int upsgi_route_condition_empty(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, ur->subject_str_len);
        if (!ub) return -1;

	if (ub->pos == 0) {
        	upsgi_buffer_destroy(ub);
        	return 1;
	}

        upsgi_buffer_destroy(ub);
        return 0;
}


static int upsgi_route_condition_equal(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
	char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
	if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

	struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
		upsgi_buffer_destroy(ub);
		return -1;
	}

	if(!upsgi_strncmp(ub->buf, ub->pos, ub2->buf, ub2->pos)) {
		upsgi_buffer_destroy(ub);
		upsgi_buffer_destroy(ub2);
		return 1;
	}
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}

static int upsgi_route_condition_higher(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

	long num1 = strtol(ub->buf, NULL, 10);
	long num2 = strtol(ub2->buf, NULL, 10);
        if(num1 > num2) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}

static int upsgi_route_condition_higherequal(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

        long num1 = strtol(ub->buf, NULL, 10);
        long num2 = strtol(ub2->buf, NULL, 10);
        if(num1 >= num2) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}


static int upsgi_route_condition_lower(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;
        
        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

        long num1 = strtol(ub->buf, NULL, 10);
        long num2 = strtol(ub2->buf, NULL, 10);
        if(num1 < num2) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}

static int upsgi_route_condition_lowerequal(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;
        
        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

        long num1 = strtol(ub->buf, NULL, 10);
        long num2 = strtol(ub2->buf, NULL, 10);
        if(num1 <= num2) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}




static int upsgi_route_condition_startswith(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

        if(!upsgi_starts_with(ub->buf, ub->pos, ub2->buf, ub2->pos)) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}

static int upsgi_route_condition_ipv4in(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
#define IP4_LEN		(sizeof("255.255.255.255")-1)
#define IP4PFX_LEN	(sizeof("255.255.255.255/32")-1)
	char ipbuf[IP4_LEN+1] = {}, maskbuf[IP4PFX_LEN+1] = {};
	char *slash;
	int pfxlen = 32;
	in_addr_t ip, net, mask;

        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

	if (ub->pos > IP4_LEN || ub2->pos >= IP4PFX_LEN) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
		return -1;
	}

	memcpy(ipbuf, ub->buf, ub->pos);
	memcpy(maskbuf, ub2->buf, ub2->pos);

	if ((slash = strchr(maskbuf, '/')) != NULL) {
		*slash++ = 0;
		pfxlen = atoi(slash);
	}

        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);

	if ((ip = htonl(inet_addr(ipbuf))) == ~(in_addr_t)0)
		return 0;
	if ((net = htonl(inet_addr(maskbuf))) == ~(in_addr_t)0)
		return 0;
	if (pfxlen < 0 || pfxlen > 32)
		return 0;

	mask = (~0UL << (32 - pfxlen)) & ~0U;

	return ((ip & mask) == (net & mask));
#undef IP4_LEN
#undef IP4PFX_LEN
}

static int upsgi_route_condition_ipv6in(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
#define IP6_LEN 	(sizeof("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")-1)
#define IP6PFX_LEN 	(sizeof("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128")-1)
#define IP6_U32LEN	(128 / 8 / 4)
	char ipbuf[IP6_LEN+1] = {}, maskbuf[IP6PFX_LEN+1] = {};
	char *slash;
	int pfxlen = 128;
	uint32_t ip[IP6_U32LEN], net[IP6_U32LEN], mask[IP6_U32LEN] = {};

        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

	if (ub->pos > IP6_LEN || ub2->pos >= IP6PFX_LEN) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
		return -1;
	}

	memcpy(ipbuf, ub->buf, ub->pos);
	memcpy(maskbuf, ub2->buf, ub2->pos);

	if ((slash = strchr(maskbuf, '/')) != NULL) {
		*slash++ = 0;
		pfxlen = atoi(slash);
	}

        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);

	if (inet_pton(AF_INET6, ipbuf, ip) != 1)
		return 0;
	if (inet_pton(AF_INET6, maskbuf, net) != 1)
		return 0;
	if (pfxlen < 0 || pfxlen > 128)
		return 0;

	memset(mask, 0xFF, sizeof(mask));

	int i = (pfxlen / 32);
	switch (i) {
	case 0: mask[0] = 0; /* fallthrough */
	case 1: mask[1] = 0; /* fallthrough */
	case 2: mask[2] = 0; /* fallthrough */
	case 3: mask[3] = 0; /* fallthrough */
	}

	if (pfxlen % 32)
		mask[i] = htonl(~(uint32_t)0 << (32 - (pfxlen % 32)));

	for (i = 0; i < 4; i++)
		if ((ip[i] & mask[i]) != (net[i] & mask[i]))
			return 0;

	return 1;
#undef IP6_LEN
#undef IP6PFX_LEN
#undef IP6_U32LEN
}

static int upsgi_route_condition_contains(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

        if(upsgi_contains_n(ub->buf, ub->pos, ub2->buf, ub2->pos)) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}

static int upsgi_route_condition_endswith(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        char *semicolon = memchr(ur->subject_str, ';', ur->subject_str_len);
        if (!semicolon) return 0;

        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, semicolon - ur->subject_str);
        if (!ub) return -1;

        struct upsgi_buffer *ub2 = upsgi_routing_translate(wsgi_req, ur, NULL, 0, semicolon+1, ur->subject_str_len - ((semicolon+1) - ur->subject_str));
        if (!ub2) {
                upsgi_buffer_destroy(ub);
                return -1;
        }

	if (ub2->pos > ub->pos) goto zero;
        if(!upsgi_strncmp(ub->buf + (ub->pos - ub2->pos), ub2->pos, ub2->buf, ub2->pos)) {
                upsgi_buffer_destroy(ub);
                upsgi_buffer_destroy(ub2);
                return 1;
        }

zero:
        upsgi_buffer_destroy(ub);
        upsgi_buffer_destroy(ub2);
        return 0;
}



static int upsgi_route_condition_isdir(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, ur->subject_str_len);
        if (!ub) return -1;
        if (upsgi_is_dir(ub->buf)) {
                upsgi_buffer_destroy(ub);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        return 0;
}

static int upsgi_route_condition_islink(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, ur->subject_str_len);
        if (!ub) return -1;
        if (upsgi_is_link(ub->buf)) {
                upsgi_buffer_destroy(ub);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        return 0;
}


static int upsgi_route_condition_isexec(struct wsgi_request *wsgi_req, struct upsgi_route *ur) {
        struct upsgi_buffer *ub = upsgi_routing_translate(wsgi_req, ur, NULL, 0, ur->subject_str, ur->subject_str_len);
        if (!ub) return -1;
        if (!access(ub->buf, X_OK)) {
                upsgi_buffer_destroy(ub);
                return 1;
        }
        upsgi_buffer_destroy(ub);
        return 0;
}

static char *upsgi_route_var_upsgi(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
	char *ret = NULL;
	if (!upsgi_strncmp(key, keylen, "wid", 3)) {
		ret = upsgi_num2str(upsgi.mywid);
		*vallen = strlen(ret);
	}
	else if (!upsgi_strncmp(key, keylen, "pid", 3)) {
		ret = upsgi_num2str(upsgi.mypid);
		*vallen = strlen(ret);
	}
	else if (!upsgi_strncmp(key, keylen, "uuid", 4)) {
		ret = upsgi_malloc(37);
		upsgi_uuid(ret);
		*vallen = 36;
	}
	else if (!upsgi_strncmp(key, keylen, "status", 6)) {
                ret = upsgi_num2str(wsgi_req->status);
                *vallen = strlen(ret);
        }
	else if (!upsgi_strncmp(key, keylen, "rtime", 5)) {
                ret = upsgi_num2str(wsgi_req->end_of_request - wsgi_req->start_of_request);
                *vallen = strlen(ret);
        }

	else if (!upsgi_strncmp(key, keylen, "lq", 2)) {
                ret = upsgi_num2str(upsgi.shared->backlog);
                *vallen = strlen(ret);
        }
	else if (!upsgi_strncmp(key, keylen, "rsize", 5)) {
                ret = upsgi_64bit2str(wsgi_req->response_size);
                *vallen = strlen(ret);
        }
	else if (!upsgi_strncmp(key, keylen, "sor", 3)) {
                ret = upsgi_64bit2str(wsgi_req->start_of_request);
                *vallen = strlen(ret);
        }

	return ret;
}

static char *upsgi_route_var_mime(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
	char *ret = NULL;
        uint16_t var_vallen = 0;
        char *var_value = upsgi_get_var(wsgi_req, key, keylen, &var_vallen);
        if (var_value) {
		size_t mime_type_len = 0;
		ret = upsgi_get_mime_type(var_value, var_vallen, &mime_type_len);
		if (ret) *vallen = mime_type_len;
        }
        return ret;
}

static char *upsgi_route_var_httptime(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
        // 30+1
        char *ht = upsgi_calloc(31);
	size_t t = upsgi_str_num(key, keylen);
        int len = upsgi_http_date(upsgi_now() + t, ht);
	if (len == 0) {
		free(ht);
		return NULL;
	}
	*vallen = len;
        return ht;
}


static char *upsgi_route_var_time(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
        char *ret = NULL;
        if (!upsgi_strncmp(key, keylen, "unix", 4)) {
                ret = upsgi_num2str(upsgi_now());
                *vallen = strlen(ret);
        }
	else if (!upsgi_strncmp(key, keylen, "micros", 6)) {
		ret = upsgi_64bit2str(upsgi_micros());
                *vallen = strlen(ret);
	}
        return ret;
}

static char *upsgi_route_var_base64(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
	char *ret = NULL;
	uint16_t var_vallen = 0;
        char *var_value = upsgi_get_var(wsgi_req, key, keylen, &var_vallen);
	if (var_value) {
		size_t b64_len = 0;
		ret = upsgi_base64_encode(var_value, var_vallen, &b64_len);
		*vallen = b64_len;
	}
        return ret;
}

static char *upsgi_route_var_hex(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
        char *ret = NULL;
        uint16_t var_vallen = 0;
        char *var_value = upsgi_get_var(wsgi_req, key, keylen, &var_vallen);
        if (var_value) {
                ret = upsgi_str_to_hex(var_value, var_vallen);
                *vallen = var_vallen*2;
        }
        return ret;
}

static char *upsgi_route_var_upper(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
    char *ret = NULL;
    char *var_value = upsgi_get_var(wsgi_req, key, keylen, vallen);
    if (var_value) {
        ret = upsgi_malloc((size_t) *vallen);
        size_t i;
        for (i = 0; i < *vallen; i++) {
            ret[i] = toupper((int) var_value[i]);
        }
    }
    return ret;
}

static char *upsgi_route_var_lower(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t *vallen) {
    char *ret = NULL;
    char *var_value = upsgi_get_var(wsgi_req, key, keylen, vallen);
    if (var_value) {
        ret = upsgi_malloc((size_t) *vallen);
        size_t i;
        for (i = 0; i < *vallen; i++) {
            ret[i] = tolower((int) var_value[i]);
        }
    }
    return ret;
}

// register embedded routers
void upsgi_register_embedded_routers() {
	upsgi_register_router("continue", upsgi_router_continue);
        upsgi_register_router("last", upsgi_router_continue);
        upsgi_register_router("break", upsgi_router_break);
	upsgi_register_router("return", upsgi_router_return);
	upsgi_register_router("break-with-status", upsgi_router_return);
        upsgi_register_router("log", upsgi_router_log);
        upsgi_register_router("donotlog", upsgi_router_donotlog);
        upsgi_register_router("donotoffload", upsgi_router_donotoffload);
        upsgi_register_router("logvar", upsgi_router_logvar);
        upsgi_register_router("goto", upsgi_router_goto);
        upsgi_register_router("addvar", upsgi_router_addvar);
        upsgi_register_router("addheader", upsgi_router_addheader);
        upsgi_register_router("delheader", upsgi_router_remheader);
        upsgi_register_router("remheader", upsgi_router_remheader);
        upsgi_register_router("clearheaders", upsgi_router_clearheaders);
        upsgi_register_router("resetheaders", upsgi_router_clearheaders);
        upsgi_register_router("disableheaders", upsgi_router_disableheaders);
        upsgi_register_router("signal", upsgi_router_signal);
        upsgi_register_router("send", upsgi_router_send);
        upsgi_register_router("send-crnl", upsgi_router_send_crnl);
        upsgi_register_router("chdir", upsgi_router_chdir);
        upsgi_register_router("setapp", upsgi_router_setapp);
        upsgi_register_router("setuser", upsgi_router_setuser);
        upsgi_register_router("sethome", upsgi_router_sethome);
        upsgi_register_router("setfile", upsgi_router_setfile);
        upsgi_register_router("setscriptname", upsgi_router_setscriptname);
        upsgi_register_router("setmethod", upsgi_router_setmethod);
        upsgi_register_router("seturi", upsgi_router_seturi);
        upsgi_register_router("setremoteaddr", upsgi_router_setremoteaddr);
        upsgi_register_router("setpathinfo", upsgi_router_setpathinfo);
        upsgi_register_router("fixpathinfo", upsgi_router_fixpathinfo);
        upsgi_register_router("setdocroot", upsgi_router_setdocroot);
        upsgi_register_router("setscheme", upsgi_router_setscheme);
        upsgi_register_router("setprocname", upsgi_router_setprocname);
        upsgi_register_router("alarm", upsgi_router_alarm);

        upsgi_register_router("setmodifier1", upsgi_router_setmodifier1);
        upsgi_register_router("setmodifier2", upsgi_router_setmodifier2);

        upsgi_register_router("+", upsgi_router_simple_math_plus);
        upsgi_register_router("-", upsgi_router_simple_math_minus);
        upsgi_register_router("*", upsgi_router_simple_math_multiply);
        upsgi_register_router("/", upsgi_router_simple_math_divide);

        upsgi_register_router("flush", upsgi_router_flush);
        upsgi_register_router("fixcl", upsgi_router_fixcl);
        upsgi_register_router("forcecl", upsgi_router_forcecl);

        upsgi_register_router("harakiri", upsgi_router_harakiri);

        upsgi_register_route_condition("exists", upsgi_route_condition_exists);
        upsgi_register_route_condition("isfile", upsgi_route_condition_isfile);
        upsgi_register_route_condition("isdir", upsgi_route_condition_isdir);
        upsgi_register_route_condition("islink", upsgi_route_condition_islink);
        upsgi_register_route_condition("isexec", upsgi_route_condition_isexec);
        upsgi_register_route_condition("equal", upsgi_route_condition_equal);
        upsgi_register_route_condition("isequal", upsgi_route_condition_equal);
        upsgi_register_route_condition("eq", upsgi_route_condition_equal);
        upsgi_register_route_condition("==", upsgi_route_condition_equal);
        upsgi_register_route_condition("startswith", upsgi_route_condition_startswith);
        upsgi_register_route_condition("endswith", upsgi_route_condition_endswith);
        upsgi_register_route_condition("regexp", upsgi_route_condition_regexp);
        upsgi_register_route_condition("re", upsgi_route_condition_regexp);
        upsgi_register_route_condition("ishigher", upsgi_route_condition_higher);
        upsgi_register_route_condition(">", upsgi_route_condition_higher);
        upsgi_register_route_condition("islower", upsgi_route_condition_lower);
        upsgi_register_route_condition("<", upsgi_route_condition_lower);
        upsgi_register_route_condition("ishigherequal", upsgi_route_condition_higherequal);
        upsgi_register_route_condition(">=", upsgi_route_condition_higherequal);
        upsgi_register_route_condition("islowerequal", upsgi_route_condition_lowerequal);
        upsgi_register_route_condition("<=", upsgi_route_condition_lowerequal);
        upsgi_register_route_condition("contains", upsgi_route_condition_contains);
        upsgi_register_route_condition("contain", upsgi_route_condition_contains);
        upsgi_register_route_condition("ipv4in", upsgi_route_condition_ipv4in);
        upsgi_register_route_condition("ipv6in", upsgi_route_condition_ipv6in);

        upsgi_register_route_condition("empty", upsgi_route_condition_empty);

        upsgi_register_route_var("cookie", upsgi_get_cookie);
        upsgi_register_route_var("qs", upsgi_get_qs);
        upsgi_register_route_var("mime", upsgi_route_var_mime);
        struct upsgi_route_var *urv = upsgi_register_route_var("upsgi", upsgi_route_var_upsgi);
	urv->need_free = 1;
        urv = upsgi_register_route_var("time", upsgi_route_var_time);
	urv->need_free = 1;
        urv = upsgi_register_route_var("httptime", upsgi_route_var_httptime);
	urv->need_free = 1;
        urv = upsgi_register_route_var("base64", upsgi_route_var_base64);
	urv->need_free = 1;

        urv = upsgi_register_route_var("hex", upsgi_route_var_hex);
	urv->need_free = 1;
    urv = upsgi_register_route_var("upper", upsgi_route_var_upper);
    urv->need_free = 1;
    urv = upsgi_register_route_var("lower", upsgi_route_var_lower);
    urv->need_free = 1;
}

struct upsgi_router *upsgi_register_router(char *name, int (*func) (struct upsgi_route *, char *)) {

	struct upsgi_router *ur = upsgi.routers;
	if (!ur) {
		upsgi.routers = upsgi_calloc(sizeof(struct upsgi_router));
		upsgi.routers->name = name;
		upsgi.routers->func = func;
		return upsgi.routers;
	}

	while (ur) {
		if (!ur->next) {
			ur->next = upsgi_calloc(sizeof(struct upsgi_router));
			ur->next->name = name;
			ur->next->func = func;
			return ur->next;
		}
		ur = ur->next;
	}

	return NULL;

}

struct upsgi_route_condition *upsgi_register_route_condition(char *name, int (*func) (struct wsgi_request *, struct upsgi_route *)) {
	struct upsgi_route_condition *old_urc = NULL,*urc = upsgi.route_conditions;
	while(urc) {
		if (!strcmp(urc->name, name)) {
			return urc;
		}
		old_urc = urc;
		urc = urc->next;
	}

	urc = upsgi_calloc(sizeof(struct upsgi_route_condition));
	urc->name = name;
	urc->func = func;

	if (old_urc) {
		old_urc->next = urc;
	}
	else {
		upsgi.route_conditions = urc;
	}

	return urc;
}

void upsgi_routing_dump() {
	struct upsgi_string_list *usl = NULL;
	struct upsgi_route *routes = upsgi.routes;
	if (!routes) goto next;
	upsgi_log("*** dumping internal routing table ***\n");
	while(routes) {
		if (routes->label) {
			upsgi_log("[rule: %llu] label: %s\n", (unsigned long long ) routes->pos, routes->label);
		}
		else if (!routes->subject_str && !routes->if_func) {
			upsgi_log("[rule: %llu] action: %s\n", (unsigned long long ) routes->pos, routes->action);
		}
		else {
			upsgi_log("[rule: %llu] subject: %s %s: %s%s action: %s\n", (unsigned long long ) routes->pos, routes->subject_str, routes->if_func ? "func" : "regexp", routes->if_negate ? "!" : "", routes->regexp, routes->action);
		}
		routes = routes->next;
	}
	upsgi_log("*** end of the internal routing table ***\n");
next:
	routes = upsgi.error_routes;
        if (!routes) goto next2;
        upsgi_log("*** dumping internal error routing table ***\n");
        while(routes) {
                if (routes->label) {
                        upsgi_log("[rule: %llu] label: %s\n", (unsigned long long ) routes->pos, routes->label);
                }
                else if (!routes->subject_str && !routes->if_func) {
                        upsgi_log("[rule: %llu] action: %s\n", (unsigned long long ) routes->pos, routes->action);
                }
                else {
                        upsgi_log("[rule: %llu] subject: %s %s: %s%s action: %s\n", (unsigned long long ) routes->pos, routes->subject_str, routes->if_func ? "func" : "regexp", routes->if_negate ? "!" : "", routes->regexp, routes->action);
                }
                routes = routes->next;
        }
        upsgi_log("*** end of the internal error routing table ***\n");
next2:
	routes = upsgi.response_routes;
        if (!routes) goto next3;
        upsgi_log("*** dumping internal response routing table ***\n");
        while(routes) {
                if (routes->label) {
                        upsgi_log("[rule: %llu] label: %s\n", (unsigned long long ) routes->pos, routes->label);
                }
                else if (!routes->subject_str && !routes->if_func) {
                        upsgi_log("[rule: %llu] action: %s\n", (unsigned long long ) routes->pos, routes->action);
                }
                else {
                        upsgi_log("[rule: %llu] subject: %s %s: %s%s action: %s\n", (unsigned long long ) routes->pos, routes->subject_str, routes->if_func ? "func" : "regexp", routes->if_negate ? "!" : "", routes->regexp, routes->action);
                }
                routes = routes->next;
        }
        upsgi_log("*** end of the internal response routing table ***\n");
next3:
	routes = upsgi.final_routes;
	if (!routes) goto next4;
        upsgi_log("*** dumping internal final routing table ***\n");
        while(routes) {
                if (routes->label) {
                        upsgi_log("[rule: %llu] label: %s\n", (unsigned long long ) routes->pos, routes->label);
                }
                else if (!routes->subject_str && !routes->if_func) {
                        upsgi_log("[rule: %llu] action: %s\n", (unsigned long long ) routes->pos, routes->action);
                }
                else {
                        upsgi_log("[rule: %llu] subject: %s %s: %s%s action: %s\n", (unsigned long long ) routes->pos, routes->subject_str, routes->if_func ? "func" : "regexp", routes->if_negate ? "!" : "", routes->regexp, routes->action);
                }
                routes = routes->next;
        }
        upsgi_log("*** end of the internal final routing table ***\n");

next4:
	upsgi_foreach(usl, upsgi.collect_headers) {
		char *space = strchr(usl->value, ' ');
		if (!space) {
			upsgi_log("invalid collect header syntax, must be <header> <var>\n");
			exit(1);
		}
		*space = 0;
		usl->custom = strlen(usl->value);
		*space = ' ';
		usl->custom_ptr = space+1;
		usl->custom2 = strlen(space+1);
		upsgi_log("collecting header %.*s to var %s\n", usl->custom, usl->value, usl->custom_ptr);
	}

	upsgi_foreach(usl, upsgi.pull_headers) {
                char *space = strchr(usl->value, ' ');
                if (!space) {
                        upsgi_log("invalid pull header syntax, must be <header> <var>\n");
                        exit(1);
                }
                *space = 0;
                usl->custom = strlen(usl->value);
                *space = ' ';
                usl->custom_ptr = space+1;
                usl->custom2 = strlen(space+1);
                upsgi_log("pulling header %.*s to var %s\n", usl->custom, usl->value, usl->custom_ptr);
        }
}
#endif
