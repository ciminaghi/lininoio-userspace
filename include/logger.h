#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

/*
 * If f is NULL use syslog
 */
extern int logger_init(FILE *f, char *ident);
/*
 * prio is LOG_EMERG, LOG_ALERT, LOG_CRIT, ....
 */
extern int logger_log(int prio, const char *fmt, ...);
extern int logger_log_upto(int prio);
extern int logger_close(void);

#ifndef DEBUG
#define pr_debug(a,...)
#else
#define pr_debug(a,args...) logger_log(LOG_DEBUG, a, ##args)
#endif

#define pr_err(a, args...) logger_log(LOG_ERR, a, ##args)
#define pr_warn(a, args...) logger_log(LOG_WARNING, a, ##args)
#define pr_info(a, args...) logger_log(LOG_INFO, a, ##args)

#endif /* __LOGGER_H__ */
