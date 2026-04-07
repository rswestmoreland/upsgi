#include "upsgi.h"


extern struct upsgi_server upsgi;

#ifdef __BIG_ENDIAN__
uint16_t upsgi_swap16(uint16_t x) {
	return (uint16_t) ((x & 0xff) << 8 | (x & 0xff00) >> 8);
}

uint32_t upsgi_swap32(uint32_t x) {
	x = ((x << 8) & 0xFF00FF00) | ((x >> 8) & 0x00FF00FF);
	return (x >> 16) | (x << 16);
}

// thanks to ffmpeg project for this idea :P
uint64_t upsgi_swap64(uint64_t x) {
	union {
		uint64_t ll;
		uint32_t l[2];
	} w, r;
	w.ll = x;
	r.l[0] = upsgi_swap32(w.l[1]);
	r.l[1] = upsgi_swap32(w.l[0]);
	return r.ll;
}

#endif

// check if a string is a valid hex number
int check_hex(char *str, int len) {
	int i;
	for (i = 0; i < len; i++) {
		if ((str[i] < '0' && str[i] > '9') && (str[i] < 'a' && str[i] > 'f') && (str[i] < 'A' && str[i] > 'F')
			) {
			return 0;
		}
	}

	return 1;

}

// increase worker harakiri
void inc_harakiri(struct wsgi_request *wsgi_req, int sec) {
	if (upsgi.master_process) {
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri += sec;
	}
	else {
		alarm(upsgi.harakiri_options.workers + sec);
	}
}

// set worker harakiri
void set_harakiri(struct wsgi_request *wsgi_req, int sec) {
	if (!wsgi_req) return;
	if (sec == 0) {
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri = 0;
	}
	else {
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri = upsgi_now() + sec;
	}
	if (!upsgi.master_process) {
		alarm(sec);
	}
}

// set user harakiri
void set_user_harakiri(struct wsgi_request *wsgi_req, int sec) {
	if (!upsgi.master_process) {
		upsgi_log("!!! unable to set user harakiri without the master process !!!\n");
		return;
	}

	// a 0 seconds value, reset the timer
	time_t timeout = sec == 0 ? 0 : upsgi_now() + sec;

	if (upsgi.muleid > 0) {
		upsgi.mules[upsgi.muleid - 1].user_harakiri = timeout;
	}
	else if (upsgi.i_am_a_spooler) {
		struct upsgi_spooler *uspool = upsgi.i_am_a_spooler;
		uspool->user_harakiri = timeout;
	}
	else if (wsgi_req) {
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].user_harakiri = timeout;
	}
}

// set mule harakiri
void set_mule_harakiri(int sec) {
	if (sec == 0) {
		upsgi.mules[upsgi.muleid - 1].harakiri = 0;
	}
	else {
		upsgi.mules[upsgi.muleid - 1].harakiri = upsgi_now() + sec;
	}
	if (!upsgi.master_process) {
		alarm(sec);
	}
}

// set spooler harakiri
void set_spooler_harakiri(int sec) {
	if (sec == 0) {
		upsgi.i_am_a_spooler->harakiri = 0;
	}
	else {
		upsgi.i_am_a_spooler->harakiri = upsgi_now() + sec;
	}
	if (!upsgi.master_process) {
		alarm(sec);
	}
}


// daemonize to the specified logfile
void daemonize(char *logfile) {
	pid_t pid;

	// do not daemonize under emperor
	if (upsgi.has_emperor) {
		logto(logfile);
		return;
	}

	pid = fork();
	if (pid < 0) {
		upsgi_error("fork()");
		exit(1);
	}
	if (pid != 0) {
		_exit(0);
	}

	if (setsid() < 0) {
		upsgi_error("setsid()");
		exit(1);
	}

	/* refork... */
	pid = fork();
	if (pid < 0) {
		upsgi_error("fork()");
		exit(1);
	}
	if (pid != 0) {
		_exit(0);
	}

	if (!upsgi.do_not_change_umask) {
		umask(0);
	}

	/*if (chdir("/") != 0) {
	   upsgi_error("chdir()");
	   exit(1);
	   } */

	upsgi_remap_fd(0, "/dev/null");

	logto(logfile);
}

// get current working directory
char *upsgi_get_cwd() {
#if defined(__GLIBC__)
	return getcwd(NULL, 0);
#else
	// set this to static to avoid useless reallocations in stats mode
	static size_t newsize = 256;

	char *cwd = upsgi_malloc(newsize);

	if (getcwd(cwd, newsize) == NULL && errno == ERANGE) {
		newsize += 256;
		upsgi_log("need a bigger buffer (%lu bytes) for getcwd(). doing reallocation.\n", (unsigned long) newsize);
		free(cwd);
		cwd = upsgi_malloc(newsize);
		if (getcwd(cwd, newsize) == NULL) {
			upsgi_error("getcwd()");
			exit(1);
		}
	}

	return cwd;
#endif

}

#ifdef __linux__
void upsgi_set_cgroup() {

	char *cgroup_taskfile;
	FILE *cgroup;
	char *cgroup_opt;
	struct upsgi_string_list *usl, *uslo;

	if (!upsgi.cgroup)
		return;

	if (getuid())
		return;

	usl = upsgi.cgroup;

	while (usl) {
		int mode = strtol(upsgi.cgroup_dir_mode, 0, 8);
		if (mkdir(usl->value, mode)) {
			if (errno != EEXIST) {
				upsgi_error("upsgi_set_cgroup()/mkdir()");
				exit(1);
			}
			if (chmod(usl->value, mode)) {
				upsgi_error("upsgi_set_cgroup()/chmod()");
				exit(1);
			}
			upsgi_log("using Linux cgroup %s with mode %o\n", usl->value, mode);
		}
		else {
			upsgi_log("created Linux cgroup %s with mode %o\n", usl->value, mode);
		}

		cgroup_taskfile = upsgi_concat2(usl->value, "/tasks");
		cgroup = fopen(cgroup_taskfile, "w");
		if (!cgroup) {
			upsgi_error_open(cgroup_taskfile);
			exit(1);
		}
		if (fprintf(cgroup, "%d\n", (int) getpid()) <= 0 || ferror(cgroup) || fclose(cgroup)) {
			upsgi_error("could not set cgroup");
			exit(1);
		}
		upsgi_log("assigned process %d to cgroup %s\n", (int) getpid(), cgroup_taskfile);
		free(cgroup_taskfile);


		uslo = upsgi.cgroup_opt;
		while (uslo) {
			cgroup_opt = strchr(uslo->value, '=');
			if (!cgroup_opt) {
				cgroup_opt = strchr(uslo->value, ':');
				if (!cgroup_opt) {
					upsgi_log("invalid cgroup-opt syntax\n");
					exit(1);
				}
			}

			cgroup_opt[0] = 0;
			cgroup_opt++;

			cgroup_taskfile = upsgi_concat3(usl->value, "/", uslo->value);
			cgroup = fopen(cgroup_taskfile, "w");
			if (cgroup) {
				if (fprintf(cgroup, "%s\n", cgroup_opt) <= 0 || ferror(cgroup) || fclose(cgroup)) {
					upsgi_log("could not set cgroup option %s to %s\n", uslo->value, cgroup_opt);
					exit(1);
				}
				upsgi_log("set %s to %s\n", cgroup_opt, cgroup_taskfile);
			}
			free(cgroup_taskfile);

			cgroup_opt[-1] = '=';

			uslo = uslo->next;
		}

		usl = usl->next;
	}

}
#endif

#ifdef UPSGI_CAP
void upsgi_apply_cap(cap_value_t * cap, int caps_count) {
	cap_value_t minimal_cap_values[] = { CAP_SYS_CHROOT, CAP_SETUID, CAP_SETGID, CAP_SETPCAP };

	cap_t caps = cap_init();

	if (!caps) {
		upsgi_error("cap_init()");
		exit(1);
	}
	cap_clear(caps);

	cap_set_flag(caps, CAP_EFFECTIVE, 4, minimal_cap_values, CAP_SET);

	cap_set_flag(caps, CAP_PERMITTED, 4, minimal_cap_values, CAP_SET);
	cap_set_flag(caps, CAP_PERMITTED, caps_count, cap, CAP_SET);

	cap_set_flag(caps, CAP_INHERITABLE, caps_count, cap, CAP_SET);

	if (cap_set_proc(caps) < 0) {
		upsgi_error("cap_set_proc()");
		exit(1);
	}
	cap_free(caps);

#ifdef __linux__
#ifdef SECBIT_KEEP_CAPS
	if (prctl(SECBIT_KEEP_CAPS, 1, 0, 0, 0) < 0) {
		upsgi_error("prctl()");
		exit(1);
	}
#else
	if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) < 0) {
		upsgi_error("prctl()");
		exit(1);
	}
#endif
#endif
}
#endif

// drop privileges (as root)
/*

	here we manage jails/namespaces too
	it is a pretty huge function... refactory is needed

*/
void upsgi_as_root() {


	if (getuid() > 0)
		goto nonroot;

#ifndef __RUMP__
	if (!upsgi.master_as_root && !upsgi.uidname) {
		upsgi_log_initial("upsgi running as root, you can use --uid/--gid/--chroot options\n");
	}
#endif

	int in_jail = 0;

#if defined(__linux__) && !defined(OBSOLETE_LINUX_KERNEL)
	if (upsgi.unshare && !upsgi.reloads) {

		if (unshare(upsgi.unshare)) {
			upsgi_error("unshare()");
			exit(1);
		}
		else {
			upsgi_log("[linux-namespace] applied unshare() mask: %d\n", upsgi.unshare);
		}

#ifdef CLONE_NEWUSER
		if (upsgi.unshare & CLONE_NEWUSER) {
			if (setuid(0)) {
				upsgi_error("upsgi_as_root()/setuid(0)");
				exit(1);
			}
		}
#endif
		in_jail = 1;
	}
#endif

#ifdef UPSGI_CAP
	if (upsgi.cap && upsgi.cap_count > 0 && !upsgi.reloads) {
		upsgi_apply_cap(upsgi.cap, upsgi.cap_count);
	}
#endif


#if defined(__FreeBSD__) || defined(__GNU_kFreeBSD__)
	if (upsgi.jail && !upsgi.reloads) {

		struct jail ujail;
		char *jarg = upsgi_str(upsgi.jail);
		char *j_hostname = NULL;
		char *j_name = NULL;

		char *space = strchr(jarg, ' ');
		if (space) {
			*space = 0;
			j_hostname = space + 1;
			space = strchr(j_hostname, ' ');
			if (space) {
				*space = 0;
				j_name = space + 1;
			}
		}
		ujail.version = JAIL_API_VERSION;
		ujail.path = jarg;
		ujail.hostname = j_hostname ? j_hostname : "";
		ujail.jailname = j_name;
		ujail.ip4s = 0;
		ujail.ip6s = 0;

		struct upsgi_string_list *usl = NULL;

		upsgi_foreach(usl, upsgi.jail_ip4) {
			ujail.ip4s++;
		}
		struct in_addr *saddr = upsgi_calloc(sizeof(struct in_addr) * ujail.ip4s);
		int i = 0;
		upsgi_foreach(usl, upsgi.jail_ip4) {
			if (!inet_pton(AF_INET, usl->value, &saddr[i].s_addr)) {
				upsgi_error("jail()/inet_pton()");
				exit(1);
			}
			i++;
		}
		ujail.ip4 = saddr;
#ifdef AF_INET6
		upsgi_foreach(usl, upsgi.jail_ip6) {
			ujail.ip6s++;
		}

		struct in6_addr *saddr6 = upsgi_calloc(sizeof(struct in6_addr) * ujail.ip6s);
		i = 0;
		upsgi_foreach(usl, upsgi.jail_ip6) {
			if (!inet_pton(AF_INET6, usl->value, &saddr6[i].s6_addr)) {
				upsgi_error("jail()/inet_pton()");
				exit(1);
			}
			i++;
		}
		ujail.ip6 = saddr6;
#endif

		int jail_id = jail(&ujail);
		if (jail_id < 0) {
			upsgi_error("jail()");
			exit(1);
		}

		if (upsgi.jidfile) {
			if (upsgi_write_intfile(upsgi.jidfile, jail_id)) {
				upsgi_log("unable to write jidfile\n");
				exit(1);
			}
		}

		upsgi_log("--- running in FreeBSD jail %d ---\n", jail_id);
		in_jail = 1;
	}

#ifdef UPSGI_HAS_FREEBSD_LIBJAIL
	if (upsgi.jail_attach && !upsgi.reloads) {
		struct jailparam jparam;
		upsgi_log("attaching to FreeBSD jail %s ...\n", upsgi.jail_attach);
		if (!is_a_number(upsgi.jail_attach)) {
			if (jailparam_init(&jparam, "name")) {
				upsgi_error("jailparam_init()");
				exit(1);
			}
		}
		else {
			if (jailparam_init(&jparam, "jid")) {
				upsgi_error("jailparam_init()");
				exit(1);
			}
		}
		jailparam_import(&jparam, upsgi.jail_attach);
		int jail_id = jailparam_set(&jparam, 1, JAIL_UPDATE | JAIL_ATTACH);
		if (jail_id < 0) {
			upsgi_error("jailparam_set()");
			exit(1);
		}

		jailparam_free(&jparam, 1);
		upsgi_log("--- running in FreeBSD jail %d ---\n", jail_id);
		in_jail = 1;
	}

	if (upsgi.jail2 && !upsgi.reloads) {
		struct upsgi_string_list *usl = NULL;
		unsigned nparams = 0;
		upsgi_foreach(usl, upsgi.jail2) {
			nparams++;
		}
		struct jailparam *params = upsgi_malloc(sizeof(struct jailparam) * nparams);
		int i = 0;
		upsgi_foreach(usl, upsgi.jail2) {
			upsgi_log("FreeBSD libjail applying %s\n", usl->value);
			char *equal = strchr(usl->value, '=');
			if (equal) {
				*equal = 0;
			}
			if (jailparam_init(&params[i], usl->value)) {
				upsgi_error("jailparam_init()");
				exit(1);
			}
			if (equal) {
				jailparam_import(&params[i], equal + 1);
				*equal = '=';
			}
			else {
				jailparam_import(&params[i], "1");
			}
			i++;
		}
		int jail_id = jailparam_set(params, nparams, JAIL_CREATE | JAIL_ATTACH);
		if (jail_id < 0) {
			upsgi_error("jailparam_set()");
			exit(1);
		}

		jailparam_free(params, nparams);

		if (upsgi.jidfile) {
			if (upsgi_write_intfile(upsgi.jidfile, jail_id)) {
				upsgi_log("unable to write jidfile\n");
				exit(1);
			}
		}

		upsgi_log("--- running in FreeBSD jail %d ---\n", jail_id);
		in_jail = 1;
	}
#endif
#endif

	if (in_jail || upsgi.jailed) {
		int i;
		for (i = 0; i < upsgi.gp_cnt; i++) {
			if (upsgi.gp[i]->early_post_jail) {
				upsgi.gp[i]->early_post_jail();
			}
		}

		upsgi_hooks_run(upsgi.hook_post_jail, "post-jail", 1);
		struct upsgi_string_list *usl = NULL;
		upsgi_foreach(usl, upsgi.mount_post_jail) {
			upsgi_log("mounting \"%s\" (post-jail)...\n", usl->value);
			if (upsgi_mount_hook(usl->value)) {
				exit(1);
			}
		}

		upsgi_foreach(usl, upsgi.umount_post_jail) {
			upsgi_log("un-mounting \"%s\" (post-jail)...\n", usl->value);
			if (upsgi_umount_hook(usl->value)) {
				exit(1);
			}
		}

		upsgi_foreach(usl, upsgi.exec_post_jail) {
			upsgi_log("running \"%s\" (post-jail)...\n", usl->value);
			int ret = upsgi_run_command_and_wait(NULL, usl->value);
			if (ret != 0) {
				upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
				exit(1);
			}
		}

		upsgi_foreach(usl, upsgi.call_post_jail) {
			if (upsgi_call_symbol(usl->value)) {
				upsgi_log("unable to call function \"%s\"\n", usl->value);
				exit(1);
			}
		}

		if (upsgi.refork_post_jail) {
			upsgi_log("re-fork()ing...\n");
			pid_t pid = fork();
			if (pid < 0) {
				upsgi_error("fork()");
				exit(1);
			}
			if (pid > 0) {
				// block all signals
				sigset_t smask;
				sigfillset(&smask);
				sigprocmask(SIG_BLOCK, &smask, NULL);
				int status;
				if (waitpid(pid, &status, 0) < 0) {
					upsgi_error("waitpid()");
				}
				_exit(0);
			}
		}

		for (i = 0; i < upsgi.gp_cnt; i++) {
			if (upsgi.gp[i]->post_jail) {
				upsgi.gp[i]->post_jail();
			}
		}
	}

	if (upsgi.chroot && !upsgi.is_chrooted && !upsgi.reloads) {
		if (!upsgi.master_as_root)
			upsgi_log("chroot() to %s\n", upsgi.chroot);

		if (chroot(upsgi.chroot)) {
			upsgi_error("chroot()");
			exit(1);
		}
		upsgi.is_chrooted = 1;
#ifdef __linux__
		if (upsgi.logging_options.memory_report) {
			upsgi_log("*** Warning, on linux system you have to bind-mount the /proc fs in your chroot to get memory debug/report.\n");
		}
#endif
	}

#ifdef __linux__
	if (upsgi.pivot_root && !upsgi.reloads) {
		char *arg = upsgi_str(upsgi.pivot_root);
		char *space = strchr(arg, ' ');
		if (!space) {
			upsgi_log("invalid pivot_root syntax, new_root and put_old must be separated by a space\n");
			exit(1);
		}
		*space = 0;
#if defined(MS_REC) && defined(MS_PRIVATE)
		if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
			upsgi_error("mount()");
			exit(1);
		}
#endif
		if (chdir(arg)) {
			upsgi_error("pivot_root()/chdir()");
			exit(1);
		}
		space += 1 + strlen(arg);
		if (space[0] == '/')
			space++;
		if (pivot_root(".", space)) {
			upsgi_error("pivot_root()");
			exit(1);
		}
		if (upsgi.logging_options.memory_report) {
			upsgi_log("*** Warning, on linux system you have to bind-mount the /proc fs in your chroot to get memory debug/report.\n");
		}
		free(arg);
		if (chdir("/")) {
			upsgi_error("chdir()");
			exit(1);
		}

	}
