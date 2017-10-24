#ifndef __UDEV_EVENTS_H__
#define __UDEV_EVENTS_H__

#include <limits.h>
#include "list.h"

struct udev_event;
struct udev_device;

typedef void (*uevent_cb)(struct udev_device *dev, const char *path,
			  void *priv);

enum udev_event_id {
	udev_new_virtio_backend = 1,
	udev_del_virtio_backend = 2,
};

extern int schedule_udev_event(enum udev_event_id, uevent_cb cb, void *cb_data);

extern int udev_events_init(void);


#endif /* __UDEV_EVENTS_H__ */

