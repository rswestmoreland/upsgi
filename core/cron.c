#include "upsgi.h"

extern struct upsgi_server upsgi;

struct upsgi_cron *upsgi_cron_add(char *crontab) {
	int i;
        struct upsgi_cron *old_uc, *uc = upsgi.crons;
        if (!uc) {
                uc = upsgi_malloc(sizeof(struct upsgi_cron));
                upsgi.crons = uc;
        }
        else {
                old_uc = uc;
                while (uc->next) {
                        uc = uc->next;
                        old_uc = uc;
                }

                old_uc->next = upsgi_malloc(sizeof(struct upsgi_cron));
                uc = old_uc->next;
        }

        memset(uc, 0, sizeof(struct upsgi_cron));

        if (sscanf(crontab, "%d %d %d %d %d %n", &uc->minute, &uc->hour, &uc->day, &uc->month, &uc->week, &i) != 5) {
                upsgi_log("invalid cron syntax\n");
                exit(1);
        }
        uc->command = crontab + i;
        uc->pid = -1;
        return uc;
}


void upsgi_opt_add_cron(char *opt, char *value, void *foobar) {
        upsgi_cron_add(value);
}


void upsgi_opt_add_unique_cron(char *opt, char *value, void *foobar) {
        struct upsgi_cron *uc = upsgi_cron_add(value);
        uc->unique = 1;
}


#ifdef UPSGI_SSL
void upsgi_opt_add_legion_cron(char *opt, char *value, void *foobar) {
        char *space = strchr(value, ' ');
        if (!space) {
                upsgi_log("invalid %s syntax, must be prefixed with a legion name\n", opt);
                exit(1);
        }
        char *legion = upsgi_concat2n(value, space-value, "", 0);
        struct upsgi_cron *uc = upsgi_cron_add(space+1);
        uc->legion = legion;
}


void upsgi_opt_add_unique_legion_cron(char *opt, char *value, void *foobar) {
        char *space = strchr(value, ' ');
        if (!space) {
                upsgi_log("invalid %s syntax, must be prefixed with a legion name\n", opt);
                exit(1);
        }
        char *legion = upsgi_concat2n(value, space-value, "", 0);
        struct upsgi_cron *uc = upsgi_cron_add(space+1);
        uc->legion = legion;
        uc->unique = 1;
}
#endif


void upsgi_opt_add_cron2(char *opt, char *value, void *foobar) {

	char *c_minute = NULL;
	char *c_hour = NULL;
	char *c_day = NULL;
	char *c_month = NULL;
	char *c_week = NULL;
	char *c_unique = NULL;
	char *c_harakiri = NULL;
	char *c_legion = NULL;

	char *c_command = value;

	char *space = strchr(value, ' ');
	if (space) {
		if (upsgi_str_contains(value, space - value, '=')) {
			// --cron2 key=val command
			*space = 0;
			c_command = space + 1;
		}

		// no point in parsing key=val list if there is none
		if (upsgi_kvlist_parse(value, strlen(value), ',', '=',
			"minute", &c_minute,
			"hour", &c_hour,
			"day", &c_day,
			"month", &c_month,
			"week", &c_week,
			"unique", &c_unique,
			"harakiri", &c_harakiri,
			"legion", &c_legion,
			NULL)) {
			upsgi_log("unable to parse cron definition: %s\n", value);
			exit(1);
		}
	}
	else {
		if (upsgi_str_contains(value, strlen(value), '=')) {
			// --cron2 key=val
			upsgi_log("unable to parse cron definition: %s\n", value);
			exit(1);
		}
	}

	struct upsgi_cron *old_uc, *uc = upsgi.crons;
	if (!uc) {
		uc = upsgi_malloc(sizeof(struct upsgi_cron));
		upsgi.crons = uc;
	}
	else {
		old_uc = uc;
		while (uc->next) {
			uc = uc->next;
			old_uc = uc;
		}

		old_uc->next = upsgi_malloc(sizeof(struct upsgi_cron));
		uc = old_uc->next;
	}

	memset(uc, 0, sizeof(struct upsgi_cron));

	uc->command = c_command;
	if (!uc->command) {
		upsgi_log("[upsgi-cron] invalid command in cron definition: %s\n", value);
		exit(1);
	}

	// defaults
	uc->minute = -1;
	uc->hour = -1;
	uc->day = -1;
	uc->month = -1;
	uc->week = -1;

	uc->unique = 0;
	uc->mercy = 0;
	uc->harakiri = 0;
	uc->pid = -1;

#ifdef UPSGI_SSL
	uc->legion = c_legion;
#endif

	if (c_minute)
		uc->minute = atoi(c_minute);

	if (c_hour)
		uc->hour = atoi(c_hour);

	if (c_day)
		uc->day = atoi(c_day);

	if (c_month)
		uc->month = atoi(c_month);

	if (c_week)
		uc->week = atoi(c_week);

	if (c_unique)
		uc->unique = atoi(c_unique);

	if (c_harakiri) {
		if (atoi(c_harakiri)) {
			// harakiri > 0
			uc->mercy = atoi(c_harakiri);
		}
		else {
			// harakiri == 0
			uc->mercy = -1;
		}
	}
	else if (upsgi.cron_harakiri) {
		uc->harakiri = upsgi.cron_harakiri;
	}
}


