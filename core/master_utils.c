#include "upsgi.h"

extern struct upsgi_server upsgi;

void worker_wakeup(int sig) {
}

uint64_t upsgi_worker_exceptions(int wid) {
	uint64_t total = 0;
	int i;
	for(i=0;i<upsgi.cores;i++) {
		total += upsgi.workers[wid].cores[i].exceptions;
	}

	return total;
}

void upsgi_curse(int wid, int sig) {
	upsgi.workers[wid].cursed_at = upsgi_now();
        upsgi.workers[wid].no_mercy_at = upsgi.workers[wid].cursed_at + upsgi.worker_reload_mercy;

	if (sig) {
		(void) kill(upsgi.workers[wid].pid, sig);
	}
}

void upsgi_curse_mule(int mid, int sig) {
	upsgi.mules[mid].cursed_at = upsgi_now();
	upsgi.mules[mid].no_mercy_at = upsgi.mules[mid].cursed_at + upsgi.mule_reload_mercy;

	if (sig) {
		(void) kill(upsgi.mules[mid].pid, sig);
	}
}

static void upsgi_signal_spoolers(int signum) {

        struct upsgi_spooler *uspool = upsgi.spoolers;
        while (uspool) {
                if (uspool->pid > 0) {
                        kill(uspool->pid, signum);
                        upsgi_log("killing the spooler with pid %d\n", uspool->pid);
                }
                uspool = uspool->next;
        }

}
void upsgi_destroy_processes() {

	int i;
	int waitpid_status;

        upsgi_signal_spoolers(SIGKILL);

        upsgi_detach_daemons();

        for (i = 0; i < ushared->gateways_cnt; i++) {
                if (ushared->gateways[i].pid > 0) {
                        kill(ushared->gateways[i].pid, SIGKILL);
			waitpid(ushared->gateways[i].pid, &waitpid_status, 0);
			upsgi_log("gateway \"%s %d\" has been buried (pid: %d)\n", ushared->gateways[i].name, ushared->gateways[i].num, (int) ushared->gateways[i].pid);
		}
        }

	if (upsgi.emperor_pid > 0) {
                kill(upsgi.emperor_pid, SIGINT);
		time_t timeout = upsgi_now() + (upsgi.reload_mercy ? upsgi.reload_mercy : 3);
		// increase timeout for being more tolerant
		timeout+=2;
		int waitpid_status;
		while (upsgi_now() < timeout) {
			pid_t diedpid = waitpid(upsgi.emperor_pid, &waitpid_status, WNOHANG);
			if (diedpid == upsgi.emperor_pid) {
				goto nomoremperor;
			}
			upsgi_log("waiting for Emperor death...\n");
			sleep(1);
		}
		kill(upsgi.emperor_pid, SIGKILL);
		waitpid(upsgi.emperor_pid, &waitpid_status, 0);
nomoremperor:
		upsgi_log("The Emperor has been buried (pid: %d)\n", (int) upsgi.emperor_pid);
	}
}



void upsgi_master_cleanup_hooks(void) {

	int j;

	// could be an inherited atexit hook
	if (upsgi.mypid != upsgi.workers[0].pid)
		return;

	upsgi.status.is_cleaning = 1;

	for (j = 0; j < upsgi.gp_cnt; j++) {
		if (upsgi.gp[j]->master_cleanup) {
			upsgi.gp[j]->master_cleanup();
		}
	}

	for (j = 0; j < 256; j++) {
		if (upsgi.p[j]->master_cleanup) {
			upsgi.p[j]->master_cleanup();
		}
	}

}


int upsgi_calc_cheaper(void) {

	int i;
	static time_t last_check = 0;
	int check_interval = upsgi.master_interval;

	if (!last_check)
		last_check = upsgi_now();

	time_t now = upsgi_now();
	if (!check_interval)
		check_interval = 1;

	if ((now - last_check) < check_interval)
		return 1;

	last_check = now;

	int ignore_algo = 0;
	int needed_workers = 0;

	// first check if memory usage is not exceeded
	if (upsgi.cheaper_rss_limit_soft) {
		unsigned long long total_rss = 0;
		int i;
		int active_workers = 0;
		for(i=1;i<=upsgi.numproc;i++) {
			if (!upsgi.workers[i].cheaped) {
				total_rss += upsgi.workers[i].rss_size;
				active_workers++;
			}
		}
		if (upsgi.cheaper_rss_limit_hard && active_workers > 1 && total_rss >= upsgi.cheaper_rss_limit_hard) {
			upsgi_log("cheaper hard rss memory limit exceeded, cheap one of %d workers\n", active_workers);
			needed_workers = -1;
			ignore_algo = 1;
		}
		else if (total_rss >= upsgi.cheaper_rss_limit_soft) {
#ifdef UPSGI_DEBUG
			upsgi_log("cheaper soft rss memory limit exceeded, can't spawn more workers\n");
#endif
			ignore_algo = 1;
		}
	}

	// then check for fifo
	if (upsgi.cheaper_fifo_delta != 0) {
		if (!ignore_algo) {
			needed_workers = upsgi.cheaper_fifo_delta;
			ignore_algo = 1;
		}
		upsgi.cheaper_fifo_delta = 0;
		goto safe;
	}

	// if cheaper limits wants to change worker count, then skip cheaper algo
	if (!needed_workers) needed_workers = upsgi.cheaper_algo(!ignore_algo);
	// safe check to verify if cheaper algo obeyed ignore_algo value
	if (ignore_algo && needed_workers > 0) {
		upsgi_log("BUG! cheaper algo returned %d but it cannot spawn any worker at this time!\n", needed_workers);
		needed_workers = 0;
	}

safe:
	if (needed_workers > 0) {
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].cheaped == 1 && upsgi.workers[i].pid == 0) {
				if (upsgi_respawn_worker(i)) {
					upsgi.cheaper_fifo_delta += needed_workers;
					return 0;
				}
				needed_workers--;
			}
			if (needed_workers == 0)
				break;
		}
	}
	else if (needed_workers < 0) {
		while (needed_workers < 0) {
			int oldest_worker = 0;
			time_t oldest_worker_spawn = INT_MAX;
			for (i = 1; i <= upsgi.numproc; i++) {
				if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
					if (upsgi_worker_is_busy(i) == 0) {
						if (upsgi.workers[i].last_spawn < oldest_worker_spawn) {
							oldest_worker_spawn = upsgi.workers[i].last_spawn;
							oldest_worker = i;
						}
					}
				}
			}
			if (oldest_worker > 0) {
#ifdef UPSGI_DEBUG
				upsgi_log("worker %d should die...\n", oldest_worker);
#endif
				upsgi.workers[oldest_worker].cheaped = 1;
				upsgi.workers[oldest_worker].rss_size = 0;
				upsgi.workers[oldest_worker].vsz_size = 0;
				upsgi.workers[oldest_worker].manage_next_request = 0;
				upsgi_curse(oldest_worker, SIGWINCH);
			}
			else {
				// Return it to the pool
				upsgi.cheaper_fifo_delta--;
			}
			needed_workers++;
		}
	}

	return 1;
}

// fake algo to allow control with the fifo
int upsgi_cheaper_algo_manual(int can_spawn) {
	return 0;
}

/*

        -- Cheaper, spare algorithm, adapted from old-fashioned spare system --
        
        when all of the workers are busy, the overload_count is incremented.
        as soon as overload_count reaches to upsgi.cheaper_overload (--cheaper-overload options)
        at most cheaper_step (default to 1) new workers are spawned.

        when at least one worker is free, the overload_count is decremented and the idle_count is incremented.
        If overload_count reaches 0, the system will count active workers (the ones uncheaped) and busy workers (the ones running a request)
	if there is exactly 1 free worker we are in "stable state" (1 spare worker available). no worker will be touched.
	if the number of active workers is higher than upsgi.cheaper_count and at least upsgi.cheaper_overload cycles are passed from the last
        "cheap it" procedure, then cheap a worker.

        Example:
            10 processes
            2 cheaper
            2 cheaper step
            3 cheaper_overload 
            1 second master cycle
    
            there are 7 workers running (triggered by some kind of spike activity).
	    Of this, 6 are busy, 1 is free. We are in stable state.
            After a bit the spike disappear and idle_count start to increase.

	    After 3 seconds (upsgi.cheaper_overload cycles) the oldest worker will be cheaped. This will happens
	    every  seconds (upsgi.cheaper_overload cycles) til the number of workers is == upsgi.cheaper_count.

	    If during the "cheap them all" procedure, an overload condition come again (another spike) the "cheap them all"
            will be interrupted.


*/