#endif

#if defined(__linux__) && !defined(OBSOLETE_LINUX_KERNEL)
	if (upsgi.unshare2 && !upsgi.reloads) {

		if (unshare(upsgi.unshare2)) {
			upsgi_error("unshare()");
			exit(1);
		}
		else {
			upsgi_log("[linux-namespace] applied unshare() mask: %d\n", upsgi.unshare2);
		}
#ifdef CLONE_NEWUSER
		if (upsgi.unshare2 & CLONE_NEWUSER) {
			if (setuid(0)) {
				upsgi_error("upsgi_as_root()/setuid(0)");
				exit(1);
			}
		}
#endif
		in_jail = 1;
	}
#endif

	if (upsgi.refork_as_root) {
		upsgi_log("re-fork()ing...\n");
		pid_t pid = fork();
		if (pid < 0) {
			upsgi_error("fork()");
			exit(1);
		}
		if (pid > 0) {
			// block all signals
			sigset_t smask;
			sigfillset(&smask);
			sigprocmask(SIG_BLOCK, &smask, NULL);
			int status;
			if (waitpid(pid, &status, 0) < 0) {
				upsgi_error("waitpid()");
			}
			_exit(0);
		}
	}


	struct upsgi_string_list *usl;
	upsgi_foreach(usl, upsgi.wait_for_interface) {
		if (!upsgi.wait_for_interface_timeout) {
			upsgi.wait_for_interface_timeout = 60;
		}
		upsgi_log("waiting for interface %s (max %d seconds) ...\n", usl->value, upsgi.wait_for_interface_timeout);
		int counter = 0;
		for (;;) {
			if (counter > upsgi.wait_for_interface_timeout) {
				upsgi_log("interface %s unavailable after %d seconds\n", usl->value, counter);
				exit(1);
			}
			unsigned int index = if_nametoindex(usl->value);
			if (index > 0) {
				upsgi_log("interface %s found with index %u\n", usl->value, index);
				break;
			}
			else {
				sleep(1);
				counter++;
			}
		}
	}

	upsgi_foreach(usl, upsgi.wait_for_fs) {
		if (upsgi_wait_for_fs(usl->value, 0)) exit(1);
	}

	upsgi_foreach(usl, upsgi.wait_for_file) {
		if (upsgi_wait_for_fs(usl->value, 1)) exit(1);
	}

	upsgi_foreach(usl, upsgi.wait_for_dir) {
		if (upsgi_wait_for_fs(usl->value, 2)) exit(1);
	}

	upsgi_foreach(usl, upsgi.wait_for_mountpoint) {
		if (upsgi_wait_for_mountpoint(usl->value)) exit(1);
	}

	upsgi_hooks_run(upsgi.hook_as_root, "as root", 1);

	upsgi_foreach(usl, upsgi.mount_as_root) {
		upsgi_log("mounting \"%s\" (as root)...\n", usl->value);
		if (upsgi_mount_hook(usl->value)) {
			exit(1);
		}
	}

	upsgi_foreach(usl, upsgi.umount_as_root) {
		upsgi_log("un-mounting \"%s\" (as root)...\n", usl->value);
		if (upsgi_umount_hook(usl->value)) {
			exit(1);
		}
	}

	// now run the scripts needed by root
	upsgi_foreach(usl, upsgi.exec_as_root) {
		upsgi_log("running \"%s\" (as root)...\n", usl->value);
		int ret = upsgi_run_command_and_wait(NULL, usl->value);
		if (ret != 0) {
			upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
			exit(1);
		}
	}

	upsgi_foreach(usl, upsgi.call_as_root) {
		if (upsgi_call_symbol(usl->value)) {
			upsgi_log("unable to call function \"%s\"\n", usl->value);
		}
	}


	if (upsgi.gidname) {
		struct group *ugroup = getgrnam(upsgi.gidname);
		if (ugroup) {
			upsgi.gid = ugroup->gr_gid;
		}
		else {
			upsgi_log("group %s not found.\n", upsgi.gidname);
			exit(1);
		}
	}
	if (upsgi.uidname) {
		struct passwd *upasswd = getpwnam(upsgi.uidname);
		if (upasswd) {
			upsgi.uid = upasswd->pw_uid;
		}
		else {
			upsgi_log("user %s not found.\n", upsgi.uidname);
			exit(1);
		}
	}

	if (upsgi.logfile_chown) {
		int log_fd = 2;
		if (upsgi.log_master && upsgi.original_log_fd > -1) {
			log_fd = upsgi.original_log_fd;
		}
		if (fchown(log_fd, upsgi.uid, upsgi.gid)) {
			upsgi_error("fchown()");
			exit(1);
		}
	}

	// fix ipcsem owner
	if (upsgi.lock_ops.lock_init == upsgi_lock_ipcsem_init) {
		struct upsgi_lock_item *uli = upsgi.registered_locks;

		while (uli) {
			union semun {
				int val;
				struct semid_ds *buf;
				ushort *array;
			} semu;

			struct semid_ds sds;
			memset(&sds, 0, sizeof(sds));
			semu.buf = &sds;
			int semid = 0;
			memcpy(&semid, uli->lock_ptr, sizeof(int));

			if (semctl(semid, 0, IPC_STAT, semu)) {
				upsgi_error("semctl()");
				exit(1);
			}

			semu.buf->sem_perm.uid = upsgi.uid;
			semu.buf->sem_perm.gid = upsgi.gid;

			if (semctl(semid, 0, IPC_SET, semu)) {
				upsgi_error("semctl()");
				exit(1);
			}
			uli = uli->next;
		}

	}

	// ok try to call some special hook before finally dropping privileges
	int i;
	for (i = 0; i < upsgi.gp_cnt; i++) {
		if (upsgi.gp[i]->before_privileges_drop) {
			upsgi.gp[i]->before_privileges_drop();
		}
	}

	if (upsgi.gid) {
		if (!upsgi.master_as_root)
			upsgi_log("setgid() to %d\n", upsgi.gid);
		if (setgid(upsgi.gid)) {
			upsgi_error("setgid()");
			exit(1);
		}
		if (upsgi.no_initgroups || !upsgi.uid) {
			if (setgroups(0, NULL)) {
				upsgi_error("setgroups()");
				exit(1);
			}
		}
		else {
			char *uidname = upsgi.uidname;
			if (!uidname) {
				struct passwd *pw = getpwuid(upsgi.uid);
				if (pw)
					uidname = pw->pw_name;

			}
			if (!uidname)
				uidname = upsgi_num2str(upsgi.uid);
			if (initgroups(uidname, upsgi.gid)) {
				upsgi_error("setgroups()");
				exit(1);
			}
		}
		struct upsgi_string_list *usl;
		size_t ags = 0;
		upsgi_foreach(usl, upsgi.additional_gids) ags++;
		if (ags > 0) {
			gid_t *ags_list = upsgi_calloc(sizeof(gid_t) * ags);
			size_t g_pos = 0;
			upsgi_foreach(usl, upsgi.additional_gids) {
				ags_list[g_pos] = atoi(usl->value);
				if (!ags_list[g_pos]) {
					struct group *g = getgrnam(usl->value);
					if (g) {
						ags_list[g_pos] = g->gr_gid;
					}
					else {
						upsgi_log("unable to find group %s\n", usl->value);
						exit(1);
					}
				}
				g_pos++;
			}
			if (setgroups(ags, ags_list)) {
				upsgi_error("setgroups()");
				exit(1);
			}
		}
		int additional_groups = getgroups(0, NULL);
		if (additional_groups > 0) {
			gid_t *gids = upsgi_calloc(sizeof(gid_t) * additional_groups);
			if (getgroups(additional_groups, gids) > 0) {
				int i;
				for (i = 0; i < additional_groups; i++) {
					if (gids[i] == upsgi.gid)
						continue;
					struct group *gr = getgrgid(gids[i]);
					if (gr) {
						upsgi_log("set additional group %d (%s)\n", gids[i], gr->gr_name);
					}
					else {
						upsgi_log("set additional group %d\n", gids[i]);
					}
				}
			}
			free(gids);
		}
	}
	if (upsgi.uid) {
		if (!upsgi.master_as_root)
			upsgi_log("setuid() to %d\n", upsgi.uid);
		if (setuid(upsgi.uid)) {
			upsgi_error("setuid()");
			exit(1);
		}
	}

#ifndef __RUMP__
	if (!getuid()) {
		upsgi_log_initial("*** WARNING: you are running upsgi as root !!! (use the --uid flag) *** \n");
	}
#endif

#ifdef UPSGI_CAP

	if (upsgi.cap && upsgi.cap_count > 0 && !upsgi.reloads) {

		cap_t caps = cap_init();

		if (!caps) {
			upsgi_error("cap_init()");
			exit(1);
		}
		cap_clear(caps);

		cap_set_flag(caps, CAP_EFFECTIVE, upsgi.cap_count, upsgi.cap, CAP_SET);
		cap_set_flag(caps, CAP_PERMITTED, upsgi.cap_count, upsgi.cap, CAP_SET);
		cap_set_flag(caps, CAP_INHERITABLE, upsgi.cap_count, upsgi.cap, CAP_SET);

		if (cap_set_proc(caps) < 0) {
			upsgi_error("cap_set_proc()");
			exit(1);
		}
		cap_free(caps);
	}
#endif

	if (upsgi.refork) {
		upsgi_log("re-fork()ing...\n");
		pid_t pid = fork();
		if (pid < 0) {
			upsgi_error("fork()");
			exit(1);
		}
		if (pid > 0) {
			// block all signals
			sigset_t smask;
			sigfillset(&smask);
			sigprocmask(SIG_BLOCK, &smask, NULL);
			int status;
			if (waitpid(pid, &status, 0) < 0) {
				upsgi_error("waitpid()");
			}
			_exit(0);
		}
	}

	upsgi_hooks_run(upsgi.hook_as_user, "as user", 1);

	// now run the scripts needed by the user
	upsgi_foreach(usl, upsgi.exec_as_user) {
		upsgi_log("running \"%s\" (as uid: %d gid: %d) ...\n", usl->value, (int) getuid(), (int) getgid());
		int ret = upsgi_run_command_and_wait(NULL, usl->value);
		if (ret != 0) {
			upsgi_log("command \"%s\" exited with non-zero code: %d\n", usl->value, ret);
			exit(1);
		}
	}

	upsgi_foreach(usl, upsgi.call_as_user) {
		if (upsgi_call_symbol(usl->value)) {
			upsgi_log("unable to call function \"%s\"\n", usl->value);
			exit(1);
		}
	}

	// we could now patch the binary
	if (upsgi.unprivileged_binary_patch) {
		upsgi.argv[0] = upsgi.unprivileged_binary_patch;
		execvp(upsgi.unprivileged_binary_patch, upsgi.argv);
		upsgi_error("execvp()");
		exit(1);
	}

	if (upsgi.unprivileged_binary_patch_arg) {
		upsgi_exec_command_with_args(upsgi.unprivileged_binary_patch_arg);
	}
	return;

nonroot:
	if (upsgi.chroot && !upsgi.is_chrooted && !upsgi.is_a_reload) {
		upsgi_log("cannot chroot() as non-root user\n");
		exit(1);
	}
	if (upsgi.gid && getgid() != upsgi.gid) {
		upsgi_log("cannot setgid() as non-root user\n");
		exit(1);
	}
	if (upsgi.uid && getuid() != upsgi.uid) {
		upsgi_log("cannot setuid() as non-root user\n");
		exit(1);
	}
}

static void close_and_free_request(struct wsgi_request *wsgi_req) {

	// close the connection with the client
        if (!wsgi_req->fd_closed) {
                // NOTE, if we close the socket before receiving eventually sent data, socket layer will send a RST
                wsgi_req->socket->proto_close(wsgi_req);
        }

        if (wsgi_req->post_file) {
                fclose(wsgi_req->post_file);
        }

        if (wsgi_req->post_read_buf) {
                free(wsgi_req->post_read_buf);
        }

        if (wsgi_req->post_readline_buf) {
                free(wsgi_req->post_readline_buf);
        }

        if (wsgi_req->post_buffering_buf && upsgi.post_buffering > 0) {
                char *shared_post_buf = upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].post_buf;
                if (wsgi_req->post_buffering_buf != shared_post_buf) {
                        free(wsgi_req->post_buffering_buf);
                }
        }

        if (wsgi_req->proto_parser_buf) {
                free(wsgi_req->proto_parser_buf);
        }

}

// destroy a request
void upsgi_destroy_request(struct wsgi_request *wsgi_req) {

	close_and_free_request(wsgi_req);

	// reset for avoiding following requests to fail on non-upsgi protocols
	// thanks Marko Tiikkaja for catching it
	wsgi_req->uh->_pktsize = 0;

	// some plugins expected async_id to be defined before setup
        int tmp_id = wsgi_req->async_id;
        memset(wsgi_req, 0, sizeof(struct wsgi_request));
        wsgi_req->async_id = tmp_id;
}

// finalize/close/free a request
void upsgi_close_request(struct wsgi_request *wsgi_req) {

	int waitpid_status;
	int tmp_id;
	uint64_t tmp_rt, rss = 0, vsz = 0;
#ifdef __linux__
	uint64_t uss = 0, pss = 0;
#endif

	// apply transformations
	if (wsgi_req->transformations) {
		if (upsgi_apply_final_transformations(wsgi_req) == 0) {
			if (wsgi_req->transformed_chunk && wsgi_req->transformed_chunk_len > 0) {
				upsgi_response_write_body_do(wsgi_req, wsgi_req->transformed_chunk, wsgi_req->transformed_chunk_len);
			}
		}
		upsgi_free_transformations(wsgi_req);
	}

	// check if headers should be sent
	if (wsgi_req->headers) {
		if (!wsgi_req->headers_sent && !wsgi_req->headers_size && !wsgi_req->response_size) {
			upsgi_response_write_headers_do(wsgi_req);
		}
		upsgi_buffer_destroy(wsgi_req->headers);
	}

	uint64_t end_of_request = upsgi_micros();
	wsgi_req->end_of_request = end_of_request;

	if (!wsgi_req->do_not_account_avg_rt) {
		tmp_rt = wsgi_req->end_of_request - wsgi_req->start_of_request;
		upsgi.workers[upsgi.mywid].running_time += tmp_rt;
		upsgi.workers[upsgi.mywid].avg_response_time = (upsgi.workers[upsgi.mywid].avg_response_time + tmp_rt) / 2;
	}

	// get memory usage
	if (upsgi.logging_options.memory_report || upsgi.force_get_memusage) {
		get_memusage(&rss, &vsz);
		upsgi.workers[upsgi.mywid].vsz_size = vsz;
		upsgi.workers[upsgi.mywid].rss_size = rss;
	}

#ifdef __linux__
	if (upsgi.logging_options.memory_report || upsgi.reload_on_uss || upsgi.reload_on_pss) {
		get_memusage_extra(&uss, &pss);
		upsgi.workers[upsgi.mywid].uss_size = uss;
		upsgi.workers[upsgi.mywid].pss_size = pss;
	}
#endif

	if (!wsgi_req->do_not_account) {
		upsgi.workers[0].requests++;
		upsgi.workers[upsgi.mywid].requests++;
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].requests++;
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].write_errors += wsgi_req->write_errors;
		upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].read_errors += wsgi_req->read_errors;
		// this is used for MAX_REQUESTS
		upsgi.workers[upsgi.mywid].delta_requests++;
	}

#ifdef UPSGI_ROUTING
	// apply final routes after accounting
	upsgi_apply_final_routes(wsgi_req);
