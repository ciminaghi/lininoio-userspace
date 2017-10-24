/*
 * Copyright 2017 Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * userspace utility library for lininoio, virtio related stuff
 * GNU GPLv2 or later
 */
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <strings.h>
#include <libudev.h>
#include <linux/r2proc_ioctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include "fd_event.h"
#include "timeout.h"
#include "udev-events.h"
#include "logger.h"
#include "virtqueue.h"
#include "virtio.h"

#include "remoteproc.h"

static void virtio_backend_readable(void *_vbe)
{
	struct virtio_backend *vbe = _vbe;
	struct vring_desc *desc = vbe->vring_ptr;
	unsigned long offset;
	char *ptr;

	pr_debug("%s is readable\n", vbe->devname);
	pr_debug("%s: %s, vring[0] = 0x%08x\n",
		 __func__, vbe->devname, ((unsigned int *)vbe->vring_ptr)[0]);
	pr_debug("%s: %s, addr = 0x%" PRIx64 ", len = %lu, flags = 0x%04x, next = 0x%04x\n", __func__, vbe->devname, desc->addr, (unsigned long)desc->len,
		 desc->flags, desc->next);
	/*
	 * desc->addr contains a phy address, get the offset from the
	 * beginning of the r2proc reserved memory (mmap is done starting from
	 * there);
	 */
	offset = desc->addr - vbe->phy_offset;
	ptr = vbe->vring_ptr + offset;
	pr_debug("read --- %s\n", ptr);
}

static int _create_virtqueue(struct virtio_backend *vbe)
{
	return 0;
}

void virtio_backend_add(struct udev_device *dev, const char *path, void *priv)
{
	const char *basename = rindex(path, '/') + 1;
	int fd;
	struct virtio_backend *vbe;
	const char *offs, *size;

	if (!basename) {
		pr_err("%s: invalid path %s\n", __func__, path);
		return;
	}
	vbe = malloc(sizeof(*vbe));
	if (!vbe) {
		pr_err("%s, malloc: %s", __func__, strerror(errno));
		return;
	}
	offs = udev_device_get_sysattr_value(dev, "phy_offset");
	if (!offs) {
		pr_err("%s: could not find phy_offset attribute\n", __func__);
		return;
	}
	vbe->phy_offset = strtoul(offs, NULL, 16);
	size = udev_device_get_sysattr_value(dev, "phy_len");
	if (!size) {
		pr_err("%s: could not find phy_len attribute\n", __func__);
		return;
	}
	vbe->phy_len = strtoul(size, NULL, 16);
	snprintf(vbe->devname, sizeof(vbe->devname) - 1, "/dev/%s", basename);
	/* Wait for udev to mknod: FIXME: THIS SHOULDN'T BE NEEDED ?? */
	poll(NULL, 0, 10);
	fd = open(vbe->devname, O_RDWR);
	if (fd < 0) {
		perror("open");
		return;
	}
	vbe->fd = fd;
	vbe->evt = add_fd_event(vbe->fd, EVT_FD_RD, virtio_backend_readable,
				vbe);
	if (!vbe->evt) {
		pr_err("%s: error adding virtio backend event\n", __func__);
		close(fd);
		free(vbe);
		return;
	}
	
	vbe->vring_ptr = mmap(NULL, vbe->phy_len, PROT_READ, MAP_SHARED, fd, 0);
	if (vbe->vring_ptr == MAP_FAILED)
		perror("mmap");
	pr_info("%s: mapped vring (%s) to %p\n", __func__, vbe->devname,
		vbe->vring_ptr);

	if (_create_virtqueue(vbe) < 0){
		pr_err("%s: error creating virtqueue\n", __func__);
		munmap(vbe->vring_ptr, vbe->phy_len);
		close(fd);
		free(vbe);
	}
}
