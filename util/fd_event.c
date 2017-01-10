/*
 * Copyright 2014 Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/stddef.h>
#include <sys/time.h>

#include "list.h"
#include "logger.h"
#include "fd_event.h"

struct fd_event {
	int fd;
	fd_event_cb cb;
	void *data;
	struct list_head list;
};

static struct list_head fd_events[3];

int fd_events_init(void)
{
	enum fd_event_type t;

	for (t = EVT_FD_RD; t <= EVT_FD_EXC; t++)
		INIT_LIST_HEAD(&fd_events[t]);
	return 0;
}

struct fd_event *add_fd_event(int fd, enum fd_event_type t,
			      fd_event_cb cb, void *cb_data)
{
	struct fd_event *out;
	struct list_head *l;

	switch (t) {
	case EVT_FD_RD:
	case EVT_FD_WR:
	case EVT_FD_EXC:
		l = &fd_events[t];
		break;
	default:
		return NULL;
	}

	out = malloc(sizeof(*out));
	if (!out)
		return NULL;
	out->fd = fd;
	out->cb = cb;
	out->data = cb_data;
	list_add_tail(&out->list, l);
	return out;
}


int cancel_fd_event(struct fd_event *e)
{
	list_del(&e->list);
	free(e);
	return 0;
}

static void do_handle_fd_events(fd_set *fds, struct list_head *h)
{
	struct fd_event *e;

	list_for_each_entry(e, h, list) {
		if (FD_ISSET(e->fd, fds))
			e->cb(e->data);
	}
}


void handle_fd_events(fd_set *rd_fds, fd_set *wr_fds, fd_set *exc_fds)
{
	fd_set *fds[] = {
		rd_fds, wr_fds, exc_fds,
	};
	enum fd_event_type t;

	for (t = EVT_FD_RD; t <= EVT_FD_EXC; t++)
		do_handle_fd_events(fds[t], &fd_events[t]);
}

static void do_prepare_fd_events(fd_set *fds, struct list_head *h, int *max_fd)
{
	struct fd_event *e;

	list_for_each_entry(e, h, list) {
		if (e->fd > *max_fd)
			*max_fd = e->fd;
		FD_SET(e->fd, fds);
	}
}

void prepare_fd_events(fd_set *rd_fds, fd_set *wr_fds, fd_set *exc_fds,
		       int *max_fd)
{
	fd_set *fds[] = {
		rd_fds, wr_fds, exc_fds,
	};
	enum fd_event_type t;
	*max_fd = -1;

	for (t = EVT_FD_RD; t <= EVT_FD_EXC; t++)
		do_prepare_fd_events(fds[t], &fd_events[t], max_fd);
}

int fd_event_get_fd(struct fd_event *e)
{
	return e->fd;
}
