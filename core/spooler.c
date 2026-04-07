#include "upsgi.h"

#ifdef __sun__
#include "strings.h"
#endif

extern struct upsgi_server upsgi;

static void spooler_readdir(struct upsgi_spooler *, char *dir);
static void spooler_scandir(struct upsgi_spooler *, char *dir);
static void spooler_manage_task(struct upsgi_spooler *, char *, char *);

// increment it whenever a signal is raised
static uint64_t wakeup = 0;

// function to allow waking up the spooler if blocked in event_wait
void spooler_wakeup(int signum) {
	wakeup++;
}

void upsgi_opt_add_spooler(char *opt, char *directory, void *mode) {

	int i;
	struct upsgi_spooler *us;

	if (access(directory, R_OK | W_OK | X_OK) &&
	    mkdir(directory, S_IRWXU | S_IXGRP | S_IRGRP)) {
		upsgi_error("[spooler directory] access()");
		exit(1);
	}

	if (upsgi.spooler_numproc > 0) {
		for (i = 0; i < upsgi.spooler_numproc; i++) {
			us = upsgi_new_spooler(directory);
			if (mode)
				us->mode = (long) mode;
		}
	}
	else {
		us = upsgi_new_spooler(directory);
		if (mode)
			us->mode = (long) mode;
	}

}


struct upsgi_spooler *upsgi_new_spooler(char *dir) {

	struct upsgi_spooler *uspool = upsgi.spoolers;

	if (!uspool) {
		upsgi.spoolers = upsgi_calloc_shared(sizeof(struct upsgi_spooler));
		uspool = upsgi.spoolers;
	}
	else {
		while (uspool) {
			if (uspool->next == NULL) {
				uspool->next = upsgi_calloc_shared(sizeof(struct upsgi_spooler));
				uspool = uspool->next;
				break;
			}
			uspool = uspool->next;
		}
	}

	if (!realpath(dir, uspool->dir)) {
		upsgi_error("[spooler] realpath()");
		exit(1);
	}

	uspool->next = NULL;

	return uspool;
}


struct upsgi_spooler *upsgi_get_spooler_by_name(char *name, size_t name_len) {

	struct upsgi_spooler *uspool = upsgi.spoolers;

	while (uspool) {
		if (!upsgi_strncmp(uspool->dir, strlen(uspool->dir), name, name_len)) {
			return uspool;
		}
		uspool = uspool->next;
	}

	return NULL;
}

pid_t spooler_start(struct upsgi_spooler * uspool) {

	int i;

	pid_t pid = upsgi_fork("upsgi spooler");
	if (pid < 0) {
		upsgi_error("fork()");
		exit(1);
	}
	else if (pid == 0) {

		signal(SIGALRM, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
		signal(SIGINT, end_me);
		signal(SIGTERM, end_me);
		// USR1 will be used to wake up the spooler
		upsgi_unix_signal(SIGUSR1, spooler_wakeup);
		signal(SIGUSR2, SIG_IGN);
		signal(SIGPIPE, SIG_IGN);
		signal(SIGSTOP, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);

		upsgi.mypid = getpid();
		uspool->pid = upsgi.mypid;
		// avoid race conditions !!!
		upsgi.i_am_a_spooler = uspool;

		upsgi_fixup_fds(0, 0, NULL);
		upsgi_close_all_sockets();

		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->post_fork) {
				upsgi.p[i]->post_fork();
			}
		}

		upsgi_spooler_run();
	}
	else if (pid > 0) {
		upsgi_log("spawned the upsgi spooler on dir %s with pid %d\n", uspool->dir, pid);
	}

	return pid;
}

void upsgi_spooler_run() {
	int i;
	struct upsgi_spooler *uspool = upsgi.i_am_a_spooler;
	upsgi.signal_socket = upsgi.shared->spooler_signal_pipe[1];

                for (i = 0; i < 256; i++) {
                        if (upsgi.p[i]->spooler_init) {
                                upsgi.p[i]->spooler_init();
                        }
                }

                for (i = 0; i < upsgi.gp_cnt; i++) {
                        if (upsgi.gp[i]->spooler_init) {
                                upsgi.gp[i]->spooler_init();
                        }
                }

                spooler(uspool);
}

