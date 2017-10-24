
/*
 * Lininoio-over-ethernet linux userspace implementation
 * GPL v2 or later
 * Copyright Dog Hunter 2016
 * Author Davide Ciminaghi 2016
 */


#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <endian.h>
#include <libudev.h>
#include <linux/if_packet.h>
#include <linux/if.h>
#include <linux/r2proc_ioctl.h>
#include <linux/virtio_ring.h>
#include "logger.h"
#include <net/ethernet.h> /* ETHER_ADDR_LEN */
#include "list.h"
#include "fd_event.h"
#include "lininoio-ether.h"
#include "lininoio-internal.h"
#include "udev-events.h"
#include "timeout.h"

#define DEFAULT_ALIVE_TIMEOUT 2000

/* FIXME: THESE SHOULD BE EXPORTED BY THE KERNEL */
#define RVDEV_NUM_VRINGS 2
#define RVDEV_SHIFT 2
#define MAX_RVDEVS_PER_RPROC (1 << RVDEV_SHIFT)

#ifndef FIRMWARE_PREFIX
#define FIRMWARE_PREFIX ""
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE getpagesize()
#endif

#ifndef BACKEND_MEM_SIZE
#define BACKEND_MEM_SIZE (1024*1024)
#endif

struct ether_data {
	struct list_head free_nodes;
	struct list_head nodes;
	struct sockaddr_ll addr;
	int bus_id;
	int curr_dev;
	int netif_fd;
	struct fd_event *rx_event;
};

struct lininoio_ether_node {
	struct lininoio_node node;
	struct sockaddr_ll addr;
	struct ether_data *ether_data;
};

struct virtio_backend {
	char devname[PATH_MAX];
	int fd;
	struct fd_event *evt;
	void *vring_ptr;
	unsigned long phy_offset;
	int minor;
	int vring_index;
	struct lininoio_core *core;
	struct lininoio_channel *channel;
};

#define to_ether_node(n) container_of(n, struct lininoio_ether_node, node)

static int opt_alive_timeout = DEFAULT_ALIVE_TIMEOUT;

static void start_cb(void *_cb_data)
{
	struct lininoio_core *c = _cb_data;
	int stat;
	uint64_t v;

	pr_info("%s\n", __func__);
	stat = read(c->pd.start_fd, &v, sizeof(v));
	if (stat < 0) {
		pr_err("read: %s\n", strerror(errno));
		return;
	}
}

static void stop_cb(void *_cb_data)
{
	struct lininoio_core *c = _cb_data;
	int stat;
	uint64_t v;

	pr_info("%s\n", __func__);
	stat = read(c->pd.stop_fd, &v, sizeof(v));
	if (stat < 0) {
		pr_err("read: %s\n", strerror(errno));
		return;
	}
}

static inline int _assign_fd_evt(int fd, fd_event_cb cb,
				 struct lininoio_core *c)
{
	if (fd < 0 || !cb || !c)
		return -1;
	return add_fd_event(fd, EVT_FD_RD, cb, c) ? fd : -1;
}
	
/* Better way to do this ? Probably ... */
static struct lininoio_node *mac_to_node(struct ether_data *data,
					 const uint8_t *mac)
{
	struct lininoio_node *n;

	list_for_each_entry(n, &data->nodes, list) {
		struct lininoio_ether_node *en = to_ether_node(n);\

		if (!memcmp(en->addr.sll_addr, mac, sizeof(en->addr.sll_addr)))
			return n;
	}
	return NULL;
}

