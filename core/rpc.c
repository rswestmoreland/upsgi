#include "upsgi.h"

extern struct upsgi_server upsgi;

int upsgi_register_rpc(char *name, struct upsgi_plugin *plugin, uint8_t args, void *func) {

	struct upsgi_rpc *urpc;
	int ret = -1;

	if (!upsgi.workers || !upsgi.shared || !upsgi.rpc_table_lock) {
		upsgi_log("RPC subsystem still not initialized\n");
		return -1;
	}

	if (upsgi.mywid == 0 && upsgi.workers[0].pid != upsgi.mypid) {
		upsgi_log("only the master and the workers can register RPC functions\n");
		return -1;
	}

	if (strlen(name) >= UMAX8)  {
	      upsgi_log("the supplied RPC name string is too long, max size is %d\n", UMAX8-1);
	      return -1;
	}

	upsgi_lock(upsgi.rpc_table_lock);

	// first check if a function is already registered
	size_t i;
	for(i=0;i<upsgi.shared->rpc_count[upsgi.mywid];i++) {
		int pos = (upsgi.mywid * upsgi.rpc_max) + i;
		urpc = &upsgi.rpc_table[pos];
		if (!strcmp(name, urpc->name)) {
			goto already;
		}
	}

	if (upsgi.shared->rpc_count[upsgi.mywid] < upsgi.rpc_max) {
		int pos = (upsgi.mywid * upsgi.rpc_max) + upsgi.shared->rpc_count[upsgi.mywid];
		urpc = &upsgi.rpc_table[pos];
		upsgi.shared->rpc_count[upsgi.mywid]++;
already:
		memcpy(urpc->name, name, strlen(name));
		urpc->plugin = plugin;
		urpc->args = args;
		urpc->func = func;
		urpc->shared = upsgi.mywid == 0 ? 1 : 0;

		ret = 0;
		if (upsgi.mywid == 0) {
			upsgi_log("registered shared/inherited RPC function \"%s\"\n", name);
		}
		else {
			upsgi_log("registered RPC function \"%s\" on worker %d\n", name, upsgi.mywid);
		}
	}

	// implement cow
	if (upsgi.mywid == 0) {
		int i;
		for(i=1;i<=upsgi.numproc;i++) {
			upsgi.shared->rpc_count[i] = upsgi.shared->rpc_count[0];
			int pos = (i * upsgi.rpc_max);
			memcpy(&upsgi.rpc_table[pos], upsgi.rpc_table, sizeof(struct upsgi_rpc) * upsgi.rpc_max);
		}
	}

	upsgi_unlock(upsgi.rpc_table_lock);

	return ret;
}

uint64_t upsgi_rpc(char *name, uint8_t argc, char *argv[], uint16_t argvs[], char **output) {

	struct upsgi_rpc *urpc = NULL;
	uint64_t i;
	uint64_t ret = 0;

	int pos = (upsgi.mywid * upsgi.rpc_max);

	for (i = 0; i < upsgi.shared->rpc_count[upsgi.mywid]; i++) {
		if (upsgi.rpc_table[pos + i].name[0] != 0) {
			if (!strcmp(upsgi.rpc_table[pos + i].name, name)) {
				urpc = &upsgi.rpc_table[pos + i];
				break;
			}
		}
	}

	if (urpc) {
		if (urpc->plugin->rpc) {
			ret = urpc->plugin->rpc(urpc->func, argc, argv, argvs, output);
		}
	}

	return ret;
}

static void rpc_context_hook(char *key, uint16_t kl, char *value, uint16_t vl, void *data) {
	size_t *r = (size_t *) data;

	if (!upsgi_strncmp(key, kl, "CONTENT_LENGTH", 14)) {
		*r = upsgi_str_num(value, vl);
	}
}

char *upsgi_do_rpc(char *node, char *func, uint8_t argc, char *argv[], uint16_t argvs[], uint64_t * len) {

	uint8_t i;
	uint16_t ulen;
	struct upsgi_header *uh = NULL;
	char *buffer = NULL;

	*len = 0;

	if (node == NULL || !strcmp(node, "")) {
		// allocate the whole buffer
		if (!upsgi.rpc_table) {
                	upsgi_log("local rpc subsystem is still not initialized !!!\n");
                	return NULL;
        	}
		*len = upsgi_rpc(func, argc, argv, argvs, &buffer);
		if (buffer)
			return buffer;
		return NULL;
	}


	// connect to node (async way)
	int fd = upsgi_connect(node, 0, 1);
	if (fd < 0)
		return NULL;

	// wait for connection;
	int ret = upsgi.wait_write_hook(fd, upsgi.socket_timeout);
	if (ret <= 0) {
		close(fd);
		return NULL;
	}

	// prepare a upsgi array
	size_t buffer_size = 2 + strlen(func);

	for (i = 0; i < argc; i++) {
		buffer_size += 2 + argvs[i];
	}

	if (buffer_size > 0xffff) {
		upsgi_log("RPC packet length overflow!!! Must be less than or equal to 65535, have %llu\n", buffer_size);
		close(fd);
		return NULL;
	}

	// allocate the whole buffer
	buffer = upsgi_malloc(4+buffer_size);

	// set the upsgi header
	uh = (struct upsgi_header *) buffer;
	uh->modifier1 = 173;
	uh->_pktsize = (uint16_t) buffer_size;
	uh->modifier2 = 0;

	// add func to the array
	char *bufptr = buffer + 4;
	ulen = strlen(func);
	*bufptr++ = (uint8_t) (ulen & 0xff);
	*bufptr++ = (uint8_t) ((ulen >> 8) & 0xff);
	memcpy(bufptr, func, ulen);
	bufptr += ulen;

	for (i = 0; i < argc; i++) {
		ulen = argvs[i];
		*bufptr++ = (uint8_t) (ulen & 0xff);
		*bufptr++ = (uint8_t) ((ulen >> 8) & 0xff);
		memcpy(bufptr, argv[i], ulen);
		bufptr += ulen;
	}

	// ok the request is ready, let's send it in non blocking way
	if (upsgi_write_true_nb(fd, buffer, buffer_size+4, upsgi.socket_timeout)) {
		goto error;
	}

	// ok time to wait for the response in non blocking way
	size_t rlen = buffer_size+4;
	uint8_t modifier2 = 0;
	if (upsgi_read_with_realloc(fd, &buffer, &rlen, upsgi.socket_timeout, NULL, &modifier2)) {
		goto error;
	}

	// 64bit response ?
	if (modifier2 == 5) {
		size_t content_len = 0;
		if (upsgi_hooked_parse(buffer, rlen, rpc_context_hook, &content_len )) goto error;

		if (content_len > rlen) {
			char *tmp_buf = realloc(buffer, content_len);
			if (!tmp_buf) goto error;
			buffer = tmp_buf;
		}

		rlen = content_len;

		// read the raw value from the socket
                if (upsgi_read_whole_true_nb(fd, buffer, rlen, upsgi.socket_timeout)) {
			goto error;
                }
	}

	close(fd);
	*len = rlen;
	if (*len == 0) {
		goto error2;
	}
	return buffer;

error:
	close(fd);
error2:
	free(buffer);
	return NULL;

}


void upsgi_rpc_init() {
	upsgi.rpc_table = upsgi_calloc_shared((sizeof(struct upsgi_rpc) * upsgi.rpc_max) * (upsgi.numproc+1));
	upsgi.shared->rpc_count = upsgi_calloc_shared(sizeof(uint64_t) * (upsgi.numproc+1));
}
