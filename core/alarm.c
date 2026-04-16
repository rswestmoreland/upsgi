#include "../upsgi.h"

extern struct upsgi_server upsgi;

// generate a upsgi signal on alarm
void upsgi_alarm_init_signal(struct upsgi_alarm_instance *uai) {
	uai->data8 = atoi(uai->arg);
}

void upsgi_alarm_func_signal(struct upsgi_alarm_instance *uai, char *msg, size_t len) {
	upsgi_route_signal(uai->data8);
}

// simply log an alarm
void upsgi_alarm_init_log(struct upsgi_alarm_instance *uai) {
}

void upsgi_alarm_func_log(struct upsgi_alarm_instance *uai, char *msg, size_t len) {
	if (msg[len-1] != '\n') {
		if (uai->arg && strlen(uai->arg) > 0) {
			upsgi_log_verbose("ALARM: %s %.*s\n", uai->arg, len, msg);
		}
		else {
			upsgi_log_verbose("ALARM: %.*s\n", len, msg);
		}
	}
	else {
		if (uai->arg && strlen(uai->arg) > 0) {
			upsgi_log_verbose("ALARM: %s %.*s", uai->arg, len, msg);
		}
		else {
			upsgi_log_verbose("ALARM: %.*s", len, msg);
		}
	}
}

// run a command on alarm
void upsgi_alarm_init_cmd(struct upsgi_alarm_instance *uai) {
	uai->data_ptr = uai->arg;
}

void upsgi_alarm_func_cmd(struct upsgi_alarm_instance *uai, char *msg, size_t len) {
	int pipe[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipe)) {
		return;
	}
	upsgi_socket_nb(pipe[0]);
	upsgi_socket_nb(pipe[1]);
	if (write(pipe[1], msg, len) != (ssize_t) len) {
		close(pipe[0]);
		close(pipe[1]);
		return;
	}
	upsgi_run_command(uai->data_ptr, pipe, -1);
	close(pipe[0]);
	close(pipe[1]);
}


// register a new alarm
void upsgi_register_alarm(char *name, void (*init) (struct upsgi_alarm_instance *), void (*func) (struct upsgi_alarm_instance *, char *, size_t)) {
	struct upsgi_alarm *old_ua = NULL, *ua = upsgi.alarms;
	while (ua) {
		// skip already initialized alarms
		if (!strcmp(ua->name, name)) {
			return;
		}
		old_ua = ua;
		ua = ua->next;
	}

	ua = upsgi_calloc(sizeof(struct upsgi_alarm));
	ua->name = name;
	ua->init = init;
	ua->func = func;

	if (old_ua) {
		old_ua->next = ua;
	}
	else {
		upsgi.alarms = ua;
	}
}

// register embedded alarms
void upsgi_register_embedded_alarms() {
	upsgi_register_alarm("signal", upsgi_alarm_init_signal, upsgi_alarm_func_signal);
	upsgi_register_alarm("cmd", upsgi_alarm_init_cmd, upsgi_alarm_func_cmd);
	upsgi_register_alarm("log", upsgi_alarm_init_log, upsgi_alarm_func_log);
}

static int upsgi_alarm_add(char *name, char *plugin, char *arg) {
	struct upsgi_alarm *ua = upsgi.alarms;
	while (ua) {
		if (!strcmp(ua->name, plugin)) {
			break;
		}
		ua = ua->next;
	}

	if (!ua)
		return -1;

	struct upsgi_alarm_instance *old_uai = NULL, *uai = upsgi.alarm_instances;
	while (uai) {
		old_uai = uai;
		uai = uai->next;
	}

	uai = upsgi_calloc(sizeof(struct upsgi_alarm_instance));
	uai->name = name;
	uai->alarm = ua;
	uai->arg = arg;
	uai->last_msg = upsgi_malloc(upsgi.log_master_bufsize);

	if (old_uai) {
		old_uai->next = uai;
	}
	else {
		upsgi.alarm_instances = uai;
	}

	ua->init(uai);
	return 0;
}

// get an alarm instance by its name
static struct upsgi_alarm_instance *upsgi_alarm_get_instance(char *name) {
	struct upsgi_alarm_instance *uai = upsgi.alarm_instances;
	while (uai) {
		if (!strcmp(name, uai->name)) {
			return uai;
		}
		uai = uai->next;
	}
	return NULL;
}


