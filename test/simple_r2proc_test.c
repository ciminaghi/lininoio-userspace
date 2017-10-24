#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
/* HACK */
//typedef unsigned long long __uint128_t;

#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <poll.h>
#include <errno.h>
#include <libudev.h>
#include <linux/r2proc_ioctl.h>
#include <linux/virtio_ring.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include "fd_event.h"
#include "timeout.h"
#include "udev-events.h"
#include "logger.h"
//#include "virtqueue.h"

#include "remoteproc.h"

#define R2PROC_MISC_DEV "/dev/r2proc"
#define R2PROC_TEST_FW  "r2p-test-fw"

struct virtio_backend {
	char devname[PATH_MAX];
	int fd;
	struct fd_event *evt;
	void *vring_ptr;
	unsigned long phy_offset;
};

struct {
	struct r2p_simple_firmware r2p_hdr;
	struct {
		struct resource_table rt;
		uint32_t offset[1];
		struct fw_rsc_hdr h;
		struct fw_rsc_vdev vdev;
		struct fw_rsc_vdev_vring vring1;
		struct fw_rsc_vdev_vring vring2;
		uint8_t config_space[16];
	} body;
} my_table = {
	.r2p_hdr = {
		.magic = R2P_SIMPLE_FIRMWARE_MAGIC,
		.len = sizeof(my_table.body),
	},
	.body = {
		.rt = {
			.ver = 1,
			.num = 1,
		},
		.offset[0] = offsetof(typeof(my_table.body), h),
		/* RSC_VDEV */
		.h.type = 3,
		.vdev = {
			.id = VIRTIO_ID_RPROC_SERIAL,
			.dfeatures = 0,
			.config_len = 16,
			.num_of_vrings = 2,
		},
		.vring1 = {
			.align = 16,
			.num = 4,
		},
		.vring2 = {
			.align = 16,
			.num = 4,
		},
		
	},
};

static int setup_fw(void)
{
	FILE *f;

	f = fopen("/lib/firmware/" R2PROC_TEST_FW, "w");
	if (!f) {
		perror("fopen");
		return -1;
	}
	pr_info("%s: table len = %d, offset[0] = %d, sizeof hdr = %d\n",
		__func__, my_table.r2p_hdr.len, my_table.body.rt.offset[0],
		sizeof(struct fw_rsc_hdr));
	if (fwrite(&my_table, sizeof(my_table), 1, f) < 0) {
		pr_err("fwrite: %s", strerror(errno));
		return -1;
	}
	return fclose(f);
}

struct cb_data {
	struct r2p_processor_data *pd;
	int fd;
};

static void start_cb(void *_cb_data)
{
	struct cb_data *cbd = _cb_data;
	int stat;
	uint64_t v;

	pr_info("%s\n", __func__);
	stat = read(cbd->fd, &v, sizeof(v));
	if (stat < 0) {
		pr_err("read: %s\n", strerror(errno));
		return;
	}
}

static void stop_cb(void *_cb_data)
{
	struct cb_data *cbd = _cb_data;
	int stat;
	uint64_t v;

	pr_info("%s\n", __func__);
	stat = read(cbd->fd, &v, sizeof(v));
	if (stat < 0) {
		pr_err("read: %s\n", strerror(errno));
		return;
	}
}

static inline int _assign_fd_evt(int fd, fd_event_cb cb,
				 struct r2p_processor_data *pd)
{
	struct cb_data *cbd = malloc(sizeof(*cbd));

	if (fd < 0 || !cb || !pd)
		return -1;
	cbd->pd = pd;
	cbd->fd = fd;
	return add_fd_event(fd, EVT_FD_RD, cb, cbd) ? fd : -1;
}

static void virtio_backend_readable(void *_vbe)
{
	struct virtio_backend *vbe = _vbe;
	struct vring_desc *desc = vbe->vring_ptr;
	unsigned long offset;
	char *ptr;

	printf("%s is readable\n", vbe->devname);
	fprintf(stderr, "%s: %s, vring[0] = 0x%08x\n",
		__func__, vbe->devname, ((unsigned int *)vbe->vring_ptr)[0]);
	//fprintf(stderr, "%s: %s, addr = 0x%" PRIx64 ", len = %lu, flags = 0x%04x, next = 0x%04x\n", __func__, vbe->devname, desc->addr, (unsigned long)desc->len,
	//desc->flags, desc->next);
	/*
	 * desc->addr contains a phy address, get the offset from the
	 * beginning of the r2proc reserved memory (mmap is done starting from
	 * there);
	 */
	offset = desc->addr - vbe->phy_offset;
	ptr = vbe->vring_ptr + offset;
	printf("read --- %p\n", ptr);
}