int upsgi_cheaper_algo_spare(int can_spawn) {

	int i;
	static uint64_t overload_count = 0;
	static uint64_t idle_count = 0;

	// step 1 -> count the number of busy workers
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
			// if a non-busy worker is found, the overload_count is decremented and stop the cycle
			if (upsgi_worker_is_busy(i) == 0) {
				if (overload_count > 0)
					overload_count--;
				goto healthy;
			}
		}
	}

	overload_count++;
	idle_count = 0;

healthy:

	// are we overloaded ?
	if (can_spawn && overload_count >= upsgi.cheaper_overload) {

#ifdef UPSGI_DEBUG
		upsgi_log("overloaded !!!\n");
#endif

		// activate the first available worker (taking step into account)
		int decheaped = 0;
		// search for cheaped workers
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].cheaped == 1 && upsgi.workers[i].pid == 0) {
				decheaped++;
				if (decheaped >= upsgi.cheaper_step)
					break;
			}
		}
		// reset overload
		overload_count = 0;
		// return the maximum number of workers to spawn
		return decheaped;
	}
	// we are no more overloaded
	else if (overload_count == 0) {
		// how many active workers ?
		int active_workers = 0;
		int busy_workers = 0;
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
				active_workers++;
				if (upsgi_worker_is_busy(i) == 1)
					busy_workers++;
			}
		}

#ifdef UPSGI_DEBUG
		upsgi_log("active workers %d busy_workers %d\n", active_workers, busy_workers);
#endif

		// special condition: upsgi.cheaper running workers and 1 free
		if (active_workers > busy_workers && active_workers - busy_workers == 1) {
#ifdef UPSGI_DEBUG
			upsgi_log("stable status: 1 spare worker\n");
#endif
			return 0;
		}

		idle_count++;

		if (active_workers > upsgi.cheaper_count && idle_count % upsgi.cheaper_overload == 0) {
			// we are in "cheap them all"
			return -1;
		}
	}

	return 0;

}

/*
	-- Cheaper, spare2 algorithm, for large workload --

	This algorithm is very similar to spare, but more suited for higher workload.

	This algorithm increase workers *before* overloaded, and decrease workers slowly.

	This algorithm uses these options: cheaper, cheaper-initial, cheaper-step and cheaper-idle.

	* When number of idle workers is smaller than cheaper count, increase
	  min(cheaper-step, cheaper - idle workers) workers.
	* When number of idle workers is larger than cheaper count, increase idle_count.
		* When idle_count >= cheaper-idle, decrease worker.
*/
int upsgi_cheaper_algo_spare2(int can_spawn) {
	static int idle_count = 0;
	int i, idle_workers, busy_workers, cheaped_workers;

	// count the number of idle and busy workers
	idle_workers = busy_workers = 0;
	for (i = 1; i <= upsgi.numproc; i++) {
		if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
			if (upsgi_worker_is_busy(i) == 1) {
				busy_workers++;
			} else {
				idle_workers++;
			}
		}
	}
	cheaped_workers = upsgi.numproc - (idle_workers + busy_workers);

#ifdef UPSGI_DEBUG
	upsgi_log("cheaper-spare2: idle=%d, busy=%d, cheaped=%d, idle_count=%d\n",
		idle_workers, busy_workers, cheaped_workers, idle_count);
#endif

	// should we increase workers?
	if (idle_workers < upsgi.cheaper_count) {
		int spawn;
		idle_count = 0;

		if (!can_spawn)
			return 0;

		spawn = upsgi.cheaper_count - idle_workers;
		if (spawn > cheaped_workers)
			spawn = cheaped_workers;
		if (spawn > upsgi.cheaper_step)
			spawn = upsgi.cheaper_step;
		return spawn;
	}

	if (idle_workers == upsgi.cheaper_count) {
		idle_count = 0;
		return 0;
	}

	// decrease workers
	idle_count++;
	if (idle_count < upsgi.cheaper_idle)
		return 0;

	idle_count = 0;
	return -1;
}


/*

	-- Cheaper,  backlog algorithm (supported only on Linux) --

        increse the number of workers when the listen queue is higher than upsgi.cheaper_overload.
	Decrese when lower.

*/

int upsgi_cheaper_algo_backlog(int can_spawn) {

	int i;
#ifdef __linux__
	int backlog = upsgi.shared->backlog;
#else
	int backlog = 0;
#endif

	if (can_spawn && backlog > (int) upsgi.cheaper_overload) {
		// activate the first available worker (taking step into account)
		int decheaped = 0;
		// search for cheaped workers
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].cheaped == 1 && upsgi.workers[i].pid == 0) {
				decheaped++;
				if (decheaped >= upsgi.cheaper_step)
					break;
			}
		}
		// return the maximum number of workers to spawn
		return decheaped;

	}
	else if (backlog < (int) upsgi.cheaper_overload) {
		int active_workers = 0;
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].cheaped == 0 && upsgi.workers[i].pid > 0) {
				active_workers++;
			}
		}

		if (active_workers > upsgi.cheaper_count) {
			return -1;
		}
	}

	return 0;
}


// reload upsgi, close unneded file descriptor, restore the original environment and re-exec the binary

void upsgi_reload(char **argv) {
	int i;
	int waitpid_status;

	if (upsgi.new_argv) argv = upsgi.new_argv;

	if (!upsgi.master_is_reforked) {

		// call a series of waitpid to ensure all processes (gateways, mules and daemons) are dead
		for (i = 0; i < (ushared->gateways_cnt + upsgi.daemons_cnt + upsgi.mules_cnt); i++) {
			waitpid(WAIT_ANY, &waitpid_status, WNOHANG);
		}

		// call master cleanup hooks
		upsgi_master_cleanup_hooks();

		if (upsgi.exit_on_reload) {
			upsgi_log("upsgi: GAME OVER (insert coin)\n");
			exit(0);
		}

		// call atexit user exec
		upsgi_exec_atexit();

		upsgi_log("binary reloading upsgi...\n");
	}
	else {
		upsgi_log("fork()'ing upsgi...\n");
	}

	// ask for configuration (if needed)
	if (upsgi.has_emperor && upsgi.emperor_fd_config > -1) {
		char byte = 2;
                if (write(upsgi.emperor_fd, &byte, 1) != 1) {
                        upsgi_error("upsgi_reload()/write()");
                }
	}

	upsgi_log("chdir() to %s\n", upsgi.cwd);
	if (chdir(upsgi.cwd)) {
		upsgi_error("upsgi_reload()/chdir()");
	}

	/* check fd table (a module can obviously open some fd on initialization...) */
	upsgi_log("closing all non-upsgi socket fds > 2 (max_fd = %d)...\n", (int) upsgi.max_fd);
	for (i = 3; i < (int) upsgi.max_fd; i++) {
		if (upsgi.close_on_exec2) fcntl(i, F_SETFD, 0);

		if (upsgi_fd_is_safe(i)) continue;

		int found = 0;

		struct upsgi_socket *upsgi_sock = upsgi.sockets;
		while (upsgi_sock) {
			if (i == upsgi_sock->fd) {
				upsgi_log("found fd %d mapped to socket %d (%s)\n", i, upsgi_get_socket_num(upsgi_sock), upsgi_sock->name);
				found = 1;
				break;
			}
			upsgi_sock = upsgi_sock->next;
		}

		upsgi_sock = upsgi.shared_sockets;
		while (upsgi_sock) {
			if (i == upsgi_sock->fd) {
				upsgi_log("found fd %d mapped to shared socket %d (%s)\n", i, upsgi_get_shared_socket_num(upsgi_sock), upsgi_sock->name);
				found = 1;
				break;
			}
			upsgi_sock = upsgi_sock->next;
		}

		if (found) continue;

		if (upsgi.has_emperor) {
			if (i == upsgi.emperor_fd) {
				continue;
			}

			if (i == upsgi.emperor_fd_config) {
				continue;
			}
		}

		if (upsgi.alarm_thread) {
			if (i == upsgi.alarm_thread->queue) continue;
			if (i == upsgi.alarm_thread->pipe[0]) continue;
			if (i == upsgi.alarm_thread->pipe[1]) continue;
		}

		if (upsgi.log_master) {
			if (upsgi.original_log_fd > -1) {
				if (i == upsgi.original_log_fd) {
					continue;
				}
			}

			if (upsgi.shared->worker_log_pipe[0] > -1) {
				if (i == upsgi.shared->worker_log_pipe[0]) {
					continue;
				}
			}

			if (upsgi.shared->worker_log_pipe[1] > -1) {
				if (i == upsgi.shared->worker_log_pipe[1]) {
					continue;
				}
			}

		}

#ifdef __APPLE__
		fcntl(i, F_SETFD, FD_CLOEXEC);
#else
		close(i);
#endif
	}

#ifndef UPSGI_IPCSEM_ATEXIT
	// free ipc semaphores if in use
	if (upsgi.lock_engine && !strcmp(upsgi.lock_engine, "ipcsem")) {
		upsgi_ipcsem_clear();
	}
#endif

	upsgi_log("running %s\n", upsgi.binary_path);
	upsgi_flush_logs();
	argv[0] = upsgi.binary_path;
	//strcpy (argv[0], upsgi.binary_path);
	if (upsgi.log_master) {
		if (upsgi.original_log_fd > -1) {
			dup2(upsgi.original_log_fd, 1);
			dup2(1, 2);
		}
		if (upsgi.shared->worker_log_pipe[0] > -1) {
			close(upsgi.shared->worker_log_pipe[0]);
		}
		if (upsgi.shared->worker_log_pipe[1] > -1) {
			close(upsgi.shared->worker_log_pipe[1]);
		}
	}
	execvp(upsgi.binary_path, argv);
	upsgi_error("execvp()");
	// never here
	exit(1);

}

