
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
#include <linux/if_packet.h>
#include <linux/if.h>
#include "logger.h"
#include "list.h"
#include "fd_event.h"
#include "lininoio-internal.h"
#include "lininoio-proto-handler.h"
#include "timeout.h"

static const struct lininoio_proto_ops **lininoio_ops = NULL;

int lininoio_init(void)
{
	int size = sizeof(struct lininoio_proto_ops *) * (1 << 13);
	lininoio_ops = malloc(size);
	if (!lininoio_ops) {
		pr_err("%s: cannot allocate operations array\n");
		return -1;
	}
	memset(lininoio_ops, 0, size);
	return 0;
}

const struct lininoio_proto_ops *lininoio_find_proto_ops(uint16_t proto_id)
{
	if (!lininoio_ops)
		return NULL;
	if (proto_id >= LININOIO_N_PROTOS)
		return NULL;
	if (!lininoio_ops[proto_id]) {
		/* Handler not found, try loading it */
		struct lininoio_proto_handler *h;

		h = load_lininoio_proto_handler(LIBDIR, proto_id);
		if (!h) {
			pr_err("%s: no handler for proto 0x%04x\n",
			       __func__, proto_id);
			return NULL;
		}
		if (!h->data->ops)
			pr_err("%s: protocol handler with no ops !!\n",
			       __func__);
		lininoio_ops[proto_id] = h->data->ops;
	}
	return lininoio_ops[proto_id];
}

int lininoio_register_proto_handler(uint16_t proto_id,
				    struct lininoio_proto_ops *ops)
{
	if (!lininoio_ops)
		return -1;
	if (proto_id >= LININOIO_N_PROTOS)
		return -1;
	lininoio_ops[proto_id] = ops;
	return 0;
}

int lininoio_send_packet(struct lininoio_node *n,
			 const struct lininoio_packet *packet)
{
	if (!lininoio_ops)
		return -1;
	if (!n->send_packet)
		return -1;
	return n->send_packet(n, packet);
}
