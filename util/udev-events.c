
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <libudev.h>
#include "list.h"
#include "logger.h"
#include "fd_event.h"
#include "udev-events.h"

struct udev_event {
	enum udev_event_id id;
	uevent_cb cb;
	void *cb_data;
	struct list_head list;
};

static struct list_head udev_events;

struct udev_action {
	const char *a;
	enum udev_event_id id;
};

static const struct udev_action actions[] = {
	{
		.a = "add",
		.id = udev_new_virtio_backend,
	},
	{
		.a = "remove",
		.id = udev_del_virtio_backend,
	},
	{
		.a = NULL,
	},
};

static int _action_to_id(const char *action, enum udev_event_id *out)
{
	const struct udev_action *ptr;

	for (ptr = actions; ptr->a; ptr++)
		if (!strcmp(action, ptr->a)) {
			*out = ptr->id;
			return 0;
		}
	return -1;
}

static void do_udev_event(void *arg)
{
	struct udev_monitor *mon = arg;
	struct udev_device *dev;
	const char *action, *path;
	enum udev_event_id id;
	struct udev_event *ptr, *tmp;

	dev = udev_monitor_receive_device(mon);
	if (!dev) {
		pr_err("No Device from receive_device(). An error occured.\n");
		return;
	}
	action = udev_device_get_action(dev);
	if (_action_to_id(action, &id) < 0)
		return;
	pr_info("Got Device\n");
	path = udev_device_get_syspath(dev);
	pr_info("   Path: %s\n", path);
	pr_info("   Subsystem: %s\n", udev_device_get_subsystem(dev));
	pr_info("   Devtype: %s\n", udev_device_get_devtype(dev));	
	pr_info("   Action: %s\n", action);
	list_for_each_entry_safe(ptr, tmp, &udev_events, list) {
		if (ptr->id == id) {
			ptr->cb(dev, path, ptr->cb_data);
			//list_del(&ptr->list);
			//free(ptr);
		}
	}
	udev_device_unref(dev);
}

#define SUBSYS "r2proc-backend-devs"
#define DEVTYP "*"

int udev_events_init(void)
{
	struct udev *udev;
	struct udev_monitor *mon;
	int fd;

	udev = udev_new();
	if (!udev) {
		pr_err("%s: udev_new error\n", __func__);
		return -1;
	}
	mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, SUBSYS, NULL);
	udev_monitor_enable_receiving(mon);
	fd = udev_monitor_get_fd(mon);
	add_fd_event(fd, EVT_FD_RD, do_udev_event, mon);
	INIT_LIST_HEAD(&udev_events);
	return 0;
}

int schedule_udev_event(enum udev_event_id id, uevent_cb cb, void *cb_data)
{
	struct udev_event *evt = malloc(sizeof(*evt));

	if (!evt) {
		pr_err("%s: malloc(): %s\n", __func__, strerror(errno));
		return -1;
	}
	evt->id = id;
	evt->cb = cb;
	evt->cb_data = cb_data;
	list_add_tail(&evt->list, &udev_events);
	return 0;
}