void upsgi_fixup_fds(int wid, int muleid, struct upsgi_gateway *ug) {

	int i;

	if (upsgi.master_process) {
		if (upsgi.master_queue > -1)
			close(upsgi.master_queue);
		// close gateways
		if (!ug) {
			for (i = 0; i < ushared->gateways_cnt; i++) {
				close(ushared->gateways[i].internal_subscription_pipe[0]);
				close(ushared->gateways[i].internal_subscription_pipe[1]);
			}
		}
		struct upsgi_gateway_socket *ugs = upsgi.gateway_sockets;
		while (ugs) {
			if (ug && !strcmp(ug->name, ugs->owner)) {
				ugs = ugs->next;
				continue;
			}
			// do not close shared sockets !!!
			if (!ugs->shared) {
				close(ugs->fd);
			}
			ugs = ugs->next;
		}
		// fix the communication pipe
		close(upsgi.shared->worker_signal_pipe[0]);
		for (i = 1; i <= upsgi.numproc; i++) {
			if (upsgi.workers[i].signal_pipe[0] != -1)
				close(upsgi.workers[i].signal_pipe[0]);
			if (i != wid) {
				if (upsgi.workers[i].signal_pipe[1] != -1)
					close(upsgi.workers[i].signal_pipe[1]);
			}
		}

		
		if (upsgi.shared->spooler_signal_pipe[0] != -1)
                	close(upsgi.shared->spooler_signal_pipe[0]);
		if (upsgi.i_am_a_spooler && upsgi.i_am_a_spooler->pid != getpid()) {
			if (upsgi.shared->spooler_signal_pipe[1] != -1)
				close(upsgi.shared->spooler_signal_pipe[1]);
		}

		if (upsgi.shared->mule_signal_pipe[0] != -1)
			close(upsgi.shared->mule_signal_pipe[0]);

		if (muleid == 0) {
			if (upsgi.shared->mule_signal_pipe[1] != -1)
				close(upsgi.shared->mule_signal_pipe[1]);
			if (upsgi.shared->mule_queue_pipe[1] != -1)
				close(upsgi.shared->mule_queue_pipe[1]);
		}

		for (i = 0; i < upsgi.mules_cnt; i++) {
			if (upsgi.mules[i].signal_pipe[0] != -1)
				close(upsgi.mules[i].signal_pipe[0]);
			if (muleid != i + 1) {
				if (upsgi.mules[i].signal_pipe[1] != -1)
					close(upsgi.mules[i].signal_pipe[1]);
				if (upsgi.mules[i].queue_pipe[1] != -1)
					close(upsgi.mules[i].queue_pipe[1]);
			}
		}

		for (i = 0; i < upsgi.farms_cnt; i++) {
			if (upsgi.farms[i].signal_pipe[0] != -1)
				close(upsgi.farms[i].signal_pipe[0]);

			if (muleid == 0) {
				if (upsgi.farms[i].signal_pipe[1] != -1)
					close(upsgi.farms[i].signal_pipe[1]);
				if (upsgi.farms[i].queue_pipe[1] != -1)
					close(upsgi.farms[i].queue_pipe[1]);
			}
		}

		if (upsgi.master_fifo_fd > -1) close(upsgi.master_fifo_fd);

		if (upsgi.notify_socket_fd > -1) close(upsgi.notify_socket_fd);

#ifdef __linux__
		for(i=0;i<upsgi.setns_fds_count;i++) {
			close(upsgi.setns_fds[i]);
		}
#endif

		// fd alarms
		struct upsgi_alarm_fd *uafd = upsgi.alarm_fds;
        	while(uafd) {
			close(uafd->fd);
                	uafd = uafd->next;
        	}
	}


}

int upsgi_respawn_worker(int wid) {
	int i;
	int respawns = upsgi.workers[wid].respawn_count;
	// the workers is not accepting (obviously)
	upsgi.workers[wid].accepting = 0;
	// we count the respawns before errors...
	upsgi.workers[wid].respawn_count++;
	// ... same for update time
	upsgi.workers[wid].last_spawn = upsgi.current_time;
	// ... and memory/harakiri
	for(i=0;i<upsgi.cores;i++) {
		upsgi.workers[wid].cores[i].harakiri = 0;
		upsgi.workers[wid].cores[i].user_harakiri = 0;
	}
	upsgi.workers[wid].pending_harakiri = 0;
	upsgi.workers[wid].rss_size = 0;
	upsgi.workers[wid].vsz_size = 0;
	upsgi.workers[wid].uss_size = 0;
	upsgi.workers[wid].pss_size = 0;
	// ... reset stopped_at
	upsgi.workers[wid].cursed_at = 0;
	upsgi.workers[wid].no_mercy_at = 0;

	// internal statuses should be reset too

	upsgi.workers[wid].cheaped = 0;
	// SUSPENSION is managed by the user, not the master...
	//upsgi.workers[wid].suspended = 0;
	upsgi.workers[wid].sig = 0;

	// this is required for various checks
	upsgi.workers[wid].delta_requests = 0;

	if (upsgi.threaded_logger) {
		pthread_mutex_lock(&upsgi.threaded_logger_lock);
	}


	for (i = 0; i < 256; i++) {
		if (upsgi.p[i]->pre_upsgi_fork) {
			upsgi.p[i]->pre_upsgi_fork();
		}
	}

	pid_t pid = upsgi_fork(upsgi.workers[wid].name);

	if (pid == 0) {
		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->post_upsgi_fork) {
				upsgi.p[i]->post_upsgi_fork(1);
			}
		}

		signal(SIGWINCH, worker_wakeup);
		signal(SIGTSTP, worker_wakeup);
		upsgi.mywid = wid;
		upsgi.mypid = getpid();
		// pid is updated by the master
		//upsgi.workers[upsgi.mywid].pid = upsgi.mypid;
		// OVERENGINEERING (just to be safe)
		upsgi.workers[upsgi.mywid].id = upsgi.mywid;
		/*
		   upsgi.workers[upsgi.mywid].harakiri = 0;
		   upsgi.workers[upsgi.mywid].user_harakiri = 0;
		   upsgi.workers[upsgi.mywid].rss_size = 0;
		   upsgi.workers[upsgi.mywid].vsz_size = 0;
		 */
		// do not reset worker counters on reload !!!
		//upsgi.workers[upsgi.mywid].requests = 0;
		// ...but maintain a delta counter (yes this is racy in multithread)
		//upsgi.workers[upsgi.mywid].delta_requests = 0;
		//upsgi.workers[upsgi.mywid].failed_requests = 0;
		//upsgi.workers[upsgi.mywid].respawn_count++;
		//upsgi.workers[upsgi.mywid].last_spawn = upsgi.current_time;
		upsgi.workers[upsgi.mywid].manage_next_request = 1;
		/*
		   upsgi.workers[upsgi.mywid].cheaped = 0;
		   upsgi.workers[upsgi.mywid].suspended = 0;
		   upsgi.workers[upsgi.mywid].sig = 0;
		 */

		// reset the apps count with a copy from the master 
		upsgi.workers[upsgi.mywid].apps_cnt = upsgi.workers[0].apps_cnt;

		// reset wsgi_request structures
		for(i=0;i<upsgi.cores;i++) {
			upsgi.workers[upsgi.mywid].cores[i].in_request = 0;
			memset(&upsgi.workers[upsgi.mywid].cores[i].req, 0, sizeof(struct wsgi_request));
			memset(upsgi.workers[upsgi.mywid].cores[i].buffer, 0, sizeof(struct upsgi_header));
		}

		upsgi_fixup_fds(wid, 0, NULL);

		upsgi.my_signal_socket = upsgi.workers[wid].signal_pipe[1];

		if (upsgi.master_process) {
			if ((upsgi.workers[upsgi.mywid].respawn_count || upsgi.status.is_cheap)) {
				for (i = 0; i < 256; i++) {
					if (upsgi.p[i]->master_fixup) {
						upsgi.p[i]->master_fixup(1);
					}
				}
			}
		}

		if (upsgi.threaded_logger) {
			pthread_mutex_unlock(&upsgi.threaded_logger_lock);
		}

		return 1;
	}
	else if (pid < 1) {
		upsgi_error("fork()");
	}
	else {
		for (i = 0; i < 256; i++) {
			if (upsgi.p[i]->post_upsgi_fork) {
				upsgi.p[i]->post_upsgi_fork(0);
			}
		}

		// the pid is set only in the master, as the worker should never use it
		upsgi.workers[wid].pid = pid;

		if (respawns > 0) {
			upsgi_log("Respawned upsgi worker %d (new pid: %d)\n", wid, (int) pid);
		}
		else {
			upsgi_log("spawned upsgi worker %d (pid: %d, cores: %d)\n", wid, pid, upsgi.cores);
		}
	}

	if (upsgi.threaded_logger) {
		pthread_mutex_unlock(&upsgi.threaded_logger_lock);
	}


	return 0;
}

