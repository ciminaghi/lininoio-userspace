#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/stddef.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#include "list.h"
#include "logger.h"

struct logger {
    FILE *f;
    char *ident;
};

static struct logger *process_logger;

int logger_init(FILE *f, char *ident)
{
    struct logger *out = malloc(sizeof(*out));
    if (!out) {
	perror("allocating memory for logger");
	return -1;
    }
    out->f = f;
    out->ident = ident;
    if (!f)
	openlog(ident, LOG_CONS, LOG_USER);
    process_logger = out;
    return 0;
}

static int __logger_log_file(int prio, const char *fmt, va_list ap)
{
    time_t t;
    char date[100];
    char hostname[100];
    if (!(LOG_MASK(prio) & setlogmask(0)))
	return 0;
    time(&t);
    ctime_r(&t, date);
    gethostname(hostname, sizeof(hostname));
    /* Delete the final '\n' */
    date[strlen(date) - 1] = 0;
    fprintf(process_logger->f, "%s %s %s: ", date, hostname,
	    process_logger->ident);
    vfprintf(process_logger->f, fmt, ap);
    return 0;
}

int logger_log(int prio, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (!process_logger) {
	fprintf(stderr, "LOG WITH NO INITIALIZED LOGGER: ");
	vfprintf(stderr, fmt, ap);
	return 0;
    }
    if (process_logger->f)
	return __logger_log_file(prio, fmt, ap);
    vsyslog(prio, fmt, ap);
    va_end(ap);
    return 0;
}

int logger_log_upto(int prio)
{
    return setlogmask(prio);
}

int logger_close(void)
{
    if (!process_logger)
	return -1;
    if (process_logger->f)
	fclose(process_logger->f);
    else
	closelog();
    free(process_logger);
    return 0;
}