void destroy_spool(char *dir, char *file) {

	if (chdir(dir)) {
		upsgi_error("chdir()");
		upsgi_log("[spooler] something horrible happened to the spooler. Better to kill it.\n");
		exit(1);
	}

	if (unlink(file)) {
		upsgi_error("unlink()");
		upsgi_log("[spooler] something horrible happened to the spooler. Better to kill it.\n");
		exit(1);
	}

}

struct spooler_req {
	char *spooler;
	size_t spooler_len;
	char *priority;
	size_t priority_len;
	time_t at;
};

static void spooler_req_parser_hook(char *key, uint16_t key_len, char *value, uint16_t value_len, void *data) {
	struct spooler_req *sr = (struct spooler_req *) data;
	if (!upsgi_strncmp(key, key_len, "spooler", 7)) {
		sr->spooler = value;
		sr->spooler_len = value_len;
		return;
	} 

	if (!upsgi_strncmp(key, key_len, "priority", 8)) {
                sr->priority = value;
                sr->priority_len = value_len;
                return;
        }

	if (!upsgi_strncmp(key, key_len, "at", 2)) {
		// at can be a float...
		char *dot = memchr(value, '.', value_len);
		if (dot) {
			value_len = dot - value;
		}
		sr->at = upsgi_str_num(value, value_len);
		return;
	}
}

/*
CHANGED in 2.0.7: wsgi_req is useless !
*/
char *upsgi_spool_request(struct wsgi_request *wsgi_req, char *buf, size_t len, char *body, size_t body_len) {

	struct timeval tv;
	static uint64_t internal_counter = 0;
	int fd = -1;
	struct spooler_req sr;

	if (len > 0xffff) {
		upsgi_log("[upsgi-spooler] args buffer is limited to 64k, use the 'body' for bigger values\n");
		return NULL;
	}

	// parse the request buffer
	memset(&sr, 0, sizeof(struct spooler_req));
	upsgi_hooked_parse(buf, len, spooler_req_parser_hook, &sr);
	
	struct upsgi_spooler *uspool = upsgi.spoolers;
	if (!uspool) {
		upsgi_log("[upsgi-spooler] no spooler available\n");
		return NULL;
	}

	// if it is a number, get the spooler by id instead of by name
	if (sr.spooler && sr.spooler_len) {
		uspool = upsgi_get_spooler_by_name(sr.spooler, sr.spooler_len);
		if (!uspool) {
			upsgi_log("[upsgi-spooler] unable to find spooler \"%.*s\"\n", sr.spooler_len, sr.spooler);
			return NULL;
		}
	}

	// this lock is for threads, the pid value in filename will avoid multiprocess races
	upsgi_lock(uspool->lock);

	// we increase it even if the request fails
	internal_counter++;

	gettimeofday(&tv, NULL);

	char *filename = NULL;
	size_t filename_len = 0;

	if (sr.priority && sr.priority_len) {
		filename_len = strlen(uspool->dir) + sr.priority_len + strlen(upsgi.hostname) + 256;	
		filename = upsgi_malloc(filename_len);
		int ret = snprintf(filename, filename_len, "%s/%.*s", uspool->dir, (int) sr.priority_len, sr.priority);
		if (ret <= 0 || ret >= (int) filename_len) {
			upsgi_log("[upsgi-spooler] error generating spooler filename\n");
			free(filename);
			upsgi_unlock(uspool->lock);
			return NULL;
		}
		// no need to check for errors...
		(void) mkdir(filename, 0777);

		ret = snprintf(filename, filename_len, "%s/%.*s/upsgi_spoolfile_on_%s_%d_%llu_%d_%llu_%llu", uspool->dir, (int)sr.priority_len, sr.priority, upsgi.hostname, (int) getpid(), (unsigned long long) internal_counter, rand(),
				(unsigned long long) tv.tv_sec, (unsigned long long) tv.tv_usec);
		if (ret <= 0 || ret >=(int)  filename_len) {
                        upsgi_log("[upsgi-spooler] error generating spooler filename\n");
			free(filename);
			upsgi_unlock(uspool->lock);
			return NULL;
		}
	}
	else {
		filename_len = strlen(uspool->dir) + strlen(upsgi.hostname) + 256;
                filename = upsgi_malloc(filename_len);
		int ret = snprintf(filename, filename_len, "%s/upsgi_spoolfile_on_%s_%d_%llu_%d_%llu_%llu", uspool->dir, upsgi.hostname, (int) getpid(), (unsigned long long) internal_counter,
				rand(), (unsigned long long) tv.tv_sec, (unsigned long long) tv.tv_usec);
		if (ret <= 0 || ret >= (int) filename_len) {
                        upsgi_log("[upsgi-spooler] error generating spooler filename\n");
			free(filename);
			upsgi_unlock(uspool->lock);
			return NULL;
		}
	}

	fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		upsgi_error_open(filename);
		free(filename);
		upsgi_unlock(uspool->lock);
		return NULL;
	}

	// now lock the file, it will no be runnable, until the lock is not removed
	// a race could come if the spooler take the file before fcntl is called
	// in such case the spooler will detect a zeroed file and will retry later
	if (upsgi_fcntl_lock(fd)) {
		close(fd);
		free(filename);
		upsgi_unlock(uspool->lock);
		return NULL;
	}

	struct upsgi_header uh;
	uh.modifier1 = 17;
	uh.modifier2 = 0;
	uh._pktsize = (uint16_t) len;