#endif

	// close socket and free parsers-allocated memory
	close_and_free_request(wsgi_req);

	// after_request hook
	if (!wsgi_req->is_raw && upsgi.p[wsgi_req->uh->modifier1]->after_request)
		upsgi.p[wsgi_req->uh->modifier1]->after_request(wsgi_req);

	// after_request custom hooks
	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, upsgi.after_request_hooks) {
		void (*func) (struct wsgi_request *) = (void (*)(struct wsgi_request *)) usl->custom_ptr;
		func(wsgi_req);
	}

	// leave harakiri mode
	if (upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].harakiri > 0) {
		set_harakiri(wsgi_req, 0);
	}

	// leave user harakiri mode
	if (upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].user_harakiri > 0) {
		set_user_harakiri(wsgi_req, 0);
	}

	if (!wsgi_req->do_not_account) {
		// this is racy in multithread mode
		if (wsgi_req->response_size > 0) {
			upsgi.workers[upsgi.mywid].tx += wsgi_req->response_size;
		}
		if (wsgi_req->headers_size > 0) {
			upsgi.workers[upsgi.mywid].tx += wsgi_req->headers_size;
		}
	}

	// defunct process reaper
	if (upsgi.reaper == 1) {
		while (waitpid(WAIT_ANY, &waitpid_status, WNOHANG) > 0);
	}

	// free logvars
	struct upsgi_logvar *lv = wsgi_req->logvars;
	while (lv) {
		struct upsgi_logvar *ptr = lv;
		lv = lv->next;
		free(ptr);
	}

	// free additional headers
	struct upsgi_string_list *ah = wsgi_req->additional_headers;
	while (ah) {
		struct upsgi_string_list *ptr = ah;
		ah = ah->next;
		free(ptr->value);
		free(ptr);
	}
	// free remove headers
	ah = wsgi_req->remove_headers;
	while (ah) {
		struct upsgi_string_list *ptr = ah;
		ah = ah->next;
		free(ptr->value);
		free(ptr);
	}

	// free chunked input
	if (wsgi_req->chunked_input_buf) {
		upsgi_buffer_destroy(wsgi_req->chunked_input_buf);
	}

	if (wsgi_req->body_chunked_buf) {
		upsgi_buffer_destroy(wsgi_req->body_chunked_buf);
	}

	// free websocket engine
	if (wsgi_req->websocket_buf) {
		upsgi_buffer_destroy(wsgi_req->websocket_buf);
	}
	if (wsgi_req->websocket_send_buf) {
		upsgi_buffer_destroy(wsgi_req->websocket_send_buf);
	}


	// reset request
	wsgi_req->uh->_pktsize = 0;
	tmp_id = wsgi_req->async_id;
	memset(wsgi_req, 0, sizeof(struct wsgi_request));
	// some plugins expected async_id to be defined before setup
	wsgi_req->async_id = tmp_id;
	// yes, this is pretty useless but we cannot ensure all of the plugin have the same behaviour
	upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].in_request = 0;

	if (upsgi.max_requests > 0 && upsgi.workers[upsgi.mywid].delta_requests >= (upsgi.max_requests + ((upsgi.mywid-1) * upsgi.max_requests_delta))
	    && (end_of_request - (upsgi.workers[upsgi.mywid].last_spawn * 1000000) >= upsgi.min_worker_lifetime * 1000000)) {
		goodbye_cruel_world("max requests reached (%llu >= %llu)",
			(unsigned long long) upsgi.workers[upsgi.mywid].delta_requests,
			(unsigned long long) (upsgi.max_requests + ((upsgi.mywid-1) * upsgi.max_requests_delta))
		);
	}

	if (upsgi.reload_on_as && (rlim_t) vsz >= upsgi.reload_on_as && (end_of_request - (upsgi.workers[upsgi.mywid].last_spawn * 1000000) >= upsgi.min_worker_lifetime * 1000000)) {
		goodbye_cruel_world("reload-on-as limit reached (%llu >= %llu)",
			(unsigned long long) (rlim_t) vsz,
			(unsigned long long) upsgi.reload_on_as
		);
	}

	if (upsgi.reload_on_rss && (rlim_t) rss >= upsgi.reload_on_rss && (end_of_request - (upsgi.workers[upsgi.mywid].last_spawn * 1000000) >= upsgi.min_worker_lifetime * 1000000)) {
		goodbye_cruel_world("reload-on-rss limit reached (%llu >= %llu)",
			(unsigned long long) (rlim_t) rss,
			(unsigned long long) upsgi.reload_on_rss
		);
	}

#ifdef __linux__
	if (upsgi.reload_on_uss && (rlim_t) uss >= upsgi.reload_on_uss && (end_of_request - (upsgi.workers[upsgi.mywid].last_spawn * 1000000) >= upsgi.min_worker_lifetime * 1000000)) {
		goodbye_cruel_world("reload-on-uss limit reached (%llu >= %llu)",
			(unsigned long long) (rlim_t) uss,
			(unsigned long long) upsgi.reload_on_uss
		);
	}

	if (upsgi.reload_on_pss && (rlim_t) pss >= upsgi.reload_on_pss && (end_of_request - (upsgi.workers[upsgi.mywid].last_spawn * 1000000) >= upsgi.min_worker_lifetime * 1000000)) {
		goodbye_cruel_world("reload-on-pss limit reached (%llu >= %llu)",
			(unsigned long long) (rlim_t) pss,
			(unsigned long long) upsgi.reload_on_pss
		);
	}
#endif

	// after the first request, if i am a vassal, signal Emperor about my loyalty
	if (upsgi.has_emperor && !upsgi.loyal) {
		upsgi_log("announcing my loyalty to the Emperor...\n");
		char byte = 17;
		if (write(upsgi.emperor_fd, &byte, 1) != 1) {
			upsgi_error("write()");
		}
		upsgi.loyal = 1;
	}

#ifdef __linux__
#ifdef MADV_MERGEABLE
	// run the ksm mapper
	if (upsgi.linux_ksm > 0 && (upsgi.workers[upsgi.mywid].requests % upsgi.linux_ksm) == 0) {
		upsgi_linux_ksm_map();
	}
#endif
#endif

}

#ifdef __linux__
#ifdef MADV_MERGEABLE

void upsgi_linux_ksm_map(void) {

	int dirty = 0;
	size_t i;
	unsigned long long start = 0, end = 0;
	int errors = 0;
	int lines = 0;

	int fd = open("/proc/self/maps", O_RDONLY);
	if (fd < 0) {
		upsgi_error_open("[upsgi-KSM] /proc/self/maps");
		return;
	}

	// allocate memory if not available;
	if (upsgi.ksm_mappings_current == NULL) {
		if (!upsgi.ksm_buffer_size)
			upsgi.ksm_buffer_size = 32768;
		upsgi.ksm_mappings_current = upsgi_malloc(upsgi.ksm_buffer_size);
		upsgi.ksm_mappings_current_size = 0;
	}
	if (upsgi.ksm_mappings_last == NULL) {
		if (!upsgi.ksm_buffer_size)
			upsgi.ksm_buffer_size = 32768;
		upsgi.ksm_mappings_last = upsgi_malloc(upsgi.ksm_buffer_size);
		upsgi.ksm_mappings_last_size = 0;
	}

	upsgi.ksm_mappings_current_size = read(fd, upsgi.ksm_mappings_current, upsgi.ksm_buffer_size);
	close(fd);
	if (upsgi.ksm_mappings_current_size <= 0) {
		upsgi_log("[upsgi-KSM] unable to read /proc/self/maps data\n");
		return;
	}

	// we now have areas
	if (upsgi.ksm_mappings_last_size == 0 || upsgi.ksm_mappings_current_size != upsgi.ksm_mappings_last_size) {
		dirty = 1;
	}
	else {
		if (memcmp(upsgi.ksm_mappings_current, upsgi.ksm_mappings_last, upsgi.ksm_mappings_current_size) != 0) {
			dirty = 1;
		}
	}

	// it is dirty, swap addresses and parse it
	if (dirty) {
		char *tmp = upsgi.ksm_mappings_last;
		upsgi.ksm_mappings_last = upsgi.ksm_mappings_current;
		upsgi.ksm_mappings_current = tmp;

		size_t tmp_size = upsgi.ksm_mappings_last_size;
		upsgi.ksm_mappings_last_size = upsgi.ksm_mappings_current_size;
		upsgi.ksm_mappings_current_size = tmp_size;

		// scan each line and call madvise on it
		char *ptr = upsgi.ksm_mappings_last;
		for (i = 0; i < upsgi.ksm_mappings_last_size; i++) {
			if (upsgi.ksm_mappings_last[i] == '\n') {
				lines++;
				upsgi.ksm_mappings_last[i] = 0;
				if (sscanf(ptr, "%llx-%llx %*s", &start, &end) == 2) {
					if (madvise((void *) (long) start, (size_t) (end - start), MADV_MERGEABLE)) {
						errors++;
					}
				}
				upsgi.ksm_mappings_last[i] = '\n';
				ptr = upsgi.ksm_mappings_last + i + 1;
			}
		}

		if (errors >= lines) {
			upsgi_error("[upsgi-KSM] unable to share pages");
		}
	}
}
#endif
#endif

#ifdef __linux__
long upsgi_num_from_file(char *filename, int quiet) {
	char buf[16];
	ssize_t len;
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		if (!quiet)
			upsgi_error_open(filename);
		return -1L;
	}
	len = read(fd, buf, sizeof(buf));
	if (len == 0) {
		if (!quiet)
			upsgi_log("read error %s\n", filename);
		close(fd);
		return -1L;
	}
	close(fd);
	return strtol(buf, (char **) NULL, 10);
}
#endif

// setup for a new request
void wsgi_req_setup(struct wsgi_request *wsgi_req, int async_id, struct upsgi_socket *upsgi_sock) {

	wsgi_req->app_id = -1;

	wsgi_req->async_id = async_id;
	wsgi_req->sendfile_fd = -1;

	wsgi_req->hvec = upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].hvec;
	// skip the first 4 bytes;
	wsgi_req->uh = (struct upsgi_header *) upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].buffer;
	wsgi_req->buffer = upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].buffer + 4;

	if (upsgi.post_buffering > 0) {
		wsgi_req->post_buffering_buf = upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].post_buf;
	}

	if (upsgi_sock) {
		wsgi_req->socket = upsgi_sock;
	}

	upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].in_request = 0;

	// now check for suspend request
	if (upsgi.workers[upsgi.mywid].suspended == 1) {
		upsgi_log_verbose("*** worker %d suspended ***\n", upsgi.mywid);
cycle:
		// wait for some signal (normally SIGTSTP) or 10 seconds (as fallback)
		(void) poll(NULL, 0, 10 * 1000);
		if (upsgi.workers[upsgi.mywid].suspended == 1)
			goto cycle;
		upsgi_log_verbose("*** worker %d resumed ***\n", upsgi.mywid);
	}
}

int wsgi_req_async_recv(struct wsgi_request *wsgi_req) {

	upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].in_request = 1;

	wsgi_req->start_of_request = upsgi_micros();
	wsgi_req->start_of_request_in_sec = wsgi_req->start_of_request / 1000000;

	if (!wsgi_req->do_not_add_to_async_queue) {
		if (event_queue_add_fd_read(upsgi.async_queue, wsgi_req->fd) < 0)
			return -1;

		async_add_timeout(wsgi_req, upsgi.socket_timeout);
		upsgi.async_proto_fd_table[wsgi_req->fd] = wsgi_req;
	}

	// enter harakiri mode
	if (upsgi.harakiri_options.workers > 0) {
		set_harakiri(wsgi_req, upsgi.harakiri_options.workers);
	}

	return 0;
}

// receive a new request
int wsgi_req_recv(int queue, struct wsgi_request *wsgi_req) {

	upsgi.workers[upsgi.mywid].cores[wsgi_req->async_id].in_request = 1;

	wsgi_req->start_of_request = upsgi_micros();
	wsgi_req->start_of_request_in_sec = wsgi_req->start_of_request / 1000000;

	// edge triggered sockets get the whole request during accept() phase
	if (!wsgi_req->socket->edge_trigger) {
		for (;;) {
			int ret = wsgi_req->socket->proto(wsgi_req);
			if (ret == UPSGI_OK)
				break;
			if (ret == UPSGI_AGAIN) {
				ret = upsgi_wait_read_req(wsgi_req);
				if (ret <= 0)
					return -1;
				continue;
			}
			return -1;
		}
	}

	// enter harakiri mode
	if (upsgi.harakiri_options.workers > 0) {
		set_harakiri(wsgi_req, upsgi.harakiri_options.workers);
	}

#ifdef UPSGI_ROUTING
	if (upsgi_apply_routes(wsgi_req) == UPSGI_ROUTE_BREAK)
		return 0;
#endif

	wsgi_req->async_status = upsgi.p[wsgi_req->uh->modifier1]->request(wsgi_req);

	return 0;
}

void upsgi_post_accept(struct wsgi_request *wsgi_req) {

	// set close on exec (if not a new socket)
	if (!wsgi_req->socket->edge_trigger && upsgi.close_on_exec) {
		if (fcntl(wsgi_req->fd, F_SETFD, FD_CLOEXEC) < 0) {
			upsgi_error("fcntl()");
		}
	}

	// enable TCP_NODELAY ?
	if (upsgi.tcp_nodelay) {
		upsgi_tcp_nodelay(wsgi_req->fd);
	}
}

// accept a new request
int wsgi_req_simple_accept(struct wsgi_request *wsgi_req, int fd) {

	wsgi_req->fd = wsgi_req->socket->proto_accept(wsgi_req, fd);

	if (wsgi_req->fd < 0) {
		return -1;
	}

	upsgi_post_accept(wsgi_req);

	return 0;
}

// send heartbeat to the emperor
void upsgi_heartbeat() {

	if (!upsgi.has_emperor)
		return;

	time_t now = upsgi_now();
	if (upsgi.next_heartbeat <= now) {
		char byte = 26;
		if (write(upsgi.emperor_fd, &byte, 1) != 1) {
			upsgi_error("write()");
		}
		upsgi.next_heartbeat = now + upsgi.heartbeat;
	}

}

// accept a request
int wsgi_req_accept(int queue, struct wsgi_request *wsgi_req) {

	int ret;
	int interesting_fd = -1;
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	int timeout = -1;


	thunder_lock;

	// Recheck the manage_next_request before going forward.
	// This is because the worker might get cheaped while it's
	// blocking on the thunder_lock, because thunder_lock is
	// not interruptable, it'll slow down the cheaping process
	// (the worker will handle the next request before shuts down).
	if (!upsgi.workers[upsgi.mywid].manage_next_request) {
		thunder_unlock;
		return -1;
	}

	// heartbeat
	// in multithreaded mode we are now locked
	if (upsgi.has_emperor && upsgi.heartbeat) {
		time_t now = upsgi_now();
		// overengineering ... (reduce skew problems)
		timeout = upsgi.heartbeat;
		if (!upsgi.next_heartbeat) {
			upsgi.next_heartbeat = now;
		}
		if (upsgi.next_heartbeat >= now) {
			timeout = upsgi.next_heartbeat - now;
		}
	}

	// need edge trigger ?
	if (upsgi.is_et) {
		while (upsgi_sock) {
			if (upsgi_sock->retry && upsgi_sock->retry[wsgi_req->async_id]) {
				timeout = 0;
				break;
			}
			upsgi_sock = upsgi_sock->next;
		}
		// reset pointer
		upsgi_sock = upsgi.sockets;
	}

	ret = event_queue_wait(queue, timeout, &interesting_fd);
	if (ret < 0) {
		thunder_unlock;
		return -1;
	}

	// check for heartbeat
	if (upsgi.has_emperor && upsgi.heartbeat) {
		upsgi_heartbeat();
		// no need to continue if timed-out
		if (ret == 0) {
			thunder_unlock;
			return -1;
		}
	}

	if (upsgi.signal_socket > -1 && (interesting_fd == upsgi.signal_socket || interesting_fd == upsgi.my_signal_socket)) {

		thunder_unlock;

		upsgi_receive_signal(wsgi_req, interesting_fd, "worker", upsgi.mywid);

		return -1;
	}


	while (upsgi_sock) {
		if (interesting_fd == upsgi_sock->fd || (upsgi_sock->retry && upsgi_sock->retry[wsgi_req->async_id]) || (upsgi_sock->fd_threads && interesting_fd == upsgi_sock->fd_threads[wsgi_req->async_id])) {
			wsgi_req->socket = upsgi_sock;
			wsgi_req->fd = wsgi_req->socket->proto_accept(wsgi_req, interesting_fd);
			thunder_unlock;
			if (wsgi_req->fd < 0) {
				return -1;
			}

			if (!upsgi_sock->edge_trigger) {
				upsgi_post_accept(wsgi_req);
			}

			return 0;
		}

		upsgi_sock = upsgi_sock->next;
	}

	thunder_unlock;
	return -1;
}

// translate a OS env to a upsgi option
void env_to_arg(char *src, char *dst) {
	int i;
	int val = 0;

	for (i = 0; i < (int) strlen(src); i++) {
		if (src[i] == '=') {
			val = 1;
		}
		if (val) {
			dst[i] = src[i];
		}
		else {
			dst[i] = tolower((int) src[i]);
			if (dst[i] == '_') {
				dst[i] = '-';
			}
		}
	}

	dst[strlen(src)] = 0;
}

