#include "upsgi.h"

extern struct upsgi_server upsgi;

#ifdef UPSGI_ELF
static void upsgi_plugin_parse_section(char *filename) {
	size_t s_len = 0;
	char *buf = upsgi_elf_section(filename, "upsgi", &s_len);
	if (buf) {
		char *ctx = NULL;
		char *p = strtok_r(buf, "\n", &ctx);
		while (p) {
			char *equal = strchr(p, '=');
			if (equal) {
				*equal = 0;
				if (!strcmp(p, "requires")) {
					if (!plugin_already_loaded(equal+1)) {	
						upsgi_load_plugin(-1, equal + 1, NULL);
					}
				}
			}
			p = strtok_r(NULL, "\n", &ctx);
		}
		free(buf);
	}
}
#endif

struct upsgi_plugin *upsgi_plugin_get(const char *plugin) {
	int i;

	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->name) {
			if (!strcmp(plugin, upsgi.p[i]->name)) {
#ifdef UPSGI_DEBUG
				upsgi_log("%s plugin already available\n", plugin);
#endif
				return upsgi.p[i];
			}
		}
		if (upsgi.p[i]->alias) {
			if (!strcmp(plugin, upsgi.p[i]->alias)) {
#ifdef UPSGI_DEBUG
				upsgi_log("%s plugin already available\n", plugin);
#endif
				return upsgi.p[i];
			}
		}
	}

	for (i = 0; i < upsgi.gp_cnt; i++) {

		if (upsgi.gp[i]->name) {
			if (!strcmp(plugin, upsgi.gp[i]->name)) {
#ifdef UPSGI_DEBUG
				upsgi_log("%s plugin already available\n", plugin);
#endif
				return upsgi.p[i];
			}
		}
		if (upsgi.gp[i]->alias) {
			if (!strcmp(plugin, upsgi.gp[i]->alias)) {
#ifdef UPSGI_DEBUG
				upsgi_log("%s plugin already available\n", plugin);
#endif
				return upsgi.p[i];
			}
		}
	}

	return NULL;
}

int plugin_already_loaded(const char *plugin) {
	struct upsgi_plugin *up = upsgi_plugin_get(plugin);
	if (up) return 1;
	return 0;
}