static void virtio_backend_add(struct udev_device *dev,
			       const char *path, void *priv)
{
	const char *basename = rindex(path, '/') + 1;
	int fd;
	struct virtio_backend *vbe;
	const char *offs;

	if (!basename) {
		fprintf(stderr, "%s: invalid path %s\n", __func__, path);
		return;
	}
	vbe = malloc(sizeof(*vbe));
	if (!vbe) {
		perror("malloc");
		return;
	}
	offs = udev_device_get_sysattr_value(dev, "phy_offset");
	if (!offs) {
		fprintf(stderr, "%s: could not find phy_offset attribute\n",
			__func__);
		return;
	}
	vbe->phy_offset = strtoul(offs, NULL, 16);
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
		fprintf(stderr, "Error adding virtio backend event\n");
		close(fd);
		free(vbe);
		return;
	}
	vbe->vring_ptr = mmap(NULL, 1024*1024, PROT_READ, MAP_SHARED, fd, 0);
	if (vbe->vring_ptr == MAP_FAILED)
		perror("mmap");
	fprintf(stderr, "%s: mapped vring (%s) to %p\n", __func__, vbe->devname,
		vbe->vring_ptr);
#if 0	
	vbe->poll_timer = schedule_timeout(1000, vring_poll, vbe);
#endif
}

static int r2proc_fd;

void sigint_handler(int signum)
{
	struct r2p_name n;

	strncpy(n.name, "test", sizeof(n.name));
	if (ioctl(r2proc_fd, R2P_REMOVE_PROC, &n) < 0)
		perror("removing processor");
	exit(0);
}

int main(int argc, char *argv[])
{
	int fd = open(R2PROC_MISC_DEV, O_RDWR);
	struct r2p_processor_data *pd;

	if (fd < 0) {
		perror("opening r2proc ctl device");
		exit(127);
	}
	r2proc_fd = fd;
	logger_init(stderr, "test");
	if (fd_events_init() < 0) {
		pr_err("Error initializing fd events\n");
		exit(127);
	}
	if (timeouts_init() < 0) {
		pr_err("Error initializing timeouts\n");
		exit(127);
	}
	if (udev_events_init() < 0) {
		pr_err("Error initializing udev events\n");
		exit(127);
	}
	pd = malloc(sizeof(*pd));
	if (!pd) {
		pr_err("malloc: %s", strerror(errno));
		exit(127);
	}
	pd->fw_type = R2P_FW_SIMPLE;
	strncpy((char *)pd->fw_name, R2PROC_TEST_FW, sizeof(pd->fw_name));
	strncpy((char *)pd->name, "test", sizeof(pd->name));
	pd->start_fd = _assign_fd_evt(eventfd(0, 0), start_cb, pd);
	pd->stop_fd = _assign_fd_evt(eventfd(0, 0), stop_cb, pd);
	pd->reserved_memsize = 1024*1024;
	
	if (setup_fw() < 0)
		exit(127);

	if (schedule_udev_event(udev_new_virtio_backend,
				virtio_backend_add, NULL) < 0)
		exit(127);
	
	if (ioctl(fd, R2P_ADD_PROC, pd) < 0) {
		pr_err("adding remote processor: %s", strerror(errno));
		exit(127);

	}
	signal(SIGINT, sigint_handler);
	while(1) {
		fd_set fds;
		int nfds, max_fd;

		FD_ZERO(&fds);
		prepare_fd_events(&fds, NULL, NULL, &max_fd);
		nfds = max_fd + 1;
		switch (select(nfds, &fds, NULL, NULL, get_next_timeout())) {
		case 0:
			handle_timeouts();
			break;
		case -1:
			pr_err("main, select: %s\n", strerror(errno));
			break;
		default:
			handle_fd_events(&fds, NULL, NULL);
			break;
		}
	}
	return 0;
}
