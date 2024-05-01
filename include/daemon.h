#ifndef DAEMON_H
#define DAEMON_H

int daemonize(int (*daemon)(volatile int *stop, int argc, char *argv[]),
		const char *name,
		int argc, char *argv[]);

#endif