// parse OS envs
void parse_sys_envs(char **envs) {

	char **uenvs = envs;
	char *earg, *eq_pos;

	while (*uenvs) {
		if (!strncmp(*uenvs, "UPSGI_", 6) && strncmp(*uenvs, "UPSGI_RELOADS=", 14) && strncmp(*uenvs, "UPSGI_VASSALS_DIR=", 18) && strncmp(*uenvs, "UPSGI_EMPEROR_FD=", 17) && strncmp(*uenvs, "UPSGI_BROODLORD_NUM=", 20) && strncmp(*uenvs, "UPSGI_EMPEROR_FD_CONFIG=", 24) && strncmp(*uenvs, "UPSGI_EMPEROR_PROXY=", 20) && strncmp(*uenvs, "UPSGI_JAIL_PID=", 15) && strncmp(*uenvs, "UPSGI_ORIGINAL_PROC_NAME=", 25)) {
			earg = upsgi_malloc(strlen(*uenvs + 6) + 1);
			env_to_arg(*uenvs + 6, earg);
			eq_pos = strchr(earg, '=');
			if (!eq_pos) {
				break;
			}
			eq_pos[0] = 0;

			add_exported_option(earg, eq_pos + 1, 0);
		}
		uenvs++;
	}

}

// get the application id
int upsgi_get_app_id(struct wsgi_request *wsgi_req, char *key, uint16_t key_len, int modifier1) {

	int i;
	struct stat st;
	int found;

	char *app_name = key;
	uint16_t app_name_len = key_len;

	if (app_name_len == 0 && wsgi_req) {
		app_name = wsgi_req->appid;
		app_name_len = wsgi_req->appid_len;
		if (app_name_len == 0) {
			if (!upsgi.ignore_script_name) {
				app_name = wsgi_req->script_name;
				app_name_len = wsgi_req->script_name_len;
			}

			if (upsgi.vhost) {
				char *vhost_name = upsgi_concat3n(wsgi_req->host, wsgi_req->host_len, "|", 1, wsgi_req->script_name, wsgi_req->script_name_len);
				app_name_len = wsgi_req->host_len + 1 + wsgi_req->script_name_len;
				app_name = upsgi_req_append(wsgi_req, "UPSGI_APPID", 11, vhost_name, app_name_len);
				free(vhost_name);
				if (!app_name) {
					upsgi_log("unable to add UPSGI_APPID to the upsgi buffer, consider increasing it\n");
					return -1;
				}
#ifdef UPSGI_DEBUG
				upsgi_debug("VirtualHost KEY=%.*s\n", app_name_len, app_name);
#endif
			}
			wsgi_req->appid = app_name;
			wsgi_req->appid_len = app_name_len;
		}
	}


	for (i = 0; i < upsgi_apps_cnt; i++) {
		// reset check
		found = 0;
#ifdef UPSGI_DEBUG
		upsgi_log("searching for %.*s in %.*s %p\n", app_name_len, app_name, upsgi_apps[i].mountpoint_len, upsgi_apps[i].mountpoint, upsgi_apps[i].callable);
#endif
		if (!upsgi_apps[i].callable) {
			continue;
		}

		if (!upsgi_strncmp(upsgi_apps[i].mountpoint, upsgi_apps[i].mountpoint_len, app_name, app_name_len)) {
			found = 1;
		}

		if (found) {
			if (upsgi_apps[i].touch_reload[0]) {
				if (!stat(upsgi_apps[i].touch_reload, &st)) {
					if (st.st_mtime != upsgi_apps[i].touch_reload_mtime) {
						// serve the new request and reload
						upsgi.workers[upsgi.mywid].manage_next_request = 0;
						if (upsgi.threads > 1) {
							upsgi.workers[upsgi.mywid].destroy = 1;
						}

#ifdef UPSGI_DEBUG
						upsgi_log("mtime %d %d\n", st.st_mtime, upsgi_apps[i].touch_reload_mtime);
#endif
					}
				}
			}
			if (modifier1 == -1)
				return i;
			if (modifier1 == upsgi_apps[i].modifier1)
				return i;
		}
	}

	return -1;
}

char *upsgi_substitute(char *src, char *what, char *with) {

	int count = 0;
	if (!with)
		return src;

	size_t len = strlen(src);
	size_t wlen = strlen(what);
	size_t with_len = strlen(with);

	char *p = strstr(src, what);
	if (!p) {
		return src;
	}

	while (p) {
		count++;
		p = strstr(p + wlen, what);
	}

	len += (count * with_len) + 1;

	char *dst = upsgi_calloc(len);
	char *ptr = src;
	char *dst_ptr = dst;

	p = strstr(ptr, what);
	while (p) {
		memcpy(dst_ptr, ptr, p - ptr);
		dst_ptr += p - ptr;
		memcpy(dst_ptr, with, with_len);
		dst_ptr += with_len;
		ptr = p + wlen;
		p = strstr(ptr, what);
	}

	snprintf(dst_ptr, strlen(ptr) + 1, "%s", ptr);

	return dst;
}

int upsgi_is_file(char *filename) {
	struct stat st;
	if (stat(filename, &st)) {
		return 0;
	}
	if (S_ISREG(st.st_mode))
		return 1;
	return 0;
}

int upsgi_is_file2(char *filename, struct stat *st) {
	if (stat(filename, st)) {
		return 0;
	}
	if (S_ISREG(st->st_mode))
		return 1;
	return 0;
}


int upsgi_is_dir(char *filename) {
	struct stat st;
	if (stat(filename, &st)) {
		return 0;
	}
	if (S_ISDIR(st.st_mode))
		return 1;
	return 0;
}

int upsgi_is_link(char *filename) {
	struct stat st;
	if (lstat(filename, &st)) {
		return 0;
	}
	if (S_ISLNK(st.st_mode))
		return 1;
	return 0;
}

void *upsgi_malloc(size_t size) {

	char *ptr = malloc(size);
	if (ptr == NULL) {
		upsgi_error("malloc()");
		upsgi_log("!!! tried memory allocation of %llu bytes !!!\n", (unsigned long long) size);
		upsgi_backtrace(upsgi.backtrace_depth);
		exit(1);
	}

	return ptr;
}

void *upsgi_calloc(size_t size) {
	// thanks Mathieu Dupuy for pointing out that calloc is faster
	// than malloc + memset
	char *ptr = calloc(1, size);
	if (ptr == NULL) {
		upsgi_error("calloc()");
		upsgi_log("!!! tried memory allocation of %llu bytes !!!\n", (unsigned long long) size);
		upsgi_backtrace(upsgi.backtrace_depth);
		exit(1);
	}
	return ptr;
}

#ifdef AF_INET6
#define ADDR_AF_INET_FAMILY(addrtype) (addrtype == AF_INET || addrtype == AF_INET6)
#else
#define ADDR_AF_INET_FAMILY(addrtype) (addrtype == AF_INET)
#endif

char *upsgi_resolve_ip(char *domain) {

	struct hostent *he;

	he = gethostbyname(domain);
	if (!he || !*he->h_addr_list || !ADDR_AF_INET_FAMILY(he->h_addrtype)) {
		return NULL;
	}

	return inet_ntoa(*(struct in_addr *) he->h_addr_list[0]);
}

int upsgi_file_exists(char *filename) {
	// TODO check for http url or stdin
	return !access(filename, R_OK);
}

int upsgi_file_executable(char *filename) {
	// TODO check for http url or stdin
	return !access(filename, R_OK | X_OK);
}

char *magic_sub(char *buffer, size_t len, size_t * size, char *magic_table[]) {

	size_t i;
	size_t magic_len = 0;
	char *magic_buf = upsgi_malloc(len);
	char *magic_ptr = magic_buf;
	char *old_magic_buf;

	for (i = 0; i < len; i++) {
		if (buffer[i] == '%' && (i + 1) < len && magic_table[(unsigned char) buffer[i + 1]]) {
			old_magic_buf = magic_buf;
			magic_buf = upsgi_concat3n(old_magic_buf, magic_len, magic_table[(unsigned char) buffer[i + 1]], strlen(magic_table[(unsigned char) buffer[i + 1]]), buffer + i + 2, len - i - 2);
			free(old_magic_buf);
			magic_len += strlen(magic_table[(unsigned char) buffer[i + 1]]);
			magic_ptr = magic_buf + magic_len;
			i++;
		}
		else {
			*magic_ptr = buffer[i];
			magic_ptr++;
			magic_len++;
		}
	}

	*size = magic_len;

	return magic_buf;

}

void init_magic_table(char *magic_table[]) {

	int i;
	for (i = 0; i <= 0xff; i++) {
		magic_table[i] = "";
	}

	magic_table['%'] = "%";
	magic_table['('] = "%(";
}

char *upsgi_num2str(int num) {

	char *str = upsgi_malloc(11);

	snprintf(str, 11, "%d", num);
	return str;
}

char *upsgi_float2str(float num) {

	char *str = upsgi_malloc(11);

	snprintf(str, 11, "%f", num);
	return str;
}

char *upsgi_64bit2str(int64_t num) {
	char *str = upsgi_malloc(sizeof(MAX64_STR) + 1);
	snprintf(str, sizeof(MAX64_STR) + 1, "%lld", (long long) num);
	return str;
}

char *upsgi_size2str(size_t num) {
	char *str = upsgi_malloc(sizeof(UMAX64_STR) + 1);
	snprintf(str, sizeof(UMAX64_STR) + 1, "%llu", (unsigned long long) num);
	return str;	
}

int upsgi_num2str2(int num, char *ptr) {

	return snprintf(ptr, 11, "%d", num);
}

int upsgi_num2str2n(int num, char *ptr, int size) {
	return snprintf(ptr, size, "%d", num);
}

int upsgi_long2str2n(unsigned long long num, char *ptr, int size) {
	int ret = snprintf(ptr, size, "%llu", num);
	if (ret <= 0 || ret > size)
		return 0;
	return ret;
}

int is_unix(char *socket_name, int len) {
	return !memchr(socket_name, ':', len);
}

int is_a_number(char *what) {
	int i;

	for (i = 0; i < (int) strlen(what); i++) {
		if (!isdigit((int) what[i]))
			return 0;
	}

	return 1;
}

void upsgi_unix_signal(int signum, void (*func) (int)) {

	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));

	sa.sa_handler = func;

	sigemptyset(&sa.sa_mask);

	if (sigaction(signum, &sa, NULL) < 0) {
		upsgi_error("sigaction()");
	}
}

int upsgi_list_has_num(char *list, int num) {

	char *list2 = upsgi_concat2(list, "");
	char *p, *ctx = NULL;
	upsgi_foreach_token(list2, ",", p, ctx) {
		if (atoi(p) == num) {
			free(list2);
			return 1;
		}
	}

	free(list2);
	return 0;
}

int upsgi_list_has_str(char *list, char *str) {

	char *list2 = upsgi_str(list);
	char *p, *ctx = NULL;
	upsgi_foreach_token(list2, " ", p, ctx) {
		if (!strcasecmp(p, str)) {
			free(list2);
			return 1;
		}
	}

	free(list2);
	return 0;
}

static char hex2num(char *str) {

	char val = 0;

	val <<= 4;

	if (str[0] >= '0' && str[0] <= '9') {
		val += str[0] & 0x0F;
	}
	else if (str[0] >= 'A' && str[0] <= 'F') {
		val += (str[0] & 0x0F) + 9;
	}
	else if (str[0] >= 'a' && str[0] <= 'f') {
		val += (str[0] & 0x0F) + 9;
	}
	else {
		return 0;
	}

	val <<= 4;

	if (str[1] >= '0' && str[1] <= '9') {
		val += str[1] & 0x0F;
	}
	else if (str[1] >= 'A' && str[1] <= 'F') {
		val += (str[1] & 0x0F) + 9;
	}
	else if (str[1] >= 'a' && str[1] <= 'f') {
		val += (str[1] & 0x0F) + 9;
	}
	else {
		return 0;
	}

	return val;
}

int upsgi_str2_num(char *str) {

	int num = 0;

	num = 10 * (str[0] - 48);
	num += str[1] - 48;

	return num;
}

int upsgi_str3_num(char *str) {

	int num = 0;

	num = 100 * (str[0] - 48);
	num += 10 * (str[1] - 48);
	num += str[2] - 48;

	return num;
}


int upsgi_str4_num(char *str) {

	int num = 0;

	num = 1000 * (str[0] - 48);
	num += 100 * (str[1] - 48);
	num += 10 * (str[2] - 48);
	num += str[3] - 48;

	return num;
}

uint64_t upsgi_str_num(char *str, int len) {

	int i;
	uint64_t num = 0;

	uint64_t delta = pow(10, len);

	for (i = 0; i < len; i++) {
		delta = delta / 10;
		num += delta * (str[i] - 48);
	}

	return num;
}

char *upsgi_split3(char *buf, size_t len, char sep, char **part1, size_t * part1_len, char **part2, size_t * part2_len, char **part3, size_t * part3_len) {

	size_t i;
	int status = 0;

	*part1 = NULL;
	*part2 = NULL;
	*part3 = NULL;

	for (i = 0; i < len; i++) {
		if (buf[i] == sep) {
			// get part1
			if (status == 0) {
				*part1 = buf;
				*part1_len = i;
				status = 1;
			}
			// get part2
			else if (status == 1) {
				*part2 = *part1 + *part1_len + 1;
				*part2_len = (buf + i) - *part2;
				break;
			}
		}
	}

	if (*part1 && *part2) {
		if (*part2 + *part2_len + 1 > buf + len) {
			return NULL;
		}
		*part3 = *part2 + *part2_len + 1;
		*part3_len = (buf + len) - *part3;
		return buf + len;
	}

	return NULL;
}

char *upsgi_split4(char *buf, size_t len, char sep, char **part1, size_t * part1_len, char **part2, size_t * part2_len, char **part3, size_t * part3_len, char **part4, size_t * part4_len) {

	size_t i;
	int status = 0;

	*part1 = NULL;
	*part2 = NULL;
	*part3 = NULL;
	*part4 = NULL;

	for (i = 0; i < len; i++) {
		if (buf[i] == sep) {
			// get part1
			if (status == 0) {
				*part1 = buf;
				*part1_len = i;
				status = 1;
			}
			// get part2
			else if (status == 1) {
				*part2 = *part1 + *part1_len + 1;
				*part2_len = (buf + i) - *part2;
				status = 2;
			}
			// get part3
			else if (status == 2) {
				*part3 = *part2 + *part2_len + 1;
				*part3_len = (buf + i) - *part3;
				break;
			}
		}
	}

	if (*part1 && *part2 && *part3) {
		if (*part3 + *part3_len + 1 > buf + len) {
			return NULL;
		}
		*part4 = *part3 + *part3_len + 1;
		*part4_len = (buf + len) - *part4;
		return buf + len;
	}

	return NULL;
}


char *upsgi_netstring(char *buf, size_t len, char **netstring, size_t * netstring_len) {

	char *ptr = buf;
	char *watermark = buf + len;
	*netstring_len = 0;

	while (ptr < watermark) {
		// end of string size ?
		if (*ptr == ':') {
			*netstring_len = upsgi_str_num(buf, ptr - buf);

			if (ptr + *netstring_len + 2 > watermark) {
				return NULL;
			}
			*netstring = ptr + 1;
			return ptr + *netstring_len + 2;
		}
		ptr++;
	}

	return NULL;
}

struct upsgi_dyn_dict *upsgi_dyn_dict_new(struct upsgi_dyn_dict **dd, char *key, int keylen, char *val, int vallen) {

	struct upsgi_dyn_dict *upsgi_dd = *dd, *old_dd;

	if (!upsgi_dd) {
		*dd = upsgi_malloc(sizeof(struct upsgi_dyn_dict));
		upsgi_dd = *dd;
		upsgi_dd->prev = NULL;
	}
	else {
		while (upsgi_dd) {
			old_dd = upsgi_dd;
			upsgi_dd = upsgi_dd->next;
		}

		upsgi_dd = upsgi_malloc(sizeof(struct upsgi_dyn_dict));
		old_dd->next = upsgi_dd;
		upsgi_dd->prev = old_dd;
	}

	upsgi_dd->key = key;
	upsgi_dd->keylen = keylen;
	upsgi_dd->value = val;
	upsgi_dd->vallen = vallen;
	upsgi_dd->hits = 0;
	upsgi_dd->status = 0;
	upsgi_dd->next = NULL;

	return upsgi_dd;
}

void upsgi_dyn_dict_del(struct upsgi_dyn_dict *item) {

	struct upsgi_dyn_dict *prev = item->prev;
	struct upsgi_dyn_dict *next = item->next;

	if (prev) {
		prev->next = next;
	}

	if (next) {
		next->prev = prev;
	}

	free(item);
}