void *upsgi_load_plugin(int modifier, char *plugin, char *has_option) {

	void *plugin_handle = NULL;
	char *plugin_abs_path = NULL;
	char *plugin_filename = NULL;

	int need_free = 0;
	char *plugin_name = upsgi_strip(upsgi_str(plugin));
	char *plugin_symbol_name_start;

	struct upsgi_plugin *up;
	char linkpath_buf[1024], linkpath[1024];
	int linkpath_size;

	char *colon = strchr(plugin_name, ':');
	if (colon) {
		colon[0] = 0;
		modifier = atoi(plugin_name);
		plugin_name = colon + 1;
		colon[0] = ':';
	}

	char *init_func = strchr(plugin_name, '|');
	if (init_func) {
		init_func[0] = 0;
		init_func++;	
	}

	if (!upsgi_endswith(plugin_name, "_plugin.so")) {
		plugin_name = upsgi_concat2(plugin_name, "_plugin.so");
		need_free = 1;
	}

	plugin_symbol_name_start = plugin_name;

	// step 1: check for absolute plugin (stop if it fails)
	if (strchr(plugin_name, '/')) {
#ifdef UPSGI_ELF
		upsgi_plugin_parse_section(plugin_name);
#endif
		plugin_handle = dlopen(plugin_name, RTLD_NOW | RTLD_GLOBAL);
		if (!plugin_handle) {
			if (!has_option)
				upsgi_log("%s\n", dlerror());
			goto end;
		}
		plugin_symbol_name_start = upsgi_get_last_char(plugin_name, '/');
		plugin_symbol_name_start++;
		plugin_abs_path = plugin_name;
		goto success;
	}

	// step dir, check for user-supplied plugins directory
	struct upsgi_string_list *pdir = upsgi.plugins_dir;
	while (pdir) {
		plugin_filename = upsgi_concat3(pdir->value, "/", plugin_name);
#ifdef UPSGI_ELF
		upsgi_plugin_parse_section(plugin_filename);
#endif
		plugin_handle = dlopen(plugin_filename, RTLD_NOW | RTLD_GLOBAL);
		if (plugin_handle) {
			plugin_abs_path = plugin_filename;
			//free(plugin_filename);
			goto success;
		}
		free(plugin_filename);
		plugin_filename = NULL;
		pdir = pdir->next;
	}

	// last step: search in compile-time plugin_dir
	if (!plugin_handle) {
		plugin_filename = upsgi_concat3(UPSGI_PLUGIN_DIR, "/", plugin_name);
#ifdef UPSGI_ELF
		upsgi_plugin_parse_section(plugin_filename);
#endif
		plugin_handle = dlopen(plugin_filename, RTLD_NOW | RTLD_GLOBAL);
		plugin_abs_path = plugin_filename;
		//free(plugin_filename);
	}

success:
	if (!plugin_handle) {
		if (!has_option)
			upsgi_log("!!! UNABLE to load upsgi plugin: %s !!!\n", dlerror());
	}
	else {
		if (init_func) {
			void (*plugin_init_func)() = dlsym(plugin_handle, init_func);
			if (plugin_init_func) {
				plugin_init_func();
			}
		}
		char *plugin_entry_symbol = upsgi_concat2n(plugin_symbol_name_start, strlen(plugin_symbol_name_start) - 3, "", 0);
		up = dlsym(plugin_handle, plugin_entry_symbol);
		if (!up) {
			// is it a link ?
			memset(linkpath_buf, 0, 1024);
			memset(linkpath, 0, 1024);
			if ((linkpath_size = readlink(plugin_abs_path, linkpath_buf, 1023)) > 0) {
				do {
					linkpath_buf[linkpath_size] = '\0';
					strncpy(linkpath, linkpath_buf, linkpath_size + 1);
				} while ((linkpath_size = readlink(linkpath, linkpath_buf, 1023)) > 0);
#ifdef UPSGI_DEBUG
				upsgi_log("%s\n", linkpath);
#endif
				free(plugin_entry_symbol);
				char *slash = upsgi_get_last_char(linkpath, '/');
				if (!slash) {
					slash = linkpath;
				}
				else {
					slash++;
				}
				plugin_entry_symbol = upsgi_concat2n(slash, strlen(slash) - 3, "", 0);
				up = dlsym(plugin_handle, plugin_entry_symbol);
			}
		}
		if (up) {
			if (!up->name) {
				upsgi_log("the loaded plugin (%s) has no .name attribute\n", plugin_name);
				if (dlclose(plugin_handle)) {
					upsgi_error("dlclose()");
				}
				if (need_free)
					free(plugin_name);
				if (plugin_filename)
					free(plugin_filename);
				free(plugin_entry_symbol);
				return NULL;
			}
			if (plugin_already_loaded(up->name)) {
				if (dlclose(plugin_handle)) {
					upsgi_error("dlclose()");
				}
				if (need_free)
					free(plugin_name);
				if (plugin_filename)
					free(plugin_filename);
				free(plugin_entry_symbol);
				return NULL;
			}
			if (has_option) {
				struct upsgi_option *op = up->options;
				int found = 0;
				while (op && op->name) {
					if (!strcmp(has_option, op->name)) {
						found = 1;
						break;
					}
					op++;
				}
				if (!found) {
					if (dlclose(plugin_handle)) {
						upsgi_error("dlclose()");
					}
					if (need_free)
						free(plugin_name);
					if (plugin_filename)
						free(plugin_filename);
					free(plugin_entry_symbol);
					return NULL;
				}

			}
			if (modifier != -1) {
				fill_plugin_table(modifier, up);
				up->modifier1 = modifier;
			}
			else {
				fill_plugin_table(up->modifier1, up);
			}
			if (need_free)
				free(plugin_name);
			if (plugin_filename)
				free(plugin_filename);
			free(plugin_entry_symbol);

			if (up->on_load)
				up->on_load();
			return plugin_handle;
		}
		if (!has_option)
			upsgi_log("%s\n", dlerror());
	}

end:
	if (need_free)
		free(plugin_name);
	if (plugin_filename)
		free(plugin_filename);

	return NULL;
}

int upsgi_try_autoload(char *option) {
	DIR *d;
	struct dirent *dp;
	// step dir, check for user-supplied plugins directory
	struct upsgi_string_list *pdir = upsgi.plugins_dir;
	while (pdir) {
		d = opendir(pdir->value);
		if (d) {
			while ((dp = readdir(d)) != NULL) {
				if (upsgi_endswith(dp->d_name, "_plugin.so")) {
					char *filename = upsgi_concat3(pdir->value, "/", dp->d_name);
					if (upsgi_load_plugin(-1, filename, option)) {
						upsgi_log("option \"%s\" found in plugin %s\n", option, filename);
						free(filename);
						closedir(d);
						// add new options
						build_options();
						return 1;
					}
					free(filename);
				}
			}
			closedir(d);
		}
		pdir = pdir->next;
	}

	// last step: search in compile-time plugin_dir
	d = opendir(UPSGI_PLUGIN_DIR);
	if (!d)
		return 0;

	while ((dp = readdir(d)) != NULL) {
		if (upsgi_endswith(dp->d_name, "_plugin.so")) {
			char *filename = upsgi_concat3(UPSGI_PLUGIN_DIR, "/", dp->d_name);
			if (upsgi_load_plugin(-1, filename, option)) {
				upsgi_log("option \"%s\" found in plugin %s\n", option, filename);
				free(filename);
				closedir(d);
				// add new options
				build_options();
				return 1;
			}
			free(filename);
		}
	}

	closedir(d);

	return 0;

}
