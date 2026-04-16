#include "upsgi.h"

extern struct upsgi_server upsgi;

// check if all of the workers are dead and exit upsgi
void upsgi_master_check_death() {
	if (upsgi_instance_is_dying) {
		int i;
		for(i=1;i<=upsgi.numproc;i++) {
			if (upsgi.workers[i].pid > 0) {
				return;
			}
		}
		upsgi_log("goodbye to upsgi.\n");
		exit(upsgi.status.dying_for_need_app ? UPSGI_FAILED_APP_CODE : 0);
	}
}

// check if all of the workers are dead, and trigger a reload
int upsgi_master_check_reload(char **argv) {
        if (upsgi_instance_is_reloading) {
                int i;
                for(i=1;i<=upsgi.numproc;i++) {
                        if (upsgi.workers[i].pid > 0) {
                                return 0;
                        }
                }
		upsgi_reload(argv);
		// never here (unless in shared library mode)
		return -1;
        }
	return 0;
}

// check for chain reload
void upsgi_master_check_chain() {
	static time_t last_accepting_check = 0;
	static time_t last_pacing_check = 0;
	static time_t last_chain_curse = 0;

	if (!upsgi.status.chain_reloading) {
		last_chain_curse = 0;
		return;
	}

	// we need to ensure the previous worker (if alive) is accepting new requests
	// before going on
	if (upsgi.status.chain_reloading > 1) {
		struct upsgi_worker *previous_worker = &upsgi.workers[upsgi.status.chain_reloading-1];
		// is the previous worker alive ?
		if (previous_worker->pid > 0 && !previous_worker->cheaped) {
			// the worker has been respawned but it is still not ready
			if (previous_worker->accepting == 0) {
				time_t now = upsgi_now();
				if (now != last_accepting_check) {
					upsgi_log_verbose("chain is still waiting for worker %d...\n", upsgi.status.chain_reloading-1);
					last_accepting_check = now;
				}
				return;
			}
		}
	}

	// if all the processes are recycled, the chain is over
	if (upsgi.status.chain_reloading > upsgi.numproc) {
		upsgi.status.chain_reloading = 0;
		last_chain_curse = 0;
                upsgi_log_verbose("chain reloading complete\n");
		return;
	}

	if (upsgi.chain_reload_delay > 0 && last_chain_curse > 0) {
		time_t now = upsgi_now();
		if (now - last_chain_curse < upsgi.chain_reload_delay) {
			if (now != last_pacing_check) {
				upsgi_log_verbose("chain reload pacing delay active (%d sec between worker turnovers)\n", upsgi.chain_reload_delay);
				last_pacing_check = now;
			}
			return;
		}
	}

	upsgi_block_signal(SIGHUP);
	int i;
	for(i=upsgi.status.chain_reloading;i<=upsgi.numproc;i++) {
		struct upsgi_worker *uw = &upsgi.workers[i];
		if (uw->pid > 0 && !uw->cheaped && uw->accepting) {
			// the worker could have been already cursed
			if (uw->cursed_at == 0) {
				upsgi_log_verbose("chain next victim is worker %d\n", i);
				upsgi_curse(i, SIGHUP);
				last_chain_curse = upsgi_now();
			}
			break;
		}
		else {
			upsgi.status.chain_reloading++;
		}
        }
	upsgi_unblock_signal(SIGHUP);
}


// special function for assuming all of the workers are dead
void upsgi_master_commit_status() {
	int i;
	for(i=1;i<=upsgi.numproc;i++) {
		upsgi.workers[i].pid = 0;
	}
}