#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
static int upsgi_alarm_log_add(char *alarms, char *regexp, int negate) {

	struct upsgi_alarm_log *old_ual = NULL, *ual = upsgi.alarm_logs;
	while (ual) {
		old_ual = ual;
		ual = ual->next;
	}

	ual = upsgi_calloc(sizeof(struct upsgi_alarm_log));
	if (upsgi_regexp_build(regexp, &ual->pattern)) {
		free(ual);
		return -1;
	}
	ual->negate = negate;

	if (old_ual) {
		old_ual->next = ual;
	}
	else {
		upsgi.alarm_logs = ual;
	}

	// map instances to the log
	char *list = upsgi_str(alarms);
	char *p, *ctx = NULL;
	upsgi_foreach_token(list, ",", p, ctx) {
		struct upsgi_alarm_instance *uai = upsgi_alarm_get_instance(p);
		if (!uai) {
			free(list);
			return -1;
		}
		struct upsgi_alarm_ll *old_uall = NULL, *uall = ual->alarms;
		while (uall) {
			old_uall = uall;
			uall = uall->next;
		}

		uall = upsgi_calloc(sizeof(struct upsgi_alarm_ll));
		uall->alarm = uai;
		if (old_uall) {
			old_uall->next = uall;
		}
		else {
			ual->alarms = uall;
		}
	}
	free(list);
	return 0;
}
#endif

static void upsgi_alarm_thread_loop(struct upsgi_thread *ut) {
	// add upsgi_alarm_fd;
	struct upsgi_alarm_fd *uafd = upsgi.alarm_fds;
	while(uafd) {
		event_queue_add_fd_read(ut->queue, uafd->fd);
		uafd = uafd->next;
	}
	char *buf = upsgi_malloc(upsgi.alarm_msg_size + sizeof(long));
	for (;;) {
		int interesting_fd = -1;
                int ret = event_queue_wait(ut->queue, -1, &interesting_fd);
		if (ret > 0) {
			if (interesting_fd == ut->pipe[1]) {
				ssize_t len = read(ut->pipe[1], buf, upsgi.alarm_msg_size + sizeof(long));
				if (len > (ssize_t)(sizeof(long) + 1)) {
					size_t msg_size = len - sizeof(long);
					char *msg = buf + sizeof(long);
					long ptr = 0;
					memcpy(&ptr, buf, sizeof(long));
					struct upsgi_alarm_instance *uai = (struct upsgi_alarm_instance *) ptr;
					if (!uai)
						break;
					upsgi_alarm_run(uai, msg, msg_size);
				}
			}
			// check for alarm_fd
			else {
				uafd = upsgi.alarm_fds;
				int fd_read = 0;
				while(uafd) {
					if (interesting_fd == uafd->fd) {
						if (fd_read) goto raise;	
						size_t remains = uafd->buf_len;
						while(remains) {
							ssize_t len = read(uafd->fd, uafd->buf + (uafd->buf_len-remains), remains);
							if (len <= 0) {
								upsgi_error("[upsgi-alarm-fd]/read()");
								upsgi_log("[upsgi-alarm-fd] i will stop monitoring fd %d\n", uafd->fd);
								event_queue_del_fd(ut->queue, uafd->fd, event_queue_read());
								break;
							}
							remains-=len;
						}
						fd_read = 1;
raise:
						upsgi_alarm_run(uafd->alarm, uafd->msg, uafd->msg_len);
					}
					uafd = uafd->next;
				}
			}
		}
	}
	free(buf);
}

// initialize alarms, instances and log regexps
void upsgi_alarms_init() {

	if (!upsgi.master_process) return;

	// first of all, create instance of alarms
	struct upsgi_string_list *usl = upsgi.alarm_list;
	while (usl) {
		char *line = upsgi_str(usl->value);
		char *space = strchr(line, ' ');
		if (!space) {
			upsgi_log("invalid alarm syntax: %s\n", usl->value);
			exit(1);
		}
		*space = 0;
		char *plugin = space + 1;
		char *colon = strchr(plugin, ':');
		if (!colon) {
			upsgi_log("invalid alarm syntax: %s\n", usl->value);
			exit(1);
		}
		*colon = 0;
		char *arg = colon + 1;
		// here the alarm is mapped to a name and initialized
		if (upsgi_alarm_add(line, plugin, arg)) {
			upsgi_log("invalid alarm: %s\n", usl->value);
			exit(1);
		}
		usl = usl->next;
	}

	if (!upsgi.alarm_instances) return;

	// map alarm file descriptors
	usl = upsgi.alarm_fd_list;
	while(usl) {
		char *space0 = strchr(usl->value, ' ');
		if (!space0) {
			upsgi_log("invalid alarm-fd syntax: %s\n", usl->value);
			exit(1);
		}
		*space0 = 0;
		size_t buf_len = 1;
		char *space1 = strchr(space0+1, ' ');
		if (!space1) {
			upsgi_log("invalid alarm-fd syntax: %s\n", usl->value);
                        exit(1);
		}

		char *colon = strchr(space0+1, ':');
		if (colon) {
			buf_len = strtoul(colon+1, NULL, 10);
			*colon = 0;
		}
		int fd = atoi(space0+1);
		upsgi_add_alarm_fd(fd, usl->value, buf_len, space1+1, strlen(space1+1));
		*space0 = ' ';
		*space1 = ' ';
		if (colon) {
			*colon = ':';
		}
		usl = usl->next;
	}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
	// then map log-alarm
	usl = upsgi.alarm_logs_list;
	while (usl) {
		char *line = upsgi_str(usl->value);
		char *space = strchr(line, ' ');
		if (!space) {
			upsgi_log("invalid log-alarm syntax: %s\n", usl->value);
			exit(1);
		}
		*space = 0;
		char *regexp = space + 1;
		// here the log-alarm is created
		if (upsgi_alarm_log_add(line, regexp, usl->custom)) {
			upsgi_log("invalid log-alarm: %s\n", usl->value);
			exit(1);
		}

		usl = usl->next;
	}
#endif
}