#ifdef __BIG_ENDIAN__
	uh._pktsize = upsgi_swap16(uh._pktsize);
#endif

	if (write(fd, &uh, 4) != 4) {
		upsgi_log("[spooler] unable to write header for %s\n", filename);
		goto clear;
	}

	if (write(fd, buf, len) != (ssize_t) len) {
		upsgi_log("[spooler] unable to write args for %s\n", filename);
		goto clear;
	}

	if (body && body_len > 0) {
		if ((size_t) write(fd, body, body_len) != body_len) {
			upsgi_log("[spooler] unable to write body for %s\n", filename);
			goto clear;
		}
	}

	if (sr.at > 0) {
#ifdef __UCLIBC__
		struct timespec ts[2]; 
		ts[0].tv_sec = sr.at; 
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = sr.at;
		ts[1].tv_nsec = 0; 
		if (futimens(fd, ts)) {
			upsgi_error("upsgi_spooler_request()/futimens()");	
		}
#else
		struct timeval tv[2];
		tv[0].tv_sec = sr.at;
		tv[0].tv_usec = 0;
		tv[1].tv_sec = sr.at;
		tv[1].tv_usec = 0;
#ifdef __sun__
		if (futimesat(fd, NULL, tv)) {
#else
		if (futimes(fd, tv)) {
#endif
			upsgi_error("upsgi_spooler_request()/futimes()");
		}
#endif
	}

	// here the file will be unlocked too
	close(fd);

	if (!upsgi.spooler_quiet)
		upsgi_log("[spooler] written %lu bytes to file %s\n", (unsigned long) len + body_len + 4, filename);

	// and here waiting threads can continue
	upsgi_unlock(uspool->lock);

/*	wake up the spoolers attached to the specified dir ... (HACKY) 
	no need to fear races, as USR1 is harmless an all of the upsgi processes...
	it could be a problem if a new process takes the old pid, but modern systems should avoid that
*/

	struct upsgi_spooler *spoolers = upsgi.spoolers;
	while (spoolers) {
		if (!strcmp(spoolers->dir, uspool->dir)) {
			if (spoolers->pid > 0 && spoolers->running == 0) {
				(void) kill(spoolers->pid, SIGUSR1);
			}
		}
		spoolers = spoolers->next;
	}

	return filename;


clear:
	upsgi_unlock(uspool->lock);
	upsgi_error("upsgi_spool_request()/write()");
	if (unlink(filename)) {
		upsgi_error("upsgi_spool_request()/unlink()");
	}
	free(filename);
	// unlock the file too
	close(fd);
	return NULL;
}