struct upsgi_stats *upsgi_master_generate_stats() {

	int i;

	struct upsgi_stats *us = upsgi_stats_new(8192);

	if (upsgi_stats_keyval_comma(us, "version", UPSGI_VERSION))
		goto end;

#ifdef __linux__
	if (upsgi_stats_keylong_comma(us, "listen_queue", (unsigned long long) upsgi.shared->backlog))
		goto end;
	if (upsgi_stats_keylong_comma(us, "listen_queue_errors", (unsigned long long) upsgi.shared->backlog_errors))
		goto end;
#endif

	int signal_queue = 0;
	if (ioctl(upsgi.shared->worker_signal_pipe[1], FIONREAD, &signal_queue)) {
		upsgi_error("upsgi_master_generate_stats() -> ioctl()\n");
	}

	if (upsgi_stats_keylong_comma(us, "signal_queue", (unsigned long long) signal_queue))
		goto end;

	if (upsgi_stats_keylong_comma(us, "log_records", (unsigned long long) upsgi.shared->log_records))
		goto end;
	if (upsgi_stats_keylong_comma(us, "req_log_records", (unsigned long long) upsgi.shared->req_log_records))
		goto end;
	if (upsgi_stats_keylong_comma(us, "log_backpressure_events", (unsigned long long) upsgi.shared->log_backpressure_events))
		goto end;
	if (upsgi_stats_keylong_comma(us, "req_log_backpressure_events", (unsigned long long) upsgi.shared->req_log_backpressure_events))
		goto end;
	if (upsgi_stats_keylong_comma(us, "log_sink_stall_events", (unsigned long long) upsgi.shared->log_sink_stall_events))
		goto end;
	if (upsgi_stats_keylong_comma(us, "req_log_sink_stall_events", (unsigned long long) upsgi.shared->req_log_sink_stall_events))
		goto end;
	if (upsgi_stats_keylong_comma(us, "log_dropped_messages", (unsigned long long) upsgi.shared->log_dropped_messages))
		goto end;
	if (upsgi_stats_keylong_comma(us, "req_log_dropped_messages", (unsigned long long) upsgi.shared->req_log_dropped_messages))
		goto end;
	if (upsgi_stats_keylong_comma(us, "load", (unsigned long long) upsgi.shared->load))
		goto end;
	if (upsgi_stats_keylong_comma(us, "pid", (unsigned long long) getpid()))
		goto end;
	if (upsgi_stats_keylong_comma(us, "uid", (unsigned long long) getuid()))
		goto end;
	if (upsgi_stats_keylong_comma(us, "gid", (unsigned long long) getgid()))
		goto end;

	char *cwd = upsgi_get_cwd();
	if (upsgi_stats_keyval_comma(us, "cwd", cwd)) {
		free(cwd);
		goto end;
	}
	free(cwd);

	if (upsgi.daemons) {
		if (upsgi_stats_key(us, "daemons"))
			goto end;
		if (upsgi_stats_list_open(us))
			goto end;

		struct upsgi_daemon *ud = upsgi.daemons;
		while (ud) {
			if (upsgi_stats_object_open(us))
				goto end;

			// allocate 2x the size of original command
			// in case we need to escape all chars
			char *cmd = upsgi_malloc((strlen(ud->command)*2)+1);
			escape_json(ud->command, strlen(ud->command), cmd);
			if (upsgi_stats_keyval_comma(us, "cmd", cmd)) {
				free(cmd);
				goto end;
			}
			free(cmd);

			if (upsgi_stats_keylong_comma(us, "pid", (unsigned long long) (ud->pid < 0) ? 0 : ud->pid))
				goto end;
			if (upsgi_stats_keylong(us, "respawns", (unsigned long long) ud->respawns ? 0 : ud->respawns))
				goto end;
			if (upsgi_stats_object_close(us))
				goto end;
			if (ud->next) {
				if (upsgi_stats_comma(us))
					goto end;
			}
			ud = ud->next;
		}
		if (upsgi_stats_list_close(us))
			goto end;
		if (upsgi_stats_comma(us))
			goto end;
	}

	if (upsgi_stats_key(us, "locks"))
		goto end;
	if (upsgi_stats_list_open(us))
		goto end;

	struct upsgi_lock_item *uli = upsgi.registered_locks;
	while (uli) {
		if (upsgi_stats_object_open(us))
			goto end;
		if (upsgi_stats_keylong(us, uli->id, (unsigned long long) uli->pid))
			goto end;
		if (upsgi_stats_object_close(us))
			goto end;
		if (uli->next) {
			if (upsgi_stats_comma(us))
				goto end;
		}
		uli = uli->next;
	}

	if (upsgi_stats_list_close(us))
		goto end;
	if (upsgi_stats_comma(us))
		goto end;

