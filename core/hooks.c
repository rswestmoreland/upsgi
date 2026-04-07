#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	advanced (pluggable) hooks

	they are executed before the other hooks, and can be extended by plugins

	if a plugin tries to register an hook with a name already available in the list, its function
	will be overridden

*/

struct upsgi_hook *upsgi_hook_by_name(char *name) {
	struct upsgi_hook *uh = upsgi.hooks;
	while(uh) {
		if (!strcmp(uh->name, name)) {
			return uh;
		}
		uh = uh->next;
	}
	return NULL;
}

void upsgi_register_hook(char *name, int (*func)(char *)) {
	struct upsgi_hook *old_uh = NULL, *uh = upsgi.hooks;
        while(uh) {
                if (!strcmp(uh->name, name)) {
                        uh->func = func;
			return;
                }
		old_uh = uh;
		uh = uh->next;
        }

	uh = upsgi_calloc(sizeof(struct upsgi_hook));
	uh->name = name;
	uh->func = func;

	if (old_uh) {
		old_uh->next = uh;
	}
	else {
		upsgi.hooks = uh;
	}
}

static int upsgi_hook_alarm(char *arg) {
	char *space = strchr(arg,' ');
	if (!space) {
		upsgi_log("invalid alarm hook syntax, must be: <alarm> <msg>\n");
		return -1;
	}
	*space = 0;
	upsgi_alarm_trigger(arg, space+1,  strlen(space+1));
	*space = ' ';
	return 0;
}

static int upsgi_hook_chdir(char *arg) {
	int ret = chdir(arg);
	if (ret) {
		upsgi_error("upsgi_hook_chdir()");
	}
	return ret;
}

static int upsgi_hook_mkdir(char *arg) {
        int ret = mkdir(arg, 0777);
        if (ret) {
                upsgi_error("upsgi_hook_mkdir()");
        }
        return ret;
}

static int upsgi_hook_putenv(char *arg) {
        int ret = putenv(arg);
        if (ret) {
                upsgi_error("upsgi_hook_putenv()");
        }
        return ret;
}

static int upsgi_hook_exec(char *arg) {
	int ret = upsgi_run_command_and_wait(NULL, arg);
	if (ret != 0) {
        	upsgi_log("command \"%s\" exited with non-zero code: %d\n", arg, ret);
        }
	return ret;
}

static int upsgi_hook_safeexec(char *arg) {
        int ret = upsgi_run_command_and_wait(NULL, arg);
        if (ret != 0) {
                upsgi_log("command \"%s\" exited with non-zero code: %d\n", arg, ret);
        }
        return 0;
}

static int upsgi_hook_exit(char *arg) {
	int exit_code = 0;
	if (strlen(arg) > 1) {
		exit_code = atoi(arg);
	}
	exit(exit_code);
}

static int upsgi_hook_print(char *arg) {
	char *line = upsgi_concat2(arg, "\n");
	upsgi_log(line);
	free(line);
	return 0;
}

static int upsgi_hook_unlink(char *arg) {
	int ret = unlink(arg);
	if (ret) {
		upsgi_error("upsgi_hook_unlink()/unlink()");
	}
	return ret;
}

static int upsgi_hook_writefifo(char *arg) {
	char *space = strchr(arg, ' ');
	if (!space) {
		upsgi_log("invalid hook writefifo syntax, must be: <file> <string>\n");
		return -1;
	}
	*space = 0;
	int fd = open(arg, O_WRONLY|O_NONBLOCK);
	if (fd < 0) {
		upsgi_error_open(arg);
		*space = ' ';
		if (errno == ENODEV) return 0;
#ifdef ENXIO
		if (errno == ENXIO) return 0;
#endif
		return -1;
	}
	*space = ' ';
	size_t l = strlen(space+1);
	if (write(fd, space+1, l) != (ssize_t) l) {
		upsgi_error("upsgi_hook_writefifo()/write()");
		close(fd);
		return -1;
	}
	close(fd);
        return 0;
}

