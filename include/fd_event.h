#ifndef __FD_EVENT_H__
#define __FD_EVENT_H__

#include <sys/select.h>

#include "list.h"

enum fd_event_type {
	EVT_FD_RD = 0,
	EVT_FD_WR = 1,
	EVT_FD_EXC = 2,
};

struct fd_event ;

typedef void (*fd_event_cb)(void *);

extern struct fd_event *add_fd_event(int fd, enum fd_event_type t,
				     fd_event_cb cb, void *cb_data);

extern int cancel_fd_event(struct fd_event *);

extern void handle_fd_events(fd_set *rd, fd_set *wr, fd_set *exc);

extern void prepare_fd_events(fd_set *rd, fd_set *wr, fd_set *exc, int *max_fd);

extern int fd_events_init(void);

extern int fd_event_get_fd(struct fd_event *);

#endif /* __FD_EVENT_H__ */