void upsgi_dyn_dict_free(struct upsgi_dyn_dict **dd) {
	struct upsgi_dyn_dict *attr = *dd;

	while(attr) {
		struct upsgi_dyn_dict *tmp = attr;
		attr = attr->next;
		if (tmp->value) free(tmp->value);
		free(tmp);
	}

	*dd = NULL;
}

void *upsgi_malloc_shared(size_t size) {

	void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);

	if (addr == MAP_FAILED) {
		upsgi_log("unable to allocate %llu bytes (%lluMB)\n", (unsigned long long) size, (unsigned long long) (size / (1024 * 1024)));
		upsgi_error("mmap()");
		exit(1);
	}

	return addr;
}

void *upsgi_calloc_shared(size_t size) {
	void *ptr = upsgi_malloc_shared(size);
	// NOTE by Mathieu Dupuy:
	// OSes guarantee mmap MAP_ANON memory area to be zero-filled (see man pages)

	// we should trust it, but history has taught us it is better to be paranoid.
	// Lucky enough this function is called ony in startup phases, so performance
	// tips/tricks are irrelevant (So, le'ts call memset...)
	memset(ptr, 0, size);
	return ptr;
}


struct upsgi_string_list *upsgi_string_new_list(struct upsgi_string_list **list, char *value) {

	struct upsgi_string_list *upsgi_string = *list, *old_upsgi_string;

	if (!upsgi_string) {
		*list = upsgi_malloc(sizeof(struct upsgi_string_list));
		upsgi_string = *list;
	}
	else {
		while (upsgi_string) {
			old_upsgi_string = upsgi_string;
			upsgi_string = upsgi_string->next;
		}

		upsgi_string = upsgi_malloc(sizeof(struct upsgi_string_list));
		old_upsgi_string->next = upsgi_string;
	}

	upsgi_string->value = value;
	upsgi_string->len = 0;
	if (value) {
		upsgi_string->len = strlen(value);
	}
	upsgi_string->next = NULL;
	upsgi_string->custom = 0;
	upsgi_string->custom2 = 0;
	upsgi_string->custom_ptr = NULL;

	return upsgi_string;
}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
struct upsgi_regexp_list *upsgi_regexp_custom_new_list(struct upsgi_regexp_list **list, char *value, char *custom) {

	struct upsgi_regexp_list *url = *list, *old_url;

	if (!url) {
		*list = upsgi_malloc(sizeof(struct upsgi_regexp_list));
		url = *list;
	}
	else {
		while (url) {
			old_url = url;
			url = url->next;
		}

		url = upsgi_malloc(sizeof(struct upsgi_regexp_list));
		old_url->next = url;
	}

	if (upsgi_regexp_build(value, &url->pattern)) {
		exit(1);
	}
	url->next = NULL;
	url->custom = 0;
	url->custom_ptr = NULL;
	url->custom_str = custom;

	return url;
}

int upsgi_regexp_match_pattern(char *pattern, char *str) {

	upsgi_pcre *regexp;

	if (upsgi_regexp_build(pattern, &regexp))
		return 1;

	return !upsgi_regexp_match(regexp, str, strlen(str));
}

#endif

char *upsgi_string_get_list(struct upsgi_string_list **list, int pos, size_t * len) {

	struct upsgi_string_list *upsgi_string = *list;
	int counter = 0;

	while (upsgi_string) {
		if (counter == pos) {
			*len = upsgi_string->len;
			return upsgi_string->value;
		}
		upsgi_string = upsgi_string->next;
		counter++;
	}

	*len = 0;
	return NULL;

}


void upsgi_string_del_list(struct upsgi_string_list **list, struct upsgi_string_list *item) {

	struct upsgi_string_list *upsgi_string = *list, *old_upsgi_string = NULL;

	while (upsgi_string) {
		if (upsgi_string == item) {
			// parent instance ?
			if (old_upsgi_string == NULL) {
				*list = upsgi_string->next;
			}
			else {
				old_upsgi_string->next = upsgi_string->next;
			}

			free(upsgi_string);
			return;
		}

		old_upsgi_string = upsgi_string;
		upsgi_string = upsgi_string->next;
	}

}

void upsgi_sig_pause() {

	sigset_t mask;
	sigemptyset(&mask);
	sigsuspend(&mask);
}

char *upsgi_binsh() {
	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, upsgi.binsh) {
		if (upsgi_file_executable(usl->value)) {
			return usl->value;
		}
	}
	return "/bin/sh";
}

void upsgi_exec_command_with_args(char *cmdline) {
	char *argv[4];
	argv[0] = upsgi_binsh();
	argv[1] = "-c";
	argv[2] = cmdline;
	argv[3] = NULL;
	execvp(argv[0], argv);
	upsgi_error("execvp()");
	exit(1);
}

static int upsgi_run_command_do(char *command, char *arg) {

	char *argv[4];

#ifdef __linux__
	if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) {
		upsgi_error("prctl()");
	}
#endif

	if (command == NULL) {
		argv[0] = upsgi_binsh();
		argv[1] = "-c";
		argv[2] = arg;
		argv[3] = NULL;
		execvp(argv[0], argv);
	}
	else {
		argv[0] = command;
		argv[1] = arg;
		argv[2] = NULL;
		execvp(command, argv);
	}


	upsgi_error("execvp()");
	//never here
	exit(1);
}

int upsgi_run_command_and_wait(char *command, char *arg) {

	int waitpid_status = 0;
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}

	if (pid > 0) {
		if (waitpid(pid, &waitpid_status, 0) < 0) {
			upsgi_error("upsgi_run_command_and_wait()/waitpid()");
			return -1;
		}

		return WEXITSTATUS(waitpid_status);
	}
	return upsgi_run_command_do(command, arg);
}

int upsgi_run_command_putenv_and_wait(char *command, char *arg, char **envs, unsigned int nenvs) {

	int waitpid_status = 0;
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}

	if (pid > 0) {
		if (waitpid(pid, &waitpid_status, 0) < 0) {
			upsgi_error("upsgi_run_command_and_wait()/waitpid()");
			return -1;
		}

		return WEXITSTATUS(waitpid_status);
	}

	unsigned int i;
	for (i = 0; i < nenvs; i++) {
		if (putenv(envs[i])) {
			upsgi_error("upsgi_run_command_putenv_and_wait()/putenv()");
			exit(1);
		}
	}

	return upsgi_run_command_do(command, arg);
}


pid_t upsgi_run_command(char *command, int *stdin_fd, int stdout_fd) {

	char *argv[4];

	int waitpid_status = 0;
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}

	if (pid > 0) {
		if (stdin_fd && stdin_fd[0] > -1) {
			close(stdin_fd[0]);
		}
		if (stdout_fd > -1) {
			close(stdout_fd);
		}
		if (waitpid(pid, &waitpid_status, WNOHANG) < 0) {
			upsgi_error("waitpid()");
			return -1;
		}

		return pid;
	}

	upsgi_close_all_sockets();
	//upsgi_close_all_fds();
	int i;
	for (i = 3; i < (int) upsgi.max_fd; i++) {
		if (stdin_fd) {
			if (i == stdin_fd[0] || i == stdin_fd[1]) {
				continue;
			}
		}
		if (stdout_fd > -1) {
			if (i == stdout_fd) {
				continue;
			}
		}
#ifdef __APPLE__
		fcntl(i, F_SETFD, FD_CLOEXEC);
#else
		close(i);
#endif
	}



	if (stdin_fd) {
		close(stdin_fd[1]);
	}
	else {
		if (!upsgi_valid_fd(0)) {
			int in_fd = open("/dev/null", O_RDONLY);
			if (in_fd < 0) {
				upsgi_error_open("/dev/null");
			}
			else {
				if (in_fd != 0) {
					if (dup2(in_fd, 0) < 0) {
						upsgi_error("dup2()");
					}
				}
			}
		}
	}

	if (stdout_fd > -1 && stdout_fd != 1) {
		if (dup2(stdout_fd, 1) < 0) {
			upsgi_error("dup2()");
			exit(1);
		}
	}

	if (stdin_fd && stdin_fd[0] > -1 && stdin_fd[0] != 0) {
		if (dup2(stdin_fd[0], 0) < 0) {
			upsgi_error("dup2()");
			exit(1);
		}
	}

	if (setsid() < 0) {
		upsgi_error("setsid()");
		exit(1);
	}

	argv[0] = upsgi_binsh();
	argv[1] = "-c";
	argv[2] = command;
	argv[3] = NULL;

	execvp(upsgi_binsh(), argv);

	upsgi_error("execvp()");
	//never here
	exit(1);
}

int upsgi_endswith(char *str1, char *str2) {

	size_t i;
	size_t str1len = strlen(str1);
	size_t str2len = strlen(str2);
	char *ptr;

	if (str2len > str1len)
		return 0;

	ptr = (str1 + str1len) - str2len;

	for (i = 0; i < str2len; i++) {
		if (*ptr != str2[i])
			return 0;
		ptr++;
	}

	return 1;
}

void upsgi_chown(char *filename, char *owner) {

	uid_t new_uid = -1;
	uid_t new_gid = -1;
	struct group *new_group = NULL;
	struct passwd *new_user = NULL;

	char *colon = strchr(owner, ':');
	if (colon) {
		colon[0] = 0;
	}


	if (is_a_number(owner)) {
		new_uid = atoi(owner);
	}
	else {
		new_user = getpwnam(owner);
		if (!new_user) {
			upsgi_log("unable to find user %s\n", owner);
			exit(1);
		}
		new_uid = new_user->pw_uid;
	}

	if (colon) {
		colon[0] = ':';
		if (is_a_number(colon + 1)) {
			new_gid = atoi(colon + 1);
		}
		else {
			new_group = getgrnam(colon + 1);
			if (!new_group) {
				upsgi_log("unable to find group %s\n", colon + 1);
				exit(1);
			}
			new_gid = new_group->gr_gid;
		}
	}

	if (chown(filename, new_uid, new_gid)) {
		upsgi_error("chown()");
		exit(1);
	}

}

char *upsgi_get_binary_path(char *argvzero) {

#if defined(__linux__) || defined(__CYGWIN__)
	char *buf = upsgi_calloc(PATH_MAX + 1);
	ssize_t len = readlink("/proc/self/exe", buf, PATH_MAX);
	if (len > 0) {
		return buf;
	}
	free(buf);
#elif defined(_WIN32)
	char *buf = upsgi_calloc(PATH_MAX + 1);
	if (GetModuleFileName(NULL, buf, PATH_MAX) > 0) {
		return buf;
	}
	free(buf);
#elif defined(__NetBSD__)
	char *buf = upsgi_calloc(PATH_MAX + 1);
	ssize_t len = readlink("/proc/curproc/exe", buf, PATH_MAX);
	if (len > 0) {
		return buf;
	}

	if (realpath(argvzero, buf)) {
		return buf;
	}
	free(buf);
#elif defined(__APPLE__)
	char *buf = upsgi_malloc(upsgi.page_size);
	uint32_t len = upsgi.page_size;
	if (_NSGetExecutablePath(buf, &len) == 0) {
		// return only absolute path
#ifndef OLD_REALPATH
		char *newbuf = realpath(buf, NULL);
		if (newbuf) {
			free(buf);
			return newbuf;
		}
#endif
	}
	free(buf);
#elif defined(__sun__)
	// do not free this value !!!
	char *buf = (char *) getexecname();
	if (buf) {
		// return only absolute path
		if (buf[0] == '/') {
			return buf;
		}

		char *newbuf = upsgi_malloc(PATH_MAX + 1);
		if (realpath(buf, newbuf)) {
			return newbuf;
		}
	}
#elif defined(__FreeBSD__) || defined(__GNU_kFreeBSD__)
	char *buf = upsgi_malloc(upsgi.page_size);
	size_t len = upsgi.page_size;
	int mib[4];
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1;
	if (sysctl(mib, 4, buf, &len, NULL, 0) == 0) {
		return buf;
	}
	free(buf);
#endif


	return argvzero;

}

char *upsgi_get_line(char *ptr, char *watermark, int *size) {
	char *p = ptr;
	int count = 0;

	while (p < watermark) {
		if (*p == '\n') {
			*size = count;
			return ptr + count;
		}
		count++;
		p++;
	}

	return NULL;
}

void upsgi_build_mime_dict(char *filename) {

	size_t size = 0;
	char *buf = upsgi_open_and_read(filename, &size, 1, NULL);
	char *watermark = buf + size;

	int linesize = 0;
	char *line = buf;
	int i;
	int type_size = 0;
	int ext_start = 0;
	int found;
	int entries = 0;

	upsgi_log("building mime-types dictionary from file %s...", filename);

	while (upsgi_get_line(line, watermark, &linesize) != NULL) {
		found = 0;
		if (isalnum((int) line[0])) {
			// get the type size
			for (i = 0; i < linesize; i++) {
				if (isblank((int) line[i])) {
					type_size = i;
					found = 1;
					break;
				}
			}
			if (!found) {
				line += linesize + 1;
				continue;
			}
			found = 0;
			for (i = type_size; i < linesize; i++) {
				if (!isblank((int) line[i])) {
					ext_start = i;
					found = 1;
					break;
				}
			}
			if (!found) {
				line += linesize + 1;
				continue;
			}

			char *current = line + ext_start;
			int ext_size = 0;
			for (i = ext_start; i < linesize; i++) {
				if (isblank((int) line[i])) {
#ifdef UPSGI_DEBUG
					upsgi_log("%.*s %.*s\n", ext_size, current, type_size, line);
#endif
					upsgi_dyn_dict_new(&upsgi.mimetypes, current, ext_size, line, type_size);
					entries++;
					ext_size = 0;
					current = NULL;
					continue;
				}
				else if (current == NULL) {
					current = line + i;
				}
				ext_size++;
			}
			if (current && ext_size > 1) {
#ifdef UPSGI_DEBUG
				upsgi_log("%.*s %.*s\n", ext_size, current, type_size, line);
#endif
				upsgi_dyn_dict_new(&upsgi.mimetypes, current, ext_size, line, type_size);
				entries++;
			}

		}
		line += linesize + 1;
	}

	upsgi_log("%d entry found\n", entries);

}

#ifdef __linux__
struct upsgi_unshare_id {
	char *name;
	int value;
};

static struct upsgi_unshare_id upsgi_unshare_list[] = {
#ifdef CLONE_FILES
	{"files", CLONE_FILES},
#endif
#ifdef CLONE_NEWIPC
	{"ipc", CLONE_NEWIPC},
#endif
#ifdef CLONE_NEWNET
	{"net", CLONE_NEWNET},
#endif
#ifdef CLONE_IO
	{"io", CLONE_IO},
#endif
#ifdef CLONE_PARENT
	{"parent", CLONE_PARENT},
#endif
#ifdef CLONE_NEWPID
	{"pid", CLONE_NEWPID},
#endif
#ifdef CLONE_NEWNS
	{"ns", CLONE_NEWNS},
	{"fs", CLONE_NEWNS},
	{"mount", CLONE_NEWNS},
	{"mnt", CLONE_NEWNS},
#endif
#ifdef CLONE_SYSVSEM
	{"sysvsem", CLONE_SYSVSEM},
#endif
#ifdef CLONE_NEWUTS
	{"uts", CLONE_NEWUTS},
#endif
#ifdef CLONE_NEWUSER
	{"user", CLONE_NEWUSER},
#endif
	{NULL, -1}
};

static int upsgi_get_unshare_id(char *name) {

	struct upsgi_unshare_id *uui = upsgi_unshare_list;
	while (uui->name) {
		if (!strcmp(uui->name, name))
			return uui->value;
		uui++;
	}

	return -1;
}

void upsgi_build_unshare(char *what, int *mask) {

	char *list = upsgi_str(what);
	char *p, *ctx = NULL;
	upsgi_foreach_token(list, ",", p, ctx) {
		int u_id = upsgi_get_unshare_id(p);
		if (u_id != -1) {
			*mask |= u_id;
		}
		else {
			upsgi_log("unknown namespace subsystem: %s\n", p);
			exit(1);
		}
	}
	free(list);
}


#endif

#ifdef UPSGI_CAP
struct upsgi_cap {
	char *name;
	cap_value_t value;
};