static int upsgi_hook_write(char *arg) {
        char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook write syntax, must be: <file> <string>\n");
                return -1;
        }
        *space = 0;
        int fd = open(arg, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if (fd < 0) {
                upsgi_error_open(arg);
                *space = ' ';
                return -1;
        }
        *space = ' ';
        size_t l = strlen(space+1);
        if (write(fd, space+1, l) != (ssize_t) l) {
                upsgi_error("upsgi_hook_write()/write()");
                close(fd);
                return -1;
        }
        close(fd);
        return 0;
}

static int upsgi_hook_creat(char *arg) {
        int fd = open(arg, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if (fd < 0) {
                upsgi_error_open(arg);
                return -1;
        }
        close(fd);
        return 0;
}


static int upsgi_hook_append(char *arg) {
        char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook append syntax, must be: <file> <string>\n");
                return -1;
        }
        *space = 0;
        int fd = open(arg, O_WRONLY|O_CREAT|O_APPEND, 0666);
        if (fd < 0) {
                upsgi_error_open(arg);
                *space = ' ';
                return -1;
        }
        *space = ' ';
        size_t l = strlen(space+1);
        if (write(fd, space+1, l) != (ssize_t) l) {
                upsgi_error("upsgi_hook_append()/write()");
                close(fd);
                return -1;
        }
        close(fd);
        return 0;
}


static int upsgi_hook_writen(char *arg) {
        char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook writen syntax, must be: <file> <string>\n");
                return -1;
        }
        *space = 0;
        int fd = open(arg, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if (fd < 0) {
                upsgi_error_open(arg);
                *space = ' ';
                return -1;
        }
        *space = ' ';
        size_t l = strlen(space+1);
	char *buf = upsgi_malloc(l + 1);
	memcpy(buf, space+1, l);
	buf[l] = '\n';
        if (write(fd, buf, l+1) != (ssize_t) (l+1)) {
                upsgi_error("upsgi_hook_writen()/write()");
		free(buf);
                close(fd);
                return -1;
        }
	free(buf);
        close(fd);
        return 0;
}

static int upsgi_hook_appendn(char *arg) {
        char *space = strchr(arg, ' ');
	if (space)
        	*space = 0;
        int fd = open(arg, O_WRONLY|O_CREAT|O_APPEND, 0666);
        if (fd < 0) {
                upsgi_error_open(arg);
		if (space)
                	*space = ' ';
                return -1;
        }
	if (!space) {
		// simple newline
		if (write(fd, "\n", 1) != 1) {
                	upsgi_error("upsgi_hook_appendn()/write()");
			close(fd);
			return -1;
		}
		close(fd);
		return 0;
	}

        *space = ' ';
        size_t l = strlen(space+1);
        char *buf = upsgi_malloc(l + 1);
	memcpy(buf, space+1, l);
        buf[l] = '\n';
        if (write(fd, buf, l+1) != (ssize_t) (l+1)) {
                upsgi_error("upsgi_hook_appendn()/write()");
                free(buf);
                close(fd);
                return -1;
        }
        free(buf);
        close(fd);
        return 0;
}



static int upsgi_hook_chmod(char *arg) {
	char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook chmod syntax, must be: <file> <mode>\n");
                return -1;
        }
        *space = 0;
	int error = 0;
	mode_t mask = upsgi_mode_t(space+1, &error);
	if (error) {
		upsgi_log("invalid hook chmod mask: %s\n", space+1); 
		*space = ' ';
		return -1;
	}

	int ret = chmod(arg, mask);
	*space = ' ';
	if (ret) {
		upsgi_error("upsgi_hook_chmod()/chmod()");
	}
	return ret;
}

static int upsgi_hook_sticky(char *arg) {
	struct stat st;
	if (stat(arg, &st)) {
                upsgi_error("upsgi_hook_sticky()/stat()");
		return -1;
	}
        if (chmod(arg, st.st_mode | S_ISVTX)) {
                upsgi_error("upsgi_hook_sticky()/chmod()");
		return -1;
        }
        return 0;
}