	if (upsgi.caches) {

		
		if (upsgi_stats_key(us, "caches"))
                goto end;

		if (upsgi_stats_list_open(us)) goto end;

		struct upsgi_cache *uc = upsgi.caches;
		while(uc) {
			if (upsgi_stats_object_open(us))
                        	goto end;

			if (upsgi_stats_keyval_comma(us, "name", uc->name ? uc->name : "default"))
                        	goto end;

			if (upsgi_stats_keyval_comma(us, "hash", uc->hash->name))
                        	goto end;

			if (upsgi_stats_keylong_comma(us, "hashsize", (unsigned long long) uc->hashsize))
				goto end;

			if (upsgi_stats_keylong_comma(us, "keysize", (unsigned long long) uc->keysize))
				goto end;

			if (upsgi_stats_keylong_comma(us, "max_items", (unsigned long long) uc->max_items))
				goto end;

			if (upsgi_stats_keylong_comma(us, "blocks", (unsigned long long) uc->blocks))
				goto end;

			if (upsgi_stats_keylong_comma(us, "blocksize", (unsigned long long) uc->blocksize))
				goto end;

			if (upsgi_stats_keylong_comma(us, "items", (unsigned long long) uc->n_items))
				goto end;

			if (upsgi_stats_keylong_comma(us, "hits", (unsigned long long) uc->hits))
				goto end;

			if (upsgi_stats_keylong_comma(us, "miss", (unsigned long long) uc->miss))
				goto end;

			if (upsgi_stats_keylong_comma(us, "full", (unsigned long long) uc->full))
				goto end;

			if (upsgi_stats_keylong(us, "last_modified_at", (unsigned long long) uc->last_modified_at))
				goto end;

			if (upsgi_stats_object_close(us))
				goto end;

			if (uc->next) {
				if (upsgi_stats_comma(us))
					goto end;
			}
			uc = uc->next;
		}

		if (upsgi_stats_list_close(us))
		goto end;

		if (upsgi_stats_comma(us))
		goto end;
	}

	if (upsgi.has_metrics && !upsgi.stats_no_metrics) {
		if (upsgi_stats_key(us, "metrics"))
                	goto end;

		if (upsgi_stats_object_open(us))
			goto end;

		upsgi_rlock(upsgi.metrics_lock);
		struct upsgi_metric *um = upsgi.metrics;
		while(um) {
        		int64_t um_val = *um->value;

			if (upsgi_stats_key(us, um->name)) {
				upsgi_rwunlock(upsgi.metrics_lock);
                		goto end;
			}

			if (upsgi_stats_object_open(us)) {
				upsgi_rwunlock(upsgi.metrics_lock);
                                goto end;
			}

			if (upsgi_stats_keylong(us, "type", (long long) um->type)) {
        			upsgi_rwunlock(upsgi.metrics_lock);
				goto end;
			} 

			if (upsgi_stats_comma(us)) {
        			upsgi_rwunlock(upsgi.metrics_lock);
				goto end;
			}

			if (upsgi_stats_keyval_comma(us, "oid", um->oid ? um->oid : "")) {
                                upsgi_rwunlock(upsgi.metrics_lock);
                                goto end;
                        }

			if (upsgi_stats_keyslong(us, "value", (long long) um_val)) {
        			upsgi_rwunlock(upsgi.metrics_lock);
				goto end;
			} 

			if (upsgi_stats_object_close(us)) {
                                upsgi_rwunlock(upsgi.metrics_lock);
                                goto end;
                        }

			um = um->next;
			if (um) {
				if (upsgi_stats_comma(us)) {
        				upsgi_rwunlock(upsgi.metrics_lock);
					goto end;
				}
			}
		}
        	upsgi_rwunlock(upsgi.metrics_lock);

		if (upsgi_stats_object_close(us))
			goto end;

		if (upsgi_stats_comma(us))
		goto end;
	}

	if (upsgi_stats_key(us, "sockets"))
		goto end;

	if (upsgi_stats_list_open(us))
		goto end;

	struct upsgi_socket *upsgi_sock = upsgi.sockets;
	while (upsgi_sock) {
		if (upsgi_stats_object_open(us))
			goto end;

		if (upsgi_stats_keyval_comma(us, "name", upsgi_sock->name))
			goto end;

		if (upsgi_stats_keyval_comma(us, "proto", upsgi_sock->proto_name ? upsgi_sock->proto_name : "upsgi"))
			goto end;

		if (upsgi_stats_keylong_comma(us, "queue", (unsigned long long) upsgi_sock->queue))
			goto end;

		if (upsgi_stats_keylong_comma(us, "max_queue", (unsigned long long) upsgi_sock->max_queue))
			goto end;

		if (upsgi_stats_keylong_comma(us, "shared", (unsigned long long) upsgi_sock->shared))
			goto end;

		if (upsgi_stats_keylong(us, "can_offload", (unsigned long long) upsgi_sock->can_offload))
			goto end;

		if (upsgi_stats_object_close(us))
			goto end;

		upsgi_sock = upsgi_sock->next;
		if (upsgi_sock) {
			if (upsgi_stats_comma(us))
				goto end;
		}
	}

	if (upsgi_stats_list_close(us))
		goto end;

	if (upsgi_stats_comma(us))
		goto end;

	if (upsgi_stats_key(us, "workers"))
		goto end;
	if (upsgi_stats_list_open(us))
		goto end;

