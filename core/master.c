#include "upsgi.h"

extern struct upsgi_server upsgi;

static void master_check_processes() {

	// run the function, only if required
	if (!upsgi.die_on_no_workers) return;

	int alive_processes = 0;

	int i;
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
			alive_processes++;
		}
	}

	if (upsgi.die_on_no_workers) {
		if (!alive_processes) {
			upsgi_log_verbose("no more processes running, auto-killing ...\n");
			exit(1);
			// never here;
		}
	}
}

void upsgi_update_load_counters() {

	int i;
	uint64_t busy_workers = 0;
	uint64_t idle_workers = 0;
	static time_t last_sos = 0;

        for (i = 1; i <= upsgi.numproc; i++) {
                if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
                        if (upsgi_worker_is_busy(i) == 0) {
				idle_workers++;
			}
			else {
				busy_workers++;
			}
                }
        }

	if (busy_workers >= (uint64_t) upsgi.numproc) {
		ushared->overloaded++;
	
		if (upsgi.vassal_sos) {
			if (upsgi.current_time - last_sos > upsgi.vassal_sos) {
                        	upsgi_log_verbose("asking Emperor for reinforcements (overload: %llu)...\n", (unsigned long long) ushared->overloaded);
				vassal_sos();
				last_sos = upsgi.current_time;
			}
		}

	}

	ushared->busy_workers = busy_workers;
	ushared->idle_workers = idle_workers;

}

void upsgi_block_signal(int signum) {
	sigset_t smask;
	sigemptyset(&smask);
	sigaddset(&smask, signum);
	if (sigprocmask(SIG_BLOCK, &smask, NULL)) {
		upsgi_error("sigprocmask()");
	}
}

void upsgi_unblock_signal(int signum) {
	sigset_t smask;
	sigemptyset(&smask);
	sigaddset(&smask, signum);
	if (sigprocmask(SIG_UNBLOCK, &smask, NULL)) {
		upsgi_error("sigprocmask()");
	}
}

void upsgi_master_manage_udp(int udp_fd) {
	char buf[4096];
	struct sockaddr_in udp_client;
	char udp_client_addr[16];
	int i;

	socklen_t udp_len = sizeof(udp_client);
	ssize_t rlen = recvfrom(udp_fd, buf, 4096, 0, (struct sockaddr *) &udp_client, &udp_len);

	if (rlen < 0) {
		upsgi_error("upsgi_master_manage_udp()/recvfrom()");
	}
	else if (rlen > 0) {

		memset(udp_client_addr, 0, 16);
		if (inet_ntop(AF_INET, &udp_client.sin_addr.s_addr, udp_client_addr, 16)) {
			if (buf[0] == UPSGI_MODIFIER_MULTICAST_ANNOUNCE) {
			}
			else if (buf[0] == 0x30 && upsgi.snmp) {
				manage_snmp(udp_fd, (uint8_t *) buf, rlen, &udp_client);
			}
			else {

				// loop the various udp manager until one returns true
				int udp_managed = 0;
				for (i = 0; i < 256; i++) {
					if (upsgi.p[i]->manage_udp) {
						if (upsgi.p[i]->manage_udp(udp_client_addr, udp_client.sin_port, buf, rlen)) {
							udp_managed = 1;
							break;
						}
					}
				}

				// else a simple udp logger
				if (!udp_managed) {
					upsgi_log("[udp:%s:%d] %.*s", udp_client_addr, ntohs(udp_client.sin_port), (int) rlen, buf);
				}
			}
		}
		else {
			upsgi_error("upsgi_master_manage_udp()/inet_ntop()");
		}

	}
}

void suspend_resume_them_all(int signum) {

	int i;
	int suspend = 0;

	if (upsgi.workers[0].suspended == 1) {
		upsgi_log_verbose("*** (SIGTSTP received) resuming workers ***\n");
		upsgi.workers[0].suspended = 0;
	}
	else {
		upsgi_log_verbose("*** PAUSE (press start to resume, if you do not have a joypad send SIGTSTP) ***\n");
		suspend = 1;
		upsgi.workers[0].suspended = 1;
	}

	// subscribe/unsubscribe if needed
	upsgi_subscribe_all(suspend, 1);

	for (i = 1; i <= upsgi.numproc; i++) {
		upsgi.workers[i].suspended = suspend;
		if (upsgi.workers[i].pid > 0) {
			if (kill(upsgi.workers[i].pid, SIGTSTP)) {
				upsgi_error("kill()");
			}
		}
	}
}


void upsgi_master_check_mercy() {

	int i;

	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid > 0 && upsgi.workers[i].cursed_at) {
			if (upsgi_now() > upsgi.workers[i].no_mercy_at) {
				upsgi_log_verbose("worker %d (pid: %d) is taking too much time to die...NO MERCY !!!\n", i, upsgi.workers[i].pid);
				// yes that looks strange but we avoid calling it again if we skip waitpid() call below
				upsgi_curse(i, SIGKILL);
			}
		}
	}

	for (i = 0; i < upsgi.mules_cnt; i++) {
		if (upsgi.mules[i].pid > 0 && upsgi.mules[i].cursed_at) {
			if (upsgi_now() > upsgi.mules[i].no_mercy_at) {
				upsgi_log_verbose("mule %d (pid: %d) is taking too much time to die...NO MERCY !!!\n", i + 1, upsgi.mules[i].pid);
				upsgi_curse_mule(i, SIGKILL);
			}
		}
	}


	struct upsgi_spooler *us;
	for (us = upsgi.spoolers; us; us = us->next) {
		if (us->pid > 0 && us->cursed_at && upsgi_now() > us->no_mercy_at) {
				upsgi_log_verbose("spooler %d (pid: %d) is taking too much time to die...NO MERCY !!!\n", i + 1, us->pid);
				kill(us->pid, SIGKILL);
		}
	}
}