static int setup_remoteproc_fw(struct lininoio_core *c, char *firmware_name)
{
	struct lininoio_channel *ch;
	int fd, i;
	struct r2p_simple_firmware *r2p_hdr;
	struct resource_table *rt;
	struct fw_rsc_hdr *r;
	char firmware_path[PATH_MAX];
	void *ptr;

	/*
	 * Firmware table
	 *
	 * r2p header            -------> magic (u32)
	 *                                length (u32)
	 * struct resource_table -------> ver (u32)
	 *                                num (u32)
	 *                                reserved[2] (u32)
	 *                                offset[nchannels] (u32)
	 * =============== resource0 ==========================
	 * struct fw_rsc_hdr     -------> type (u32)
	 * .........................................
	 * ====================================================
	 * ================ resource 1 ========================
	 * ....
	 * ====================================================
	 *
	 * ....
	 *
	 *
	 * ================ resource nchannels - 1 ============
	 * ....
	 * ====================================================
	 */
	if (!c->rproc_name[0]) {
		pr_err("%s: no name for core, giving up\n", __func__);
		return -1;
	}
	snprintf(firmware_name, PATH_MAX - 1,
		 FIRMWARE_PREFIX "%s-fw", c->rproc_name);
	snprintf(firmware_path, PATH_MAX - 1, "/lib/firmware/%s",
		 firmware_name);
	pr_info("creating file %s\n", firmware_name);
	fd = open(firmware_path, O_RDWR | O_CREAT | O_TRUNC,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd < 0) {
		pr_err("%s: could not open firmware file %s: %s\n",
		       __func__, firmware_name, strerror(errno));
		return -1;
	}
	ptr = malloc(PAGE_SIZE);
	if (!ptr) {
		pr_err("%s: malloc error %s\n", __func__, strerror(errno));
		return -1;
	}
	memset(ptr, 0, PAGE_SIZE);
	r2p_hdr = ptr;
	r2p_hdr->magic = R2P_SIMPLE_FIRMWARE_MAGIC;
	/* Resource table comes right after r2p header */
	rt = ptr + sizeof(*r2p_hdr);
	rt->ver = 1;
	rt->num = c->nchannels;
	r = (void *)rt + sizeof(*rt) + c->nchannels * sizeof(uint32_t);
	i = 0;
	list_for_each_entry(ch, &c->channels, list) {
		rt->offset[i++] = (void *)r - (void *)rt;
		memcpy(r, ch->resources, ch->resources_len);
		r = (void *)r + ch->resources_len;
	}
	r2p_hdr->len = (void *)r - (void *)rt;
	write(fd, ptr, (void *)r - ptr);
	close(fd);
	
	return 0;
}

static void virtio_backend_readable(void *_vbe)
{
	struct virtio_backend *vbe = _vbe;
	struct vring_desc *desc = vbe->vring_ptr;
	unsigned long offset;
	char *ptr;

	pr_debug("%s is readable\n", vbe->devname);
	/*
	 * desc->addr contains a phy address, get the offset from the
	 * beginning of the r2proc reserved memory (mmap is done starting from
	 * there);
	 */
	offset = desc->addr - vbe->phy_offset;
	ptr = vbe->vring_ptr + offset;
	pr_debug("read --- %s\n", ptr);
	/* GET BUFFER AND SEND IT TO THE OTHER END !!!! */
}

/* Match a backend with the relevant channel */
static void match_backend(struct virtio_backend *vbe)
{
	int rvdev_index;
	struct lininoio_node *n = vbe->core->node;
	
#if RVDEV_NUM_VRINGS != 2
#error RVDEV_NUM_VRINGS MUST BE 2 AT THE MOMENT
#endif
	rvdev_index = vbe->minor >> 1;
	vbe->vring_index = vbe->minor & 0x1;
	vbe->channel = n->channels[rvdev_index];
}