void upsgi_master_check_idle() {

	static time_t last_request_timecheck = 0;
	static uint64_t last_request_count = 0;
	int i;
	int waitpid_status;

	if (!upsgi.idle || upsgi.status.is_cheap)
		return;

	upsgi.current_time = upsgi_now();
	if (!last_request_timecheck)
		last_request_timecheck = upsgi.current_time;

	// security check, stop the check if there are busy workers
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
			if (upsgi_worker_is_busy(i)) {
				return;
			}
		}
	}

	if (last_request_count != upsgi.workers[0].requests) {
		last_request_timecheck = upsgi.current_time;
		last_request_count = upsgi.workers[0].requests;
	}
	// a bit of over-engineering to avoid clock skews
	else if (last_request_timecheck < upsgi.current_time && (upsgi.current_time - last_request_timecheck > upsgi.idle)) {
		upsgi_log("workers have been inactive for more than %d seconds (%llu-%llu)\n", upsgi.idle, (unsigned long long) upsgi.current_time, (unsigned long long) last_request_timecheck);
		upsgi.status.is_cheap = 1;
		if (upsgi.die_on_idle) {
			if (upsgi.has_emperor) {
				char byte = 22;
				if (write(upsgi.emperor_fd, &byte, 1) != 1) {
					upsgi_error("write()");
					kill_them_all(0);
				}
			}
			else {
				kill_them_all(0);
			}
			return;
		}
		for (i = 1; i <= upsgi.numproc; i++) {
			upsgi.workers[i].cheaped = 1;
			if (upsgi.workers[i].pid == 0)
				continue;
			// first send SIGINT
			kill(upsgi.workers[i].pid, SIGINT);
			// and start waiting up to 3 seconds
			int j;
			for(j=0;j<3;j++) {
				sleep(1);
				int ret = waitpid(upsgi.workers[i].pid, &waitpid_status, WNOHANG);
				if (ret == 0) continue;
				if (ret == (int) upsgi.workers[i].pid) goto done;
				// on error, directly send SIGKILL
				break;
			}
			kill(upsgi.workers[i].pid, SIGKILL);
			if (waitpid(upsgi.workers[i].pid, &waitpid_status, 0) < 0) {
				if (errno != ECHILD)
					upsgi_error("upsgi_master_check_idle()/waitpid()");
			}
			else {
done:
				upsgi.workers[i].pid = 0;
				upsgi.workers[i].rss_size = 0;
				upsgi.workers[i].vsz_size = 0;
			}
		}
		upsgi_add_sockets_to_queue(upsgi.master_queue, -1);
		upsgi_log("cheap mode enabled: waiting for socket connection...\n");
		last_request_timecheck = 0;
	}

}

int upsgi_master_check_harakiri(int w, int c, time_t harakiri) {
	/**
	 * Triggers a harakiri when the following conditions are met:
	 * - harakiri timeout > current time
	 * - listen queue pressure (ie backlog > harakiri_queue_threshold)
	 *
	 * The first harakiri attempt on a worker will be graceful if harakiri_graceful_timeout > 0,
	 * then the worker has harakiri_graceful_timeout seconds to shutdown cleanly, otherwise
	 * a second harakiri will trigger a SIGKILL
	 *
	 */
#ifdef __linux__
	int backlog = upsgi.shared->backlog;
#else
	int backlog = 0;
#endif
	if (harakiri == 0 || harakiri > (time_t) upsgi.current_time) {
		return 0;
	}
	// no pending harakiri for the worker and no backlog pressure, safe to skip
	if (upsgi.workers[w].pending_harakiri == 0 &&  backlog < upsgi.harakiri_queue_threshold) {
		upsgi_log_verbose("HARAKIRI: Skipping harakiri on worker %d. Listen queue is smaller than the threshold (%d < %d)\n",
			w, backlog, upsgi.harakiri_queue_threshold);
		return 0;
	}

	trigger_harakiri(w);
	if (upsgi.harakiri_graceful_timeout > 0) {
		upsgi.workers[w].cores[c].harakiri = harakiri + upsgi.harakiri_graceful_timeout;
		upsgi_log_verbose("HARAKIRI: graceful termination attempt on worker %d with signal %d. Next harakiri: %d\n",
			w, upsgi.harakiri_graceful_signal, upsgi.workers[w].cores[c].harakiri);
	}
	return 1;
}

