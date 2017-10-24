/*
 * mcuio userspace library - virtio utility functions
 *
 * Copyright Davide Ciminaghi 2017
 * GPLv2
 */
#ifndef __VIRTIO_H__
#define __VIRTIO_H__

struct virtio_backend {
	char devname[PATH_MAX];
	int fd;
	struct fd_event *evt;
	void *vring_ptr;
	unsigned long phy_offset;
	size_t phy_len;
};

void virtio_backend_add(struct udev_device *dev, const char *path, void *priv);

#endif