int upsgi_signal_add_cron(uint8_t sig, int minute, int hour, int day, int month, int week) {

        if (!upsgi.master_process)
                return -1;

        upsgi_lock(upsgi.cron_table_lock);

        if (ushared->cron_cnt < MAX_CRONS) {

                ushared->cron[ushared->cron_cnt].sig = sig;
                ushared->cron[ushared->cron_cnt].minute = minute;
                ushared->cron[ushared->cron_cnt].hour = hour;
                ushared->cron[ushared->cron_cnt].day = day;
                ushared->cron[ushared->cron_cnt].month = month;
                ushared->cron[ushared->cron_cnt].week = week;
                ushared->cron_cnt++;
        }
        else {
                upsgi_log("you can register max %d cron !!!\n", MAX_CRONS);
                upsgi_unlock(upsgi.cron_table_lock);
                return -1;
        }

        upsgi_unlock(upsgi.cron_table_lock);

        return 0;
}

void upsgi_manage_signal_cron(time_t now) {

        struct tm *upsgi_cron_delta;
        int i;

        upsgi_cron_delta = localtime(&now);

        if (upsgi_cron_delta) {

                // fix month
                upsgi_cron_delta->tm_mon++;

                upsgi_lock(upsgi.cron_table_lock);
                for (i = 0; i < ushared->cron_cnt; i++) {

                        struct upsgi_cron *ucron = &ushared->cron[i];

                        int run_task = upsgi_cron_task_needs_execution(upsgi_cron_delta, ucron->minute, ucron->hour, ucron->day, ucron->month, ucron->week);

                        if (run_task == 1) {
                                // date match, signal it ?
                                if (now - ucron->last_job >= 60) {
                                        upsgi_log_verbose("[upsgi-cron] routing signal %d\n", ucron->sig);
                                        upsgi_route_signal(ucron->sig);
                                        ucron->last_job = now;
                                }
                        }

                }
                upsgi_unlock(upsgi.cron_table_lock);
        }
        else {
                upsgi_error("localtime()");
        }

}

void upsgi_manage_command_cron(time_t now) {

        struct tm *upsgi_cron_delta;

        struct upsgi_cron *current_cron = upsgi.crons;

        upsgi_cron_delta = localtime(&now);


        if (!upsgi_cron_delta) {
                upsgi_error("upsgi_manage_command_cron()/localtime()");
                return;
        }

        // fix month
        upsgi_cron_delta->tm_mon++;

        while (current_cron) {

#ifdef UPSGI_SSL
                // check for legion cron
                if (current_cron->legion) {
                        if (!upsgi_legion_i_am_the_lord(current_cron->legion))
                            goto next;
                }
#endif

		// skip unique crons that are still running
		if (current_cron->unique && current_cron->pid >= 0)
			goto next;

                int run_task = upsgi_cron_task_needs_execution(upsgi_cron_delta, current_cron->minute, current_cron->hour, current_cron->day, current_cron->month, current_cron->week);
                if (run_task == 1) {

                        // date match, run command ?
                        if (now - current_cron->last_job >= 60) {
                                //call command
                                if (current_cron->command) {
					if (current_cron->func) {
						current_cron->func(current_cron, now);
					}
					else {
						pid_t pid = upsgi_run_command(current_cron->command, NULL, -1);
						if (pid >= 0) {
							current_cron->pid = pid;
							current_cron->started_at = now;
							upsgi_log_verbose("[upsgi-cron] running \"%s\" (pid %d)\n", current_cron->command, current_cron->pid);
							if (current_cron->mercy) {
								//upsgi_cron->mercy can be negative to inform master that harakiri should be disabled for this cron
								if (current_cron->mercy > 0)
									current_cron->harakiri = now + current_cron->mercy;
							}
							else if (upsgi.cron_harakiri)
								current_cron->harakiri = now + upsgi.cron_harakiri;
						}
					}
                                }
                                current_cron->last_job = now;
                        }
                }

next:
                current_cron = current_cron->next;
        }
}