void expire_rb_timeouts(struct upsgi_rbtree *tree) {

	uint64_t current = (uint64_t) upsgi_now();
	struct upsgi_rb_timer *urbt;
	struct upsgi_signal_rb_timer *usrbt;

	for (;;) {

		urbt = upsgi_min_rb_timer(tree, NULL);

		if (urbt == NULL)
			return;

		if (urbt->value <= current) {
			// remove the timeout and add another
			usrbt = (struct upsgi_signal_rb_timer *) urbt->data;
			upsgi_del_rb_timer(tree, urbt);
			free(urbt);
			usrbt->iterations_done++;
			upsgi_route_signal(usrbt->sig);
			if (!usrbt->iterations || usrbt->iterations_done < usrbt->iterations) {
				usrbt->upsgi_rb_timer = upsgi_add_rb_timer(tree, upsgi_now() + usrbt->value, usrbt);
			}
			continue;
		}

		break;
	}
}

static void get_tcp_info(struct upsgi_socket *upsgi_sock) {

#if defined(__linux__) || defined(__FreeBSD__)
	int fd = upsgi_sock->fd;
	struct tcp_info ti;
	socklen_t tis = sizeof(struct tcp_info);

	if (!getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &tis)) {

		// checks for older kernels
#if defined(__linux__)
		if (!ti.tcpi_sacked) {
#elif defined(__FreeBSD__)
		if (!ti.__tcpi_sacked) {
#endif
			return;
		}

#if defined(__linux__)
		upsgi_sock->queue = (uint64_t) ti.tcpi_unacked;
		upsgi_sock->max_queue = (uint64_t) ti.tcpi_sacked;
#elif defined(__FreeBSD__)
		upsgi_sock->queue = (uint64_t) ti.__tcpi_unacked;
		upsgi_sock->max_queue = (uint64_t) ti.__tcpi_sacked;
#endif
	}

#endif
}


#ifdef __linux__
#include <linux/sockios.h>

#ifdef UNBIT
#define SIOBKLGQ 0x8908
#endif

#ifdef SIOBKLGQ

static void get_linux_unbit_SIOBKLGQ(struct upsgi_socket *upsgi_sock) {

	int fd = upsgi_sock->fd;
	int queue = 0;
	if (ioctl(fd, SIOBKLGQ, &queue) >= 0) {
		upsgi_sock->queue = (uint64_t) queue;
		upsgi_sock->max_queue = (uint64_t) upsgi.listen_queue;
	}
}
#endif
#endif

static void master_check_listen_queue() {

	uint64_t backlog = 0;
	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while(upsgi_sock) {
		if (upsgi_sock->family == AF_INET || upsgi_sock->family == AF_INET6) {
			get_tcp_info(upsgi_sock);
                }
#ifdef __linux__
#ifdef SIOBKLGQ
                else if (upsgi_sock->family == AF_UNIX) {
                	get_linux_unbit_SIOBKLGQ(upsgi_sock);
                }
#endif
#endif

		if (upsgi_sock->queue > backlog) {
			backlog = upsgi_sock->queue;
		}

		if (upsgi_sock->queue > 0 && upsgi_sock->queue >= upsgi_sock->max_queue) {
			upsgi_log_verbose("*** upsgi listen queue of socket \"%s\" (fd: %d) full !!! (%llu/%llu) ***\n", upsgi_sock->name, upsgi_sock->fd, (unsigned long long) upsgi_sock->queue, (unsigned long long) upsgi_sock->max_queue);

			if (upsgi.alarm_backlog) {
				char buf[1024];
				int ret = snprintf(buf, 1024, "listen queue of socket \"%s\" (fd: %d) full !!! (%llu/%llu)", upsgi_sock->name, upsgi_sock->fd, (unsigned long long) upsgi_sock->queue, (unsigned long long) upsgi_sock->max_queue);
				if (ret > 0 && ret < 1024) {
					struct upsgi_string_list *usl = NULL;
					upsgi_foreach(usl, upsgi.alarm_backlog) {
						upsgi_alarm_trigger(usl->value, buf, ret);
					}
				}
			}
		}
		upsgi_sock = upsgi_sock->next;
	}

	// TODO load should be something more advanced based on different values
	upsgi.shared->load = backlog;

	upsgi.shared->backlog = backlog;

        if (upsgi.vassal_sos_backlog > 0 && upsgi.has_emperor) {
        	if (upsgi.shared->backlog >= (uint64_t) upsgi.vassal_sos_backlog) {
                	// ask emperor for help
                        upsgi_log_verbose("asking Emperor for reinforcements (backlog: %llu)...\n", (unsigned long long) upsgi.shared->backlog);
			vassal_sos();
                }
	}
}