static int upsgi_hook_chown(char *arg) {
        char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook chown syntax, must be: <file> <uid> <gid>\n");
                return -1;
        }
        *space = 0;

	char *space2 = strchr(space+1, ' ');
	if (!space2) {
		*space = ' ';
                upsgi_log("invalid hook chown syntax, must be: <file> <uid> <gid>\n");
                return -1;
	}
	*space2 = 0;

	struct passwd *pw = getpwnam(space+1);
	if (!pw) {
		upsgi_log("unable to find uid %s\n", space+1);
		*space = ' ';
		*space2 = ' ';
		return -1;
	}

	struct group *gr = getgrnam(space2+1);
	if (!gr) {
                upsgi_log("unable to find gid %s\n", space2+1);
                *space = ' ';
                *space2 = ' ';
                return -1;
        }
        int ret = chown(arg, pw->pw_uid, gr->gr_gid);
        *space = ' ';
        *space2 = ' ';
        if (ret) {
                upsgi_error("upsgi_hook_chown()/chown)");
        }
        return ret;
}

static int upsgi_hook_chown2(char *arg) {
        char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook chown2 syntax, must be: <file> <uid> <gid>\n");
                return -1;
        }
        *space = 0;

        char *space2 = strchr(space+1, ' ');
        if (!space2) {
                *space = ' ';
                upsgi_log("invalid hook chown2 syntax, must be: <file> <uid> <gid>\n");
                return -1;
        }
        *space2 = 0;

	if (!is_a_number(space+1)) {
		upsgi_log("invalid hook chown2 syntax, uid must be a number\n");
		*space = ' ';
		*space2 = ' ';
		return -1;
	}

	if (!is_a_number(space2+1)) {
                upsgi_log("invalid hook chown2 syntax, gid must be a number\n");
                *space = ' ';
                *space2 = ' ';
                return -1;
        }

        int ret = chown(arg, atoi(space+1), atoi(space2+1));
        *space = ' ';
        *space2 = ' ';
        if (ret) {
                upsgi_error("upsgi_hook_chown2()/chown)");
        }
        return ret;
}


#if defined(UPSGI_SUNOS_EXTERN_SETHOSTNAME)
extern int sethostname(char *, int);
#endif

static int upsgi_hook_hostname(char *arg) {
#ifdef __CYGWIN__
	return -1;
#else
	return sethostname(arg, strlen(arg));
#endif
}

static int upsgi_hook_unix_signal(char *arg) {
	char *space = strchr(arg, ' ');
	if (!space) {
		upsgi_log("invalid unix_signal syntax, must be <signum> <func>\n");
		return -1;
	}
	*space = 0;
	int signum = atoi(arg);
	*space = ' ';
	void (*func)(int) = dlsym(RTLD_DEFAULT, space+1);
	if (!func) {
		upsgi_log("unable to find function \"%s\"\n", space+1);
		return -1;
	}
	upsgi_unix_signal(signum, func);
	return 0;
}


static int upsgi_hook_callint(char *arg) {
        char *space = strchr(arg, ' ');
        if (space) {
                *space = 0;
                int num = atoi(space+1);
                void (*func)(int) = dlsym(RTLD_DEFAULT, arg);
                if (!func) {
                	upsgi_log("unable to call function \"%s(%d)\"\n", arg, num);
                        *space = ' ';
                        return -1;
		}
                *space = ' ';
                func(num);
        }
        else {
                void (*func)(void) = dlsym(RTLD_DEFAULT, arg);
                if (!func) {
                        upsgi_log("unable to call function \"%s\"\n", arg);
                        return -1;
                }
                func();
        }
        return 0;
}


static int upsgi_hook_call(char *arg) {
	char *space = strchr(arg, ' ');
	if (space) {
		*space = 0;
		void (*func)(char *) = dlsym(RTLD_DEFAULT, arg);
                if (!func) {
                	upsgi_log("unable to call function \"%s(%s)\"\n", arg, space + 1);
			*space = ' ';
			return -1;
		}
		*space = ' ';
                func(space + 1);
	}
	else {
		void (*func)(void) = dlsym(RTLD_DEFAULT, arg);
                if (!func) {
                	upsgi_log("unable to call function \"%s\"\n", arg);
			return -1;
		}
                func();
	}
	return 0;
}