static void virtio_backend_add(struct udev_device *dev,
			       const char *path, void *priv)
{
	const char *basename = rindex(path, '/') + 1;
	int fd;
	struct virtio_backend *vbe;
	const char *offs;

	if (!basename) {
		pr_err("%s: invalid path %s\n", __func__, path);
		return;
	}
	vbe = malloc(sizeof(*vbe));
	if (!vbe) {
		pr_err("%s: malloc(): %s", __func__, strerror(errno));
		return;
	}
	offs = udev_device_get_sysattr_value(dev, "phy_offset");
	if (!offs) {
		pr_err("%s: could not find phy_offset attribute\n",
		       __func__);
		free(vbe);
		return;
	}
	vbe->phy_offset = strtoul(offs, NULL, 16);
	snprintf(vbe->devname, sizeof(vbe->devname) - 1, "/dev/%s", basename);
	fd = open(vbe->devname, O_RDWR);
	if (fd < 0) {
		pr_err("%s: open(): %s", __func__, strerror(errno));
		free(vbe);
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
	vbe->vring_ptr = mmap(NULL, BACKEND_MEM_SIZE, PROT_READ, MAP_SHARED,
			      fd, 0);
	if (vbe->vring_ptr == MAP_FAILED)
		pr_err("%s: mmap(): %s", __func__, strerror(errno));
	pr_debug("%s: mapped vring (%s) to %p\n", __func__,
		 vbe->devname, vbe->vring_ptr);
	vbe->minor = minor(udev_device_get_devnum(dev));
	vbe->core = priv;
	match_backend(vbe);
}

static void setup_remoteproc(struct lininoio_core *c)
{
	char *firmware_name;
	int fd = open(R2PROC_MISC_DEV, O_RDWR);

	if (fd < 0) {
		pr_err("%s, open(): %s\n", __func__, strerror(errno));
		return;
	}
	firmware_name = malloc(PATH_MAX);
	if (!firmware_name) {
		pr_err("%s: malloc(): %s\n", __func__, strerror(errno));
		goto err0;
	}
	if (setup_remoteproc_fw(c, firmware_name) < 0)
		goto err1;
	c->pd.fw_type = R2P_FW_SIMPLE;
	strncpy((char *)c->pd.fw_name, firmware_name,
		sizeof(c->pd.fw_name) - 1);
	strncpy((char *)c->pd.name, c->rproc_name, sizeof(c->pd.name) - 1);
	c->pd.start_fd = _assign_fd_evt(eventfd(0, 0), start_cb, c);
	c->pd.stop_fd = _assign_fd_evt(eventfd(0, 0), stop_cb, c);
	/* FIXME: calculate this ? */
	c->pd.reserved_memsize = BACKEND_MEM_SIZE;
	if (schedule_udev_event(udev_new_virtio_backend,
				virtio_backend_add, c) < 0) {
		pr_err("Error setting up udev event\n");
		goto err2;
	}
	if (ioctl(fd, R2P_ADD_PROC, &c->pd) < 0) {
		pr_err("%s, ioctl(): %s\n", __func__, strerror(errno));
		goto err2;
	}
	return;

err2:
	unlink(firmware_name);
err1:
	free(firmware_name);
err0:
	close(fd);
}

/*
 * Setup all the remote processors related to node @n
 */
static int setup_remoteprocs(struct lininoio_node *n)
{
	int i;

	for (i = 0; i < LININOIO_MAX_NCORES; i++) {
		if (!n->cores[i])
			break;
		setup_remoteproc(n->cores[i]);
	}
	return 0;
}

/*
 * Kill all remote processors related to node @n
 */
static void kill_remoteprocs(struct lininoio_node *n)
{
}


/* This assumes timeout does not need to be canceled */
static void kill_node(struct timeout *t, void *_node)
{
	struct lininoio_node *node = _node;
	struct lininoio_ether_node *en = to_ether_node(node);
	struct ether_data *data = en->ether_data;
	struct lininoio_channel *c;
	int i;

	kill_remoteprocs(node);
	for (i = 0; i < ARRAY_SIZE(node->channels); i++) {
		c = node->channels[i];
		if (!c)
			break;
		if (c->ops->disconnect)
			c->ops->disconnect(c, node);
		free(c);
	}
	list_move(&node->list, &data->free_nodes);
}

static struct lininoio_node *find_node(struct ether_data *data,
				       const struct sockaddr_ll *from)
{
	struct lininoio_node *ptr;
	
	list_for_each_entry(ptr, &data->nodes, list) {
		if (!memcmp(from, &to_ether_node(ptr)->addr, sizeof(*from)))
			return ptr;
	}
	return NULL;
}

static struct lininoio_node *get_node(struct ether_data *data,
				      const struct sockaddr_ll *from)
{
	struct lininoio_node *out = NULL;
	struct lininoio_ether_node *en;

	if (list_empty(&data->free_nodes))
		return out;
	out = list_first_entry(&data->free_nodes, struct lininoio_node, list);
	out->name[0] = 0;
	out->alive_to = NULL;
	memset(out->cores, 0, sizeof(out->cores));
	out->nchannels = 0;
	out->ll_data = NULL;
	out->send_packet = NULL;
	memset(out->channels, 0, sizeof(out->channels));
	list_move(&out->list, &data->nodes);
	en = to_ether_node(out);
	en->ether_data = data;
	en->addr = *from;
	return out;
}

static int ether_send_areply(struct lininoio_node *node, int stat)
{
	int i, ret;
	struct lininoio_areply_packet p = {
		.type = htole32(LININOIO_PACKET_AREPLY),
		.status = stat,
	};
	struct msghdr mhdr;
	/* We need a vec for the packet header, then 1 vec for each channel */
	struct iovec *vecs = malloc(sizeof(struct iovec) *
				    (1 + node->nchannels));
	struct lininoio_ether_node *en = to_ether_node(node);
	struct ether_data *data = en->ether_data;
	struct lininoio_channel *c;

	if (!vecs) {
		pr_err("%s: malloc(): %s\n", __func__, strerror(errno));
		return -1;
	}
	mhdr.msg_name = &en->addr;
	mhdr.msg_namelen = sizeof(en->addr);
	mhdr.msg_iov = vecs;
	mhdr.msg_iovlen = 1 + node->nchannels;
	mhdr.msg_control = NULL;
	mhdr.msg_controllen = 0;
	mhdr.msg_flags = 0;
	i = 0;
	vecs[i].iov_base = &p;
	vecs[i++].iov_len = sizeof(p);
	printf("pippo stat = %d\n", stat);
	for (c = node->channels[i - 1]; c; i++) {
		vecs[i].iov_base = c->adata;
		vecs[i++].iov_len =
			lininoio_decode_cdlen(c->adata->chan_dlen, NULL) +
			sizeof(c->adata->chan_dlen);
		printf("pluto i = %d\n", i);
	}
	ret = sendmsg(data->netif_fd, &mhdr, 0);
	free(vecs);
	return ret;
}

static struct lininoio_core *get_core(struct lininoio_node *n,
				      int core_id)
{
	struct lininoio_core *core = n->cores[core_id];
	
	if (core)
		return core;
	core = malloc(sizeof(*core));
	if (!core) {
		pr_err("allocating new core: %s\n", strerror(errno));
		return core;
	}
	core->nchannels = 0;
	INIT_LIST_HEAD(&core->channels);
	core->node = n;
	snprintf(core->rproc_name, sizeof(core->rproc_name) - 1, "%s-%d",
		 n->name, core_id);
	n->cores[core_id] = core;
	return core;
}

static struct lininoio_channel *new_channel(struct lininoio_node *n,
					    int chan_id,
					    uint16_t cdescr)
{
	uint8_t core_id = lininoio_cdescr_to_core_id(cdescr);
	struct lininoio_core *core;
	struct lininoio_channel *out = NULL;

	core = get_core(n, core_id);
	if (!core)
		return out;
	out = malloc(sizeof(*out));
	if (!out) {
		pr_err("allocating new channel: %s\n", strerror(errno));
		return out;
	}
	list_add_tail(&out->list, &core->channels);
	core->nchannels++;
	return out;
}

static void ether_rx_arequest(const struct sockaddr_ll *from,
			      const struct lininoio_arequest_packet *packet,
			      int len, struct ether_data *data)
{
	struct lininoio_node *n;
	int i, stat;

	if (packet->nchannels > LININOIO_MAX_NCHANNELS) {
		pr_err("%s: new node with invalid number of channels\n",
		       __func__);
		return;
	}
	n = find_node(data, from);
	if (n) {
		pr_err("%s: association request from an already associated node\n", __func__);
		/* FIXME: SEND POSITIVE AREPLY AND DO NOTHING ELSE */
		ether_send_areply(n, 0);
		return;
	}
	
	/* Create new node and add it to list */
	n = get_node(data, from);
	if (!n) {
		pr_err("%s: no more free nodes\n", __func__);
		/* Silently ignore the request */
		return;
	}
	n->nchannels = packet->nchannels;
	n->alive_to = schedule_timeout(opt_alive_timeout, kill_node, n);
	pr_info("Association request received from %s (%02x:%02x:%02x:%02x:%02x:%02x), %d channels, alive timeout = %d\n",
		packet->slave_name,
		from->sll_addr[0],
		from->sll_addr[1],
		from->sll_addr[2],
		from->sll_addr[3],
		from->sll_addr[4],
		from->sll_addr[5],
		n->nchannels,
		n->alive_to);
	list_add_tail(&n->list, &data->nodes);
	strncpy(n->name, (const char *)packet->slave_name,
		sizeof(packet->slave_name));
	for (i = 0, stat = 0; !stat && i < n->nchannels; i++) {
		struct lininoio_channel *c;
		uint16_t cdescr;

		cdescr = le16toh(packet->chan_descr[i]);
		c = new_channel(n, i, cdescr);
		c->protocol = lininoio_cdescr_to_proto_id(cdescr);
		c->core_id = lininoio_cdescr_to_core_id(cdescr);
		c->id = i;
		c->null_adata.chan_dlen = lininoio_encode_cdlen(0, c->id);
		c->adata = &c->null_adata;
		pr_info("%s: new channel for node %s (core %u, proto 0x%04x)\n",
			__func__, n->name, c->core_id, c->protocol);
		c->ops = lininoio_find_proto_ops(c->protocol);
		if (!c->ops)
			pr_info("%s: warning: no protocol operations "
				"for this node\n");
		/* connect also sets up association data for this channel */
		if (c->ops && c->ops->connect) {
			stat = c->ops->connect(c, n);
			if (stat)
				pr_err("%s: connect returns error\n", __func__);
		}
	}
	if (n->nchannels <= 0) {
		pr_err("Slave %s has no channels !\n", n->name);
		stat = -EINVAL;
	}
	if (!stat)
		stat = setup_remoteprocs(n);
	if (stat)
		pr_err("%s: error setting up remoteproc stuff\n", __func__);
	if (ether_send_areply(n, stat) < 0)
		pr_err("%s: error sending association reply\n", __func__);
	if (stat) {
		cancel_timeout(n->alive_to);
		kill_node(n->alive_to, n);
	}
}

static void ether_data_packet(const uint8_t *mac,
			      const struct lininoio_data_packet *dp,
			      struct ether_data *data)
{
	struct lininoio_channel *c;
	uint8_t chan_id;
	struct lininoio_node *node;

	chan_id = lininoio_decode_cdlen(le16toh(dp->cdlen), NULL);
	/* Data to node */
	node = mac_to_node(data, mac);
	if (!node) {
		pr_err("%s: data packet from unknown mac %s\n",
		       __func__, mac);
		return;
	}
	if (chan_id >= LININOIO_MAX_NCHANNELS) {
		pr_err("%s: invalid chan id, ignoring packet\n",
		       __func__);
		return;
	}
	c = node->channels[chan_id];
	if (!c) {
		pr_err("%s: packet to NULL channel, ignoring\n",
		       __func__);
		return;
	}
	cancel_timeout(node->alive_to);
	node->alive_to = schedule_timeout(opt_alive_timeout, kill_node, node);
	if (!c->ops || !c->ops->inbound_packet) {
		pr_debug("%s: no handler for packet\n", __func__);
		return;
	}
	c->ops->inbound_packet(c, dp);
}

static void ether_rx_cb(const struct sockaddr_ll *from,
			const void *p, int len, struct ether_data *data)
{
	const struct lininoio_packet *packet = p;

	switch (packet->type) {
	case LININOIO_PACKET_DATA:
		ether_data_packet(from->sll_addr, p, data);
		break;
	case LININOIO_PACKET_AREQUEST:
		/* Association request */
		ether_rx_arequest(from, p, len, data);
		break;
	default:
		pr_err("%s: unexpected packet type %02x\n", __func__,
		       packet->type);
		break;
	}
}

static void _ether_rx_cb(void *_data)
{
	struct ether_data *data = _data;
	int stat;
	struct sockaddr_ll from;
	socklen_t l = sizeof(from);
	static uint8_t buf[1024];

	stat = recvfrom(data->netif_fd, buf, sizeof(buf), 0,
			(struct sockaddr *)&from, &l);
	if (stat < 0) {
		pr_err("%s, recvfrom: %s\n", __func__, strerror(errno));
		return;
	}
	ether_rx_cb(&from, buf, stat, data);
}

static int setup_ether_socket(const char *ifname, struct ether_data *data)
{
	struct ifreq ifr;
	size_t if_name_len = strlen(ifname);
	int fd;
	struct sockaddr_ll *addr = &data->addr;

	if (if_name_len >= sizeof(ifr.ifr_name)) {
		pr_err("interface name is too long\n");
		return -1;
	}
	fd = socket(AF_PACKET, SOCK_DGRAM, htons(LININOIO_ETH_TYPE));
	if (fd < 0) {		
		pr_err("%s, socket: %s\n", __func__, strerror(errno));
		return -1;
	}
	memcpy(ifr.ifr_name, ifname, if_name_len);
	ifr.ifr_name[if_name_len] = 0;
	if (ioctl(fd,SIOCGIFINDEX,&ifr) == -1) {
		pr_err("%s, ioctl, SIOCGIFINDEX: %s",
		       __func__, strerror(errno));
		close(fd);
		return -1;
	}	
	addr->sll_family = AF_PACKET;
	addr->sll_ifindex = ifr.ifr_ifindex;
	addr->sll_halen = ETHER_ADDR_LEN;
	addr->sll_protocol = htons(LININOIO_ETH_TYPE);
	if (bind(fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
		pr_err("%s, bind: %s\n", __func__, strerror(errno));
		close(fd);
		return -1;
	}
	data->netif_fd = fd;
	data->rx_event = add_fd_event(fd, EVT_FD_RD, _ether_rx_cb, data);
	if (!data->rx_event) {
		pr_err("%s: error in add_fd_event\n", __func__);
		close(fd);
		return -1;
	}
	return 0;
}

int lininoio_ether_init(const char *netif_name)
{
	int ret, i;
	struct ether_data *data;

	data = malloc(sizeof(*data));
	if (!data) {
		pr_err("%s: malloc(): %s\n", __func__, strerror(errno));
		return -1;
	}
	memset(data, 0, sizeof(*data));
	INIT_LIST_HEAD(&data->nodes);
	INIT_LIST_HEAD(&data->free_nodes);
	for (i = 0; i < 7; i++) {
		struct lininoio_ether_node *en = malloc(sizeof(*en));
		struct lininoio_node *n = &en->node;

		if (!n) {
			pr_err("%s, malloc: %s\n", __func__, strerror(errno));
			return -ENOMEM;
		}
		memset(en, 0, sizeof(*en));
		list_add_tail(&n->list, &data->free_nodes);
	}

	ret = setup_ether_socket(netif_name, data);
	if (ret < 0)
		return ret;
	
	return ret;
}

void ether_exit(struct ether_data *data)
{
	free(data);
}