static struct upsgi_cap upsgi_cap_list[] = {
	{"chown", CAP_CHOWN},
	{"dac_override", CAP_DAC_OVERRIDE},
	{"dac_read_search", CAP_DAC_READ_SEARCH},
	{"fowner", CAP_FOWNER},
	{"fsetid", CAP_FSETID},
	{"kill", CAP_KILL},
	{"setgid", CAP_SETGID},
	{"setuid", CAP_SETUID},
	{"setpcap", CAP_SETPCAP},
	{"linux_immutable", CAP_LINUX_IMMUTABLE},
	{"net_bind_service", CAP_NET_BIND_SERVICE},
	{"net_broadcast", CAP_NET_BROADCAST},
	{"net_admin", CAP_NET_ADMIN},
	{"net_raw", CAP_NET_RAW},
	{"ipc_lock", CAP_IPC_LOCK},
	{"ipc_owner", CAP_IPC_OWNER},
	{"sys_module", CAP_SYS_MODULE},
	{"sys_rawio", CAP_SYS_RAWIO},
	{"sys_chroot", CAP_SYS_CHROOT},
	{"sys_ptrace", CAP_SYS_PTRACE},
	{"sys_pacct", CAP_SYS_PACCT},
	{"sys_admin", CAP_SYS_ADMIN},
	{"sys_boot", CAP_SYS_BOOT},
	{"sys_nice", CAP_SYS_NICE},
	{"sys_resource", CAP_SYS_RESOURCE},
	{"sys_time", CAP_SYS_TIME},
	{"sys_tty_config", CAP_SYS_TTY_CONFIG},
	{"mknod", CAP_MKNOD},
#ifdef CAP_LEASE
	{"lease", CAP_LEASE},
#endif
#ifdef CAP_AUDIT_WRITE
	{"audit_write", CAP_AUDIT_WRITE},
#endif
#ifdef CAP_AUDIT_CONTROL
	{"audit_control", CAP_AUDIT_CONTROL},
#endif
#ifdef CAP_SETFCAP
	{"setfcap", CAP_SETFCAP},
#endif
#ifdef CAP_MAC_OVERRIDE
	{"mac_override", CAP_MAC_OVERRIDE},
#endif
#ifdef CAP_MAC_ADMIN
	{"mac_admin", CAP_MAC_ADMIN},
#endif
#ifdef CAP_SYSLOG
	{"syslog", CAP_SYSLOG},
#endif
#ifdef CAP_WAKE_ALARM
	{"wake_alarm", CAP_WAKE_ALARM},
#endif
	{NULL, -1}
};

static int upsgi_get_cap_id(char *name) {

	struct upsgi_cap *ucl = upsgi_cap_list;
	while (ucl->name) {
		if (!strcmp(ucl->name, name))
			return ucl->value;
		ucl++;
	}

	return -1;
}

int upsgi_build_cap(char *what, cap_value_t ** cap) {

	int cap_id;
	char *caps = upsgi_str(what);
	int pos = 0;
	int count = 0;

	char *p, *ctx = NULL;
	upsgi_foreach_token(caps, ",", p, ctx) {
		if (is_a_number(p)) {
			count++;
		}
		else {
			cap_id = upsgi_get_cap_id(p);
			if (cap_id != -1) {
				count++;
			}
			else {
				upsgi_log("[security] unknown capability: %s\n", p);
			}
		}
	}
	free(caps);

	*cap = upsgi_malloc(sizeof(cap_value_t) * count);

	caps = upsgi_str(what);
	ctx = NULL;
	upsgi_foreach_token(caps, ",", p, ctx) {
		if (is_a_number(p)) {
			cap_id = atoi(p);
		}
		else {
			cap_id = upsgi_get_cap_id(p);
		}
		if (cap_id != -1) {
			(*cap)[pos] = cap_id;
			upsgi_log("setting capability %s [%d]\n", p, cap_id);
			pos++;
		}
		else {
			upsgi_log("[security] unknown capability: %s\n", p);
		}
	}
	free(caps);

	return count;
}

#endif

void upsgi_apply_config_pass(char symbol, char *(*hook) (char *)) {

	int i, j;

	for (i = 0; i < upsgi.exported_opts_cnt; i++) {
		int has_symbol = 0;
		int depth = 0;
		char *magic_key = NULL;
		char *magic_val = NULL;

		if (upsgi.exported_opts[i]->value && !upsgi.exported_opts[i]->configured) {
			for (j = 0; j < (int) strlen(upsgi.exported_opts[i]->value); j++) {
				if (upsgi.exported_opts[i]->value[j] == symbol) {
					has_symbol = 1;
				}
				else if (upsgi.exported_opts[i]->value[j] == '(' && has_symbol == 1) {
					has_symbol = 2;
					depth = 0;
					magic_key = upsgi.exported_opts[i]->value + j + 1;
				}
				else if (has_symbol > 1) {
					if (upsgi.exported_opts[i]->value[j] == '(') {
						has_symbol++;
						depth++;
					}
					else if (upsgi.exported_opts[i]->value[j] == ')') {
						if (depth > 0) {
							has_symbol++;
							depth--;
							continue;
						}
						if (has_symbol <= 2) {
							magic_key = NULL;
							has_symbol = 0;
							continue;
						}
#ifdef UPSGI_DEBUG
						upsgi_log("need to interpret the %.*s tag\n", has_symbol - 2, magic_key);
#endif
						char *tmp_magic_key = upsgi_concat2n(magic_key, has_symbol - 2, "", 0);
						magic_val = hook(tmp_magic_key);
						free(tmp_magic_key);
						if (!magic_val) {
							magic_key = NULL;
							has_symbol = 0;
							continue;
						}
						upsgi.exported_opts[i]->value = upsgi_concat4n(upsgi.exported_opts[i]->value, (magic_key - 2) - upsgi.exported_opts[i]->value, magic_val, strlen(magic_val), magic_key + (has_symbol - 1), strlen(magic_key + (has_symbol - 1)), "", 0);
#ifdef UPSGI_DEBUG
						upsgi_log("computed new value = %s\n", upsgi.exported_opts[i]->value);
#endif
						magic_key = NULL;
						has_symbol = 0;
						j = 0;
					}
					else {
						has_symbol++;
					}
				}
				else {
					has_symbol = 0;
				}
			}
		}
	}

}

void upsgi_set_processname(char *name) {

#if defined(__linux__) || defined(__sun__)
	size_t amount = 0;
	size_t max_procname = upsgi.argv_len + upsgi.environ_len;

	// prepare for strncat
	*upsgi.orig_argv[0] = 0;

	if (upsgi.procname_prefix) {
		amount += strlen(upsgi.procname_prefix);
		if (amount >= max_procname)
			return;
		strncat(upsgi.orig_argv[0], upsgi.procname_prefix, max_procname - (amount + 1));
	}

	amount += strlen(name);
	if (amount >= max_procname)
		return;
	strncat(upsgi.orig_argv[0], name, max_procname - (amount + 1));

	if (upsgi.procname_append) {
		amount += strlen(upsgi.procname_append);
		if (amount >= max_procname)
			return;
		strncat(upsgi.orig_argv[0], upsgi.procname_append, max_procname - (amount + 1));
	}

	// if we fit into argv, only fill argv with spaces, otherwise use environ as well
	if (amount < upsgi.argv_len)  {
		max_procname = upsgi.argv_len;
	}
	// fill with spaces...
	memset(upsgi.orig_argv[0] + amount + 1, ' ', max_procname - (amount + 1));

#elif defined(__FreeBSD__) || defined(__GNU_kFreeBSD__) || defined(__NetBSD__)
	if (upsgi.procname_prefix) {
		if (!upsgi.procname_append) {
			setproctitle("-%s%s", upsgi.procname_prefix, name);
		}
		else {
			setproctitle("-%s%s%s", upsgi.procname_prefix, name, upsgi.procname_append);
		}
	}
	else if (upsgi.procname_append) {
		if (!upsgi.procname_prefix) {
			setproctitle("-%s%s", name, upsgi.procname_append);
		}
		else {
			setproctitle("-%s%s%s", upsgi.procname_prefix, name, upsgi.procname_append);
		}
	}
	else {
		setproctitle("-%s", name);
	}
#endif
}

// this is a wrapper for fork restoring original argv
pid_t upsgi_fork(char *name) {


	pid_t pid = fork();
	if (pid == 0) {

#ifndef __CYGWIN__
		if (upsgi.never_swap) {
			if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
				upsgi_error("mlockall()");
			}
		}
#endif

#if defined(__linux__) || defined(__sun__)
		int i;
		for (i = 0; i < upsgi.argc; i++) {
			// stop fixing original argv if the new one is bigger
			if (!upsgi.orig_argv[i]) break;
			strcpy(upsgi.orig_argv[i], upsgi.argv[i]);
		}
#endif

		if (upsgi.auto_procname && name) {
			if (upsgi.procname) {
				upsgi_set_processname(upsgi.procname);
			}
			else {
				upsgi_set_processname(name);
			}
		}
	}

	return pid;
}

void escape_shell_arg(char *src, size_t len, char *dst) {

	size_t i;
	char *ptr = dst;

	for (i = 0; i < len; i++) {
		if (strchr("&;`'\"|*?~<>^()[]{}$\\\n", src[i])) {
			*ptr++ = '\\';
		}
		*ptr++ = src[i];
	}

	*ptr++ = 0;
}

void escape_json(char *src, size_t len, char *dst) {

	size_t i;
	char *ptr = dst;

	for (i = 0; i < len; i++) {
		if (src[i] == '\t') {
			*ptr++ = '\\';
			*ptr++ = 't';
		}
		else if (src[i] == '\n') {
			*ptr++ = '\\';
			*ptr++ = 'n';
		}
		else if (src[i] == '\r') {
			*ptr++ = '\\';
			*ptr++ = 'r';
		}
		else if (src[i] == '"') {
			*ptr++ = '\\';
			*ptr++ = '"';
		}
		else if (src[i] == '\\') {
			*ptr++ = '\\';
			*ptr++ = '\\';
		}
		else {
			*ptr++ = src[i];
		}
	}

	*ptr++ = 0;
}

/*

build PATH_INFO from raw_uri

it manages:

	percent encoding
	dot_segments removal
	stop at the first #

*/
void http_url_decode4(char *buf, uint16_t * len, char *dst, int no_slash_decode) {

	enum {
		zero = 0,
		percent1,
		percent2,
		slash,
		dot,
		dotdot
	} status;

	uint16_t i, current_new_len, new_len = 0;

	char value[2];

	char *ptr = dst;

	value[0] = '0';
	value[1] = '0';

	status = zero;
	int no_slash = 0;

	if (*len > 0 && buf[0] != '/') {
		status = slash;
		no_slash = 1;
	}

	for (i = 0; i < *len; i++) {
		char c = buf[i];
		if (c == '#')
			break;
		switch (status) {
		case zero:
			if (c == '%') {
				status = percent1;
				break;
			}
			if (c == '/') {
				status = slash;
				break;
			}
			*ptr++ = c;
			new_len++;
			break;
		case percent1:
			if (c == '%') {
				*ptr++ = '%';
				new_len++;
				status = zero;
				break;
			}
			value[0] = c;
			status = percent2;
			break;
		case percent2:
			value[1] = c;
			if (no_slash_decode && value[0] == '2' && (value[1] == 'F' || value[1] == 'f')) {
				*ptr++ = '%';
				*ptr++ = value[0];
				*ptr++ = value[1];
				new_len += 3;
			} else {
				*ptr++ = hex2num(value);
				new_len++;
			}
			status = zero;
			break;
		case slash:
			if (c == '.') {
				status = dot;
				break;
			}
			// we could be at the first round (in non slash)
			if (i > 0 || !no_slash) {
				*ptr++ = '/';
				new_len++;
			}
			if (c == '%') {
				status = percent1;
				break;
			}
			if (c == '/') {
				status = slash;
				break;
			}
			*ptr++ = c;
			new_len++;
			status = zero;
			break;
		case dot:
			if (c == '.') {
				status = dotdot;
				break;
			}
			if (c == '/') {
				status = slash;
				break;
			}
			if (i > 1) {
				*ptr++ = '/';
				new_len++;
			}
			*ptr++ = '.';
			new_len++;
			if (c == '%') {
				status = percent1;
				break;
			}
			*ptr++ = c;
			new_len++;
			status = zero;
			break;
		case dotdot:
			// here we need to remove a segment
			if (c == '/') {
				current_new_len = new_len;
				while (current_new_len) {
					current_new_len--;
					ptr--;
					if (dst[current_new_len] == '/') {
						break;
					}
				}
				new_len = current_new_len;
				status = slash;
				break;
			}
			if (i > 2) {
				*ptr++ = '/';
				new_len++;
			}
			*ptr++ = '.';
			new_len++;
			*ptr++ = '.';
			new_len++;
			if (c == '%') {
				status = percent1;
				break;
			}
			*ptr++ = c;
			new_len++;
			status = zero;
			break;
			// over engineering
		default:
			*ptr++ = c;
			new_len++;
			break;
		}
	}

	switch (status) {
	case slash:
	case dot:
		*ptr++ = '/';
		new_len++;
		break;
	case dotdot:
		current_new_len = new_len;
		while (current_new_len) {
			if (dst[current_new_len - 1] == '/') {
				break;
			}
			current_new_len--;
		}
		new_len = current_new_len;
		break;
	default:
		break;
	}

	*len = new_len;

}


/*
	we scan the table in reverse, as updated values are at the end
*/
char *upsgi_get_var(struct wsgi_request *wsgi_req, char *key, uint16_t keylen, uint16_t * len) {

	int i;

	for (i = wsgi_req->var_cnt - 1; i > 0; i -= 2) {
		if (!upsgi_strncmp(key, keylen, wsgi_req->hvec[i - 1].iov_base, wsgi_req->hvec[i - 1].iov_len)) {
			*len = wsgi_req->hvec[i].iov_len;
			return wsgi_req->hvec[i].iov_base;
		}
	}

	return NULL;
}

struct upsgi_app *upsgi_add_app(int id, uint8_t modifier1, char *mountpoint, int mountpoint_len, void *interpreter, void *callable) {

	if (id > upsgi.max_apps) {
		upsgi_log("FATAL ERROR: you cannot load more than %d apps in a worker\n", upsgi.max_apps);
		exit(1);
	}

	struct upsgi_app *wi = &upsgi_apps[id];
	memset(wi, 0, sizeof(struct upsgi_app));

	wi->modifier1 = modifier1;
	wi->mountpoint_len = mountpoint_len < 0xff ? mountpoint_len : (0xff - 1);
	strncpy(wi->mountpoint, mountpoint, wi->mountpoint_len);
	wi->interpreter = interpreter;
	wi->callable = callable;

	upsgi_apps_cnt++;
	// check if we need to emulate fork() COW
	int i;
	if (upsgi.mywid == 0) {
		for (i = 1; i <= upsgi.numproc; i++) {
			memcpy(&upsgi.workers[i].apps[id], &upsgi.workers[0].apps[id], sizeof(struct upsgi_app));
			upsgi.workers[i].apps_cnt = upsgi_apps_cnt;
		}
	}

	if (!upsgi.no_default_app) {
		if ((mountpoint_len == 0 || (mountpoint_len == 1 && mountpoint[0] == '/')) && upsgi.default_app == -1) {
			upsgi.default_app = id;
		}
	}

	return wi;
}


char *upsgi_check_touches(struct upsgi_string_list *touch_list) {

	// touch->value   - file path
	// touch->custom  - file timestamp
	// touch->custom2 - 0 if file exists, 1 if it does not exists

	struct upsgi_string_list *touch = touch_list;
	while (touch) {
		struct stat tr_st;
		if (stat(touch->value, &tr_st)) {
			if (touch->custom && !touch->custom2) {
#ifdef UPSGI_DEBUG
				upsgi_log("[upsgi-check-touches] File %s was removed\n", touch->value);
#endif
				touch->custom2 = 1;
				return touch->custom_ptr ? touch->custom_ptr : touch->value;
			}
			else if (!touch->custom && !touch->custom2) {
				upsgi_log("unable to stat() %s, events will be triggered as soon as the file is created\n", touch->value);
				touch->custom2 = 1;
			}
			touch->custom = 0;
		}
		else {
			if (!touch->custom && touch->custom2) {
#ifdef UPSGI_DEBUG
				upsgi_log("[upsgi-check-touches] File was created: %s\n", touch->value);
#endif
				touch->custom = (uint64_t) tr_st.st_mtime;
				touch->custom2 = 0;
				return touch->custom_ptr ? touch->custom_ptr : touch->value;
			}
			else if (touch->custom && (uint64_t) tr_st.st_mtime > touch->custom) {
#ifdef UPSGI_DEBUG
				upsgi_log("[upsgi-check-touches] modification detected on %s: %llu -> %llu\n", touch->value, (unsigned long long) touch->custom, (unsigned long long) tr_st.st_mtime);
#endif
				touch->custom = (uint64_t) tr_st.st_mtime;
				return touch->custom_ptr ? touch->custom_ptr : touch->value;
			}
			touch->custom = (uint64_t) tr_st.st_mtime;
		}
		touch = touch->next;
	}

	return NULL;
}

char *upsgi_chomp(char *str) {
	ssize_t slen = (ssize_t) strlen(str), i;
	if (!slen)
		return str;
	slen--;
	for (i = slen; i >= 0; i--) {
		if (str[i] == '\r' || str[i] == '\n') {
			str[i] = 0;
		}
		else {
			return str;
		}
	}

	return str;
}

char *upsgi_chomp2(char *str) {
	ssize_t slen = (ssize_t) strlen(str), i;
	if (!slen)
		return str;
	slen--;
	for (i = slen; i >= 0; i--) {
		if (str[i] == '\r' || str[i] == '\n' || str[i] == '\t' || str[i] == ' ') {
			str[i] = 0;
		}
		else {
			return str;
		}
	}

	return str;
}