void spooler(struct upsgi_spooler *uspool) {

	// prevent process blindly reading stdin to make mess
	int nullfd;

	// asked by Marco Beri
#ifdef __HAIKU__
#ifdef UPSGI_DEBUG
	upsgi_log("lowering spooler priority to %d\n", B_LOW_PRIORITY);
#endif
	set_thread_priority(find_thread(NULL), B_LOW_PRIORITY);
#else
#ifdef UPSGI_DEBUG
	upsgi_log("lowering spooler priority to %d\n", PRIO_MAX);
#endif
	setpriority(PRIO_PROCESS, getpid(), PRIO_MAX);
#endif

	nullfd = open("/dev/null", O_RDONLY);
	if (nullfd < 0) {
		upsgi_error_open("/dev/null");
		exit(1);
	}

	if (nullfd != 0) {
		dup2(nullfd, 0);
		close(nullfd);
	}

	int spooler_event_queue = event_queue_init();
	int interesting_fd = -1;

	if (upsgi.master_process) {
		event_queue_add_fd_read(spooler_event_queue, upsgi.shared->spooler_signal_pipe[1]);
	}

	// reset the tasks counter
	uspool->tasks = 0;

	time_t last_task_managed = 0;

	for (;;) {

		if (chdir(uspool->dir)) {
			upsgi_error("chdir()");
			exit(1);
		}

		if (upsgi.spooler_ordered) {
			spooler_scandir(uspool, NULL);
		}
		else {
			spooler_readdir(uspool, NULL);
		}

		// here we check (if in cheap mode), if the spooler has done its job
		if (upsgi.spooler_cheap) {
			if (last_task_managed == uspool->last_task_managed) {
				upsgi_log_verbose("cheaping spooler %s ...\n", uspool->dir);
				exit(0);
			}
			last_task_managed = uspool->last_task_managed;
		}


		int timeout = upsgi.shared->spooler_frequency ? upsgi.shared->spooler_frequency : upsgi.spooler_frequency;
		if (wakeup > 0) {
			timeout = 0;
		}

		if (event_queue_wait(spooler_event_queue, timeout, &interesting_fd) > 0) {
			if (upsgi.master_process) {
				if (interesting_fd == upsgi.shared->spooler_signal_pipe[1]) {
					if (upsgi_receive_signal(NULL, interesting_fd, "spooler", (int) getpid())) {
					    if (upsgi.spooler_signal_as_task) {
					        uspool->tasks++;
					        if (upsgi.spooler_max_tasks > 0 && uspool->tasks >= (uint64_t) upsgi.spooler_max_tasks) {
					            upsgi_log("[spooler %s pid: %d] maximum number of tasks reached (%d) recycling ...\n", uspool->dir, (int) upsgi.mypid, upsgi.spooler_max_tasks);
					            end_me(0);
					        }
					    }
					}
				}
			}
		}

		// avoid races
		uint64_t tmp_wakeup = wakeup;
		if (tmp_wakeup > 0) {
			tmp_wakeup--;
		}
		wakeup = tmp_wakeup;

	}
}

static void spooler_scandir(struct upsgi_spooler *uspool, char *dir) {

	struct dirent **tasklist;
	int n, i;

	if (!dir)
		dir = uspool->dir;

#ifdef __NetBSD__
	n = scandir(dir, &tasklist, NULL, (void *)upsgi_versionsort);
#else
	n = scandir(dir, &tasklist, NULL, upsgi_versionsort);
#endif
	if (n < 0) {
		upsgi_error("scandir()");
		return;
	}

	for (i = 0; i < n; i++) {
		spooler_manage_task(uspool, dir, tasklist[i]->d_name);
		free(tasklist[i]);
	}

	free(tasklist);
}


