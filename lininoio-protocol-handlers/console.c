#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "logger.h"
#include "lininoio-proto-handler.h"
#include "remoteproc.h"
#include "fd_event.h"

struct console_channel_resources {
	struct fw_rsc_hdr h;
	struct fw_rsc_vdev vdev;
	struct fw_rsc_vdev_vring vring1;
	struct fw_rsc_vdev_vring vring2;
	uint8_t config_space[16];
} __attribute__((packed));

struct console_channel {
	/* Metti i dati privati qui */

	struct console_channel_resources res;
};

/*
 * A new node has been connected: setup a fw resource for a console channel
 */
static int lininoio_console_connect(struct lininoio_channel *c,
				    struct lininoio_node *n)
{
	//unsigned short port;
	struct console_channel *cc = malloc(sizeof(*cc));
	struct console_channel_resources *ccr;

	if (!cc) {
		pr_err("%s: malloc(): %s\n", __func__, strerror(errno));
		return -1;
	}
	memset(cc, 0, sizeof(*cc));
	c->priv = cc;
	pr_info("New lininoio console channel, node %s, core %u\n",
		n->name, c->core_id);
	ccr = &cc->res;
	/* RSC_VDEV */
	ccr->h.type = RSC_VDEV;
	ccr->vdev.id = VIRTIO_ID_RPROC_SERIAL;
	ccr->vdev.dfeatures = 0;
	ccr->vdev.config_len = sizeof(ccr->config_space);
	ccr->vdev.num_of_vrings = 2;
	ccr->vring1.align = 16;
	ccr->vring1.num = 4;
	ccr->vring2.align = 16;
	ccr->vring2.num = 4;
	c->resources = &ccr->h;
	c->resources_len = sizeof(*ccr);
	/* ARRIVATO QUI */
	return 0;
}

/*
 * A packet is coming from the node, get it and send it through the virtqueue
 */
static void
lininoio_console_inbound_packet(struct lininoio_channel *c,
				const struct lininoio_data_packet *p)
{
	//struct console_channel *cc = c->priv;
	uint16_t len = lininoio_decode_cdlen(p->cdlen, NULL);
	//struct console_client *client;

	if (!c->priv) {
		pr_err("%s: channel private data pointer is NULL\n", __func__);
		return;
	}
	/* SEND */
	pr_debug("%s, len = %u\n", __func__, len);
}

static void lininoio_console_disconnect(struct lininoio_channel *c,
					struct lininoio_node *n)
{
	struct console_channel *cc = c->priv;

	/* Stop and delete the virtqueue ? */
	free(cc);
	c->priv = NULL;
}

static const struct lininoio_proto_ops console_ops = {
	.connect = lininoio_console_connect,
	.inbound_packet = lininoio_console_inbound_packet,
	.disconnect = lininoio_console_disconnect,
};

static const struct lininoio_proto_handler_plugin_data plugin_data = {
	.ops = &console_ops,
	.proto_id = LININOIO_PROTO_CONSOLE,
};

DECLARE_LININOIO_PROTO_HANDLER(mcuio, "console protocol handler", &plugin_data);