void vassal_sos() {
	if (!upsgi.has_emperor) {
		upsgi_log("[broodlord] instance not governed by an Emperor !!!\n");
		return;	
	}
	char byte = 30;
        if (write(upsgi.emperor_fd, &byte, 1) != 1) {
        	upsgi_error("vassal_sos()/write()");
        }
}

int master_loop(char **argv, char **environ) {

	struct timeval last_respawn;
	int last_respawn_rate = 0;

	pid_t diedpid;
	int waitpid_status;

	time_t now = 0;

	int i = 0;
	int rlen;

	int check_interval = 1;

	struct upsgi_rb_timer *min_timeout;
	struct upsgi_rbtree *rb_timers = upsgi_init_rb_timer();

	if (upsgi.procname_master) {
		upsgi_set_processname(upsgi.procname_master);
	}
	else if (upsgi.procname) {
		upsgi_set_processname(upsgi.procname);
	}
	else if (upsgi.auto_procname) {
		upsgi_set_processname("upsgi master");
	}


	upsgi.current_time = upsgi_now();

	upsgi_unix_signal(SIGTSTP, suspend_resume_them_all);
	upsgi_unix_signal(SIGHUP, grace_them_all);

	upsgi_unix_signal(SIGTERM, kill_them_all);
	upsgi_unix_signal(SIGQUIT, reap_them_all);

	upsgi_unix_signal(SIGINT, kill_them_all);
	upsgi_unix_signal(SIGUSR1, stats);

	atexit(upsgi_master_cleanup_hooks);

	upsgi.master_queue = event_queue_init();

	/* route signals to workers... */
#ifdef UPSGI_DEBUG
	upsgi_log("adding %d to signal poll\n", upsgi.shared->worker_signal_pipe[0]);
#endif
	event_queue_add_fd_read(upsgi.master_queue, upsgi.shared->worker_signal_pipe[0]);

	if (upsgi.master_fifo) {
		upsgi.master_fifo_fd = upsgi_master_fifo();
		event_queue_add_fd_read(upsgi.master_queue, upsgi.master_fifo_fd);
	}

	if (upsgi.notify_socket) {
		upsgi.notify_socket_fd = bind_to_unix_dgram(upsgi.notify_socket);
		upsgi_log("notification socket enabled on %s (fd: %d)\n", upsgi.notify_socket, upsgi.notify_socket_fd);
		event_queue_add_fd_read(upsgi.master_queue, upsgi.notify_socket_fd);
	}

	if (upsgi.spoolers) {
		event_queue_add_fd_read(upsgi.master_queue, upsgi.shared->spooler_signal_pipe[0]);
	}

	if (upsgi.mules_cnt > 0) {
		event_queue_add_fd_read(upsgi.master_queue, upsgi.shared->mule_signal_pipe[0]);
	}

	if (upsgi.log_master) {
		upsgi.log_master_buf = upsgi_malloc(upsgi.log_master_bufsize);
		if (!upsgi.threaded_logger) {
#ifdef UPSGI_DEBUG
			upsgi_log("adding %d to master logging\n", upsgi.shared->worker_log_pipe[0]);
#endif
			event_queue_add_fd_read(upsgi.master_queue, upsgi.shared->worker_log_pipe[0]);
			if (upsgi.req_log_master) {
				event_queue_add_fd_read(upsgi.master_queue, upsgi.shared->worker_req_log_pipe[0]);
			}
		}
		else {
			upsgi_threaded_logger_spawn();
		}

	}

#ifdef UPSGI_SSL
	upsgi_start_legions();
#endif
	upsgi_metrics_start_collector();

	upsgi_add_reload_fds();

	upsgi_cache_start_sweepers();
	upsgi_cache_start_sync_servers();

	upsgi.wsgi_req->buffer = upsgi.workers[0].cores[0].buffer;

	if (upsgi.has_emperor) {
		if (upsgi.emperor_proxy) {
			upsgi.emperor_fd_proxy = bind_to_unix(upsgi.emperor_proxy, upsgi.listen_queue, 0, 0);
			if (upsgi.emperor_fd_proxy < 0) exit(1);
			if (chmod(upsgi.emperor_proxy, S_IRUSR|S_IWUSR)) {
				upsgi_error("[emperor-proxy] chmod()");
				exit(1);
			}
			event_queue_add_fd_read(upsgi.master_queue, upsgi.emperor_fd_proxy);
		}
		else {
			event_queue_add_fd_read(upsgi.master_queue, upsgi.emperor_fd);
		}
	}

#ifdef __linux__
	if (upsgi.setns_socket) {
		upsgi.setns_socket_fd = bind_to_unix(upsgi.setns_socket, upsgi.listen_queue, 0, 0);
		if (upsgi.setns_socket_fd < 0) exit(1);
		if (chmod(upsgi.setns_socket, S_IRUSR|S_IWUSR)) {
                	upsgi_error("[setns-socket] chmod()");
                        exit(1);
                }
                event_queue_add_fd_read(upsgi.master_queue, upsgi.setns_socket_fd);
	}
#endif

	if (upsgi.zerg_server) {
		upsgi.zerg_server_fd = bind_to_unix(upsgi.zerg_server, upsgi.listen_queue, 0, 0);
		event_queue_add_fd_read(upsgi.master_queue, upsgi.zerg_server_fd);
		upsgi_log("*** Zerg server enabled on %s ***\n", upsgi.zerg_server);
	}

	if (upsgi.stats) {
		char *tcp_port = strrchr(upsgi.stats, ':');
		if (tcp_port) {
			// disable deferred accept for this socket
			int current_defer_accept = upsgi.no_defer_accept;
			upsgi.no_defer_accept = 1;
			upsgi.stats_fd = bind_to_tcp(upsgi.stats, upsgi.listen_queue, tcp_port);
			upsgi.no_defer_accept = current_defer_accept;
		}
		else {
			upsgi.stats_fd = bind_to_unix(upsgi.stats, upsgi.listen_queue, upsgi.chmod_socket, upsgi.abstract_socket);
		}

		event_queue_add_fd_read(upsgi.master_queue, upsgi.stats_fd);
		upsgi_log("*** Stats server enabled on %s fd: %d ***\n", upsgi.stats, upsgi.stats_fd);
	}


	if (upsgi.stats_pusher_instances) {
		if (!upsgi_thread_new(upsgi_stats_pusher_loop)) {
			upsgi_log("!!! unable to spawn stats pusher thread !!!\n");
			exit(1);
		}
	}

	if (upsgi.udp_socket) {
		upsgi.udp_fd = bind_to_udp(upsgi.udp_socket, 0, 0);
		if (upsgi.udp_fd < 0) {
			upsgi_log("unable to bind to udp socket. SNMP services will be disabled.\n");
		}
		else {
			upsgi_log("UDP server enabled.\n");
			event_queue_add_fd_read(upsgi.master_queue, upsgi.udp_fd);
		}
	}

	upsgi.snmp_fd = upsgi_setup_snmp();

	if (upsgi.status.is_cheap) {
		upsgi_add_sockets_to_queue(upsgi.master_queue, -1);
		for (i = 1; i <= upsgi.numproc; i++) {
			upsgi.workers[i].cheaped = 1;
		}
		upsgi_log("cheap mode enabled: waiting for socket connection...\n");
	}


	// spawn mules
	for (i = 0; i < upsgi.mules_cnt; i++) {
		size_t mule_patch_size = 0;
		upsgi.mules[i].patch = upsgi_string_get_list(&upsgi.mules_patches, i, &mule_patch_size);
		upsgi_mule(i + 1);
	}

	// spawn gateways
	for (i = 0; i < ushared->gateways_cnt; i++) {
		if (ushared->gateways[i].pid == 0) {
			gateway_respawn(i);
		}
	}

	// spawn daemons
	upsgi_daemons_spawn_all();

	// first subscription
	upsgi_subscribe_all(0, 1);

	// sync the cache store if needed
	upsgi_cache_sync_all();

	if (upsgi.queue_store && upsgi.queue_filesize) {
		if (msync(upsgi.queue_header, upsgi.queue_filesize, MS_ASYNC)) {
			upsgi_error("msync()");
		}
	}

	// update touches timestamps
	upsgi_check_touches(upsgi.touch_reload);
	upsgi_check_touches(upsgi.touch_logrotate);
	upsgi_check_touches(upsgi.touch_logreopen);
	upsgi_check_touches(upsgi.touch_chain_reload);
	upsgi_check_touches(upsgi.touch_workers_reload);
	upsgi_check_touches(upsgi.touch_gracefully_stop);
	// update exec touches
	struct upsgi_string_list *usl = upsgi.touch_exec;
	while (usl) {
		char *space = strchr(usl->value, ' ');
		if (space) {
			*space = 0;
			usl->len = strlen(usl->value);
			usl->custom_ptr = space + 1;
		}
		usl = usl->next;
	}
	upsgi_check_touches(upsgi.touch_exec);
	// update signal touches
	usl = upsgi.touch_signal;
	while (usl) {
		char *space = strchr(usl->value, ' ');
		if (space) {
			*space = 0;
			usl->len = strlen(usl->value);
			usl->custom_ptr = space + 1;
		}
		usl = usl->next;
	}
	upsgi_check_touches(upsgi.touch_signal);
	// daemon touches
	struct upsgi_daemon *ud = upsgi.daemons;
        while (ud) {
		if (ud->touch) {
			upsgi_check_touches(ud->touch);
		}
		ud = ud->next;
	}
	// hook touches
	upsgi_foreach(usl, upsgi.hook_touch) {
		char *space = strchr(usl->value, ' ');
		if (space) {
                        *space = 0;
                        usl->len = strlen(usl->value);
			upsgi_string_new_list((struct upsgi_string_list **)&usl->custom_ptr, space+1);
                }
	}
	upsgi_check_touches(upsgi.hook_touch);

	// fsmon
	upsgi_fsmon_setup();

	upsgi_foreach(usl, upsgi.signal_timers) {
		char *space = strchr(usl->value, ' ');
		if (!space) {
			upsgi_log("invalid signal timer syntax, must be: <signal> <seconds>\n");
			exit(1);
		}
		*space = 0;
		upsgi_add_timer(atoi(usl->value), atoi(space+1));
		*space = ' ';
	}

	upsgi_foreach(usl, upsgi.rb_signal_timers) {
                char *space = strchr(usl->value, ' ');
                if (!space) {
                        upsgi_log("invalid redblack signal timer syntax, must be: <signal> <seconds>\n");
                        exit(1);
                }
                *space = 0;
                upsgi_signal_add_rb_timer(atoi(usl->value), atoi(space+1), 0);
                *space = ' ';
        }

	// setup cheaper algos (can be stacked)
	upsgi.cheaper_algo = upsgi_cheaper_algo_spare;
	if (upsgi.requested_cheaper_algo) {
		upsgi.cheaper_algo = NULL;
		struct upsgi_cheaper_algo *uca = upsgi.cheaper_algos;
		while (uca) {
			if (!strcmp(uca->name, upsgi.requested_cheaper_algo)) {
				upsgi.cheaper_algo = uca->func;
				break;
			}
			uca = uca->next;
		}

		if (!upsgi.cheaper_algo) {
			upsgi_log("unable to find requested cheaper algorithm, falling back to spare\n");
			upsgi.cheaper_algo = upsgi_cheaper_algo_spare;
		}

	}

	// here really starts the master loop
	upsgi_hooks_run(upsgi.hook_master_start, "master-start", 1);

	for (;;) {
		//upsgi_log("upsgi.ready_to_reload %d %d\n", upsgi.ready_to_reload, upsgi.numproc);

		// run master_cycle hook for every plugin
		for (i = 0; i < upsgi.gp_cnt; i++) {
			if (upsgi.gp[i]->master_cycle) {
				upsgi.gp[i]->master_cycle();
			}
		}
		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->master_cycle) {
				upsgi.p[i]->master_cycle();
			}
		}

		// check for death (before reload !!!)
		upsgi_master_check_death();
		// check for reload
		if (upsgi_master_check_reload(argv)) {
			return -1;
		}

		// check chain reload
		upsgi_master_check_chain();

		// check if some worker is taking too much to die...
		upsgi_master_check_mercy();

		// check for daemons (smart and dumb)
		upsgi_daemons_smart_check();

		// cheaper management
		if (upsgi.cheaper && !upsgi.status.is_cheap && !upsgi_instance_is_reloading && !upsgi_instance_is_dying && !upsgi.workers[0].suspended) {
			if (!upsgi_calc_cheaper())
				return 0;
		}

		// spooler cheap management
		if (upsgi.spooler_cheap) {
			if ((upsgi.master_cycles % upsgi.spooler_frequency) == 0) {
				upsgi_spooler_cheap_check();
			}
		}


		// check if someone is dead
		diedpid = waitpid(WAIT_ANY, &waitpid_status, WNOHANG);
		if (diedpid == -1) {
			if (errno == ECHILD) {
				// something did not work as expected, just assume all has been cleared
				upsgi_master_commit_status();
				diedpid = 0;
			}
			else {
				upsgi_error("waitpid()");
				/* here is better to reload all the upsgi stack */
				upsgi_log("something horrible happened...\n");
				reap_them_all(0);
				exit(1);
			}
		}

		// no one died just run all of the standard master tasks
		if (diedpid == 0) {

			/* all processes ok, doing status scan after N seconds */
			check_interval = upsgi.master_interval;
			if (!check_interval) {
				check_interval = 1;
				upsgi.master_interval = 1;
			}


			// add unregistered file monitors
			// locking is not needed as monitors can only increase
			for (i = 0; i < ushared->files_monitored_cnt; i++) {
				if (!ushared->files_monitored[i].registered) {
					ushared->files_monitored[i].fd = event_queue_add_file_monitor(upsgi.master_queue, ushared->files_monitored[i].filename, &ushared->files_monitored[i].id);
					ushared->files_monitored[i].registered = 1;
				}
			}


			// add unregistered timers
			// locking is not needed as timers can only increase
			for (i = 0; i < ushared->timers_cnt; i++) {
				if (!ushared->timers[i].registered) {
					ushared->timers[i].fd = event_queue_add_timer_hr(upsgi.master_queue, &ushared->timers[i].id, ushared->timers[i].value, ushared->timers[i].nsvalue);
					ushared->timers[i].registered = 1;
				}
			}

			// add unregistered rb_timers
			// locking is not needed as rb_timers can only increase
			for (i = 0; i < ushared->rb_timers_cnt; i++) {
				if (!ushared->rb_timers[i].registered) {
					ushared->rb_timers[i].upsgi_rb_timer = upsgi_add_rb_timer(rb_timers, upsgi_now() + ushared->rb_timers[i].value, &ushared->rb_timers[i]);
					ushared->rb_timers[i].registered = 1;
				}
			}

			int interesting_fd = -1;

			if (ushared->rb_timers_cnt > 0) {
				min_timeout = upsgi_min_rb_timer(rb_timers, NULL);
				if (min_timeout) {
					int delta = min_timeout->value - upsgi_now();
					if (delta <= 0) {
						expire_rb_timeouts(rb_timers);
					}
					// if the timer expires before the check_interval, override it
					else if (delta < check_interval) {
						check_interval = delta;
					}
				}
			}

			// wait for event
			rlen = event_queue_wait(upsgi.master_queue, check_interval, &interesting_fd);

			if (rlen == 0) {
				if (ushared->rb_timers_cnt > 0) {
					expire_rb_timeouts(rb_timers);
				}
			}

			// update load counter
			upsgi_update_load_counters();

			master_check_processes();


			// check upsgi-cron table
			if (ushared->cron_cnt) {
				upsgi_manage_signal_cron(upsgi_now());
			}

			if (upsgi.crons) {
				upsgi_manage_command_cron(upsgi_now());
			}

			// some event returned
			if (rlen > 0) {
				// if the following function returns -1, a new worker has just spawned
				if (upsgi_master_manage_events(interesting_fd)) {
					return 0;
				}
			}

			now = upsgi_now();
			if (now - upsgi.current_time < 1) {
				continue;
			}
			upsgi.current_time = now;

			// checking logsize
			if (upsgi.logfile) {
				upsgi_check_logrotate();
			}

			// this will be incremented at (more or less) regular intervals
			upsgi.master_cycles++;

			// recalculate requests counter on race conditions risky configurations
			// a bit of inaccuracy is better than locking;)
			upsgi_master_fix_request_counters();

			// check for idle
			upsgi_master_check_idle();

			check_interval = upsgi.master_interval;
			if (!check_interval) {
				check_interval = 1;
				upsgi.master_interval = 1;
			}


			// check listen_queue status
			master_check_listen_queue();

			int someone_killed = 0;
			// check if some worker has to die (harakiri, evil checks...)
			if (upsgi_master_check_workers_deadline()) someone_killed++;
			if (upsgi_master_check_gateways_deadline()) someone_killed++;
			if (upsgi_master_check_mules_deadline()) someone_killed++;
			if (upsgi_master_check_spoolers_deadline()) someone_killed++;
			if (upsgi_master_check_crons_deadline()) someone_killed++;

			// this could trigger a complete exit...
			upsgi_master_check_mountpoints();

#ifdef __linux__
#ifdef MADV_MERGEABLE
			if (upsgi.linux_ksm > 0 && (upsgi.master_cycles % upsgi.linux_ksm) == 0) {
				upsgi_linux_ksm_map();
			}
#endif
#endif

			// resubscribe every 10 cycles by default
			if (((upsgi.subscriptions || upsgi.subscriptions2) && ((upsgi.master_cycles % upsgi.subscribe_freq) == 0 || upsgi.master_cycles == 1)) && !upsgi_instance_is_reloading && !upsgi_instance_is_dying && !upsgi.workers[0].suspended) {
				upsgi_subscribe_all(0, 0);
			}

			upsgi_cache_sync_all();

			if (upsgi.queue_store && upsgi.queue_filesize && upsgi.queue_store_sync && ((upsgi.master_cycles % upsgi.queue_store_sync) == 0)) {
				if (msync(upsgi.queue_header, upsgi.queue_filesize, MS_ASYNC)) {
					upsgi_error("msync()");
				}
			}

			// check touch_reload
			if (!upsgi_instance_is_reloading && !upsgi_instance_is_dying) {
				char *touched = upsgi_check_touches(upsgi.touch_reload);
				if (touched) {
					upsgi_log_verbose("*** %s has been touched... grace them all !!! ***\n", touched);
					upsgi_block_signal(SIGHUP);
					grace_them_all(0);
					upsgi_unblock_signal(SIGHUP);
					continue;
				}
				touched = upsgi_check_touches(upsgi.touch_workers_reload);
				if (touched) {
					upsgi_log_verbose("*** %s has been touched... workers reload !!! ***\n", touched);
					upsgi_reload_workers();
					continue;
				}
				touched = upsgi_check_touches(upsgi.touch_mules_reload);
				if (touched) {
					upsgi_log_verbose("*** %s has been touched... mules reload !!! ***\n", touched);
					upsgi_reload_mules();
					continue;
				}
				touched = upsgi_check_touches(upsgi.touch_spoolers_reload);
				if (touched) {
					upsgi_log_verbose("*** %s has been touched... spoolers reload !!! ***\n", touched);
					upsgi_reload_spoolers();
					continue;
				}
				touched = upsgi_check_touches(upsgi.touch_chain_reload);
				if (touched) {
					if (upsgi.status.chain_reloading == 0) {
						upsgi_log_verbose("*** %s has been touched... chain reload !!! ***\n", touched);
						upsgi.status.chain_reloading = 1;
					}
					else {
						upsgi_log_verbose("*** %s has been touched... but chain reload is already running ***\n", touched);
					}
				}

				// be sure to run it as the last touch check
				touched = upsgi_check_touches(upsgi.touch_exec);
				if (touched) {
					if (upsgi_run_command(touched, NULL, -1) >= 0) {
						upsgi_log_verbose("[upsgi-touch-exec] running %s\n", touched);
					}
				}
				touched = upsgi_check_touches(upsgi.touch_signal);
				if (touched) {
					uint8_t signum = atoi(touched);
					upsgi_route_signal(signum);
					upsgi_log_verbose("[upsgi-touch-signal] raising %u\n", signum);
				}

				// daemon touches
        			struct upsgi_daemon *ud = upsgi.daemons;
        			while (ud) {
                			if (ud->pid > 0 && ud->touch) {
                        			touched = upsgi_check_touches(ud->touch);
						if (touched) {
							upsgi_log_verbose("*** %s has been touched... reloading daemon \"%s\" (pid: %d) !!! ***\n", touched, ud->command, (int) ud->pid);
							if (kill(-ud->pid, ud->stop_signal)) {
								// killing process group failed, try to kill by process id
								if (kill(ud->pid, ud->stop_signal)) {
									upsgi_error("[upsgi-daemon/touch] kill()");
								}
							}
						}
                			}
					ud = ud->next;
                		}

				// hook touches
				touched = upsgi_check_touches(upsgi.hook_touch);
				if (touched) {
					upsgi_hooks_run((struct upsgi_string_list *) touched, "touch", 0);
				}

			}

			// allows the KILL signal to be delivered;
			if (someone_killed > 0) sleep(1);
			continue;

		}

		// no one died
		if (diedpid <= 0)
			continue;

		// check for deadlocks first
		upsgi_deadlock_check(diedpid);

		// reload gateways and daemons only on normal workflow (+outworld status)
		if (!upsgi_instance_is_reloading && !upsgi_instance_is_dying) {

			if (upsgi_master_check_emperor_death(diedpid))
				continue;
			if (upsgi_master_check_spoolers_death(diedpid))
				continue;
			if (upsgi_master_check_mules_death(diedpid))
				continue;
			if (upsgi_master_check_gateways_death(diedpid))
				continue;
			if (upsgi_master_check_daemons_death(diedpid))
				continue;
			if (upsgi_master_check_cron_death(diedpid))
				continue;
		}


		/* What happens here ?

		   case 1) the diedpid is not a worker, report it and continue
		   case 2) the diedpid is a worker and we are not in a reload procedure -> reload it
		   case 3) the diedpid is a worker and we are in graceful reload -> upsgi.ready_to_reload++ and continue
		   case 3) the diedpid is a worker and we are in brutal reload -> upsgi.ready_to_die++ and continue


		 */

		int thewid = find_worker_id(diedpid);
		if (thewid <= 0) {
			// check spooler, mules, gateways and daemons
			struct upsgi_spooler *uspool = upsgi.spoolers;
			while (uspool) {
				if (uspool->pid > 0 && diedpid == uspool->pid) {
					upsgi_log("spooler (pid: %d) annihilated\n", (int) diedpid);
					goto next;
				}
				uspool = uspool->next;
			}

			for (i = 0; i < upsgi.mules_cnt; i++) {
				if (upsgi.mules[i].pid == diedpid) {
					upsgi_log("mule %d (pid: %d) annihilated\n", i + 1, (int) diedpid);
					upsgi.mules[i].pid = 0;
					goto next;
				}
			}

			for (i = 0; i < ushared->gateways_cnt; i++) {
				if (ushared->gateways[i].pid == diedpid) {
					upsgi_log("gateway %d (%s, pid: %d) annihilated\n", i + 1, ushared->gateways[i].fullname, (int) diedpid);
					goto next;
				}
			}

			if (upsgi_daemon_check_pid_death(diedpid))
				goto next;

			if (WIFEXITED(waitpid_status)) {
				upsgi_log("subprocess %d exited with code %d\n", (int) diedpid, WEXITSTATUS(waitpid_status));
			}
			else if (WIFSIGNALED(waitpid_status)) {
				upsgi_log("subprocess %d exited by signal %d\n", (int) diedpid, WTERMSIG(waitpid_status));
			}
			else if (WIFSTOPPED(waitpid_status)) {
				upsgi_log("subprocess %d stopped\n", (int) diedpid);
			}
next:
			continue;
		}


		// ok a worker died...
		upsgi.workers[thewid].pid = 0;
		// only to be safe :P
		for(i=0;i<upsgi.cores;i++) {
			upsgi.workers[thewid].cores[i].harakiri = 0;
		}


		// first check failed app loading in need-app mode
		if (WIFEXITED(waitpid_status) && WEXITSTATUS(waitpid_status) == UPSGI_FAILED_APP_CODE) {
			if (upsgi.lazy_apps && upsgi.need_app) {
				upsgi_log("OOPS ! failed loading app in worker %d (pid %d)\n", thewid, (int) diedpid);
				upsgi_log_verbose("need-app requested, destroying the instance...\n");
				upsgi.status.dying_for_need_app = 1;
				kill_them_all(0);
				continue;
			}
			else {
				upsgi_log("OOPS ! failed loading app in worker %d (pid %d) :( trying again...\n", thewid, (int) diedpid);
			}
		}

		// ok, if we are reloading or dying, just continue the master loop
		// as soon as all of the workers have pid == 0, the action (exit, or reload) is triggered
		if (upsgi_instance_is_reloading || upsgi_instance_is_dying) {
			if (!upsgi.workers[thewid].cursed_at)
				upsgi.workers[thewid].cursed_at = upsgi_now();
			upsgi_log("worker %d buried after %d seconds\n", thewid, (int) (upsgi_now() - upsgi.workers[thewid].cursed_at));
			upsgi.workers[thewid].cursed_at = 0;
			// if we are stopping workers, just end here
			continue;
		}

		if (WIFEXITED(waitpid_status) && WEXITSTATUS(waitpid_status) == UPSGI_DE_HIJACKED_CODE) {
			upsgi_log("...restoring worker %d (pid: %d)...\n", thewid, (int) diedpid);
		}
		else if (WIFEXITED(waitpid_status) && WEXITSTATUS(waitpid_status) == UPSGI_EXCEPTION_CODE) {
			upsgi_log("... monitored exception detected, respawning worker %d (pid: %d)...\n", thewid, (int) diedpid);
		}
		else if (WIFEXITED(waitpid_status) && WEXITSTATUS(waitpid_status) == UPSGI_QUIET_CODE) {
			// noop
		}
		else if (WIFEXITED(waitpid_status) && WEXITSTATUS(waitpid_status) == UPSGI_BRUTAL_RELOAD_CODE) {
			upsgi_log("!!! inconsistent state reported by worker %d (pid: %d) !!!\n", thewid, (int) diedpid);
			reap_them_all(0);
			continue;
		}
		else if (WIFEXITED(waitpid_status) && WEXITSTATUS(waitpid_status) == UPSGI_GO_CHEAP_CODE) {
			upsgi_log("worker %d asked for cheap mode (pid: %d)...\n", thewid, (int) diedpid);
			upsgi.workers[thewid].cheaped = 1;
		}
		else if (upsgi.workers[thewid].manage_next_request) {
			if (WIFSIGNALED(waitpid_status)) {
				upsgi_log("DAMN ! worker %d (pid: %d) died, killed by signal %d :( trying respawn ...\n", thewid, (int) diedpid, (int) WTERMSIG(waitpid_status));
			}
			else {
				upsgi_log("DAMN ! worker %d (pid: %d) died :( trying respawn ...\n", thewid, (int) diedpid);
			}
		}
		else if (upsgi.workers[thewid].cursed_at > 0) {
			upsgi_log("worker %d killed successfully (pid: %d)\n", thewid, (int) diedpid);
		}
		// manage_next_request is zero, but killed by signal...
		else if (WIFSIGNALED(waitpid_status)) {
			upsgi_log("DAMN ! worker %d (pid: %d) MYSTERIOUSLY killed by signal %d :( trying respawn ...\n", thewid, (int) diedpid, (int) WTERMSIG(waitpid_status));
		}

		if (upsgi.workers[thewid].cheaped == 1) {
			upsgi_log("upsgi worker %d cheaped.\n", thewid);
			continue;
		}

		// avoid fork bombing
		gettimeofday(&last_respawn, NULL);
		if (last_respawn.tv_sec <= upsgi.respawn_delta + check_interval) {
			last_respawn_rate++;
			if (last_respawn_rate > upsgi.numproc) {
				if (upsgi.forkbomb_delay > 0) {
					upsgi_log("worker respawning too fast !!! i have to sleep a bit (%d seconds)...\n", upsgi.forkbomb_delay);
					/* use --forkbomb-delay 0 to disable sleeping */
					sleep(upsgi.forkbomb_delay);
				}
				last_respawn_rate = 0;
			}
		}
		else {
			last_respawn_rate = 0;
		}
		gettimeofday(&last_respawn, NULL);
		upsgi.respawn_delta = last_respawn.tv_sec;

		// are we chain reloading it ?
		if (upsgi.status.chain_reloading == thewid) {
			upsgi.status.chain_reloading++;
		}

		// respawn the worker (if needed)
		if (upsgi_respawn_worker(thewid))
			return 0;

		// end of the loop
	}

	// never here
}

