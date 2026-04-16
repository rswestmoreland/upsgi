#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	External upsgi daemons

	There are 3 kinds of daemons (read: external applications) that can be managed
	by upsgi.

	1) dumb daemons (attached with --attach-daemon)
		they must not daemonize() and when they exit() the master is notified (via waitpid)
		and the process is respawned
	2) smart daemons with daemonization
		you specify a pidfile and a command
		- on startup - if the pidfile does not exist or contains a non-available pid (checked with kill(pid, 0))
		  the daemon is respawned
		- on master check - if the pidfile does not exist or if it points to a non-existent pid
		  the daemon is respawned
	3) smart daemons without daemonization
		same as 2, but the daemonization and pidfile creation are managed by upsgi

	status:

	0 -> never started
	1 -> just started
	2 -> started and monitored (only in pidfile based)

*/

void upsgi_daemons_smart_check() {
	static time_t last_run = 0;

	time_t now = upsgi_now();

	if (now - last_run <= 0) {
		return;
	}

	last_run = now;

	struct upsgi_daemon *ud = upsgi.daemons;
	while (ud) {
		if (ud->pidfile) {
			int checked_pid = upsgi_check_pidfile(ud->pidfile);
			if (checked_pid <= 0) {
				// monitored instance
				if (ud->status == 2) {
					upsgi_spawn_daemon(ud);
				}
				else {
					ud->pidfile_checks++;
					if (ud->pidfile_checks >= (unsigned int) ud->freq) {
						if (!ud->has_daemonized) {
							upsgi_log_verbose("[upsgi-daemons] \"%s\" (pid: %d) did not daemonize !!!\n", ud->command, (int) ud->pid);
							ud->pidfile_checks = 0;
						}
						else {
							upsgi_log("[upsgi-daemons] found changed pidfile for \"%s\" (old_pid: %d new_pid: %d)\n", ud->command, (int) ud->pid, (int) checked_pid);
							upsgi_spawn_daemon(ud);
						}
					}
				}
			}
			else if (checked_pid != ud->pid) {
				upsgi_log("[upsgi-daemons] found changed pid for \"%s\" (old_pid: %d new_pid: %d)\n", ud->command, (int) ud->pid, (int) checked_pid);
				ud->pid = checked_pid;
			}
			// all ok, pidfile and process found
			else {
				ud->status = 2;
			}
		}
		ud = ud->next;
	}
}

// this function is called when a dumb daemon dies and we do not want to respawn it
int upsgi_daemon_check_pid_death(pid_t diedpid) {
	struct upsgi_daemon *ud = upsgi.daemons;
	while (ud) {
		if (ud->pid == diedpid) {
			if (!ud->pidfile) {
				upsgi_log("daemon \"%s\" (pid: %d) annihilated\n", ud->command, (int) diedpid);
				ud->pid = -1;
				return -1;
			}
			else {
				if (!ud->has_daemonized) {
					ud->has_daemonized = 1;
				}
				else {
					upsgi_log("[upsgi-daemons] BUG !!! daemon \"%s\" has already daemonized !!!\n", ud->command);
				}
			}
		}
		ud = ud->next;
	}
	return 0;
}

// this function is called when a dumb daemon dies and we want to respawn it
int upsgi_daemon_check_pid_reload(pid_t diedpid) {
	struct upsgi_daemon *ud = upsgi.daemons;
	while (ud) {
		if (ud->pid == diedpid && !ud->pidfile) {
			if (ud->control) {
				gracefully_kill_them_all(0);
				return 0;
			}
			upsgi_spawn_daemon(ud);
			return 1;
		}
		ud = ud->next;
	}
	return 0;
}



int upsgi_check_pidfile(char *filename) {
	struct stat st;
	pid_t ret = -1;
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		upsgi_error_open(filename);
		goto end;
	}
	if (fstat(fd, &st)) {
		upsgi_error("fstat()");
		goto end2;
	}
	char *pidstr = upsgi_calloc(st.st_size + 1);
	if (read(fd, pidstr, st.st_size) != st.st_size) {
		upsgi_error("read()");
		goto end3;
	}
	pid_t pid = atoi(pidstr);
	if (pid <= 0)
		goto end3;
	if (!kill(pid, 0)) {
		ret = pid;
	}
end3:
	free(pidstr);
end2:
	close(fd);
end:
	return ret;
}