int upsgi_tmpfd() {
	int fd = -1;
	char *tmpdir = getenv("TMPDIR");
	if (!tmpdir) {
		tmpdir = "/tmp";
	}
#ifdef O_TMPFILE
	fd = open(tmpdir, O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd >= 0) {
		return fd;
	}
	// fallback to old style
#endif
	char *template = upsgi_concat2(tmpdir, "/upsgiXXXXXX");
	fd = mkstemp(template);
	unlink(template);
	free(template);
	return fd;
}

FILE *upsgi_tmpfile() {
	int fd = upsgi_tmpfd();
	if (fd < 0)
		return NULL;
	return fdopen(fd, "w+");
}

int upsgi_file_to_string_list(char *filename, struct upsgi_string_list **list) {

	char line[1024];

	FILE *fh = fopen(filename, "r");
	if (fh) {
		while (fgets(line, 1024, fh)) {
			upsgi_string_new_list(list, upsgi_chomp(upsgi_str(line)));
		}
		fclose(fh);
		return 1;
	}
	upsgi_error_open(filename);
	return 0;
}

void upsgi_validate_runtime_tunables() {
	if (upsgi.chain_reload_delay < 0) {
		upsgi_log("invalid chain-reload-delay %d, clamping to 0\n", upsgi.chain_reload_delay);
		upsgi.chain_reload_delay = 0;
	}

	if (upsgi.log_drain_burst < 1) {
		upsgi_log("invalid log-drain-burst %d, clamping to 1\n", upsgi.log_drain_burst);
		upsgi.log_drain_burst = 1;
	}
	else if (upsgi.log_drain_burst > 1024) {
		upsgi_log("excessive log-drain-burst %d, clamping to 1024\n", upsgi.log_drain_burst);
		upsgi.log_drain_burst = 1024;
	}
}

void upsgi_setup_post_buffering() {

	if (!upsgi.post_buffering_bufsize)
		upsgi.post_buffering_bufsize = 8192;

}

void upsgi_emulate_cow_for_apps(int id) {
	int i;
	// check if we need to emulate fork() COW
	if (upsgi.mywid == 0) {
		for (i = 1; i <= upsgi.numproc; i++) {
			memcpy(&upsgi.workers[i].apps[id], &upsgi.workers[0].apps[id], sizeof(struct upsgi_app));
			upsgi.workers[i].apps_cnt = upsgi_apps_cnt;
		}
	}
}

int upsgi_write_intfile(char *filename, int n) {
	FILE *pidfile = fopen(filename, "w");
	if (!pidfile) {
		upsgi_error_open(filename);
		exit(1);
	}
	if (fprintf(pidfile, "%d\n", n) <= 0 || ferror(pidfile)) {
		fclose(pidfile);
		return -1;
	}
	if (fclose(pidfile)) {
		return -1;
	}
	return 0;
}

void upsgi_write_pidfile(char *pidfile_name) {
	upsgi_log("writing pidfile to %s\n", pidfile_name);
	if (upsgi_write_intfile(pidfile_name, (int) getpid())) {
		upsgi_log("could not write pidfile.\n");
	}
}

void upsgi_write_pidfile_explicit(char *pidfile_name, pid_t pid) {
	upsgi_log("writing pidfile to %s\n", pidfile_name);
	if (upsgi_write_intfile(pidfile_name, (int) pid)) {
		upsgi_log("could not write pidfile.\n");
	}
}

char *upsgi_expand_path(char *dir, int dir_len, char *ptr) {
	if (dir_len > PATH_MAX)
	{
		upsgi_log("invalid path size: %d (max %d)\n", dir_len, PATH_MAX);
		return NULL;
	}
	char *src = upsgi_concat2n(dir, dir_len, "", 0);
	char *dst = ptr;
	if (!dst)
		dst = upsgi_malloc(PATH_MAX + 1);
	if (!realpath(src, dst)) {
		upsgi_error_realpath(src);
		if (!ptr)
			free(dst);
		free(src);
		return NULL;
	}
	free(src);
	return dst;
}


void upsgi_set_cpu_affinity() {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__GNU_kFreeBSD__)
	char buf[4096];
	int ret;
	int pos = 0;
	if (upsgi.cpu_affinity) {
		int base_cpu = (upsgi.mywid - 1) * upsgi.cpu_affinity;
		if (base_cpu >= upsgi.cpus) {
			base_cpu = base_cpu % upsgi.cpus;
		}
		ret = snprintf(buf, 4096, "mapping worker %d to CPUs:", upsgi.mywid);
		if (ret < 25 || ret >= 4096) {
			upsgi_log("unable to initialize cpu affinity !!!\n");
			exit(1);
		}
		pos += ret;
#if defined(__linux__) || defined(__GNU_kFreeBSD__)
		cpu_set_t cpuset;
#elif defined(__FreeBSD__)
		cpuset_t cpuset;
#endif
		CPU_ZERO(&cpuset);
		int i;
		for (i = 0; i < upsgi.cpu_affinity; i++) {
			if (base_cpu >= upsgi.cpus)
				base_cpu = 0;
			CPU_SET(base_cpu, &cpuset);
			ret = snprintf(buf + pos, 4096 - pos, " %d", base_cpu);
			if (ret < 2 || ret >= 4096) {
				upsgi_log("unable to initialize cpu affinity !!!\n");
				exit(1);
			}
			pos += ret;
			base_cpu++;
		}
#if defined(__linux__) || defined(__GNU_kFreeBSD__)
		if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset)) {
			upsgi_error("sched_setaffinity()");
		}
#elif defined(__FreeBSD__)
		if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset), &cpuset)) {
			upsgi_error("cpuset_setaffinity");
		}
#endif
		upsgi_log("%s\n", buf);
	}
#endif
}

#ifdef UPSGI_ELF
#if defined(__linux__)
#include <elf.h>
#endif
char *upsgi_elf_section(char *filename, char *s, size_t * len) {
	struct stat st;
	char *output = NULL;
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		upsgi_error_open(filename);
		return NULL;
	}

	if (fstat(fd, &st)) {
		upsgi_error("stat()");
		close(fd);
		return NULL;
	}

	if (st.st_size < EI_NIDENT) {
		upsgi_log("invalid elf file: %s\n", filename);
		close(fd);
		return NULL;
	}

	char *addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		upsgi_error("mmap()");
		close(fd);
		return NULL;
	}

	if (addr[0] != ELFMAG0)
		goto clear;
	if (addr[1] != ELFMAG1)
		goto clear;
	if (addr[2] != ELFMAG2)
		goto clear;
	if (addr[3] != ELFMAG3)
		goto clear;

	if (addr[4] == ELFCLASS32) {
		// elf header
		Elf32_Ehdr *elfh = (Elf32_Ehdr *) addr;
		// first section
		Elf32_Shdr *sections = ((Elf32_Shdr *) (addr + elfh->e_shoff));
		// number of sections
		int ns = elfh->e_shnum;
		// the names table
		Elf32_Shdr *table = &sections[elfh->e_shstrndx];
		// string table session pointer
		char *names = addr + table->sh_offset;
		Elf32_Shdr *ss = NULL;
		int i;
		for (i = 0; i < ns; i++) {
			char *name = names + sections[i].sh_name;
			if (!strcmp(name, s)) {
				ss = &sections[i];
				break;
			}
		}

		if (ss) {
			*len = ss->sh_size;
			output = upsgi_concat2n(addr + ss->sh_offset, ss->sh_size, "", 0);
		}
	}
	else if (addr[4] == ELFCLASS64) {
		// elf header
		Elf64_Ehdr *elfh = (Elf64_Ehdr *) addr;
		// first section
		Elf64_Shdr *sections = ((Elf64_Shdr *) (addr + elfh->e_shoff));
		// number of sections
		int ns = elfh->e_shnum;
		// the names table
		Elf64_Shdr *table = &sections[elfh->e_shstrndx];
		// string table session pointer
		char *names = addr + table->sh_offset;
		Elf64_Shdr *ss = NULL;
		int i;
		for (i = 0; i < ns; i++) {
			char *name = names + sections[i].sh_name;
			if (!strcmp(name, s)) {
				ss = &sections[i];
				break;
			}
		}

		if (ss) {
			*len = ss->sh_size;
			output = upsgi_concat2n(addr + ss->sh_offset, ss->sh_size, "", 0);
		}
	}


clear:
	close(fd);
	munmap(addr, st.st_size);
	return output;
}
#endif

static void *upsgi_thread_run(void *arg) {
	struct upsgi_thread *ut = (struct upsgi_thread *) arg;
	// block all signals
	sigset_t smask;
	sigfillset(&smask);
	pthread_sigmask(SIG_BLOCK, &smask, NULL);

	ut->queue = event_queue_init();
	event_queue_add_fd_read(ut->queue, ut->pipe[1]);

	ut->func(ut);
	return NULL;
}

struct upsgi_thread *upsgi_thread_new_with_data(void (*func) (struct upsgi_thread *), void *data) {

	struct upsgi_thread *ut = upsgi_calloc(sizeof(struct upsgi_thread));

#if defined(SOCK_SEQPACKET) && defined(__linux__)
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, ut->pipe)) {
#else
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, ut->pipe)) {
#endif
		free(ut);
		return NULL;
	}

	upsgi_socket_nb(ut->pipe[0]);
	upsgi_socket_nb(ut->pipe[1]);

	ut->func = func;
	ut->data = data;

	pthread_attr_init(&ut->tattr);
	pthread_attr_setdetachstate(&ut->tattr, PTHREAD_CREATE_DETACHED);
	// 512K should be enough...
	pthread_attr_setstacksize(&ut->tattr, 512 * 1024);

	if (pthread_create(&ut->tid, &ut->tattr, upsgi_thread_run, ut)) {
		upsgi_error("pthread_create()");
		goto error;
	}

	return ut;
error:
	close(ut->pipe[0]);
	close(ut->pipe[1]);
	free(ut);
	return NULL;
}

struct upsgi_thread *upsgi_thread_new(void (*func) (struct upsgi_thread *)) {
	return upsgi_thread_new_with_data(func, NULL);
}

int upsgi_kvlist_parse(char *src, size_t len, char list_separator, int kv_separator, ...) {
	size_t i;
	va_list ap;
	struct upsgi_string_list *itemlist = NULL;

	char *buf = upsgi_calloc(len + 1);

	// ok let's start splitting the string
	int escaped = 0;
	char *base = buf;
	char *ptr = buf;
	for (i = 0; i < len; i++) {
		if (src[i] == list_separator && !escaped) {
			*ptr++ = 0;
			upsgi_string_new_list(&itemlist, base);
			base = ptr;
		}
		else if (src[i] == '\\' && !escaped) {
			escaped = 1;
		}
		else if (escaped) {
			*ptr++ = src[i];
			escaped = 0;
		}
		else {
			*ptr++ = src[i];
		}
	}

	if (ptr > base) {
		upsgi_string_new_list(&itemlist, base);
	}

	struct upsgi_string_list *usl = itemlist;
	while (usl) {
		len = strlen(usl->value);
		char *item_buf = upsgi_calloc(len + 1);
		base = item_buf;
		ptr = item_buf;
		escaped = 0;
		for (i = 0; i < len; i++) {
			if (usl->value[i] == kv_separator && !escaped) {
				*ptr++ = 0;
				va_start(ap, kv_separator);
				for (;;) {
					char *p = va_arg(ap, char *);
					if (!p)
						break;
					char **pp = va_arg(ap, char **);
					if (!pp)
						break;
					if (!strcmp(p, base)) {
						*pp = upsgi_str(usl->value + i + 1);
					}
				}
				va_end(ap);
				break;
			}
			else if (usl->value[i] == '\\' && !escaped) {
				escaped = 1;
			}
			else if (escaped) {
				escaped = 0;
			}
			else {
				*ptr++ = usl->value[i];
			}
		}
		free(item_buf);
		usl = usl->next;
	}

	// destroy the list (no need to destroy the value as it is a pointer to buf)
	usl = itemlist;
	while (usl) {
		struct upsgi_string_list *tmp_usl = usl;
		usl = usl->next;
		free(tmp_usl);
	}

	free(buf);
	return 0;
}

int upsgi_send_http_stats(int fd) {

	char buf[4096];

	int ret = upsgi_waitfd(fd, upsgi.socket_timeout);
	if (ret <= 0)
		return -1;

	if (read(fd, buf, 4096) <= 0)
		return -1;

	struct upsgi_buffer *ub = upsgi_buffer_new(upsgi.page_size);
	if (!ub)
		return -1;

	if (upsgi_buffer_append(ub, "HTTP/1.0 200 OK\r\n", 17))
		goto error;
	if (upsgi_buffer_append(ub, "Connection: close\r\n", 19))
		goto error;
	if (upsgi_buffer_append(ub, "Access-Control-Allow-Origin: *\r\n", 32))
		goto error;
	if (upsgi_buffer_append(ub, "Content-Type: application/json\r\n", 32))
		goto error;
	if (upsgi_buffer_append(ub, "\r\n", 2))
		goto error;

	if (upsgi_buffer_send(ub, fd))
		goto error;
	upsgi_buffer_destroy(ub);
	return 0;

error:
	upsgi_buffer_destroy(ub);
	return -1;
}

int upsgi_call_symbol(char *symbol) {
	void (*func) (void) = dlsym(RTLD_DEFAULT, symbol);
	if (!func)
		return -1;
	func();
	return 0;
}

int upsgi_plugin_modifier1(char *plugin) {
	int ret = -1;
	char *symbol_name = upsgi_concat2(plugin, "_plugin");
	struct upsgi_plugin *up = dlsym(RTLD_DEFAULT, symbol_name);
	if (!up)
		goto end;
	ret = up->modifier1;
end:
	free(symbol_name);
	return ret;
}

char *upsgi_strip(char *src) {
	char *dst = src;
	size_t len = strlen(src);
	int i;

	for (i = 0; i < (ssize_t) len; i++) {
		if (src[i] == ' ' || src[i] == '\t') {
			dst++;
		}
	}

	len -= (dst - src);

	for (i = len; i >= 0; i--) {
		if (dst[i] == ' ' || dst[i] == '\t') {
			dst[i] = 0;
		}
		else {
			break;
		}
	}

	return dst;
}

void upsgi_uuid(char *buf) {
#ifdef UPSGI_UUID
	uuid_t uuid_value;
	uuid_generate(uuid_value);
	uuid_unparse(uuid_value, buf);
#else
	unsigned int i, r[11];
	if (!upsgi_file_exists("/dev/urandom"))
		goto fallback;
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		goto fallback;
	for (i = 0; i < 11; i++) {
		if (read(fd, &r[i], 4) != 4) {
			close(fd);
			goto fallback;
		}
	}
	close(fd);
	goto done;
fallback:
	for (i = 0; i < 11; i++) {
		r[i] = (unsigned int) rand();
	}
done:
	snprintf(buf, 37, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10]);
#endif
}

