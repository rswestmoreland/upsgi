#include "upsgi.h"

extern struct upsgi_server upsgi;

/*

	the --master-fifo option create a unix named pipe (fifo) you can use to send management
	commands to the master:

	echo r > myfifo

*/

// this var can be accessed by plugins and hooks
void (*upsgi_fifo_table[256])(int);

static char *upsgi_fifo_by_slot() {
	int count = 0;
	struct upsgi_string_list *usl;
	upsgi_foreach(usl, upsgi.master_fifo) {
		if (count == upsgi.master_fifo_slot) return usl->value;
		count++;
	}
	return upsgi.master_fifo->value;
}

#define announce_fifo upsgi_log_verbose("active master fifo is now %s\n", upsgi_fifo_by_slot())

static void upsgi_fifo_set_slot_zero(int signum) { upsgi.master_fifo_slot = 0; announce_fifo; }
static void upsgi_fifo_set_slot_one(int signum) { upsgi.master_fifo_slot = 1; announce_fifo; }
static void upsgi_fifo_set_slot_two(int signum) { upsgi.master_fifo_slot = 2; announce_fifo; }
static void upsgi_fifo_set_slot_three(int signum) { upsgi.master_fifo_slot = 3; announce_fifo; }
static void upsgi_fifo_set_slot_four(int signum) { upsgi.master_fifo_slot = 4; announce_fifo; }
static void upsgi_fifo_set_slot_five(int signum) { upsgi.master_fifo_slot = 5; announce_fifo; }
static void upsgi_fifo_set_slot_six(int signum) { upsgi.master_fifo_slot = 6; announce_fifo; }
static void upsgi_fifo_set_slot_seven(int signum) { upsgi.master_fifo_slot = 7; announce_fifo; }
static void upsgi_fifo_set_slot_eight(int signum) { upsgi.master_fifo_slot = 8; announce_fifo; }
static void upsgi_fifo_set_slot_nine(int signum) { upsgi.master_fifo_slot = 9; announce_fifo; }

static void emperor_rescan(int signum) {
	if (upsgi.emperor_pid > 0) {
		if (kill(upsgi.emperor_pid, SIGWINCH)) {
			upsgi_error("emperor_rescan()/kill()");
		}
	}
}

/*

this is called as soon as possible allowing plugins (or hooks) to override it

*/
void upsgi_master_fifo_prepare() {
	int i;
	for(i=0;i<256;i++) {
		upsgi_fifo_table[i] = NULL;
	}

	upsgi_fifo_table['0'] = upsgi_fifo_set_slot_zero;
	upsgi_fifo_table['1'] = upsgi_fifo_set_slot_one;
	upsgi_fifo_table['2'] = upsgi_fifo_set_slot_two;
	upsgi_fifo_table['3'] = upsgi_fifo_set_slot_three;
	upsgi_fifo_table['4'] = upsgi_fifo_set_slot_four;
	upsgi_fifo_table['5'] = upsgi_fifo_set_slot_five;
	upsgi_fifo_table['6'] = upsgi_fifo_set_slot_six;
	upsgi_fifo_table['7'] = upsgi_fifo_set_slot_seven;
	upsgi_fifo_table['8'] = upsgi_fifo_set_slot_eight;
	upsgi_fifo_table['9'] = upsgi_fifo_set_slot_nine;

	upsgi_fifo_table['-'] = (void (*)(int))upsgi_cheaper_decrease;
	upsgi_fifo_table['+'] = (void (*)(int))upsgi_cheaper_increase;
	upsgi_fifo_table['B'] = (void (*)(int))vassal_sos;
	upsgi_fifo_table['c'] = (void (*)(int))upsgi_chain_reload;
	upsgi_fifo_table['C'] = (void (*)(int))upsgi_go_cheap;
	upsgi_fifo_table['E'] = emperor_rescan;
	upsgi_fifo_table['f'] = (void (*)(int))upsgi_refork_master;
	upsgi_fifo_table['l'] = (void (*)(int))upsgi_log_reopen;
	upsgi_fifo_table['L'] = (void (*)(int))upsgi_log_rotate;
	upsgi_fifo_table['p'] = suspend_resume_them_all;
	upsgi_fifo_table['P'] = (void (*)(int))upsgi_update_pidfiles;
	upsgi_fifo_table['q'] = gracefully_kill_them_all;
	upsgi_fifo_table['Q'] = kill_them_all;
	upsgi_fifo_table['r'] = grace_them_all;
	upsgi_fifo_table['R'] = reap_them_all;
	upsgi_fifo_table['s'] = stats;
	upsgi_fifo_table['w'] = (void (*)(int))upsgi_reload_workers;
	upsgi_fifo_table['W'] = (void (*)(int))upsgi_brutally_reload_workers;

}

int upsgi_master_fifo() {

	char *path = upsgi_fifo_by_slot();

	if (unlink(path) != 0 && errno != ENOENT) {
		upsgi_error("upsgi_master_fifo()/unlink()");
	}

	if (mkfifo(path, S_IRUSR|S_IWUSR)) {
		upsgi_error("upsgi_master_fifo()/mkfifo()");
		exit(1);
	}

	int fd = open(path, O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		upsgi_error("upsgi_master_fifo()/open()");
		exit(1);
	}

	upsgi_socket_nb(fd);

	return fd;
}

int upsgi_master_fifo_manage(int fd) {
	unsigned char cmd;
	ssize_t rlen = read(fd, &cmd, 1);
	if (rlen < 0) {
		if (upsgi_is_again()) return 0;
		upsgi_error("upsgi_master_fifo_manage()/read()");
		exit(1);
	}
	// fifo destroyed, recreate it
	else if (rlen == 0) {
		event_queue_del_fd(upsgi.master_queue, upsgi.master_fifo_fd, event_queue_read());
		close(fd);
		upsgi.master_fifo_fd = upsgi_master_fifo();
		event_queue_add_fd_read(upsgi.master_queue, upsgi.master_fifo_fd);
		return 0;
	}

	if (upsgi_fifo_table[(int) cmd]) {
		upsgi_fifo_table[(int) cmd](0);
	}
	
	return 0;
}
