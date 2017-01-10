
/*
 * Copyright 2014 Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */


#ifndef __TIMEOUT_H__
#define __TIMEOUT_H__

#include <stdio.h>

struct timeout ;

typedef void (to_handler)(struct timeout *t, void *priv);

extern int timeouts_init(void);

extern struct timeout *schedule_timeout(unsigned long ms,
					to_handler *toh, void *priv);
extern void cancel_timeout(struct timeout *);
extern struct timeval *get_next_timeout(void);
extern void handle_timeouts(void);
extern void print_timeouts(FILE *);


#endif /* __TIMEOUT_H__ */