void upsgi_daemons_spawn_all() {
	struct upsgi_daemon *ud = upsgi.daemons;
	while (ud) {
		if (!ud->registered) {
			ud->registered = 1;
			if (ud->pidfile) {
				int checked_pid = upsgi_check_pidfile(ud->pidfile);
				if (checked_pid <= 0) {
					upsgi_spawn_daemon(ud);
				}
				else {
					ud->pid = checked_pid;
					upsgi_log("[upsgi-daemons] found valid/active pidfile for \"%s\" (pid: %d)\n", ud->command, (int) ud->pid);
				}
			}
			else {
				upsgi_spawn_daemon(ud);
			}
		}
		ud = ud->next;
	}
}


void upsgi_detach_daemons() {
	struct upsgi_daemon *ud = upsgi.daemons;
	while (ud) {
		// stop only dumb daemons
		if (ud->pid > 0 && !ud->pidfile) {
			upsgi_log("[upsgi-daemons] stopping daemon (pid: %d): %s\n", (int) ud->pid, ud->command);
			// try to stop daemon gracefully, kill it if it won't die
			// if mercy is not set then wait up to 3 seconds
			time_t timeout = upsgi_now() + (upsgi.reload_mercy ? upsgi.reload_mercy : 3);
			int waitpid_status;
			while (!kill(ud->pid, 0)) {
				if (upsgi_instance_is_reloading && ud->reload_signal > 0) {
					kill(-(ud->pid), ud->reload_signal);
				}
				else {
					kill(-(ud->pid), ud->stop_signal);
				}
				sleep(1);
				waitpid(ud->pid, &waitpid_status, WNOHANG);
				if (upsgi_now() >= timeout) {
					upsgi_log("[upsgi-daemons] daemon did not die in time, killing (pid: %d): %s\n", (int) ud->pid, ud->command);
					kill(-(ud->pid), SIGKILL);
					break;
				}
			}
			// unregister daemon to prevent it from being respawned
			ud->registered = 0;
		}

		// smart daemons that have to be notified when master is reloading or stopping
		if (ud->notifypid && ud->pid > 0 && ud->pidfile) {
			if (upsgi_instance_is_reloading) {
				kill(-(ud->pid), ud->reload_signal > 0 ? ud->reload_signal : SIGHUP);
			}
			else {
				kill(-(ud->pid), ud->stop_signal);
			}
		}
		ud = ud->next;
	}
}

static int daemon_spawn(void *);

void upsgi_spawn_daemon(struct upsgi_daemon *ud) {

	// skip unregistered daemons
	if (!ud->registered) return;

	ud->throttle = 0;

	if (upsgi.current_time - ud->last_spawn <= 3) {
		ud->throttle = ud->respawns - (upsgi.current_time - ud->last_spawn);
		// if ud->respawns == 0 then we can end up with throttle < 0
		if (ud->throttle <= 0) ud->throttle = 1;
		if (ud->max_throttle > 0 ) {
			if (ud->throttle > ud->max_throttle) {
				ud->throttle = ud->max_throttle;
			}
		}
		// use an arbitrary value (5 minutes to avoid endless sleeps...)
		else if (ud->throttle > 300) {
			ud->throttle = 300;
		}
	}

	pid_t pid = upsgi_fork("upsgi external daemon");
	if (pid < 0) {
		upsgi_error("fork()");
		return;
	}

	if (pid > 0) {
		ud->has_daemonized = 0;
		ud->pid = pid;
		ud->status = 1;
		ud->pidfile_checks = 0;
		if (ud->respawns == 0) {
			ud->born = upsgi_now();
		}

		ud->respawns++;
		ud->last_spawn = upsgi_now();

	}
	else {
		// close upsgi sockets
		upsgi_close_all_sockets();
		upsgi_close_all_fds();

		if (ud->chdir) {
			if (chdir(ud->chdir)) {
				upsgi_error("upsgi_spawn_daemon()/chdir()");
				exit(1);
			}
		}

#if defined(__linux__) && !defined(OBSOLETE_LINUX_KERNEL) && defined(CLONE_NEWPID)
		if (ud->ns_pid) {
			// we need to create a new session
			if (setsid() < 0) {
				upsgi_error("upsgi_spawn_daemon()/setsid()");
				exit(1);
			}
			// avoid the need to set stop_signal in attach-daemon2
			signal(SIGTERM, end_me);

			char stack[PTHREAD_STACK_MIN];
                	pid_t pid = clone((int (*)(void *))daemon_spawn, stack + PTHREAD_STACK_MIN, SIGCHLD | CLONE_NEWPID, (void *) ud);
                        if (pid > 0) {

#ifdef PR_SET_PDEATHSIG
				if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) {
                                	upsgi_error("upsgi_spawn_daemon()/prctl()");
                        	}
#endif
                                // block all signals except SIGTERM
                                sigset_t smask;
                                sigfillset(&smask);
				sigdelset(&smask, SIGTERM);
                                sigprocmask(SIG_BLOCK, &smask, NULL);
                                int status;
                                if (waitpid(pid, &status, 0) < 0) {
                                        upsgi_error("upsgi_spawn_daemon()/waitpid()");
                                }
                                _exit(0);
                        }
			upsgi_error("upsgi_spawn_daemon()/clone()");
                        exit(1);
		}
#endif
		daemon_spawn((void *) ud);

	}
}