static int upsgi_hook_callintret(char *arg) {
        char *space = strchr(arg, ' ');
        if (space) {
                *space = 0;
                int num = atoi(space+1);
                int (*func)(int) = dlsym(RTLD_DEFAULT, arg);
                if (!func) {
                        upsgi_log("unable to call function \"%s(%d)\"\n", arg, num);
                        *space = ' ';
                        return -1;
                }
                *space = ' ';
                return func(num);
        }
        int (*func)(void) = dlsym(RTLD_DEFAULT, arg);
        if (!func) {
        	upsgi_log("unable to call function \"%s\"\n", arg);
                return -1;
        }
	return func();
}


static int upsgi_hook_callret(char *arg) {
        char *space = strchr(arg, ' ');
        if (space) {
                *space = 0;
                int (*func)(char *) = dlsym(RTLD_DEFAULT, arg);
                if (!func) {
                        upsgi_log("unable to call function \"%s(%s)\"\n", arg, space + 1);
                        *space = ' ';
                        return -1;
                }
                *space = ' ';
                return func(space + 1);
        }
        int (*func)(void) = dlsym(RTLD_DEFAULT, arg);
        if (!func) {
        	upsgi_log("unable to call function \"%s\"\n", arg);
                return -1;
        }
        return func();
}

static int upsgi_hook_rpc(char *arg) {

	int ret = -1;
	size_t i, argc = 0;
        char **rargv = upsgi_split_quoted(arg, strlen(arg), " \t", &argc);
        if (!argc) goto end;
	if (argc > 256) goto destroy;

        char *argv[256];
        uint16_t argvs[256];

        char *node = NULL;
        char *func = rargv[0];

	char *at = strchr(func, '@');
	if (at) {
		*at = 0;
		node = at + 1;
	}

        for(i=0;i<(argc-1);i++) {
		size_t a_len = strlen(rargv[i+1]);
		if (a_len > 0xffff) goto destroy;
                argv[i] = rargv[i+1] ;
                argvs[i] = a_len;
        }

        uint64_t size = 0;
        // response must be always freed
        char *response = upsgi_do_rpc(node, func, argc-1, argv, argvs, &size);
        if (response) {
		if (at) *at = '@';
		upsgi_log("[rpc result from \"%s\"] %.*s\n", rargv[0], size, response);
                free(response);
		ret = 0;
        }

destroy:
        for(i=0;i<argc;i++) {
                free(rargv[i]);
        }
end:
	free(rargv);
	return ret;
}

static int upsgi_hook_retryrpc(char *arg) {
	for(;;) {
		int ret = upsgi_hook_rpc(arg);
		if (!ret) break;
		sleep(2);
	}
	return 0;
}

static int upsgi_hook_wait_for_fs(char *arg) {
	return upsgi_wait_for_fs(arg, 0);
}

static int upsgi_hook_wait_for_file(char *arg) {
	return upsgi_wait_for_fs(arg, 1);
}

static int upsgi_hook_wait_for_dir(char *arg) {
	return upsgi_wait_for_fs(arg, 2);
}

static int upsgi_hook_wait_for_socket(char *arg) {
	return upsgi_wait_for_socket(arg);
}

static int spinningfifo_hook(char *arg) {
        int fd;
        char *space = strchr(arg, ' ');
        if (!space) {
                upsgi_log("invalid hook spinningfifo syntax, must be: <file> <string>\n");
                return -1;
        }
        *space = 0;
retry:
        upsgi_log("waiting for %s ...\n", arg);
        fd = open(arg, O_WRONLY|O_NONBLOCK);
        if (fd < 0) {
                if (errno == ENODEV || errno == ENOENT) {
                        sleep(1);
                        goto retry;
                }
#ifdef ENXIO
                if (errno == ENXIO) {
                        sleep(1);
                        goto retry;
                }
#endif
                upsgi_error_open(arg);
                *space = ' ';
                return -1;
        }
        *space = ' ';
        size_t l = strlen(space+1);
        if (write(fd, space+1, l) != (ssize_t) l) {
                upsgi_error("spinningfifo_hook()/write()");
                close(fd);
                return -1;
        }
        close(fd);
        return 0;
}