static void spooler_readdir(struct upsgi_spooler *uspool, char *dir) {

	DIR *sdir;
	struct dirent *dp;

	if (!dir)
		dir = uspool->dir;

	sdir = opendir(dir);
	if (sdir) {
		while ((dp = readdir(sdir)) != NULL) {
			spooler_manage_task(uspool, dir, dp->d_name);
		}
		closedir(sdir);
	}
	else {
		upsgi_error("spooler_readdir()/opendir()");
	}
}

int upsgi_spooler_read_header(char *task, int spool_fd, struct upsgi_header *uh) {

	// check if the file is locked by another process
	if (upsgi_fcntl_is_locked(spool_fd)) {
		upsgi_protected_close(spool_fd);
		return -1;
	}

	// unlink() can destroy the lock !!!
	if (access(task, R_OK|W_OK)) {
		upsgi_protected_close(spool_fd);
		return -1;
	}

	ssize_t rlen = upsgi_protected_read(spool_fd, uh, 4);

	if (rlen != 4) {
		// it could be here for broken file or just opened one
		if (rlen < 0)
			upsgi_error("spooler_manage_task()/read()");
		upsgi_protected_close(spool_fd);
		return -1;
	}

#ifdef __BIG_ENDIAN__
	uh->_pktsize = upsgi_swap16(uh->_pktsize);
#endif

	return 0;
}

int upsgi_spooler_read_content(int spool_fd, char *spool_buf, char **body, size_t *body_len, struct upsgi_header *uh, struct stat *sf_lstat) {

	if (upsgi_protected_read(spool_fd, spool_buf, uh->_pktsize) != uh->_pktsize) {
		upsgi_error("spooler_manage_task()/read()");
		upsgi_protected_close(spool_fd);
		return 1;
	}

	// body available ?
	if (sf_lstat->st_size > (uh->_pktsize + 4)) {
		*body_len = sf_lstat->st_size - (uh->_pktsize + 4);
		*body = upsgi_malloc(*body_len);
		if ((size_t) upsgi_protected_read(spool_fd, *body, *body_len) != *body_len) {
			upsgi_error("spooler_manage_task()/read()");
			upsgi_protected_close(spool_fd);
			free(*body);
			return 1;
		}
	}

	return 0;
}