static int daemon_spawn(void *arg) {

	struct upsgi_daemon *ud = (struct upsgi_daemon *) arg;

		if (ud->gid) {
			if (setgid(ud->gid)) {
				upsgi_error("upsgi_spawn_daemon()/setgid()");
				exit(1);
			}
		}

		if (ud->uid) {
			if (setuid(ud->uid)) {
				upsgi_error("upsgi_spawn_daemon()/setuid()");
				exit(1);
			}
		}

		if (ud->daemonize) {
			/* refork... */
			pid_t pid = fork();
			if (pid < 0) {
				upsgi_error("fork()");
				exit(1);
			}
			if (pid != 0) {
				_exit(0);
			}
			upsgi_write_pidfile(ud->pidfile);
		}

		if (!upsgi.daemons_honour_stdin && !ud->honour_stdin) {
			// /dev/null will became stdin
			upsgi_remap_fd(0, "/dev/null");
		}

		if (setsid() < 0) {
			upsgi_error("setsid()");
			exit(1);
		}

		if (!ud->pidfile) {
#ifdef PR_SET_PDEATHSIG
			if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) {
				upsgi_error("prctl()");
			}
#endif
		}


		if (ud->throttle) {
			upsgi_log("[upsgi-daemons] throttling \"%s\" for %d seconds\n", ud->command, ud->throttle);
			sleep((unsigned int) ud->throttle);
		}

		upsgi_log("[upsgi-daemons] %sspawning \"%s\" (uid: %d gid: %d)\n", ud->respawns > 0 ? "re" : "", ud->command, (int) getuid(), (int) getgid());
		upsgi_exec_command_with_args(ud->command);
		upsgi_log("[upsgi-daemons] unable to spawn \"%s\"\n", ud->command);

		// never here;
		exit(1);
}


void upsgi_opt_add_daemon(char *opt, char *value, void *none) {

	struct upsgi_daemon *upsgi_ud = upsgi.daemons, *old_ud;
	char *pidfile = NULL;
	int daemonize = 0;
	int freq = 10;
	char *space = NULL;
	int stop_signal = SIGTERM;
	int reload_signal = 0;

	char *command = upsgi_str(value);


	if (!strcmp(opt, "smart-attach-daemon") || !strcmp(opt, "smart-attach-daemon2")) {
		space = strchr(command, ' ');
		if (!space) {
			upsgi_log("invalid smart-attach-daemon syntax: %s\n", command);
			exit(1);
		}
		*space = 0;
		pidfile = command;
		// check for freq
		char *comma = strchr(pidfile, ',');
		if (comma) {
			*comma = 0;
			freq = atoi(comma + 1);
		}
		command = space + 1;
		if (!strcmp(opt, "smart-attach-daemon2")) {
			daemonize = 1;
		}
	}

	if (!upsgi_ud) {
		upsgi.daemons = upsgi_calloc(sizeof(struct upsgi_daemon));
		upsgi_ud = upsgi.daemons;
	}
	else {
		while (upsgi_ud) {
			old_ud = upsgi_ud;
			upsgi_ud = upsgi_ud->next;
		}

		upsgi_ud = upsgi_calloc(sizeof(struct upsgi_daemon));
		old_ud->next = upsgi_ud;
	}

	upsgi_ud->command = command;
	upsgi_ud->pid = 0;
	upsgi_ud->status = 0;
	upsgi_ud->freq = freq;
	upsgi_ud->registered = 0;
	upsgi_ud->next = NULL;
	upsgi_ud->respawns = 0;
	upsgi_ud->last_spawn = 0;
	upsgi_ud->daemonize = daemonize;
	upsgi_ud->pidfile = pidfile;
	upsgi_ud->control = 0;
	upsgi_ud->stop_signal = stop_signal;
	upsgi_ud->reload_signal = reload_signal;
	if (!strcmp(opt, "attach-control-daemon")) {
		upsgi_ud->control = 1;
	}

	upsgi.daemons_cnt++;

}