void upsgi_register_base_hooks() {
	upsgi_register_hook("cd", upsgi_hook_chdir);
	upsgi_register_hook("chdir", upsgi_hook_chdir);

	upsgi_register_hook("mkdir", upsgi_hook_mkdir);
	upsgi_register_hook("putenv", upsgi_hook_putenv);
	upsgi_register_hook("chmod", upsgi_hook_chmod);
	upsgi_register_hook("chown", upsgi_hook_chown);
	upsgi_register_hook("chown2", upsgi_hook_chown2);

	upsgi_register_hook("sticky", upsgi_hook_sticky);

	upsgi_register_hook("exec", upsgi_hook_exec);
	upsgi_register_hook("safeexec", upsgi_hook_safeexec);

	upsgi_register_hook("create", upsgi_hook_creat);
	upsgi_register_hook("creat", upsgi_hook_creat);

	upsgi_register_hook("write", upsgi_hook_write);
	upsgi_register_hook("writen", upsgi_hook_writen);
	upsgi_register_hook("append", upsgi_hook_append);
	upsgi_register_hook("appendn", upsgi_hook_appendn);
	upsgi_register_hook("writefifo", upsgi_hook_writefifo);
	upsgi_register_hook("unlink", upsgi_hook_unlink);

	upsgi_register_hook("mount", upsgi_mount_hook);
	upsgi_register_hook("umount", upsgi_umount_hook);

	upsgi_register_hook("call", upsgi_hook_call);
	upsgi_register_hook("callret", upsgi_hook_callret);

	upsgi_register_hook("callint", upsgi_hook_callint);
	upsgi_register_hook("callintret", upsgi_hook_callintret);

	upsgi_register_hook("hostname", upsgi_hook_hostname);

	upsgi_register_hook("alarm", upsgi_hook_alarm);

	upsgi_register_hook("rpc", upsgi_hook_rpc);
	upsgi_register_hook("retryrpc", upsgi_hook_retryrpc);

	upsgi_register_hook("wait_for_fs", upsgi_hook_wait_for_fs);
	upsgi_register_hook("wait_for_file", upsgi_hook_wait_for_file);
	upsgi_register_hook("wait_for_dir", upsgi_hook_wait_for_dir);

	upsgi_register_hook("wait_for_socket", upsgi_hook_wait_for_socket);

	upsgi_register_hook("unix_signal", upsgi_hook_unix_signal);

	upsgi_register_hook("spinningfifo", spinningfifo_hook);

	// for testing
	upsgi_register_hook("exit", upsgi_hook_exit);
	upsgi_register_hook("print", upsgi_hook_print);
	upsgi_register_hook("log", upsgi_hook_print);
}

int upsgi_hooks_run_and_return(struct upsgi_string_list *l, char *phase, char *context, int fatal) {
	int final_ret = 0;
	struct upsgi_string_list *usl = NULL;
	if (context) {
		if (setenv("UPSGI_HOOK_CONTEXT", context, 1)) {
			upsgi_error("upsgi_hooks_run_and_return()/setenv()");
			return -1;
		}
	}
        upsgi_foreach(usl, l) {
                char *colon = strchr(usl->value, ':');
                if (!colon) {
                        upsgi_log("invalid hook syntax, must be hook:args\n");
                        exit(1);
                }
                *colon = 0;
                int private = 0;
                char *action = usl->value;
                // private hook ?
                if (action[0] == '!') {
                        action++;
                        private = 1;
                }
                struct upsgi_hook *uh = upsgi_hook_by_name(action);
                if (!uh) {
                        upsgi_log("hook action not found: %s\n", action);
                        exit(1);
                }
                *colon = ':';

                if (private) {
                        upsgi_log("running --- PRIVATE HOOK --- (%s)...\n", phase);
                }
                else {
                        upsgi_log("running \"%s\" (%s)...\n", usl->value, phase);
                }

                int ret = uh->func(colon+1);
		if (ret != 0) {
			if (fatal) {
				if (context) {
					unsetenv("UPSGI_HOOK_CONTEXT");
				}
				return ret;
			}
			final_ret = ret;
		}
        }

	if (context) {
		unsetenv("UPSGI_HOOK_CONTEXT");
	}

	return final_ret;
}

