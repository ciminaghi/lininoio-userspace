/*
 * Copyright 2015 Davide Ciminaghi <ciminaghi@gnudd.com>
 * Copyright 2016 Dog Hunter
 *
 * GNU GPLv2 or later
 */

/*
 * etherd, user space daemon for ethernet or wifi based mcuio links
 *
 * Implements the lininoio protocol over ethernet as described in
 * Documentation/ether.txt
 * Talks to lininoio protocol handlers, which in turn talk to kernel space,
 * Proto handlers are shared libraries.
 *
 * +----------------------------------------------------+
 * | +----------+      +---------+      +----------+    |
 * | | proto    |      |         |      |          |    |
 * | | handler  |<---->| core    |<....>| netif    |    |
 * | |          |      |         |      |          |    |
 * | +----------+      +---------+      +----------+    |
 * |                                           etherd   |
 * +----------------------------------------------------+
 *
 */
#include <stdio.h>
#include <fcntl.h>
#include <pty.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <pty.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/tty.h>
#include "util.h"
#include "logger.h"
#include "plugin.h"
#include "fd_event.h"
#include "lininoio-internal.h"
#include "lininoio-ether.h"
#include "timeout.h"
#include "udev-events.h"

#define DEFAULT_VERBOSE 0
#define DEFAULT_PID_FILE_PATH "/var/run/etherd.pid"
#define DEFAULT_DONT_DAEMONIZE 0
#define DEFAULT_LOG_TO_STDERR 0


enum opt_index {
	HELP_OPT_INDEX = 0,
	VERBOSE_OPT_INDEX,
	DONT_DAEMONIZE_OPT_INDEX,
	PID_FILE_PATH_OPT_INDEX,
	LOG_TO_STDERR_OPT_INDEX,
};

static int opt_verbose = DEFAULT_VERBOSE;
static const char *opt_pid_file_path = DEFAULT_PID_FILE_PATH ;
static int opt_dont_daemonize = DEFAULT_DONT_DAEMONIZE ;
static int opt_log_to_stderr = DEFAULT_LOG_TO_STDERR;

static const char *netif;

static void help(int argc, char *argv[])
{
	fprintf(stderr, "Use %s [generic_options] [network_interface]\n", argv[0]);
	fprintf(stderr, "network_interface is the name of the involved network interface\n");
	fprintf(stderr, "Valid generic_options are:\n");
	fprintf(stderr, "\t-h|--help: print this help and exit\n");
	fprintf(stderr, "\t-v|--verbose: verbose operation\n");
	fprintf(stderr, "\t-D|--dont-daemonize: do not fork and become a daemon "
		"(default %d)\n", DEFAULT_DONT_DAEMONIZE);
	fprintf(stderr, "\t-p|--pid-file: path of pid file (default %s)\n",
		DEFAULT_PID_FILE_PATH);
	fprintf(stderr, "\t-E|--log-to-stderr: log to stderr "
		"(default is syslog)\n");
}


static int parse_cmdline(int argc, char *argv[])
{
	int opt;
	char *opts = "hvDp:E";
	struct option long_options[] = {
		[HELP_OPT_INDEX] = {
			.name = "help",
			.has_arg = 0,
			.flag = NULL,
			.val = HELP_OPT_INDEX,
		},
		[VERBOSE_OPT_INDEX] = {
			.name = "verbose",
			.has_arg = 0,
			.flag = NULL,
			.val = VERBOSE_OPT_INDEX,
		},
		[DONT_DAEMONIZE_OPT_INDEX] = {
			.name = "dont-daemonize",
			.has_arg = 0,
			.flag = NULL,
			.val = DONT_DAEMONIZE_OPT_INDEX,
		},
		[PID_FILE_PATH_OPT_INDEX] = {
			.name = "pid-file",
			.has_arg = 1,
			.flag = NULL,
			.val = PID_FILE_PATH_OPT_INDEX,
		},
		[LOG_TO_STDERR_OPT_INDEX] = {
			.name = "log-to-stderr",
			.has_arg = 0,
			.flag = NULL,
			.val = LOG_TO_STDERR_OPT_INDEX,
		},
	};
	while ((opt = getopt_long(argc, argv, opts, long_options,
				  NULL)) != -1) {
		switch(opt) {
		case HELP_OPT_INDEX:
		case 'h':
			help(argc, argv); exit(0);
		case VERBOSE_OPT_INDEX:
		case 'v':
			opt_verbose = 1 ; break;
		case DONT_DAEMONIZE_OPT_INDEX:
		case 'D':
			opt_dont_daemonize = 1; break;
		case PID_FILE_PATH_OPT_INDEX:
		case 'p':
			opt_pid_file_path = optarg; break;
		case LOG_TO_STDERR_OPT_INDEX:
		case 'E':
			opt_log_to_stderr = 1; break;
		default:
			help(argc, argv);
			break;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "interface name is missing\n");
		help(argc, argv);
		exit(127);
	}
	/* First non-option arg is network interface's name */
	netif = argv[optind++];
	return 0;
}

int main(int argc, char *argv[])
{
	int stat, nfds, max_fd;
	FILE *logf = NULL;

	stat = parse_cmdline(argc, argv);
	if (stat < 0)
		exit(1);

	if (opt_log_to_stderr)
		logf = stderr;

	if (!opt_dont_daemonize)
		daemonize(opt_pid_file_path);

	logger_init(logf, "etherd");

	if (fd_events_init() < 0) {
		pr_err("Error initializing events\n");
		exit(129);
	}
	if (timeouts_init() < 0) {
		pr_err("Error initializing timeouts\n");
		exit(129);
	}
	if (udev_events_init() < 0) {
		pr_err("Error initializing udev events\n");
		exit(131);
	}

	if (lininoio_init() < 0) {
		pr_err("Error in lininoio initialization\n");
		exit(130);
	}
	//lininoio_ether_init(netif, argc - optind, &argv[optind]);
	lininoio_ether_init(netif);
	
	while (1) {
		fd_set fds;

		FD_ZERO(&fds);
		prepare_fd_events(&fds, NULL, NULL, &max_fd);
		nfds = max_fd + 1;
		switch (select(nfds, &fds, NULL, NULL, get_next_timeout())) {
		case 0:
			handle_timeouts();
			break;
		case -1:
			pr_err("etherd main, select: %s\n", strerror(errno));
			break;
		default:
			handle_fd_events(&fds, NULL, NULL);
			break;
		}
	};
	return 0;
}
