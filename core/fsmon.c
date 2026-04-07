#include "upsgi.h"

extern struct upsgi_server upsgi;

#ifdef UPSGI_EVENT_FILEMONITOR_USE_INOTIFY
#ifndef OBSOLETE_LINUX_KERNEL
#include <sys/inotify.h>
#endif
#endif

static int fsmon_add(struct upsgi_fsmon *fs) {
#ifdef UPSGI_EVENT_FILEMONITOR_USE_INOTIFY
#ifndef OBSOLETE_LINUX_KERNEL
	static int inotify_fd = -1;
	if (inotify_fd == -1) {
		inotify_fd = inotify_init();
		if (inotify_fd < 0) {
			upsgi_error("fsmon_add()/inotify_init()");
			return -1;
		}
		if (event_queue_add_fd_read(upsgi.master_queue, inotify_fd)) {
			upsgi_error("fsmon_add()/event_queue_add_fd_read()");
			return -1;
		}
	}
	int wd = inotify_add_watch(inotify_fd, fs->path, IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO);
	if (wd < 0) {
		upsgi_error("fsmon_add()/inotify_add_watch()");
		return -1;
	}
	fs->fd = inotify_fd;
	fs->id = wd;
	return 0;
#endif
#endif
#ifdef UPSGI_EVENT_FILEMONITOR_USE_KQUEUE
	struct kevent kev;
	int fd = open(fs->path, O_RDONLY);
	if (fd < 0) {
		upsgi_error_open(fs->path);
		upsgi_error("fsmon_add()/open()");
		return -1;
	}

	EV_SET(&kev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_DELETE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME | NOTE_REVOKE, 0, 0);
	if (kevent(upsgi.master_queue, &kev, 1, NULL, 0, NULL) < 0) {
		upsgi_error("fsmon_add()/kevent()");
		return -1;
	}
	fs->fd = fd;
	return 0;
#endif
	upsgi_log("[upsgi-fsmon] filesystem monitoring interface not available in this platform !!!\n");
	return 1;
}

static void fsmon_reload(struct upsgi_fsmon *fs) {
	upsgi_block_signal(SIGHUP);
	grace_them_all(0);
	upsgi_unblock_signal(SIGHUP);
}

static void fsmon_brutal_reload(struct upsgi_fsmon *fs) {
	upsgi_block_signal(SIGQUIT);
	reap_them_all(0);
	upsgi_unblock_signal(SIGQUIT);
}

static void fsmon_signal(struct upsgi_fsmon *fs) {
	upsgi_route_signal(atoi((char *) fs->data));
}

void upsgi_fsmon_setup() {
	struct upsgi_string_list *usl = NULL;
	upsgi_foreach(usl, upsgi.fs_reload) {
		upsgi_register_fsmon(usl->value, fsmon_reload, NULL);
	}
	upsgi_foreach(usl, upsgi.fs_brutal_reload) {
		upsgi_register_fsmon(usl->value, fsmon_brutal_reload, NULL);
	}
	upsgi_foreach(usl, upsgi.fs_signal) {
		char *copy = upsgi_str(usl->value);
		char *space = strchr(copy, ' ');
		if (!space) {
			upsgi_log("[upsgi-fsmon] invalid syntax: \"%s\"\n", usl->value);
			free(copy);
			continue;
		}
		*space = 0;
		upsgi_register_fsmon(copy, fsmon_signal, space + 1);
	}

	struct upsgi_fsmon *fs = upsgi.fsmon;
	while (fs) {
		if (fsmon_add(fs)) {
			upsgi_log("[upsgi-fsmon] unable to register monitor for \"%s\"\n", fs->path);
		}
		else {
			upsgi_log("[upsgi-fsmon] registered monitor for \"%s\"\n", fs->path);
		}
		fs = fs->next;
	}
}


struct upsgi_fsmon *upsgi_register_fsmon(char *path, void (*func) (struct upsgi_fsmon *), void *data) {
	struct upsgi_fsmon *old_fs = NULL, *fs = upsgi.fsmon;
	while(fs) {
		old_fs = fs;
		fs = fs->next;
	}

	fs = upsgi_calloc(sizeof(struct upsgi_fsmon));
	fs->path = path;
	fs->func = func;
	fs->data = data;
	
	if (old_fs) {
		old_fs->next = fs;
	}
	else {
		upsgi.fsmon = fs;
	}

	return fs;
}

static struct upsgi_fsmon *upsgi_fsmon_ack(int interesting_fd) {
	struct upsgi_fsmon *found_fs = NULL;
	struct upsgi_fsmon *fs = upsgi.fsmon;
	while (fs) {
		if (fs->fd == interesting_fd) {
			found_fs = fs;
			break;
		}
		fs = fs->next;
	}

#ifdef UPSGI_EVENT_FILEMONITOR_USE_INOTIFY
#ifndef OBSOLETE_LINUX_KERNEL
	if (!found_fs)
		return NULL;
	found_fs = NULL;
	unsigned int isize = 0;
	if (ioctl(interesting_fd, FIONREAD, &isize) < 0) {
		upsgi_error("upsgi_fsmon_ack()/ioctl()");
		return NULL;
	}
	if (isize == 0)
		return NULL;
	struct inotify_event *ie = upsgi_malloc(isize);
	// read from the inotify descriptor
	ssize_t len = read(interesting_fd, ie, isize);
	if (len < 0) {
		free(ie);
		upsgi_error("upsgi_fsmon_ack()/read()");
		return NULL;
	}
	fs = upsgi.fsmon;
	while (fs) {
		if (fs->fd == interesting_fd && fs->id == ie->wd) {
			found_fs = fs;
			break;
		}
		fs = fs->next;
	}
	free(ie);
#endif
#endif
	return found_fs;
}

int upsgi_fsmon_event(int interesting_fd) {

	struct upsgi_fsmon *fs = upsgi_fsmon_ack(interesting_fd);
	if (!fs)
		return 0;

	upsgi_log_verbose("[upsgi-fsmon] detected event on \"%s\"\n", fs->path);
	fs->func(fs);
	return 1;
}