void upsgi_hooks_run(struct upsgi_string_list *l, char *phase, int fatal) {
	int ret = upsgi_hooks_run_and_return(l, phase, NULL, fatal);
	if (fatal && ret != 0) {
		upsgi_log_verbose("FATAL hook failed, destroying instance\n");
		if (upsgi.master_process) {
			if (upsgi.workers) {
				if (upsgi.workers[0].pid == getpid()) {
					kill_them_all(0);
					return;
				}
				else {
                                       	if (kill(upsgi.workers[0].pid, SIGINT)) {
						upsgi_error("upsgi_hooks_run()/kill()");
						exit(1);
					}
					return;
                               	}
			}
		}
		exit(1);
	}
}

#if defined(__linux__) && !defined(OBSOLETE_LINUX_KERNEL)
/*
this is a special hook, allowing the Emperor to enter a vassal
namespace and call hooks in its namespace context.
*/
void upsgi_hooks_setns_run(struct upsgi_string_list *l, pid_t pid, uid_t uid, gid_t gid) {
	int (*u_setns) (int, int) = (int (*)(int, int)) dlsym(RTLD_DEFAULT, "setns");
        if (!u_setns) {
                upsgi_log("your system misses setns() syscall !!!\n");
		return;
        }

	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, l) {
		// fist of all fork() the current process
		pid_t new_pid = fork();
		if (new_pid > 0) {
			// wait for its death
			int status;
			if (waitpid(new_pid, &status, 0) < 0) {
				upsgi_error("upsgi_hooks_setns_run()/waitpid()");
			}
		}
		else if (new_pid == 0) {
			// from now on, freeing memory is useless
			// now split args to know which namespaces to join
			char *action = strchr(usl->value, ' ');
			if (!action) {
				upsgi_log("invalid setns hook syntax, must be \"namespaces_list action:...\"\n");
				exit(1);
			}
			char *pidstr = upsgi_num2str(pid);
			char *uidstr = upsgi_num2str(uid);
			char *gidstr = upsgi_num2str(gid);

			char *namespaces = upsgi_concat2n(usl->value, action-usl->value, "", 0);
        		char *p, *ctx = NULL;
        		upsgi_foreach_token(namespaces, ",", p, ctx) {
				char *procfile = upsgi_concat4("/proc/", pidstr, "/ns/", p);
				int fd = open(procfile, O_RDONLY);
				if (fd < 0) {
					upsgi_error_open(procfile);
					exit(1);
				}
				if (u_setns(fd, 0) < 0){
					upsgi_error("upsgi_hooks_setns_run()/setns()");
					exit(1);
				}
				close(fd);
				free(procfile);
                	}

			if (setenv("UPSGI_VASSAL_PID", pidstr, 1)) {
				upsgi_error("upsgi_hooks_setns_run()/setenv()");
				exit(1);
			}

			if (setenv("UPSGI_VASSAL_UID", uidstr, 1)) {
				upsgi_error("upsgi_hooks_setns_run()/setenv()");
				exit(1);
			}

			if (setenv("UPSGI_VASSAL_GID", gidstr, 1)) {
				upsgi_error("upsgi_hooks_setns_run()/setenv()");
				exit(1);
			}

			// now run the action and then exit
			action++;
			char *colon = strchr(action, ':');
			if (!colon) {
				upsgi_log("invalid hook syntax must be action:arg\n");
				exit(1);
			}
			*colon = 0;
			struct upsgi_hook *uh = upsgi_hook_by_name(action);
                	if (!uh) {
                        	upsgi_log("hook action not found: %s\n", action);
                        	exit(1);
                	}
                	*colon = ':';

                        upsgi_log("running \"%s\" (setns)...\n", usl->value);
                	exit(uh->func(colon+1));
		}
		else {
			upsgi_error("upsgi_hooks_setns_run()/fork()");
		}
	}
}
#endif
