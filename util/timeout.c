/*
 * Copyright 2014 Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */
#undef DEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/stddef.h>
#include <sys/time.h>

#include "list.h"
#include "logger.h"
#include "timeout.h"

struct timeout {
	struct timeval expires;
	to_handler *handler;
	void *priv;
	struct list_head list;
	int armed;
};

struct list_head timeouts;

int timeouts_init(void)
{
	INIT_LIST_HEAD(&timeouts);
	return 0;
}

#ifdef DEBUG
static void print_timeout(const char *prev, struct timeout *to)
{
	pr_debug("%s ", prev);
	pr_debug("%p, expires %lu.%06u\n", to,
		 (unsigned long)to->expires.tv_sec,
		 (unsigned int)to->expires.tv_usec);
}
#else
static inline void print_timeout(const char *prev, struct timeout *to)
{
}
#endif

struct timeout *schedule_timeout(unsigned long ms, to_handler *toh,
				 void *priv)
{
	struct timeout *to, *ptr;

	if (!toh)
		/* An handler is mandatory */
		return NULL;

	to = malloc(sizeof(struct timeout));
	if (!to) {
		perror("allocating timeout structure");
		return NULL;
	}
	to->expires.tv_sec = ms/1000;
	to->expires.tv_usec = (ms*1000)%1000000;
	to->handler = toh;
	to->priv = priv;
	to->armed = 1;

	print_timeout("\tInserting ", to);

	if (list_empty(&timeouts)) {
		print_timeout("List empty, inserted ", to);
		list_add_tail(&to->list, &timeouts);
		return to;
	}
	list_for_each_entry(ptr, &timeouts, list) {
		if (ptr->armed)
			print_timeout("ptr = ", ptr);
		if(timercmp(&to->expires, &ptr->expires, <)) {
			struct timeval __tv;
			timersub(&ptr->expires, &to->expires, &__tv);
			ptr->expires = __tv;
			list_add_tail(&to->list, &ptr->list);
			return to;
		} else {
			struct timeval __tv;
			timersub(&to->expires, &ptr->expires, &__tv);
			to->expires = __tv;
		}
	}
	list_add_tail(&to->list, &ptr->list);
	return to;
}

void cancel_timeout(struct timeout *to)
{
	to->armed = 0;
}

struct timeval *get_next_timeout(void)
{
	struct timeout *to = NULL;
	if (list_empty(&timeouts))
		return NULL;
	to = list_entry(timeouts.next, struct timeout, list);
	return &to->expires;
}

void handle_timeouts(void)
{
	struct timeout *to, *tmp;
	struct timeval zerot = { .tv_sec = 0, .tv_usec = 0 };
	to_handler *h;
	assert(!list_empty(&timeouts));

	list_for_each_entry_safe(to, tmp, &timeouts, list) {
		if (!timercmp(&to->expires, &zerot, ==))
			break;
		list_del_init(&to->list);
		h = to->handler;
		assert(h);
		if (to->armed)
			h(to, to->priv);
		pr_debug("%s: freeing %p\n", __func__, to);
		free(to);
	};
}

#ifdef DEBUG
void print_timeouts(FILE *f)
{
	struct timeout *ptr;
	fprintf(f, "List of scheduled timeouts\n");
	list_for_each_entry(ptr, &timeouts, list)
		fprintf(f, "\to %p, expires %lu.%06u\n", ptr,
			(unsigned long)ptr->expires.tv_sec,
			(unsigned int)ptr->expires.tv_usec);
}
#endif /* DEBUG */