void spooler_manage_task(struct upsgi_spooler *uspool, char *dir, char *task) {

	int i, ret;

	char spool_buf[0xffff];
	struct upsgi_header uh;
	char *body = NULL;
	size_t body_len = 0;

	int spool_fd;

	if (!dir)
		dir = uspool->dir;

	if (!strncmp("upsgi_spoolfile_on_", task, 19) || (upsgi.spooler_ordered && is_a_number(task))) {
		struct stat sf_lstat;

		if (lstat(task, &sf_lstat)) {
			return;
		}

		// a spool request for the future
		if (sf_lstat.st_mtime > upsgi_now()) {
			return;
		}

		if (S_ISDIR(sf_lstat.st_mode) && upsgi.spooler_ordered) {
			if (chdir(task)) {
				upsgi_error("spooler_manage_task()/chdir()");
				return;
			}
#ifdef __UCLIBC__ 
			char *prio_path = upsgi_malloc(PATH_MAX);
			realpath(".", prio_path);		
#else 
			char *prio_path = realpath(".", NULL);
#endif
			spooler_scandir(uspool, prio_path);
			free(prio_path);
			if (chdir(dir)) {
				upsgi_error("spooler_manage_task()/chdir()");
			}
			return;
		}
		if (!S_ISREG(sf_lstat.st_mode)) {
			return;
		}
		if (!access(task, R_OK | W_OK)) {

			spool_fd = open(task, O_RDWR);

			if (spool_fd < 0) {
				if (errno != ENOENT)
					upsgi_error_open(task);
				return;
			}

			if (upsgi_spooler_read_header(task, spool_fd, &uh))
				return;

			// access lstat second time after getting a lock
			// first-time lstat could be dirty (for example between writes in master)
			if (lstat(task, &sf_lstat)) {
				return;
			}

			if (upsgi_spooler_read_content(spool_fd, spool_buf, &body, &body_len, &uh, &sf_lstat)) {
				destroy_spool(dir, task);
				return;
			}

			// now the task is running and should not be woken up
			uspool->running = 1;
			// this is used in cheap mode for making decision about who must die
			uspool->last_task_managed = upsgi_now();

			if (!upsgi.spooler_quiet)
				upsgi_log("[spooler %s pid: %d] managing request %s ...\n", uspool->dir, (int) upsgi.mypid, task);


			// chdir before running the task (if requested)
			if (upsgi.spooler_chdir) {
				if (chdir(upsgi.spooler_chdir)) {
					upsgi_error("spooler_manage_task()/chdir()");
				}
			}

			int callable_found = 0;
			for (i = 0; i < 256; i++) {
				if (upsgi.p[i]->spooler) {
					time_t now = upsgi_now();
					if (upsgi.harakiri_options.spoolers > 0) {
						set_spooler_harakiri(upsgi.harakiri_options.spoolers);
					}
					ret = upsgi.p[i]->spooler(task, spool_buf, uh._pktsize, body, body_len);
					if (upsgi.harakiri_options.spoolers > 0) {
						set_spooler_harakiri(0);
					}
					if (ret == 0)
						continue;
					callable_found = 1;
					// increase task counter
					uspool->tasks++;
					if (ret == -2) {
						if (!upsgi.spooler_quiet)
							upsgi_log("[spooler %s pid: %d] done with task %s after %lld seconds\n", uspool->dir, (int) upsgi.mypid, task, (long long) upsgi_now() - now);
						destroy_spool(dir, task);
					}
					// re-spool it
					break;
				}
			}

			if (body)
				free(body);

			// here we free and unlock the task
			upsgi_protected_close(spool_fd);
			uspool->running = 0;


			// need to recycle ?
			if (upsgi.spooler_max_tasks > 0 && uspool->tasks >= (uint64_t) upsgi.spooler_max_tasks) {
				upsgi_log("[spooler %s pid: %d] maximum number of tasks reached (%d) recycling ...\n", uspool->dir, (int) upsgi.mypid, upsgi.spooler_max_tasks);
				end_me(0);
			}


			if (chdir(dir)) {
				upsgi_error("chdir()");
				upsgi_log("[spooler] something horrible happened to the spooler. Better to kill it.\n");
				exit(1);
			}

			if (!callable_found) {
				upsgi_log("unable to find the spooler function, have you loaded it into the spooler process ?\n");
			}

		}
	}
}

// this function checks which spooler should be spawned
void upsgi_spooler_cheap_check() {
	struct upsgi_spooler *uspool = upsgi.spoolers;
	char *last_managed = NULL;
	while(uspool) {
		// skip already active spoolers
		if (uspool->pid > 0) goto next; 
		// spooler dir names (in multiprocess mode, are ordered, so we can use
		// this trick for avoiding spawning multiple processes for the same dir
		// in the same cycle
		if (!last_managed || strcmp(last_managed, uspool->dir)) {
			// unfortunately, reusing readdir/scandir of the spooler is too dangerous
			// as the code is run in the master, let's do a simpler check
        		struct dirent *dp;
			DIR *sdir = opendir(uspool->dir);
			if (!sdir) goto next;
                	while ((dp = readdir(sdir)) != NULL) {
				if (strncmp("upsgi_spoolfile_on_", dp->d_name, 19)) continue;
				// a upsgi_spoolfile_on_* file has been found...
				uspool->respawned++;
				// spawn a new spooler
                                uspool->pid = spooler_start(uspool);
				last_managed = uspool->dir;
				break;
			}
			closedir(sdir);
		}
next:
		uspool = uspool->next;
	}
}
