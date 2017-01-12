/*
 * Copyright 2014 Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 */

/* Util functions for user space mcuio daemons */
extern int daemonize(const char *opt_pid_file_path);

extern int send_file_descriptor(int socket, void *buf, int len, int fd);
extern int recv_fd(int socket, void *buffer, int *len, const char *sig,
		   int siglen);
