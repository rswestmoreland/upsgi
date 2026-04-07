/* this is a skeleton to use libupsgi in external projects */

extern char **environ;

int upsgi_init(int, char **, char **);

int main(int argc, char **argv, char **environ) {

	upsgi_init(argc, argv, environ);
}