int upsgi_master_check_workers_deadline() {
	int i,j;
	int ret = 0;
	for (i = 1; i <= upsgi.numproc; i++) {
		for(j=0;j<upsgi.cores;j++) {
			/* first check for harakiri */
			if (upsgi_master_check_harakiri(i, j, upsgi.workers[i].cores[j].harakiri)) {
				upsgi_log_verbose("HARAKIRI triggered by worker %d core %d !!!\n", i, j);
				ret = 1;
				break;
			}
			/* then user-defined harakiri */
			if (upsgi_master_check_harakiri(i, j, upsgi.workers[i].cores[j].user_harakiri)) {
				upsgi_log_verbose("HARAKIRI (user) triggered by worker %d core %d !!!\n", i, j);
				ret = 1;
				break;
			}
		}
		// then for evil memory checkers
		if (upsgi.evil_reload_on_as) {
			if ((rlim_t) upsgi.workers[i].vsz_size >= upsgi.evil_reload_on_as) {
				upsgi_log("*** EVIL RELOAD ON WORKER %d ADDRESS SPACE: %lld (pid: %d) ***\n", i, (long long) upsgi.workers[i].vsz_size, upsgi.workers[i].pid);
				kill(upsgi.workers[i].pid, SIGKILL);
				upsgi.workers[i].vsz_size = 0;
				ret = 1;
			}
		}
		if (upsgi.evil_reload_on_rss) {
			if ((rlim_t) upsgi.workers[i].rss_size >= upsgi.evil_reload_on_rss) {
				upsgi_log("*** EVIL RELOAD ON WORKER %d RSS: %lld (pid: %d) ***\n", i, (long long) upsgi.workers[i].rss_size, upsgi.workers[i].pid);
				kill(upsgi.workers[i].pid, SIGKILL);
				upsgi.workers[i].rss_size = 0;
				ret = 1;
			}
		}
		// check if worker was running longer than allowed lifetime
		if (upsgi.workers[i].pid > 0 && upsgi.workers[i].cheaped == 0 && upsgi.max_worker_lifetime > 0) {
			uint64_t lifetime = upsgi_now() - upsgi.workers[i].last_spawn;
			if (lifetime > (upsgi.max_worker_lifetime + (i-1) * upsgi.max_worker_lifetime_delta)  && upsgi.workers[i].manage_next_request == 1) {
				upsgi_log("worker %d lifetime reached, it was running for %llu second(s)\n", i, (unsigned long long) lifetime);
				upsgi.workers[i].manage_next_request = 0;
				kill(upsgi.workers[i].pid, SIGWINCH);
				ret = 1;
			}
		}

		// need to find a better way
		//upsgi.workers[i].last_running_time = upsgi.workers[i].running_time;
	}


	return ret;
}


int upsgi_master_check_gateways_deadline() {

	int i;
	int ret = 0;
	for (i = 0; i < ushared->gateways_cnt; i++) {
		if (ushared->gateways_harakiri[i] > 0) {
			if (ushared->gateways_harakiri[i] < (time_t) upsgi.current_time) {
				if (ushared->gateways[i].pid > 0) {
					upsgi_log("*** HARAKIRI ON GATEWAY %s %d (pid: %d) ***\n", ushared->gateways[i].name, ushared->gateways[i].num, ushared->gateways[i].pid);
					kill(ushared->gateways[i].pid, SIGKILL);
					ret = 1;
				}
				ushared->gateways_harakiri[i] = 0;
			}
		}
	}
	return ret;
}

int upsgi_master_check_emperor_death(int diedpid) {
	if (upsgi.emperor_pid >= 0 && diedpid == upsgi.emperor_pid) {
		upsgi_log_verbose("!!! Emperor died !!!\n");
		upsgi_emperor_start();
		return -1;
	}
	return 0;
}

int upsgi_master_check_gateways_death(int diedpid) {
	int i;
	for (i = 0; i < ushared->gateways_cnt; i++) {
		if (ushared->gateways[i].pid == diedpid) {
			gateway_respawn(i);
			return -1;
		}
	}
	return 0;
}

int upsgi_master_check_daemons_death(int diedpid) {
	/* reload the daemons */
	if (upsgi_daemon_check_pid_reload(diedpid)) {
		return -1;
	}
	return 0;
}

int upsgi_worker_is_busy(int wid) {
	int i;
	if (upsgi.workers[wid].sig) return 1;
	for(i=0;i<upsgi.cores;i++) {
		if (upsgi.workers[wid].cores[i].in_request) {
			return 1;
		}
	}
	return 0;
}

int upsgi_master_check_cron_death(int diedpid) {
	struct upsgi_cron *uc = upsgi.crons;
	while (uc) {
		if (uc->pid == (pid_t) diedpid) {
			upsgi_log("[upsgi-cron] command \"%s\" running with pid %d exited after %d second(s)\n", uc->command, uc->pid, upsgi_now() - uc->started_at);
			uc->pid = -1;
			uc->started_at = 0;
			return -1;
		}
		uc = uc->next;
	}
	return 0;
}

int upsgi_master_check_crons_deadline() {
	int ret = 0;
	struct upsgi_cron *uc = upsgi.crons;
	while (uc) {
		if (uc->pid >= 0 && uc->harakiri > 0 && uc->harakiri < (time_t) upsgi.current_time) {
			upsgi_log("*** HARAKIRI ON CRON \"%s\" (pid: %d) ***\n", uc->command, uc->pid);
			kill(-uc->pid, SIGKILL);
			ret = 1;
		}
		uc = uc->next;
	}
	return ret;
}

void upsgi_master_check_mountpoints() {
	struct upsgi_string_list *usl;
	upsgi_foreach(usl, upsgi.mountpoints_check) {
		if (upsgi_check_mountpoint(usl->value)) {
			upsgi_log_verbose("mountpoint %s failed, triggering detonation...\n", usl->value);
			upsgi_nuclear_blast();
			//never here
			exit(1);
		}
	}
}
