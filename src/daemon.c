#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>

static volatile int daemon_stop = 0;

static void daemon_catch_sigterm(int signum)
{
	daemon_stop = 1;
}

int daemonize(int (*daemon)(volatile int *stop, int argc, char *argv[]),
		const char *name,
		int argc, char *argv[])
{
	int result;
	pid_t pid;
	int pidfl_fd;
	char *pidfl_path;

	if ((pid = fork()) < 0) {
		return -1;
	} else if (pid) {
		return 0;
	}

	if (setsid() < 0) {
		return -1;
	}

	signal(SIGTERM, daemon_catch_sigterm);

	if ((pid = fork()) < 0) {
		return -1;
	} else if (pid) {
		return 0;
	}

	umask(0);

	if (chdir("/") < 0) {
		return -1;
	}

	for (int i = 0; i < sysconf(_SC_OPEN_MAX); i++) {
		close(i);
	}
	
	if (!(pidfl_path = malloc(strlen(name) + 13))) {
		return -1;
	}
	sprintf(pidfl_path, "/var/run/%s.pid", name);
	if ((pidfl_fd = open(pidfl_path, O_WRONLY | O_CREAT | O_EXCL,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
		free(pidfl_path);
		return -1;
	}
	if (dprintf(pidfl_fd, "%d", getpid()) < 0) {
		close(pidfl_fd);
		unlink(pidfl_path);
		free(pidfl_path);
		return -1;
	}
	close(pidfl_fd);

	openlog(name, LOG_PID, LOG_DAEMON);

	result = daemon(&daemon_stop, argc, argv);

	closelog();

	unlink(pidfl_path);
	free(pidfl_path);

	return result;
}

