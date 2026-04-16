#include "upsgi.h"

extern struct upsgi_server upsgi;

int upsgi_master_manage_events(int interesting_fd) {

	// is a logline ?
	if (upsgi.log_master && !upsgi.threaded_logger) {
		// stderr log ?
		if (interesting_fd == upsgi.shared->worker_log_pipe[0]) {
			upsgi_master_log_drain(upsgi.log_drain_burst);
			return 0;
		}
		// req log ?
		if (upsgi.req_log_master && interesting_fd == upsgi.shared->worker_req_log_pipe[0]) {
			upsgi_master_req_log_drain(upsgi.log_drain_burst);
			return 0;
		}
	}

	if (upsgi.master_fifo_fd > -1 && interesting_fd == upsgi.master_fifo_fd) {
		return upsgi_master_fifo_manage(upsgi.master_fifo_fd);
	}

	if (upsgi.notify_socket_fd > -1 && interesting_fd == upsgi.notify_socket_fd) {
		return upsgi_notify_socket_manage(interesting_fd);
	}

	// stats server ?
	if (upsgi.stats && upsgi.stats_fd > -1) {
		if (interesting_fd == upsgi.stats_fd) {
			upsgi_send_stats(upsgi.stats_fd, upsgi_master_generate_stats);
			return 0;
		}
	}

	// a zerg connection ?
	if (upsgi.zerg_server) {
		if (interesting_fd == upsgi.zerg_server_fd) {
			upsgi_manage_zerg(upsgi.zerg_server_fd, 0, NULL);
			return 0;
		}
	}

	// emperor event ?
	if (upsgi.has_emperor) {
		if (upsgi.emperor_fd_proxy > -1 && interesting_fd == upsgi.emperor_fd_proxy) {
			upsgi_master_manage_emperor_proxy(upsgi.emperor_fd_proxy, upsgi.emperor_fd, upsgi.emperor_fd_config, -1);	
			return 0;
		}

		if (interesting_fd == upsgi.emperor_fd) {
			upsgi_master_manage_emperor();
			return 0;
		}
	}

#ifdef __linux__
	if (upsgi.setns_socket && upsgi.setns_socket_fd > -1 && interesting_fd == upsgi.setns_socket_fd) {
		upsgi_master_manage_setns(upsgi.setns_socket_fd);
	}
#endif

	if (upsgi_fsmon_event(interesting_fd)) {
                return 0;
	}

	// reload on fd
	if (upsgi.reload_on_fd) {
		// custom -> fd
		// custom2 -> len (optional, default 1)
		// custom_ptr -> log message (optional)
		struct upsgi_string_list *usl = upsgi.reload_on_fd;
		while(usl) {
			if (interesting_fd == (int) usl->custom) {
				char stack_tmp[8];
				char *tmp = stack_tmp;
				if (usl->custom2 > 8) {
					tmp = upsgi_malloc(usl->custom2);
				}
				if (read(interesting_fd, tmp, usl->custom2) <= 0) {
					upsgi_error("[reload-on-fd] read()");
				}
				if (usl->custom_ptr) {
					upsgi_log_verbose("*** fd %d ready: %s ***\n", interesting_fd, usl->custom_ptr);
				}
				else {
					upsgi_log_verbose("*** fd %d ready !!! ***\n", interesting_fd);
				}
                                upsgi_block_signal(SIGHUP);
                                grace_them_all(0);
                                upsgi_unblock_signal(SIGHUP);				
				return 0;
			}
			usl = usl->next;
		}
	}

	// brutal reload on fd
        if (upsgi.brutal_reload_on_fd) {
                // custom -> fd
                // custom2 -> len (optional, default 1)
                // custom_ptr -> log message (optional)
                struct upsgi_string_list *usl = upsgi.brutal_reload_on_fd;
                while(usl) {
                        if (interesting_fd == (int) usl->custom) {
				char stack_tmp[8];
                                char *tmp = stack_tmp;
                                if (usl->custom2 > 8) {
                                        tmp = upsgi_malloc(usl->custom2);
                                }
                                if (read(interesting_fd, tmp, usl->custom2) <= 0) {
                                        upsgi_error("[brutal-reload-on-fd] read()");
                                }
                                if (usl->custom_ptr) {
                                        upsgi_log_verbose("*** fd %d ready: %s ***\n", interesting_fd, usl->custom_ptr);
                                }
                                else {
                                        upsgi_log_verbose("*** fd %d ready !!! ***\n", interesting_fd);
                                }
                                upsgi_block_signal(SIGQUIT);
                                reap_them_all(0);
                                upsgi_unblock_signal(SIGQUIT);
                                if (usl->custom2 > 8) free(tmp);
                                return 0;
                        }
                        usl = usl->next;
                }
        }


	// wakeup from cheap mode ?
	if (upsgi.status.is_cheap) {
		struct upsgi_socket *upsgi_sock = upsgi.sockets;
		while (upsgi_sock) {
			if (interesting_fd == upsgi_sock->fd) {
				upsgi.status.is_cheap = 0;
				upsgi_del_sockets_from_queue(upsgi.master_queue);
				// how many worker we need to respawn ?
				int needed = upsgi.numproc;
				// if in cheaper mode, just respawn the minimal amount
				if (upsgi.cheaper) {
					needed = upsgi.cheaper_count;
				}
				int i;
				for (i = 1; i <= needed; i++) {
					if (upsgi_respawn_worker(i))
						return -1;
				}
				// here we continue instead of returning
				break;
			}
			upsgi_sock = upsgi_sock->next;
		}
	}


	// an SNMP request ?
	if (upsgi.snmp_addr && interesting_fd == upsgi.snmp_fd) {
		upsgi_master_manage_snmp(upsgi.snmp_fd);
		return 0;
	}

	// a UDP request ?
	if (upsgi.udp_socket && interesting_fd == upsgi.udp_fd) {
		upsgi_master_manage_udp(upsgi.udp_fd);
		return 0;
	}


	// check if some file monitor is ready
	// no need to lock as we are only getting registered items (and only the master can register them)
	int i;
	for (i = 0; i < ushared->files_monitored_cnt; i++) {
		if (ushared->files_monitored[i].registered) {
			if (interesting_fd == ushared->files_monitored[i].fd) {
				struct upsgi_fmon *uf = event_queue_ack_file_monitor(upsgi.master_queue, interesting_fd);
				// now call the file_monitor handler
				if (uf)
					upsgi_route_signal(uf->sig);
				return 0;
			}
		}
	}

	// check if some timer elapsed
	// no need to lock again
	for (i = 0; i < ushared->timers_cnt; i++) {
		if (ushared->timers[i].registered) {
			if (interesting_fd == ushared->timers[i].fd) {
				struct upsgi_timer *ut = event_queue_ack_timer(interesting_fd);
				// now call the timer handler
				if (ut)
					upsgi_route_signal(ut->sig);
				return 0;
			}
		}
	}

	uint8_t upsgi_signal;
	// check for worker signal
	if (interesting_fd == upsgi.shared->worker_signal_pipe[0]) {
		ssize_t rlen = read(interesting_fd, &upsgi_signal, 1);
		if (rlen < 0) {
			upsgi_error("upsgi_master_manage_events()/read()");
		}
		else if (rlen > 0) {
			upsgi_route_signal(upsgi_signal);
		}
		else {
			// TODO restart workers here
			upsgi_log_verbose("lost connection with workers !!!\n");
			close(interesting_fd);
		}
		return 0;
	}


	return 0;

}