void upsgi_reload_workers() {
	int i;
	upsgi_block_signal(SIGHUP);
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid > 0) {
			upsgi_curse(i, SIGHUP);
		}
	}
	upsgi_unblock_signal(SIGHUP);
}

void upsgi_reload_mules() {
	int i;

	upsgi_block_signal(SIGHUP);
	for (i = 0; i <= upsgi.mules_cnt; i++) {
		if (upsgi.mules[i].pid > 0) {
			upsgi_curse_mule(i, SIGHUP);
		}
	}
	upsgi_unblock_signal(SIGHUP);
}

void upsgi_reload_spoolers() {
	struct upsgi_spooler *us;

	upsgi_block_signal(SIGHUP);
	for (us = upsgi.spoolers; us; us = us->next) {
		if (us->pid > 0) {
			kill(us->pid, SIGHUP);
			us->cursed_at = upsgi_now();
			us->no_mercy_at = us->cursed_at + upsgi.spooler_reload_mercy;
		}
	}
	upsgi_unblock_signal(SIGHUP);
}

void upsgi_chain_reload() {
	if (!upsgi.status.chain_reloading) {
		if (upsgi.chain_reload_delay > 0) {
			upsgi_log_verbose("chain reload starting (paced %d sec between worker turnovers)...\n", upsgi.chain_reload_delay);
		}
		else {
			upsgi_log_verbose("chain reload starting...\n");
		}
		upsgi.status.chain_reloading = 1;
	}
	else {
		upsgi_log_verbose("chain reload already running...\n");
	}
}

void upsgi_brutally_reload_workers() {
	int i;
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].pid > 0) {
			upsgi_log_verbose("killing worker %d (pid: %d)\n", i, (int) upsgi.workers[i].pid);
			upsgi_curse(i, SIGINT);
		}
	}
}
