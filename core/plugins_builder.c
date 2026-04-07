#include "upsgi.h"

#define UPSGI_BUILD_DIR ".upsgi_plugins_builder"

/*

	steps:

		mkdir(.upsgi_plugin_builder)
		generate .upsgi_plugin_builder/upsgi.h
		generate .upsgi_plugin_builder/upsgiconfig.py
		setenv(UPSGI_PLUGINS_BUILDER_CFLAGS=upsgi_cflags)
		exec PYTHON .upsgi_plugin_builder/upsgiconfig.py --extra-plugin <directory> [name]

*/

void upsgi_build_plugin(char *directory) {

	if (!upsgi_file_exists(UPSGI_BUILD_DIR)) {
		if (mkdir(UPSGI_BUILD_DIR, S_IRWXU) < 0) {
        		upsgi_error("upsgi_build_plugin()/mkdir() " UPSGI_BUILD_DIR "/");
			_exit(1);
		}
	}

	char *dot_h = upsgi_get_dot_h();
	if (!dot_h) {
		upsgi_log("unable to generate upsgi.h");
		_exit(1);
	}

	if (strlen(dot_h) == 0) {
		free(dot_h);
		upsgi_log("invalid upsgi.h");
		_exit(1);
	}

	int dot_h_fd = open(UPSGI_BUILD_DIR "/upsgi.h", O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
	if (dot_h_fd < 0) {
		upsgi_error_open(UPSGI_BUILD_DIR "/upsgi.h");
		free(dot_h);
		_exit(1);
	}

	ssize_t dot_h_len = (ssize_t) strlen(dot_h);
	if (write(dot_h_fd, dot_h, dot_h_len) != dot_h_len) {
		upsgi_error("upsgi_build_plugin()/write()");
		_exit(1);
	}

	char *config_py = upsgi_get_config_py();
        if (!config_py) {
                upsgi_log("unable to generate upsgiconfig.py");
                _exit(1);
        }

        if (strlen(config_py) == 0) {
                upsgi_log("invalid upsgiconfig.py");
                _exit(1);
        }

        int config_py_fd = open(UPSGI_BUILD_DIR "/upsgiconfig.py", O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
        if (config_py_fd < 0) {
                upsgi_error_open(UPSGI_BUILD_DIR "/upsgiconfig.py");
                _exit(1);
        }

        ssize_t config_py_len = (ssize_t) strlen(config_py);
        if (write(config_py_fd, config_py, config_py_len) != config_py_len) {
                upsgi_error("upsgi_build_plugin()/write()");
                _exit(1);
        }

	char *cflags = upsgi_get_cflags();
	if (!cflags) {
		upsgi_log("unable to find cflags\n");
		_exit(1);
	}
	if (strlen(cflags) == 0) {
		upsgi_log("invalid cflags\n");
		_exit(1);
	}

	if (setenv("UPSGI_PLUGINS_BUILDER_CFLAGS", cflags, 1)) {
		upsgi_error("upsgi_build_plugin()/setenv()");
		_exit(1);
	}
	
	// now run the python script
	char *argv[6];

	argv[0] = getenv("PYTHON");
	if (!argv[0]) argv[0] = "python3";

	argv[1] = UPSGI_BUILD_DIR "/upsgiconfig.py";
	argv[2] = "--extra-plugin";
	char *space = strchr(directory, ' ');
	if (space) {
		*space = 0;
		argv[3] = directory;
                argv[4] = space+1;
		argv[5] = NULL;
	}
	else {
		argv[3] = directory;
		argv[4] = NULL;
	}

	execvp(argv[0], argv);
	// never here...	
	_exit(1);
}
