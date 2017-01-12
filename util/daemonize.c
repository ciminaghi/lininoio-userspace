/* daemonize function, code comes from gpsd-2.33, gpsd.c */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

extern int daemonize(const char *opt_pid_file_path)
{
	int fd, i;
	FILE *pidfile;
	pid_t pid;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:     /* child side */
		break;
	default:    /* parent side */
		if(!(pidfile = fopen(opt_pid_file_path, "w+"))) {
			fprintf(stderr, "Could not open pid file\n");
			exit(1);
		}
		fprintf(pidfile, "%d", pid);
		fclose(pidfile);
		exit(0);
	}

	if (setsid() == -1)
		return -1;
	(void)chdir("/");
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > 2)
			(void)close(fd);
	}
	for(i=3; i<255; i++)
		close(i);
	return 0;
}