	for (i = 0; i < upsgi.numproc; i++) {
		if (upsgi_stats_object_open(us))
			goto end;

		if (upsgi_stats_keylong_comma(us, "id", (unsigned long long) upsgi.workers[i + 1].id))
			goto end;
		if (upsgi_stats_keylong_comma(us, "pid", (unsigned long long) upsgi.workers[i + 1].pid))
			goto end;
		if (upsgi_stats_keylong_comma(us, "accepting", (unsigned long long) upsgi.workers[i + 1].accepting))
			goto end;
		if (upsgi_stats_keylong_comma(us, "requests", (unsigned long long) upsgi.workers[i + 1].requests))
			goto end;
		if (upsgi_stats_keylong_comma(us, "delta_requests", (unsigned long long) upsgi.workers[i + 1].delta_requests))
			goto end;
		if (upsgi_stats_keylong_comma(us, "exceptions", (unsigned long long) upsgi_worker_exceptions(i + 1)))
			goto end;
		if (upsgi_stats_keylong_comma(us, "harakiri_count", (unsigned long long) upsgi.workers[i + 1].harakiri_count))
			goto end;
		if (upsgi_stats_keylong_comma(us, "signals", (unsigned long long) upsgi.workers[i + 1].signals))
			goto end;

		if (ioctl(upsgi.workers[i + 1].signal_pipe[1], FIONREAD, &signal_queue)) {
			upsgi_error("upsgi_master_generate_stats() -> ioctl()\n");
		}

		if (upsgi_stats_keylong_comma(us, "signal_queue", (unsigned long long) signal_queue))
			goto end;

		if (upsgi.workers[i + 1].cheaped) {
			if (upsgi_stats_keyval_comma(us, "status", "cheap"))
				goto end;
		}
		else if (upsgi.workers[i + 1].suspended && !upsgi_worker_is_busy(i+1)) {
			if (upsgi_stats_keyval_comma(us, "status", "pause"))
				goto end;
		}
		else {
			if (upsgi.workers[i + 1].sig) {
				if (upsgi_stats_keyvalnum_comma(us, "status", "sig", (unsigned long long) upsgi.workers[i + 1].signum))
					goto end;
			}
			else if (upsgi_worker_is_busy(i+1)) {
				if (upsgi_stats_keyval_comma(us, "status", "busy"))
					goto end;
			}
			else {
				if (upsgi_stats_keyval_comma(us, "status", "idle"))
					goto end;
			}
		}

		if (upsgi_stats_keylong_comma(us, "rss", (unsigned long long) upsgi.workers[i + 1].rss_size))
			goto end;
		if (upsgi_stats_keylong_comma(us, "vsz", (unsigned long long) upsgi.workers[i + 1].vsz_size))
			goto end;
		if (upsgi_stats_keylong_comma(us, "uss", (unsigned long long) upsgi.workers[i + 1].uss_size))
			goto end;
		if (upsgi_stats_keylong_comma(us, "pss", (unsigned long long) upsgi.workers[i + 1].pss_size))
			goto end;

		if (upsgi_stats_keylong_comma(us, "running_time", (unsigned long long) upsgi.workers[i + 1].running_time))
			goto end;
		if (upsgi_stats_keylong_comma(us, "last_spawn", (unsigned long long) upsgi.workers[i + 1].last_spawn))
			goto end;
		if (upsgi_stats_keylong_comma(us, "respawn_count", (unsigned long long) upsgi.workers[i + 1].respawn_count))
			goto end;

		if (upsgi_stats_keylong_comma(us, "tx", (unsigned long long) upsgi.workers[i + 1].tx))
			goto end;
		if (upsgi_stats_keylong_comma(us, "avg_rt", (unsigned long long) upsgi.workers[i + 1].avg_response_time))
			goto end;

		// applications list
		if (upsgi_stats_key(us, "apps"))
			goto end;
		if (upsgi_stats_list_open(us))
			goto end;

		int j;

		for (j = 0; j < upsgi.workers[i + 1].apps_cnt; j++) {
			struct upsgi_app *ua = &upsgi.workers[i + 1].apps[j];

			if (upsgi_stats_object_open(us))
				goto end;
			if (upsgi_stats_keylong_comma(us, "id", (unsigned long long) j))
				goto end;
			if (upsgi_stats_keylong_comma(us, "modifier1", (unsigned long long) ua->modifier1))
				goto end;

			if (upsgi_stats_keyvaln_comma(us, "mountpoint", ua->mountpoint, ua->mountpoint_len))
				goto end;
			if (upsgi_stats_keylong_comma(us, "startup_time", ua->startup_time))
				goto end;

			if (upsgi_stats_keylong_comma(us, "requests", ua->requests))
				goto end;
			if (upsgi_stats_keylong_comma(us, "exceptions", ua->exceptions))
				goto end;

			if (*ua->chdir) {
				if (upsgi_stats_keyval(us, "chdir", ua->chdir))
					goto end;
			}
			else {
				if (upsgi_stats_keyval(us, "chdir", ""))
					goto end;
			}

			if (upsgi_stats_object_close(us))
				goto end;

			if (j < upsgi.workers[i + 1].apps_cnt - 1) {
				if (upsgi_stats_comma(us))
					goto end;
			}
		}


		if (upsgi_stats_list_close(us))
			goto end;

		if (upsgi.stats_no_cores) goto nocores;

		if (upsgi_stats_comma(us))
			goto end;

		// cores list
		if (upsgi_stats_key(us, "cores"))
			goto end;
		if (upsgi_stats_list_open(us))
			goto end;

		for (j = 0; j < upsgi.cores; j++) {
			struct upsgi_core *uc = &upsgi.workers[i + 1].cores[j];
			if (upsgi_stats_object_open(us))
				goto end;
			if (upsgi_stats_keylong_comma(us, "id", (unsigned long long) j))
				goto end;

			if (upsgi_stats_keylong_comma(us, "requests", (unsigned long long) uc->requests))
				goto end;

			if (upsgi_stats_keylong_comma(us, "static_requests", (unsigned long long) uc->static_requests))
				goto end;

			if (upsgi_stats_keylong_comma(us, "routed_requests", (unsigned long long) uc->routed_requests))
				goto end;

			if (upsgi_stats_keylong_comma(us, "offloaded_requests", (unsigned long long) uc->offloaded_requests))
				goto end;

			if (upsgi_stats_keylong_comma(us, "write_errors", (unsigned long long) uc->write_errors))
				goto end;

			if (upsgi_stats_keylong_comma(us, "read_errors", (unsigned long long) uc->read_errors))
				goto end;

			if (upsgi_stats_keylong_comma(us, "in_request", (unsigned long long) uc->in_request))
				goto end;

			if (upsgi_stats_key(us, "vars"))
				goto end;

			if (upsgi_stats_list_open(us))
                        	goto end;

			if (upsgi_stats_dump_vars(us, uc)) goto end;

			if (upsgi_stats_list_close(us))
				goto end;

			if (upsgi_stats_comma(us)) goto end;

			if (upsgi_stats_key(us, "req_info"))
				goto end;

			if (upsgi_stats_object_open(us))
				goto end;

			if (upsgi_stats_dump_request(us, uc)) goto end;

			if (upsgi_stats_object_close(us))
				goto end;

			if (upsgi_stats_object_close(us))
				goto end;

			if (j < upsgi.cores - 1) {
				if (upsgi_stats_comma(us))
					goto end;
			}
		}

		if (upsgi_stats_list_close(us))
			goto end;

nocores:

		if (upsgi_stats_object_close(us))
			goto end;


		if (i < upsgi.numproc - 1) {
			if (upsgi_stats_comma(us))
				goto end;
		}
	}

	if (upsgi_stats_list_close(us))
		goto end;

	struct upsgi_spooler *uspool = upsgi.spoolers;
	if (uspool) {
		if (upsgi_stats_comma(us))
			goto end;
		if (upsgi_stats_key(us, "spoolers"))
			goto end;
		if (upsgi_stats_list_open(us))
			goto end;
		while (uspool) {
			if (upsgi_stats_object_open(us))
				goto end;

			if (upsgi_stats_keyval_comma(us, "dir", uspool->dir))
				goto end;

			if (upsgi_stats_keylong_comma(us, "pid", (unsigned long long) uspool->pid))
				goto end;

			if (upsgi_stats_keylong_comma(us, "tasks", (unsigned long long) uspool->tasks))
				goto end;

			if (upsgi_stats_keylong_comma(us, "respawns", (unsigned long long) uspool->respawned))
				goto end;

			if (upsgi_stats_keylong(us, "running", (unsigned long long) uspool->running))
				goto end;

			if (upsgi_stats_object_close(us))
				goto end;
			uspool = uspool->next;
			if (uspool) {
				if (upsgi_stats_comma(us))
					goto end;
			}
		}
		if (upsgi_stats_list_close(us))
			goto end;
	}

	struct upsgi_cron *ucron = upsgi.crons;
	if (ucron) {
		if (upsgi_stats_comma(us))
			goto end;
		if (upsgi_stats_key(us, "crons"))
			goto end;
		if (upsgi_stats_list_open(us))
			goto end;
		while (ucron) {
			if (upsgi_stats_object_open(us))
				goto end;

			if (upsgi_stats_keyslong_comma(us, "minute", (long long) ucron->minute))
				goto end;

			if (upsgi_stats_keyslong_comma(us, "hour", (long long) ucron->hour))
				goto end;

			if (upsgi_stats_keyslong_comma(us, "day", (long long) ucron->day))
				goto end;

			if (upsgi_stats_keyslong_comma(us, "month", (long long) ucron->month))
				goto end;

			if (upsgi_stats_keyslong_comma(us, "week", (long long) ucron->week))
				goto end;

			char *cmd = upsgi_malloc((strlen(ucron->command)*2)+1);
			escape_json(ucron->command, strlen(ucron->command), cmd);
			if (upsgi_stats_keyval_comma(us, "command", cmd)) {
				free(cmd);
				goto end;
			}
			free(cmd);

			if (upsgi_stats_keylong_comma(us, "unique", (unsigned long long) ucron->unique))
				goto end;

#ifdef UPSGI_SSL
			if (upsgi_stats_keyval_comma(us, "legion", ucron->legion ? ucron->legion : ""))
				goto end;
#endif

			if (upsgi_stats_keyslong_comma(us, "pid", (long long) ucron->pid))
				goto end;

			if (upsgi_stats_keylong(us, "started_at", (unsigned long long) ucron->started_at))
				goto end;

			if (upsgi_stats_object_close(us))
				goto end;

			ucron = ucron->next;
			if (ucron) {
				if (upsgi_stats_comma(us))
					goto end;
			}
		}
		if (upsgi_stats_list_close(us))
			goto end;
	}

#ifdef UPSGI_SSL
	struct upsgi_legion *legion = NULL;
	if (upsgi.legions) {

		if (upsgi_stats_comma(us))
			goto end;

		if (upsgi_stats_key(us, "legions"))
			goto end;

		if (upsgi_stats_list_open(us))
			goto end;

		legion = upsgi.legions;
		while (legion) {
			if (upsgi_stats_object_open(us))
				goto end;

			if (upsgi_stats_keyval_comma(us, "legion", legion->legion))
				goto end;

			if (upsgi_stats_keyval_comma(us, "addr", legion->addr))
				goto end;

			if (upsgi_stats_keyval_comma(us, "uuid", legion->uuid))
				goto end;

			if (upsgi_stats_keylong_comma(us, "valor", (unsigned long long) legion->valor))
				goto end;

			if (upsgi_stats_keylong_comma(us, "checksum", (unsigned long long) legion->checksum))
				goto end;

			if (upsgi_stats_keylong_comma(us, "quorum", (unsigned long long) legion->quorum))
				goto end;

			if (upsgi_stats_keylong_comma(us, "i_am_the_lord", (unsigned long long) legion->i_am_the_lord))
				goto end;

			if (upsgi_stats_keylong_comma(us, "lord_valor", (unsigned long long) legion->lord_valor))
				goto end;

			if (upsgi_stats_keyvaln_comma(us, "lord_uuid", legion->lord_uuid, 36))
				goto end;

			// legion nodes start
			if (upsgi_stats_key(us, "nodes"))
                                goto end;

                        if (upsgi_stats_list_open(us))
                                goto end;

                        struct upsgi_string_list *nodes = legion->nodes;
                        while (nodes) {

				if (upsgi_stats_str(us, nodes->value))
                                	goto end;

                                nodes = nodes->next;
                                if (nodes) {
                                        if (upsgi_stats_comma(us))
                                                goto end;
                                }
                        }

			if (upsgi_stats_list_close(us))
				goto end;

                        if (upsgi_stats_comma(us))
                        	goto end;


			// legion members start
			if (upsgi_stats_key(us, "members"))
				goto end;

			if (upsgi_stats_list_open(us))
				goto end;

			upsgi_rlock(legion->lock);
			struct upsgi_legion_node *node = legion->nodes_head;
			while (node) {
				if (upsgi_stats_object_open(us))
					goto unlock_legion_mutex;

				if (upsgi_stats_keyvaln_comma(us, "name", node->name, node->name_len))
					goto unlock_legion_mutex;

				if (upsgi_stats_keyval_comma(us, "uuid", node->uuid))
					goto unlock_legion_mutex;

				if (upsgi_stats_keylong_comma(us, "valor", (unsigned long long) node->valor))
					goto unlock_legion_mutex;

				if (upsgi_stats_keylong_comma(us, "checksum", (unsigned long long) node->checksum))
					goto unlock_legion_mutex;

				if (upsgi_stats_keylong(us, "last_seen", (unsigned long long) node->last_seen))
					goto unlock_legion_mutex;

				if (upsgi_stats_object_close(us))
					goto unlock_legion_mutex;

				node = node->next;
				if (node) {
					if (upsgi_stats_comma(us))
						goto unlock_legion_mutex;
				}
			}
			upsgi_rwunlock(legion->lock);

			if (upsgi_stats_list_close(us))
				goto end;
			// legion nodes end

			if (upsgi_stats_object_close(us))
				goto end;

			legion = legion->next;
			if (legion) {
				if (upsgi_stats_comma(us))
					goto end;
			}
		}

		if (upsgi_stats_list_close(us))
			goto end;

	}
#endif