void upsgi_alarm_thread_start() {
	if (!upsgi.alarm_instances) return;
	// start the alarm_thread
	upsgi.alarm_thread = upsgi_thread_new(upsgi_alarm_thread_loop);
	if (!upsgi.alarm_thread) {
		upsgi_log("unable to spawn alarm thread\n");
		exit(1);
	}
}

void upsgi_alarm_trigger_uai(struct upsgi_alarm_instance *uai, char *msg, size_t len) {
	struct iovec iov[2];
	iov[0].iov_base = &uai;
	iov[0].iov_len = sizeof(long);
	iov[1].iov_base = msg;
	iov[1].iov_len = len;

	// now send the message to the alarm thread
	if (writev(upsgi.alarm_thread->pipe[0], iov, 2) != (ssize_t) (len+sizeof(long))) {
		upsgi_error("[upsgi-alarm-error] upsgi_alarm_trigger()/writev()");
	}
}

#if defined(UPSGI_PCRE) || defined(UPSGI_PCRE2)
// check if a log should raise an alarm
void upsgi_alarm_log_check(char *msg, size_t len) {
	if (!upsgi_strncmp(msg, len, "[upsgi-alarm", 12))
		return;
	struct upsgi_alarm_log *ual = upsgi.alarm_logs;
	while (ual) {
		if (upsgi_regexp_match(ual->pattern, msg, len) >= 0) {
			if (!ual->negate) {
				struct upsgi_alarm_ll *uall = ual->alarms;
				while (uall) {
					if (upsgi.alarm_cheap)
						upsgi_alarm_trigger_uai(uall->alarm, msg, len);
					else
						upsgi_alarm_run(uall->alarm, msg, len);
					uall = uall->next;
				}
			}
			else {
				break;
			}
		}
		ual = ual->next;
	}
}
#endif

// call the alarm func
void upsgi_alarm_run(struct upsgi_alarm_instance *uai, char *msg, size_t len) {
	time_t now = upsgi_now();
	// avoid alarm storming/loop if last message is the same
	if (!upsgi_strncmp(msg, len, uai->last_msg, uai->last_msg_size)) {
		if (now - uai->last_run < upsgi.alarm_freq)
			return;
	}
	uai->alarm->func(uai, msg, len);
	uai->last_run = upsgi_now();
	memcpy(uai->last_msg, msg, len);
	uai->last_msg_size = len;
}

// this is the api function workers and other runtime paths can call from code
void upsgi_alarm_trigger(char *alarm_instance_name, char *msg, size_t len) {
	if (!upsgi.alarm_thread) return;
	if (len > upsgi.alarm_msg_size) return;
	struct upsgi_alarm_instance *uai = upsgi_alarm_get_instance(alarm_instance_name);
	if (!uai) return;

	upsgi_alarm_trigger_uai(uai, msg, len);
}

struct upsgi_alarm_fd *upsgi_add_alarm_fd(int fd, char *alarm, size_t buf_len, char *msg, size_t msg_len) {
	struct upsgi_alarm_fd *old_uafd = NULL, *uafd = upsgi.alarm_fds;
	struct upsgi_alarm_instance *uai = upsgi_alarm_get_instance(alarm);
	if (!uai) {
		upsgi_log("unable to find alarm \"%s\"\n", alarm);
		exit(1);
	}

	if (!buf_len) buf_len = 1;	

	while(uafd) {
		// check if an equal alarm has been added
		if (uafd->fd == fd && uafd->alarm == uai) {
			return uafd;
		}
		old_uafd = uafd;
		uafd = uafd->next;
	}

	uafd = upsgi_calloc(sizeof(struct upsgi_alarm_fd));
	uafd->fd = fd;
	uafd->buf = upsgi_malloc(buf_len);
	uafd->buf_len = buf_len;
	uafd->msg = msg;
	uafd->msg_len = msg_len;
	uafd->alarm = uai;

	if (!old_uafd) {
		upsgi.alarm_fds = uafd;
	}
	else {
		old_uafd->next = uafd;
	}

	// avoid the fd to be closed
	upsgi_add_safe_fd(fd);
	upsgi_log("[upsgi-alarm] added fd %d\n", fd);

	return uafd;
}