int upsgi_uuid_cmp(char *x, char *y) {
	int i;
	for (i = 0; i < 36; i++) {
		if (x[i] != y[i]) {
			if (x[i] > y[i]) {
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

void upsgi_additional_header_add(struct wsgi_request *wsgi_req, char *hh, uint16_t hh_len) {
	// will be freed on request's end
	char *header = upsgi_concat2n(hh, hh_len, "", 0);
	upsgi_string_new_list(&wsgi_req->additional_headers, header);
}

void upsgi_remove_header(struct wsgi_request *wsgi_req, char *hh, uint16_t hh_len) {
	char *header = upsgi_concat2n(hh, hh_len, "", 0);
	upsgi_string_new_list(&wsgi_req->remove_headers, header);
}

// based on nginx implementation

static uint8_t b64_table64[] = {
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
	77, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
	77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
	77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
};

static char b64_table64_2[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *upsgi_base64_decode(char *buf, size_t len, size_t * d_len) {

	// find the real size and check for invalid values
	size_t i;
	for (i = 0; i < len; i++) {
		if (buf[i] == '=')
			break;

		// check for invalid content
		if (b64_table64[(uint8_t) buf[i]] == 77) {
			return NULL;
		}
	}

	// check for invalid size
	if (i % 4 == 1)
		return NULL;

	// compute the new size
	*d_len = (((len + 3) / 4) * 3);
	char *dst = upsgi_malloc(*d_len + 1);

	char *ptr = dst;
	uint8_t *src = (uint8_t *) buf;
	while (i > 3) {
		*ptr++ = (char) (b64_table64[src[0]] << 2 | b64_table64[src[1]] >> 4);
		*ptr++ = (char) (b64_table64[src[1]] << 4 | b64_table64[src[2]] >> 2);
		*ptr++ = (char) (b64_table64[src[2]] << 6 | b64_table64[src[3]]);

		src += 4;
		i -= 4;
	}

	if (i > 1) {
		*ptr++ = (char) (b64_table64[src[0]] << 2 | b64_table64[src[1]] >> 4);
	}

	if (i > 2) {
		*ptr++ = (char) (b64_table64[src[1]] << 4 | b64_table64[src[2]] >> 2);
	}

	*d_len = (ptr - dst);
	*ptr++ = 0;

	return dst;

}

char *upsgi_base64_encode(char *buf, size_t len, size_t * d_len) {
	*d_len = ((len * 4) / 3) + 5;
	uint8_t *src = (uint8_t *) buf;
	char *dst = upsgi_malloc(*d_len);
	char *ptr = dst;
	while (len >= 3) {
		*ptr++ = b64_table64_2[src[0] >> 2];
		*ptr++ = b64_table64_2[((src[0] << 4) & 0x30) | (src[1] >> 4)];
		*ptr++ = b64_table64_2[((src[1] << 2) & 0x3C) | (src[2] >> 6)];
		*ptr++ = b64_table64_2[src[2] & 0x3F];
		src += 3;
		len -= 3;
	}

	if (len > 0) {
		*ptr++ = b64_table64_2[src[0] >> 2];
		uint8_t tmp = (src[0] << 4) & 0x30;
		if (len > 1)
			tmp |= src[1] >> 4;
		*ptr++ = b64_table64_2[tmp];
		if (len < 2) {
			*ptr++ = '=';
		}
		else {
			*ptr++ = b64_table64_2[(src[1] << 2) & 0x3C];
		}
		*ptr++ = '=';
	}

	*ptr = 0;
	*d_len = ((char *) ptr - dst);

	return dst;
}

uint16_t upsgi_be16(char *buf) {
	uint16_t *src = (uint16_t *) buf;
	uint16_t ret = 0;
	uint8_t *ptr = (uint8_t *) & ret;
	ptr[0] = (uint8_t) ((*src >> 8) & 0xff);
	ptr[1] = (uint8_t) (*src & 0xff);
	return ret;
}

uint32_t upsgi_be32(char *buf) {
	uint32_t *src = (uint32_t *) buf;
	uint32_t ret = 0;
	uint8_t *ptr = (uint8_t *) & ret;
	ptr[0] = (uint8_t) ((*src >> 24) & 0xff);
	ptr[1] = (uint8_t) ((*src >> 16) & 0xff);
	ptr[2] = (uint8_t) ((*src >> 8) & 0xff);
	ptr[3] = (uint8_t) (*src & 0xff);
	return ret;
}

uint64_t upsgi_be64(char *buf) {
	uint64_t *src = (uint64_t *) buf;
	uint64_t ret = 0;
	uint8_t *ptr = (uint8_t *) & ret;
	ptr[0] = (uint8_t) ((*src >> 56) & 0xff);
	ptr[1] = (uint8_t) ((*src >> 48) & 0xff);
	ptr[2] = (uint8_t) ((*src >> 40) & 0xff);
	ptr[3] = (uint8_t) ((*src >> 32) & 0xff);
	ptr[4] = (uint8_t) ((*src >> 24) & 0xff);
	ptr[5] = (uint8_t) ((*src >> 16) & 0xff);
	ptr[6] = (uint8_t) ((*src >> 8) & 0xff);
	ptr[7] = (uint8_t) (*src & 0xff);
	return ret;
}

char *upsgi_get_header(struct wsgi_request *wsgi_req, char *hh, uint16_t len, uint16_t * rlen) {
	char *key = upsgi_malloc(len + 6);
	uint16_t key_len = len;
	char *ptr = key;
	*rlen = 0;
	if (upsgi_strncmp(hh, len, "Content-Length", 14) && upsgi_strncmp(hh, len, "Content-Type", 12)) {
		memcpy(ptr, "HTTP_", 5);
		ptr += 5;
		key_len += 5;
	}

	uint16_t i;
	for (i = 0; i < len; i++) {
		if (hh[i] == '-') {
			*ptr++ = '_';
		}
		else {
			*ptr++ = toupper((int) hh[i]);
		}
	}

	char *value = upsgi_get_var(wsgi_req, key, key_len, rlen);
	free(key);
	return value;

}

static char *upsgi_hex_table[] = {
	"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0A", "0B", "0C", "0D", "0E", "0F",
	"10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "1A", "1B", "1C", "1D", "1E", "1F",
	"20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "2A", "2B", "2C", "2D", "2E", "2F",
	"30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3A", "3B", "3C", "3D", "3E", "3F",
	"40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4A", "4B", "4C", "4D", "4E", "4F",
	"50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "5A", "5B", "5C", "5D", "5E", "5F",
	"60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6A", "6B", "6C", "6D", "6E", "6F",
	"70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7A", "7B", "7C", "7D", "7E", "7F",
	"80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8A", "8B", "8C", "8D", "8E", "8F",
	"90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9A", "9B", "9C", "9D", "9E", "9F",
	"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "AA", "AB", "AC", "AD", "AE", "AF",
	"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "BA", "BB", "BC", "BD", "BE", "BF",
	"C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "CA", "CB", "CC", "CD", "CE", "CF",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "DA", "DB", "DC", "DD", "DE", "DF",
	"E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "E9", "EA", "EB", "EC", "ED", "EE", "EF",
	"F0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "FA", "FB", "FC", "FD", "FE", "FF",
};

char *upsgi_str_to_hex(char *src, size_t slen) {
	char *dst = upsgi_malloc(slen * 2);
	char *ptr = dst;
	size_t i;
	for (i = 0; i < slen; i++) {
		uint8_t pos = (uint8_t) src[i];
		memcpy(ptr, upsgi_hex_table[pos], 2);
		ptr += 2;
	}
	return dst;
}

// dst has to be 3 times buf size (USE IT ONLY FOR PATH_INFO !!!)
void http_url_encode(char *buf, uint16_t * len, char *dst) {

	uint16_t i;
	char *ptr = dst;
	for (i = 0; i < *len; i++) {
		if ((buf[i] >= 'A' && buf[i] <= 'Z') || (buf[i] >= 'a' && buf[i] <= 'z') || (buf[i] >= '0' && buf[i] <= '9') || buf[i] == '-' || buf[i] == '_' || buf[i] == '.' || buf[i] == '~' || buf[i] == '/') {
			*ptr++ = buf[i];
		}
		else {
			char *h = upsgi_hex_table[(int) buf[i]];
			*ptr++ = '%';
			*ptr++ = h[0];
			*ptr++ = h[1];
		}
	}

	*len = ptr - dst;

}

void upsgi_takeover() {
	if (upsgi.i_am_a_spooler) {
		upsgi_spooler_run();
	}
	else if (upsgi.muleid) {
		upsgi_mule_run();
	}
	else {
		upsgi_worker_run();
	}
}

// create a message pipe
void create_msg_pipe(int *fd, int bufsize) {

#if defined(SOCK_SEQPACKET) && defined(__linux__)
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd)) {
#else
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fd)) {
#endif
		upsgi_error("create_msg_pipe()/socketpair()");
		exit(1);
	}

	upsgi_socket_nb(fd[0]);
	upsgi_socket_nb(fd[1]);

	if (bufsize) {
		if (setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int))) {
			upsgi_error("create_msg_pipe()/setsockopt()");
		}
		if (setsockopt(fd[0], SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int))) {
			upsgi_error("create_msg_pipe()/setsockopt()");
		}

		if (setsockopt(fd[1], SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int))) {
			upsgi_error("create_msg_pipe()/setsockopt()");
		}
		if (setsockopt(fd[1], SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int))) {
			upsgi_error("create_msg_pipe()/setsockopt()");
		}
	}
}

char *upsgi_binary_path() {
	return upsgi.binary_path ? upsgi.binary_path : "upsgi";
}

void upsgi_envdir(char *edir) {
	DIR *d = opendir(edir);
	if (!d) {
		upsgi_error("[upsgi-envdir] opendir()");
		exit(1);
	}
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		// skip hidden files
		if (de->d_name[0] == '.')
			continue;
		struct stat st;
		char *filename = upsgi_concat3(edir, "/", de->d_name);
		if (stat(filename, &st)) {
			upsgi_log("[upsgi-envdir] error stating %s\n", filename);
			upsgi_error("[upsgi-envdir] stat()");
			exit(1);
		}

		if (!S_ISREG(st.st_mode)) {
			free(filename);
			continue;
		}

		// unsetenv
		if (st.st_size == 0) {
#ifdef UNSETENV_VOID
			unsetenv(de->d_name);
#else
			if (unsetenv(de->d_name)) {
				upsgi_log("[upsgi-envdir] unable to unset %s\n", de->d_name);
				upsgi_error("[upsgi-envdir] unsetenv");
				exit(1);
			}
#endif
			free(filename);
			continue;
		}

		// read the content of the file
		size_t size = 0;
		char *content = upsgi_open_and_read(filename, &size, 1, NULL);
		if (!content) {
			upsgi_log("[upsgi-envdir] unable to open %s\n", filename);
			upsgi_error_open(filename);
			exit(1);
		}
		free(filename);

		// HACK, envdir states we only need to strip the end of the string ....
		upsgi_chomp2(content);
		// ... and substitute 0 with \n
		size_t slen = strlen(content);
		size_t i;
		for (i = 0; i < slen; i++) {
			if (content[i] == 0) {
				content[i] = '\n';
			}
		}

		if (setenv(de->d_name, content, 1)) {
			upsgi_log("[upsgi-envdir] unable to set %s\n", de->d_name);
			upsgi_error("[upsgi-envdir] setenv");
			exit(1);
		}

		free(content);
	}
	closedir(d);
}

void upsgi_envdirs(struct upsgi_string_list *envdirs) {
	struct upsgi_string_list *usl = envdirs;
	while (usl) {
		upsgi_envdir(usl->value);
		usl = usl->next;
	}
}

void upsgi_opt_envdir(char *opt, char *value, void *foobar) {
	upsgi_envdir(value);
}

void upsgi_exit(int status) {
	upsgi.last_exit_code = status;
	// disable macro expansion
	(exit) (status);
}

int upsgi_base128(struct upsgi_buffer *ub, uint64_t l, int first) {
	if (l > 127) {
		if (upsgi_base128(ub, l / 128, 0))
			return -1;
	}
	l %= 128;
	if (first) {
		if (upsgi_buffer_u8(ub, (uint8_t) l))
			return -1;
	}
	else {
		if (upsgi_buffer_u8(ub, 0x80 | (uint8_t) l))
			return -1;
	}
	return 0;
}

#ifdef __linux__
void upsgi_setns(char *path) {
	int (*u_setns) (int, int) = (int (*)(int, int)) dlsym(RTLD_DEFAULT, "setns");
	if (!u_setns) {
		upsgi_log("your system misses setns() syscall !!!\n");
		exit(1);
	}

	// count be overwritten
	int count = 64;

	upsgi_log("joining namespaces from %s ...\n", path);
	for (;;) {
		int ns_fd = upsgi_connect(path, 30, 0);
		if (ns_fd < 0) {
			upsgi_error("upsgi_setns()/upsgi_connect()");
			sleep(1);
			continue;
		}
		int *fds = upsgi_attach_fd(ns_fd, &count, "upsgi-setns", 11);
		if (fds && count > 0) {
			int i;
			for (i = 0; i < count; i++) {
				if (fds[i] > -1) {
					if (u_setns(fds[i], 0) < 0) {
						upsgi_error("upsgi_setns()/setns()");
						exit(1);
					}
					close(fds[i]);
				}
			}
			free(fds);
			close(ns_fd);
			break;
		}
		if (fds)
			free(fds);
		close(ns_fd);
		sleep(1);
	}
}
#endif

mode_t upsgi_mode_t(char *value, int *error) {
	mode_t mode = 0;
	*error = 0;

        if (strlen(value) < 3) {
		*error = 1;
		return mode;
	}

        if (strlen(value) == 3) {
                mode = (mode << 3) + (value[0] - '0');
                mode = (mode << 3) + (value[1] - '0');
                mode = (mode << 3) + (value[2] - '0');
        }
        else {
                mode = (mode << 3) + (value[1] - '0');
                mode = (mode << 3) + (value[2] - '0');
                mode = (mode << 3) + (value[3] - '0');
        }

	return mode;
}

int upsgi_wait_for_socket(char *socket_name) {
        if (!upsgi.wait_for_socket_timeout) {
                upsgi.wait_for_socket_timeout = 60;
        }
        upsgi_log("waiting for %s (max %d seconds) ...\n", socket_name, upsgi.wait_for_socket_timeout);
        int counter = 0;
        for (;;) {
                if (counter > upsgi.wait_for_socket_timeout) {
                        upsgi_log("%s unavailable after %d seconds\n", socket_name, counter);
                        return -1;
                }
		// wait for 1 second to respect upsgi.wait_for_fs_timeout
		int fd = upsgi_connect(socket_name, 1, 0);
		if (fd < 0) goto retry;
		close(fd);
                upsgi_log_verbose("%s ready\n", socket_name);
                return 0;
retry:
                sleep(1);
                counter++;
        }
	return -1;
}

int upsgi_wait_for_mountpoint(char *mountpoint) {
        if (!upsgi.wait_for_fs_timeout) {
                upsgi.wait_for_fs_timeout = 60;
        }
        upsgi_log("waiting for %s (max %d seconds) ...\n", mountpoint, upsgi.wait_for_fs_timeout);
        int counter = 0;
        for (;;) {
                if (counter > upsgi.wait_for_fs_timeout) {
                        upsgi_log("%s unavailable after %d seconds\n", mountpoint, counter);
                        return -1;
                }
		struct stat st0;
		struct stat st1;
		if (stat(mountpoint, &st0)) goto retry;
		if (!S_ISDIR(st0.st_mode)) goto retry;
		char *relative = upsgi_concat2(mountpoint, "/../");
		if (stat(relative, &st1)) {
			free(relative);
			goto retry;
		}
		free(relative);
		// useless :P
                if (!S_ISDIR(st1.st_mode)) goto retry;
		if (st0.st_dev == st1.st_dev) goto retry;
                upsgi_log_verbose("%s mounted\n", mountpoint);
                return 0;
retry:
                sleep(1);
                counter++;
        }
	return -1;
}

// type -> 1 file, 2 dir, 0 both
int upsgi_wait_for_fs(char *filename, int type) {
	if (!upsgi.wait_for_fs_timeout) {
        	upsgi.wait_for_fs_timeout = 60;
        }
        upsgi_log("waiting for %s (max %d seconds) ...\n", filename, upsgi.wait_for_fs_timeout);
        int counter = 0;
        for (;;) {
        	if (counter > upsgi.wait_for_fs_timeout) {
                	upsgi_log("%s unavailable after %d seconds\n", filename, counter);
			return -1;
                }
		struct stat st;
		if (stat(filename, &st)) goto retry;
		if (type == 1 && !S_ISREG(st.st_mode)) goto retry;
		if (type == 2 && !S_ISDIR(st.st_mode)) goto retry;
                upsgi_log_verbose("%s found\n", filename);
		return 0;
retry:
                sleep(1);
                counter++;
	}
	return -1;
}

#if !defined(_GNU_SOURCE) && !defined(__UCLIBC__)
int upsgi_versionsort(const struct dirent **da, const struct dirent **db) {

        const char *a = (*da)->d_name;
        const char *b = (*db)->d_name;

        long la, lb;
        char *endptr;

        // Check if a and b are valid numbers.
        la = strtol(a, &endptr, 10);
        if (strcmp(endptr, "\0") || endptr == a) {
            a = NULL;
        }

        lb = strtol(b, &endptr, 10);
        if (strcmp(endptr, "\0") || endptr == b) {
            b = NULL;
        }

        if (a && b) {
            return (la < lb ? -1 : la > lb);
        } else if (a) {
            return -1;
        } else if (b) {
            return 1;
        } else {
            return strcmp((*da)->d_name, (*db)->d_name);
        }
}
#endif

void upsgi_fix_range_for_size(enum upsgi_range* parsed, int64_t* from, int64_t* to, int64_t size) {
        if (*parsed != UPSGI_RANGE_PARSED) {
                return;
        }
        if (*from < 0) {
                *from = size + *from;
        }
        if (*to > size-1) {
                *to = size-1;
        }
        if (*from == 0 && *to == size-1) {
                /* we have a right to reset to 200 OK answer */
                *parsed = UPSGI_RANGE_NOT_PARSED;
        }
        else if (*to >= *from) {
                *parsed = UPSGI_RANGE_VALID;
        }
        else { /* case *from > size-1 is also handled here */
                *parsed = UPSGI_RANGE_INVALID;
                *from = 0;
                *to = 0;
        }
}

char* upsgi_getenv_with_default(const char* key) {
    /* VARIABLE:-DEFAULT_VALUE: if VARIABLE is unset or empty returns DEFAULT_VALUE
     * VARIABLE-DEFAULT_VALUE : if VARIABLE is unset returns DEFAULT_VALUE
     */
    if (strstr(key, ":-") != NULL) {
        char* dup = strdup(key);
        char* variable = strtok(dup, ":-");
        char* defaultval = strtok(NULL, ":-");
        char* value = getenv(variable);

        if (!value || strcmp(value, "") == 0) {
            value = strdup(defaultval);
        }
        free(dup);
        return value;
    }
    else if (strstr(key, "-") != NULL) {
        char* dup = strdup(key);
        char* variable = strtok(dup, "-");
        char* defaultval = strtok(NULL, "-");
        char* value = getenv(variable);

        if (!value) {
            value = strdup(defaultval);
        }
        free(dup);
        return value;
    }
    return getenv(key);
}