	if (upsgi_stats_object_close(us))
		goto end;

	return us;
#ifdef UPSGI_SSL
unlock_legion_mutex:
	if (legion)
		upsgi_rwunlock(legion->lock);
#endif
end:
	free(us->base);
	free(us);
	return NULL;
}

void upsgi_register_cheaper_algo(char *name, int (*func) (int)) {

	struct upsgi_cheaper_algo *uca = upsgi.cheaper_algos;

	if (!uca) {
		upsgi.cheaper_algos = upsgi_malloc(sizeof(struct upsgi_cheaper_algo));
		uca = upsgi.cheaper_algos;
	}
	else {
		while (uca) {
			if (!uca->next) {
				uca->next = upsgi_malloc(sizeof(struct upsgi_cheaper_algo));
				uca = uca->next;
				break;
			}
			uca = uca->next;
		}

	}

	uca->name = name;
	uca->func = func;
	uca->next = NULL;

#ifdef UPSGI_DEBUG
	upsgi_log("[upsgi-cheaper-algo] registered \"%s\"\n", uca->name);
#endif
}

void trigger_harakiri(int i) {
	int j;
	upsgi_log_verbose("*** HARAKIRI ON WORKER %d (pid: %d, try: %d, graceful: %s) ***\n", i,
				upsgi.workers[i].pid,
				upsgi.workers[i].pending_harakiri + 1,
				upsgi.workers[i].pending_harakiri > 0 ? "no": "yes");
	if (upsgi.harakiri_verbose) {
#ifdef __linux__
		int proc_file;
		char proc_buf[4096];
		char proc_name[64];
		ssize_t proc_len;

		if (snprintf(proc_name, 64, "/proc/%d/syscall", upsgi.workers[i].pid) > 0) {
			memset(proc_buf, 0, 4096);
			proc_file = open(proc_name, O_RDONLY);
			if (proc_file >= 0) {
				proc_len = read(proc_file, proc_buf, 4096);
				if (proc_len > 0) {
					upsgi_log("HARAKIRI: -- syscall> %s", proc_buf);
				}
				close(proc_file);
			}
		}

		if (snprintf(proc_name, 64, "/proc/%d/wchan", upsgi.workers[i].pid) > 0) {
			memset(proc_buf, 0, 4096);

			proc_file = open(proc_name, O_RDONLY);
			if (proc_file >= 0) {
				proc_len = read(proc_file, proc_buf, 4096);
				if (proc_len > 0) {
					upsgi_log("HARAKIRI: -- wchan> %s\n", proc_buf);
				}
				close(proc_file);
			}
		}

#endif
	}

	if (upsgi.workers[i].pid > 0) {
		for (j = 0; j < upsgi.gp_cnt; j++) {
			if (upsgi.gp[j]->harakiri) {
				upsgi.gp[j]->harakiri(i);
			}
		}
		for (j = 0; j < 256; j++) {
			if (upsgi.p[j]->harakiri) {
				upsgi.p[j]->harakiri(i);
			}
		}

		upsgi_dump_worker(i, "HARAKIRI");
		if (upsgi.workers[i].pending_harakiri == 0 && upsgi.harakiri_graceful_timeout > 0) {
			kill(upsgi.workers[i].pid, upsgi.harakiri_graceful_signal);
		} else {
			kill(upsgi.workers[i].pid, SIGKILL);
		}
		if (!upsgi.workers[i].pending_harakiri)
			upsgi.workers[i].harakiri_count++;
		upsgi.workers[i].pending_harakiri++;
	}

}

void upsgi_master_fix_request_counters() {
	int i;
	uint64_t total_counter = 0;
        for (i = 1; i <= upsgi.numproc;i++) {
		uint64_t tmp_counter = 0;
		int j;
		for(j=0;j<upsgi.cores;j++) {
			tmp_counter += upsgi.workers[i].cores[j].requests;
		}
		upsgi.workers[i].requests = tmp_counter;
		total_counter += tmp_counter;
	}

	upsgi.workers[0].requests = total_counter;
}


int upsgi_cron_task_needs_execution(struct tm *upsgi_cron_delta, int minute, int hour, int day, int month, int week) {

	int uc_minute, uc_hour, uc_day, uc_month, uc_week;

	uc_minute = minute;
	uc_hour = hour;
	uc_day = day;
	uc_month = month;
	// support 7 as alias for sunday (0) to match crontab behaviour
	uc_week = week == 7 ? 0 : week;

	// negative values as interval -1 = * , -5 = */5
	if (minute < 0) {
		if ((upsgi_cron_delta->tm_min % abs(minute)) == 0) {
			uc_minute = upsgi_cron_delta->tm_min;
		}
	}
	if (hour < 0) {
		if ((upsgi_cron_delta->tm_hour % abs(hour)) == 0) {
			uc_hour = upsgi_cron_delta->tm_hour;
		}
	}
	if (month < 0) {
		if ((upsgi_cron_delta->tm_mon % abs(month)) == 0) {
			uc_month = upsgi_cron_delta->tm_mon;
		}
	}
	if (day < 0) {
		if ((upsgi_cron_delta->tm_mday % abs(day)) == 0) {
			uc_day = upsgi_cron_delta->tm_mday;
		}
	}
	if (week < 0) {
		if ((upsgi_cron_delta->tm_wday % abs(week)) == 0) {
			uc_week = upsgi_cron_delta->tm_wday;
		}
	}

	int run_task = 0;
	// mday and wday are ORed
	if (day >= 0 && week >= 0) {
		if (upsgi_cron_delta->tm_min == uc_minute && upsgi_cron_delta->tm_hour == uc_hour && upsgi_cron_delta->tm_mon == uc_month && (upsgi_cron_delta->tm_mday == uc_day || upsgi_cron_delta->tm_wday == uc_week)) {
			run_task = 1;
		}
	}
	else {
		if (upsgi_cron_delta->tm_min == uc_minute && upsgi_cron_delta->tm_hour == uc_hour && upsgi_cron_delta->tm_mon == uc_month && upsgi_cron_delta->tm_mday == uc_day && upsgi_cron_delta->tm_wday == uc_week) {
			run_task = 1;
		}
	}

	return run_task;

}

