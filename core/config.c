#include "upsgi.h"

/*

	pluggable configuration system

*/

extern struct upsgi_server upsgi;

struct upsgi_configurator *upsgi_register_configurator(char *name, void (*func)(char *, char **)) {
	struct upsgi_configurator *old_uc = NULL,*uc = upsgi.configurators;
        while(uc) {
                if (!strcmp(uc->name, name)) {
                        return uc;
                }
                old_uc = uc;
                uc = uc->next;
        }

        uc = upsgi_calloc(sizeof(struct upsgi_configurator));
        uc->name = name;
        uc->func = func;

        if (old_uc) {
                old_uc->next = uc;
        }
        else {
                upsgi.configurators = uc;
        }

        return uc;
}

int upsgi_logic_opt_if_exists(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (upsgi_file_exists(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_not_exists(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (!upsgi_file_exists(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}


int upsgi_logic_opt_for(char *key, char *value) {

        char *p, *ctx = NULL;
	upsgi_foreach_token(upsgi.logic_opt_data, " ", p, ctx) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", p), 0);
        }

        return 1;
}

int upsgi_logic_opt_for_glob(char *key, char *value) {

        glob_t g;
        int i;
        if (glob(upsgi.logic_opt_data, GLOB_MARK | GLOB_NOCHECK, NULL, &g)) {
                upsgi_error("upsgi_logic_opt_for_glob()");
                return 0;
        }

        for (i = 0; i < (int) g.gl_pathc; i++) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", g.gl_pathv[i]), 0);
        }

        globfree(&g);

        return 1;
}

int upsgi_logic_opt_for_readline(char *key, char *value) {

	char line[1024];

        FILE *fh = fopen(upsgi.logic_opt_data, "r");
        if (fh) {
                while (fgets(line, 1024, fh)) {
			add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi_chomp(upsgi_str(line))), 0);
                }
                fclose(fh);
                return 1;
        }
        upsgi_error_open(upsgi.logic_opt_data);
	return 0;
}

int upsgi_logic_opt_for_times(char *key, char *value) {

        int num = atoi(upsgi.logic_opt_data);
        int i;
        char str_num[11];

        for (i = 1; i <= num; i++) {
                int ret = upsgi_num2str2(i, str_num);
                // security check
                if (ret < 0 || ret > 11) {
                        exit(1);
                }
                add_exported_option(key, upsgi_substitute(value, "%(_)", str_num), 0);
        }

        return 1;
}


int upsgi_logic_opt_if_opt(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        // check for env-value syntax
        char *equal = strchr(upsgi.logic_opt_data, '=');
        if (equal)
                *equal = 0;

        char *p = upsgi_get_exported_opt(upsgi.logic_opt_data);
        if (equal)
                *equal = '=';

        if (p) {
                if (equal) {
                        if (strcmp(equal + 1, p)) {
				upsgi.logic_opt_if_failed = 1;
                                return 0;
			}
                }
                add_exported_option(key, upsgi_substitute(value, "%(_)", p), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_else(char *key, char *value) {
	if (upsgi.logic_opt_if_failed) {
		add_exported_option(key, value, 0);
		return 1;
	}
	return 0;
}


int upsgi_logic_opt_if_not_opt(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        // check for env-value syntax
        char *equal = strchr(upsgi.logic_opt_data, '=');
        if (equal)
                *equal = 0;

        char *p = upsgi_get_exported_opt(upsgi.logic_opt_data);
        if (equal)
                *equal = '=';

        if (p) {
                if (equal) {
                        if (!strcmp(equal + 1, p)) {
				upsgi.logic_opt_if_failed = 1;
                                return 0;
			}
                }
                else {
			upsgi.logic_opt_if_failed = 1;
                        return 0;
                }
        }

        add_exported_option(key, upsgi_substitute(value, "%(_)", p), 0);
        return 1;
}



int upsgi_logic_opt_if_env(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        // check for env-value syntax
        char *equal = strchr(upsgi.logic_opt_data, '=');
        if (equal)
                *equal = 0;

        char *p = getenv(upsgi.logic_opt_data);
        if (equal)
                *equal = '=';

        if (p) {
                if (equal) {
                        if (strcmp(equal + 1, p)) {
				upsgi.logic_opt_if_failed = 1;
                                return 0;
			}
                }
                add_exported_option(key, upsgi_substitute(value, "%(_)", p), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}


int upsgi_logic_opt_if_not_env(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        // check for env-value syntax
        char *equal = strchr(upsgi.logic_opt_data, '=');
        if (equal)
                *equal = 0;

        char *p = getenv(upsgi.logic_opt_data);
        if (equal)
                *equal = '=';

        if (p) {
                if (equal) {
                        if (!strcmp(equal + 1, p)) {
				upsgi.logic_opt_if_failed = 1;
                                return 0;
			}
                }
                else {
			upsgi.logic_opt_if_failed = 1;
                        return 0;
                }
        }

        add_exported_option(key, upsgi_substitute(value, "%(_)", p), 0);
        return 1;
}

int upsgi_logic_opt_if_reload(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (upsgi.is_a_reload) {
                add_exported_option(key, value, 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_not_reload(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (!upsgi.is_a_reload) {
                add_exported_option(key, value, 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_file(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (upsgi_is_file(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_not_file(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (!upsgi_is_file(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_dir(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (upsgi_is_dir(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_not_dir(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (!upsgi_is_dir(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}



int upsgi_logic_opt_if_plugin(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (plugin_already_loaded(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_not_plugin(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (!plugin_already_loaded(upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_hostname(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (!strcmp(upsgi.hostname, upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

int upsgi_logic_opt_if_not_hostname(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
        if (strcmp(upsgi.hostname, upsgi.logic_opt_data)) {
                add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
                return 1;
        }
	upsgi.logic_opt_if_failed = 1;
        return 0;
}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
int upsgi_logic_opt_if_hostname_match(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
	if (upsgi_regexp_match_pattern(upsgi.logic_opt_data, upsgi.hostname)) {
		add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
		return 1;
	}
	upsgi.logic_opt_if_failed = 1;
	return 0;
}

int upsgi_logic_opt_if_not_hostname_match(char *key, char *value) {
	upsgi.logic_opt_if_failed = 0;
	if (!upsgi_regexp_match_pattern(upsgi.logic_opt_data, upsgi.hostname)) {
		add_exported_option(key, upsgi_substitute(value, "%(_)", upsgi.logic_opt_data), 0);
		return 1;
	}
	upsgi.logic_opt_if_failed = 1;
	return 0;
}
#endif

int upsgi_count_options(struct upsgi_option *uopt) {

        struct upsgi_option *aopt;
        int count = 0;

        while ((aopt = uopt)) {
                if (!aopt->name)
                        break;
                count++;
                uopt++;
        }

        return count;
}

int upsgi_opt_exists(char *name) {
	struct upsgi_option *op = upsgi.options;
	while (op->name) {
		if (!strcmp(name, op->name)) return 1;
		op++;
	}
	return 0;	
}

/*
	avoid loops here !!!
*/
struct upsgi_option *upsgi_opt_get(char *name) {
        struct upsgi_option *op;
	int round = 0;
retry:
	round++;
	if (round > 2) goto end;
	op = upsgi.options;

        while (op->name) {
                if (!strcmp(name, op->name)) {
                        return op;
                }
                op++;
        }

	if (upsgi.autoload) {
		if (upsgi_try_autoload(name)) goto retry;
	}

end:
	if (upsgi.strict) {
                upsgi_log("[strict-mode] unknown config directive: %s\n", name);
                exit(1);
        }

        return NULL;
}



void add_exported_option(char *key, char *value, int configured) {
	add_exported_option_do(key, value, configured, 0);
}

void add_exported_option_do(char *key, char *value, int configured, int placeholder_only) {

	struct upsgi_string_list *blacklist = upsgi.blacklist;
	struct upsgi_string_list *whitelist = upsgi.whitelist;

	while (blacklist) {
		if (!strcmp(key, blacklist->value)) {
			upsgi_log("upsgi error: forbidden option \"%s\" (by blacklist)\n", key);
			exit(1);
		}
		blacklist = blacklist->next;
	}

	if (whitelist) {
		int allowed = 0;
		while (whitelist) {
			if (!strcmp(key, whitelist->value)) {
				allowed = 1;
				break;
			}
			whitelist = whitelist->next;
		}
		if (!allowed) {
			upsgi_log("upsgi error: forbidden option \"%s\" (by whitelist)\n", key);
			exit(1);
		}
	}

	if (upsgi.blacklist_context) {
		if (upsgi_list_has_str(upsgi.blacklist_context, key)) {
			upsgi_log("upsgi error: forbidden option \"%s\" (by blacklist)\n", key);
			exit(1);
		}
	}

	if (upsgi.whitelist_context) {
                if (!upsgi_list_has_str(upsgi.whitelist_context, key)) {
                        upsgi_log("upsgi error: forbidden option \"%s\" (by whitelist)\n", key);
                        exit(1);
                }
        }

	if (upsgi.logic_opt_running)
		goto add;

	if (!strcmp(key, "end") || !strcmp(key, "endfor") || !strcmp(key, "endif")
		|| !strcmp(key, "end-if") || !strcmp(key, "end-for")) {
		if (upsgi.logic_opt_data) {
			free(upsgi.logic_opt_data);
		}
		upsgi.logic_opt = NULL;
		upsgi.logic_opt_arg = NULL;
		upsgi.logic_opt_cycles = 0;
		upsgi.logic_opt_data = NULL;
		upsgi.logic_opt_if_failed = 0;
	}

	// like endif but without resetting the failed state
	if (!strcmp(key, "else")) {
		upsgi.logic_opt = NULL;
		upsgi.logic_opt_arg = NULL;
		upsgi.logic_opt_cycles = 0;
		upsgi.logic_opt_data = NULL;
	}

	if (upsgi.logic_opt) {
		if (upsgi.logic_opt_data) {
			free(upsgi.logic_opt_data);
		}
		upsgi.logic_opt_data = upsgi_str(upsgi.logic_opt_arg);
		upsgi.logic_opt_cycles++;
		upsgi.logic_opt_running = 1;
		upsgi.logic_opt(key, value);
		upsgi.logic_opt_running = 0;
		return;
	}

add:

	if (!upsgi.exported_opts) {
		upsgi.exported_opts = upsgi_malloc(sizeof(struct upsgi_opt *));
	}
	else {
		upsgi.exported_opts = realloc(upsgi.exported_opts, sizeof(struct upsgi_opt *) * (upsgi.exported_opts_cnt + 1));
		if (!upsgi.exported_opts) {
			upsgi_error("realloc()");
			exit(1);
		}
	}

	int id = upsgi.exported_opts_cnt;
	upsgi.exported_opts[id] = upsgi_malloc(sizeof(struct upsgi_opt));
	upsgi.exported_opts[id]->key = key;
	upsgi.exported_opts[id]->value = value;
	upsgi.exported_opts[id]->configured = configured;
	upsgi.exported_opts_cnt++;
	upsgi.dirty_config = 1;

	if (placeholder_only) {
		if (upsgi_opt_exists(key)) {
			upsgi_log("you cannot use %s as a placeholder, it is already available as an option\n");
			exit(1);
		}
		upsgi.exported_opts[id]->configured = 1;
		return;
	}

	struct upsgi_option *op = upsgi_opt_get(key);
	if (op) {
		// requires master ?
		if (op->flags & UPSGI_OPT_MASTER) {
			upsgi.master_process = 1;
		}
		// requires log_master ?
		if (op->flags & UPSGI_OPT_LOG_MASTER) {
			upsgi.master_process = 1;
			upsgi.log_master = 1;
		}
		if (op->flags & UPSGI_OPT_REQ_LOG_MASTER) {
			upsgi.master_process = 1;
			upsgi.log_master = 1;
			upsgi.req_log_master = 1;
		}
		// requires threads ?
		if (op->flags & UPSGI_OPT_THREADS) {
			upsgi.has_threads = 1;
		}
		// requires cheaper mode ?
		if (op->flags & UPSGI_OPT_CHEAPER) {
			upsgi.cheaper = 1;
		}
		// requires virtualhosting ?
		if (op->flags & UPSGI_OPT_VHOST) {
			upsgi.vhost = 1;
		}
		// requires memusage ?
		if (op->flags & UPSGI_OPT_MEMORY) {
			upsgi.force_get_memusage = 1;
		}
		// requires auto procname ?
		if (op->flags & UPSGI_OPT_PROCNAME) {
			upsgi.auto_procname = 1;
		}
		// requires lazy ?
		if (op->flags & UPSGI_OPT_LAZY) {
			upsgi.lazy = 1;
		}
		// requires no_initial ?
		if (op->flags & UPSGI_OPT_NO_INITIAL) {
			upsgi.no_initial_output = 1;
		}
		// requires no_server ?
		if (op->flags & UPSGI_OPT_NO_SERVER) {
			upsgi.no_server = 1;
		}
		// requires post_buffering ?
		if (op->flags & UPSGI_OPT_POST_BUFFERING) {
			if (!upsgi.post_buffering)
				upsgi.post_buffering = 4096;
		}
		// requires building mime dict ?
		if (op->flags & UPSGI_OPT_MIME) {
			upsgi.build_mime_dict = 1;
		}
		// enable metrics ?
		if (op->flags & UPSGI_OPT_METRICS) {
                        upsgi.has_metrics = 1;
                }
		// immediate ?
		if (op->flags & UPSGI_OPT_IMMEDIATE) {
			op->func(key, value, op->data);
			upsgi.exported_opts[id]->configured = 1;
		}
	}
}

void upsgi_fallback_config() {
	if (upsgi.fallback_config && upsgi.last_exit_code == 1) {
		upsgi_log_verbose("!!! %s (pid: %d) exited with status %d !!!\n", upsgi.binary_path, (int) getpid(), upsgi.last_exit_code);
		upsgi_log_verbose("!!! Fallback config to %s !!!\n", upsgi.fallback_config);
		char *argv[3];
		argv[0] = upsgi.binary_path;
		argv[1] = upsgi.fallback_config;
		argv[2] = NULL;
        	execvp(upsgi.binary_path, argv);
        	upsgi_error("execvp()");
        	// never here
	}
}

int upsgi_manage_opt(char *key, char *value) {

        struct upsgi_option *op = upsgi_opt_get(key);
	if (op) {
        	op->func(key, value, op->data);
                return 1;
        }
        return 0;

}

void upsgi_configure() {

        int i;

        // and now apply the remaining configs
restart:
        for (i = 0; i < upsgi.exported_opts_cnt; i++) {
                if (upsgi.exported_opts[i]->configured)
                        continue;
                upsgi.dirty_config = 0;
                upsgi.exported_opts[i]->configured = upsgi_manage_opt(upsgi.exported_opts[i]->key, upsgi.exported_opts[i]->value);
		// some option could cause a dirty config tree
                if (upsgi.dirty_config)
                        goto restart;
        }

}


void upsgi_opt_custom(char *key, char *value, void *data ) {
        struct upsgi_custom_option *uco = (struct upsgi_custom_option *)data;
        size_t i, count = 1;
        size_t value_len = 0;
        if (value)
                value_len = strlen(value);
        off_t pos = 0;
        char **opt_argv;
        char *tmp_val = NULL, *p = NULL;

        // now count the number of args
        for (i = 0; i < value_len; i++) {
                if (value[i] == ' ') {
                        count++;
                }
        }

        // allocate a tmp array
        opt_argv = upsgi_calloc(sizeof(char *) * count);
        //make a copy of the value;
        if (value_len > 0) {
                tmp_val = upsgi_str(value);
                // fill the array of options
                char *p, *ctx = NULL;
                upsgi_foreach_token(tmp_val, " ", p, ctx) {
                        opt_argv[pos] = p;
                        pos++;
                }
        }
        else {
                // no argument specified
                opt_argv[0] = "";
        }

#ifdef UPSGI_DEBUG
        upsgi_log("found custom option %s with %d args\n", key, count);
#endif

        // now make a copy of the option template
        char *tmp_opt = upsgi_str(uco->value);
        // split it
        char *ctx = NULL;
        upsgi_foreach_token(tmp_opt, ";", p, ctx) {
                char *equal = strchr(p, '=');
                if (!equal)
                        goto clear;
                *equal = '\0';

                // build the key
                char *new_key = upsgi_str(p);
                for (i = 0; i < count; i++) {
                        char *old_key = new_key;
                        char *tmp_num = upsgi_num2str(i + 1);
                        char *placeholder = upsgi_concat2((char *) "$", tmp_num);
                        free(tmp_num);
                        new_key = upsgi_substitute(old_key, placeholder, opt_argv[i]);
                        if (new_key != old_key)
                                free(old_key);
                        free(placeholder);
                }

                // build the value
                char *new_value = upsgi_str(equal + 1);
                for (i = 0; i < count; i++) {
                        char *old_value = new_value;
                        char *tmp_num = upsgi_num2str(i + 1);
                        char *placeholder = upsgi_concat2((char *) "$", tmp_num);
                        free(tmp_num);
                        new_value = upsgi_substitute(old_value, placeholder, opt_argv[i]);
                        if (new_value != old_value)
                                free(old_value);
                        free(placeholder);
                }
                // ignore return value here
                upsgi_manage_opt(new_key, new_value);
        }

clear:
        free(tmp_val);
        free(tmp_opt);
        free(opt_argv);

}

char *upsgi_get_exported_opt(char *key) {

        int i;

        for (i = 0; i < upsgi.exported_opts_cnt; i++) {
                if (!strcmp(upsgi.exported_opts[i]->key, key)) {
                        return upsgi.exported_opts[i]->value;
                }
        }

        return NULL;
}

char *upsgi_get_optname_by_index(int index) {

        struct upsgi_option *op = upsgi.options;

        while (op->name) {
                if (op->shortcut == index) {
                        return op->name;
                }
                op++;
        }

        return NULL;
}

/*

	this works as a pipeline

	processes = 2
	cpu_cores = 8
	foobar = %(processes cpu_cores + 2)

	translate as:

		step1 = processes cpu_cores = 2 8 = 28 (string concatenation)

		step1 + = step1_apply_func_plus (func token)

		step1_apply_func_plus 2 = 28 + 2 = 30 (math)

*/

char *upsgi_manage_placeholder(char *key) {
	enum {
		concat = 0,
		sum,
		sub,
		mul,
		div,
	} state;

	state = concat;
	char *current_value = NULL;

	char *space = strchr(key, ' ');
	if (!space) {
		return upsgi_get_exported_opt(key);
	}
	// let's start the heavy metal here
	char *tmp_value = upsgi_str(key);
	char *p, *ctx = NULL;
        upsgi_foreach_token(tmp_value, " ", p, ctx) {
		char *value = NULL;
		if (is_a_number(p)) {
			value = upsgi_str(p);
		}
		else if (!strcmp(p, "+")) {
			state = sum;
			continue;
		}
		else if (!strcmp(p, "-")) {
			state = sub;
			continue;
		}
		else if (!strcmp(p, "*")) {
			state = mul;
			continue;
		}
		else if (!strcmp(p, "/")) {
			state = div;
			continue;
		}
		else if (!strcmp(p, "++")) {
			if (current_value) {
				int64_t tmp_num = strtoll(current_value, NULL, 10);
				free(current_value);
				current_value = upsgi_64bit2str(tmp_num+1);
			}
			state = concat;
			continue;
		}
		else if (!strcmp(p, "--")) {
			if (current_value) {
				int64_t tmp_num = strtoll(current_value, NULL, 10);
				free(current_value);
				current_value = upsgi_64bit2str(tmp_num-1);
			}
			state = concat;
			continue;
		}
		// find the option
		else {
			char *ov = upsgi_get_exported_opt(p);
			if (!ov) ov = "";
			value = upsgi_str(ov);
		}

		int64_t arg1n = 0, arg2n = 0;
		char *arg1 = "", *arg2 = "";	

		switch(state) {
			case concat:
				if (current_value) arg1 = current_value;
				if (value) arg2 = value;
				char *ret = upsgi_concat2(arg1, arg2);
				if (current_value) free(current_value);
				current_value = ret;	
				break;
			case sum:
				if (current_value) arg1n = strtoll(current_value, NULL, 10);
				if (value) arg2n = strtoll(value, NULL, 10);
				if (current_value) free(current_value);
				current_value = upsgi_64bit2str(arg1n + arg2n);
				break;
			case sub:
				if (current_value) arg1n = strtoll(current_value, NULL, 10);
				if (value) arg2n = strtoll(value, NULL, 10);
				if (current_value) free(current_value);
				current_value = upsgi_64bit2str(arg1n - arg2n);
				break;
			case mul:
				if (current_value) arg1n = strtoll(current_value, NULL, 10);
				if (value) arg2n = strtoll(value, NULL, 10);
				if (current_value) free(current_value);
				current_value = upsgi_64bit2str(arg1n * arg2n);
				break;
			case div:
				if (current_value) arg1n = strtoll(current_value, NULL, 10);
				if (value) arg2n = strtoll(value, NULL, 10);
				if (current_value) free(current_value);
				// avoid division by zero
				if (arg2n == 0) {
					current_value = upsgi_64bit2str(0);
				}
				else {
					current_value = upsgi_64bit2str(arg1n / arg2n);
				}
				break;
			default:
				break;
		}

		// over engineering
		if (value)
			free(value);

		// reset state to concat
		state = concat;
	}
	free(tmp_value);

	return current_value;
}

void upsgi_opt_resolve(char *opt, char *value, void *foo) {
        char *equal = strchr(value, '=');
        if (!equal) {
                upsgi_log("invalid resolve syntax, must be placeholder=domain\n");
                exit(1);
        }
        char *ip = upsgi_resolve_ip(equal+1);
        if (!ip) {
		upsgi_log("unable to resolve name %s\n", equal+1);
                upsgi_error("upsgi_resolve_ip()");
                exit(1);
        }
        char *new_opt = upsgi_concat2n(value, (equal-value)+1, ip, strlen(ip));
        upsgi_opt_set_placeholder(opt, new_opt, (void *) 1);
}