void upsgi_opt_add_daemon2(char *opt, char *value, void *none) {

        struct upsgi_daemon *upsgi_ud = upsgi.daemons, *old_ud;

	char *d_command = NULL;
	char *d_freq = NULL;
	char *d_pidfile = NULL;
	char *d_control = NULL;
	char *d_daemonize = NULL;
	char *d_touch = NULL;
	char *d_stopsignal = NULL;
	char *d_reloadsignal = NULL;
	char *d_stdin = NULL;
	char *d_uid = NULL;
	char *d_gid = NULL;
	char *d_ns_pid = NULL;
	char *d_chdir = NULL;
	char *d_max_throttle = NULL;
	char *d_notifypid = NULL;

	char *arg = upsgi_str(value);

	if (upsgi_kvlist_parse(arg, strlen(arg), ',', '=',
		"command", &d_command,	
		"cmd", &d_command,	
		"exec", &d_command,	
		"freq", &d_freq,	
		"pidfile", &d_pidfile,	
		"control", &d_control,	
		"daemonize", &d_daemonize,	
		"daemon", &d_daemonize,	
		"touch", &d_touch,	
		"stopsignal", &d_stopsignal,	
		"stop_signal", &d_stopsignal,	
		"reloadsignal", &d_reloadsignal,	
		"reload_signal", &d_reloadsignal,	
		"stdin", &d_stdin,	
		"uid", &d_uid,	
		"gid", &d_gid,	
		"ns_pid", &d_ns_pid,	
		"chdir", &d_chdir,	
		"max_throttle", &d_max_throttle,	
		"notifypid", &d_notifypid,
	NULL)) {
		upsgi_log("invalid --%s keyval syntax\n", opt);
		exit(1);
	}

	if (!d_command) {
		upsgi_log("--%s: you need to specify a 'command' key\n", opt);
		exit(1);
	}



        if (!upsgi_ud) {
                upsgi.daemons = upsgi_calloc(sizeof(struct upsgi_daemon));
                upsgi_ud = upsgi.daemons;
        }
        else {
                while (upsgi_ud) {
                        old_ud = upsgi_ud;
                        upsgi_ud = upsgi_ud->next;
                }

                upsgi_ud = upsgi_calloc(sizeof(struct upsgi_daemon));
                old_ud->next = upsgi_ud;
        }
        upsgi_ud->command = d_command;
        upsgi_ud->freq = d_freq ? atoi(d_freq) : 10;
        upsgi_ud->daemonize = d_daemonize ? 1 : 0;
        upsgi_ud->pidfile = d_pidfile;
        upsgi_ud->stop_signal = d_stopsignal ? atoi(d_stopsignal) : SIGTERM;
        upsgi_ud->reload_signal = d_reloadsignal ? atoi(d_reloadsignal) : 0;
        upsgi_ud->control = d_control ? 1 : 0;
	upsgi_ud->uid = d_uid ? atoi(d_uid) : 0;
	upsgi_ud->gid = d_gid ? atoi(d_gid) : 0;
	upsgi_ud->honour_stdin = d_stdin ? 1 : 0;
        upsgi_ud->ns_pid = d_ns_pid ? 1 : 0;

	upsgi_ud->chdir = d_chdir;

	upsgi_ud->max_throttle = d_max_throttle ? atoi(d_max_throttle) : 0;

	upsgi_ud->notifypid = d_notifypid ? 1 : 0;

	if (d_touch) {
		size_t i,rlen = 0;
		char **argv = upsgi_split_quoted(d_touch, strlen(d_touch), ";", &rlen);
		for(i=0;i<rlen;i++) {	
			upsgi_string_new_list(&upsgi_ud->touch, argv[i]);
		}
		if (argv) free(argv);
	}

        upsgi.daemons_cnt++;

	free(arg);

}