static void add_reload_fds(struct upsgi_string_list *list, char *type) {
	struct upsgi_string_list *usl = list;
	while(usl) {
		char *strc = upsgi_str(usl->value);
		char *space = strchr(strc, ' ');
		if (space) {
			*space = 0;
			usl->custom_ptr = space+1;
		}
		char *colon = strchr(strc, ':');
		if (colon) {
			*colon = 0;
			usl->custom2 = strtoul(colon+1, NULL, 10);
		}
		usl->custom = strtoul(strc, NULL, 10);
		if (!usl->custom2) usl->custom2 = 1;
		event_queue_add_fd_read(upsgi.master_queue, usl->custom);
		upsgi_add_safe_fd(usl->custom);
		upsgi_log("added %s reload monitor for fd %d (read size: %llu)\n", type, (int) usl->custom, usl->custom2);
		usl = usl->next;
	}
}

void upsgi_add_reload_fds() {
	add_reload_fds(upsgi.reload_on_fd, "graceful");
	add_reload_fds(upsgi.brutal_reload_on_fd, "brutal");
}

void upsgi_refork_master() {
	pid_t pid = fork();
	if (pid < 0) {
		upsgi_error("upsgi_refork_master()/fork()");
		return;
	}

	if (pid > 0) {
		upsgi_log_verbose("new master copy spawned with pid %d\n", (int) pid);
		return;
	}

	// detach from the old master
	setsid();

	upsgi.master_is_reforked = 1;
	upsgi_reload(upsgi.argv);
	// never here
	exit(1);
}

void upsgi_cheaper_increase() {
	upsgi.cheaper_fifo_delta++;
}

void upsgi_cheaper_decrease() {
        upsgi.cheaper_fifo_delta--;
}

void upsgi_go_cheap() {
	int i;
	int waitpid_status;
	if (upsgi.status.is_cheap) return;
	upsgi_log_verbose("going cheap...\n");
	upsgi.status.is_cheap = 1;
                for (i = 1; i <= upsgi.numproc; i++) {
                        upsgi.workers[i].cheaped = 1;
			upsgi.workers[i].rss_size = 0;
        		upsgi.workers[i].vsz_size = 0;
                        if (upsgi.workers[i].pid == 0)
                                continue;
			upsgi_log("killing worker %d (pid: %d)\n", i, (int) upsgi.workers[i].pid);
                        kill(upsgi.workers[i].pid, SIGKILL);
                        if (waitpid(upsgi.workers[i].pid, &waitpid_status, 0) < 0) {
                                if (errno != ECHILD)
                                        upsgi_error("upsgi_go_cheap()/waitpid()");
                        }
                }
                upsgi_add_sockets_to_queue(upsgi.master_queue, -1);
                upsgi_log("cheap mode enabled: waiting for socket connection...\n");
}

#ifdef __linux__
void upsgi_setns_preopen() {
	struct dirent *de;
        DIR *ns = opendir("/proc/self/ns");
        if (!ns) {
                upsgi_error("upsgi_setns_preopen()/opendir()");
		exit(1);
        }
        while ((de = readdir(ns)) != NULL) {
                if (strlen(de->d_name) > 0 && de->d_name[0] == '.') continue;
		if (!strcmp(de->d_name, "user")) continue;
                struct upsgi_string_list *usl = NULL;
                int found = 0;
                upsgi_foreach(usl, upsgi.setns_socket_skip) {
                        if (!strcmp(de->d_name, usl->value)) {
                                found = 1;
                                break;
                        }
                }
                if (found) continue;
                char *filename = upsgi_concat2("/proc/self/ns/", de->d_name);
                upsgi.setns_fds[upsgi.setns_fds_count] = open(filename, O_RDONLY);
                if (upsgi.setns_fds[upsgi.setns_fds_count] < 0) {
                        upsgi_error_open(filename);
                        free(filename);
			exit(1);
                }
                free(filename);
                upsgi.setns_fds_count++;
        }
	closedir(ns);
}
void upsgi_master_manage_setns(int fd) {

        struct sockaddr_un snsun;
        socklen_t snsun_len = sizeof(struct sockaddr_un);

        int setns_client = accept(fd, (struct sockaddr *) &snsun, &snsun_len);
        if (setns_client < 0) {
                upsgi_error("upsgi_master_manage_setns()/accept()");
                return;
        }

	int i;
	int tmp_fds[64];
	int *fds = tmp_fds;
        int num_fds = 0;

	struct msghdr sn_msg;
        void *sn_msg_control;
        struct iovec sn_iov[2];
        struct cmsghdr *cmsg;
	DIR *ns = NULL;

	if (upsgi.setns_fds_count) {
		fds = upsgi.setns_fds;
		num_fds = upsgi.setns_fds_count;
		goto send;
	}

	struct dirent *de;
	ns = opendir("/proc/self/ns");
	if (!ns) {
		close(setns_client);
		upsgi_error("upsgi_master_manage_setns()/opendir()");
		return;
	}
	while ((de = readdir(ns)) != NULL) {
		if (strlen(de->d_name) > 0 && de->d_name[0] == '.') continue;
		if (!strcmp(de->d_name, "user")) continue;
		struct upsgi_string_list *usl = NULL;
		int found = 0;
		upsgi_foreach(usl, upsgi.setns_socket_skip) {
			if (!strcmp(de->d_name, usl->value)) {
				found = 1;
				break;
			}
		}
		if (found) continue;
		char *filename = upsgi_concat2("/proc/self/ns/", de->d_name);
		fds[num_fds] = open(filename, O_RDONLY);
		if (fds[num_fds] < 0) {
			upsgi_error_open(filename);
			free(filename);
			goto clear;
		}
		free(filename);
		num_fds++;
	}

send:

        sn_msg_control = upsgi_malloc(CMSG_SPACE(sizeof(int) * num_fds));

        sn_iov[0].iov_base = "upsgi-setns";
        sn_iov[0].iov_len = 11;
        sn_iov[1].iov_base = &num_fds;
        sn_iov[1].iov_len = sizeof(int);

        sn_msg.msg_name = NULL;
        sn_msg.msg_namelen = 0;

        sn_msg.msg_iov = sn_iov;
        sn_msg.msg_iovlen = 2;

        sn_msg.msg_flags = 0;
        sn_msg.msg_control = sn_msg_control;
        sn_msg.msg_controllen = CMSG_SPACE(sizeof(int) * num_fds);

        cmsg = CMSG_FIRSTHDR(&sn_msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * num_fds);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        int *sn_fd_ptr = (int *) CMSG_DATA(cmsg);
	for(i=0;i<num_fds;i++) {
		sn_fd_ptr[i] = fds[i];
	}

        if (sendmsg(setns_client, &sn_msg, 0) < 0) {
                upsgi_error("upsgi_master_manage_setns()/sendmsg()");
        }

        free(sn_msg_control);

clear:
	close(setns_client);
	if (ns) {
		closedir(ns);
		for(i=0;i<num_fds;i++) {
			close(fds[i]);
		}
	}
}

#endif

/*
	this is racey, but the worst thing would be printing garbage in the logs...
*/
void upsgi_dump_worker(int wid, char *msg) {
	int i;
	upsgi_log_verbose("%s !!! worker %d status !!!\n", msg, wid);
	for(i=0;i<upsgi.cores;i++) {
		struct upsgi_core *uc = &upsgi.workers[wid].cores[i];
		struct wsgi_request *wsgi_req = &uc->req;
		if (uc->in_request) {
			upsgi_log_verbose("%s [core %d] %.*s - %.*s %.*s since %llu\n", msg, i, wsgi_req->remote_addr_len, wsgi_req->remote_addr, wsgi_req->method_len, wsgi_req->method, wsgi_req->uri_len, wsgi_req->uri, (unsigned long long) (wsgi_req->start_of_request/(1000*1000)));
		}
	}
	upsgi_log_verbose("%s !!! end of worker %d status !!!\n",msg, wid);
}

/* vim: set ts=8 sts=0 sw=0 noexpandtab : */
