
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include "logger.h"
#include "mcuiod-api.h"
#include "lininoio-proto-handler.h"

#define DEFAULT_UNIX_SOCKET_PATH "/var/run/mcuiod_socket"

struct lininoio_mcuio_bus {
	int id;
	int fd;
	/* bitmap: 1 bit per free device number */
	uint8_t free_devs;
	struct list_head list;
};

static struct list_head busses;
static const char *opt_unix_socket_path = DEFAULT_UNIX_SOCKET_PATH;
static struct mcuiod_client_connection *connection = NULL;

static int lininoio_mcuio_init(void)
{
	INIT_LIST_HEAD(&busses);
	connection = mcuiod_connect(opt_unix_socket_path);
	return connection ? 0 : -1;
}

static struct lininoio_mcuio_bus *add_bus(void)
{
	struct lininoio_mcuio_bus *out = malloc(sizeof(*out));

	if (!out) {
		pr_err("%s: error allocating memory for new bus\n");
		return NULL;
	}
	if (mcuiod_new_bus(connection, &out->id, &out->fd) < 0) {
		pr_err("%s: error creating bus\n", __func__);
		free(out);
		return NULL;
	}
	/* All nodes are free */
	out->free_devs = 0xff;
	list_add(&out->list, &busses);
	return out;
}

static void free_bus(struct lininoio_mcuio_bus *bus)
{
	list_del(&bus->list);
	free(bus);
}

static int get_free_dev(struct lininoio_mcuio_bus *b, uint8_t *dev)
{
	uint8_t v = ffs(b->free_devs);

	if (!v)
		return -1;
	b->free_devs &= ~(1 << (v - 1));
	return v - 1;
}

static void put_dev(struct lininoio_mcuio_bus *b, uint8_t dev)
{
	if (dev > 7)
		return;
	b->free_devs |= 1 << dev;
	if (!b->free_devs)
		free_bus(b);
}

/*
 * A new node has been connected: do initialization if necessary,
 * get a node number, setup association data
 */
static int lininoio_mcuio_connect(struct lininoio_channel *c,
				  struct lininoio_node *n)
{
	struct lininoio_mcuio_bus *curr_bus;
	struct lininoio_association_data *adata;
	int i, stat;
	uint8_t dev = 0xff;

	if (!connection)
		if (lininoio_mcuio_init() < 0) {
			pr_err("lininoio_mcuio_init() failed\n");
			return -1;
		}
	if (list_empty(&busses))
		curr_bus = NULL;
	else
		curr_bus = list_first_entry(&busses, struct lininoio_mcuio_bus,
					    list);
	for (i = 0; i < 2; i++) {
		if (!curr_bus)
			curr_bus = add_bus();
		if (!curr_bus)
			return -1;
		c->priv = curr_bus;
		stat = get_free_dev(curr_bus, &dev);
		if (stat >= 0)
			break;
	}
	/* mcuio association data is 3 bytes long */
	adata = malloc(3);
	if (!adata) {
		put_dev(curr_bus, dev);
		return -1;
	}
	adata->chan_dlen = lininoio_encode_cdlen(1, c->id);
	adata->chan_data[0] = dev;
	c->adata = adata;
	return 0;
}

/*
 * A packet is coming from the node, get it and send it to the mcuiod host
 */
static void lininoio_mcuio_inbound_packet(struct lininoio_channel *c,
					  const struct lininoio_data_packet *p)
{
	struct lininoio_mcuio_bus *bus = c->priv;
	uint16_t len = lininoio_decode_cdlen(p->cdlen, NULL);

	if (!c->priv) {
		pr_err("%s: channel private data pointer is NULL\n", __func__);
		return;
	}
	if (write(bus->fd, p->data, len) < 0)
		pr_err("%s: write(): %s\n", __func__, strerror(errno));
}

static void lininoio_mcuio_disconnect(struct lininoio_channel *c,
				      struct lininoio_node *n)
{
	struct lininoio_mcuio_bus *bus = c->priv;
	struct lininoio_association_data *adata = c->adata;
	uint8_t dev;

	if (!bus) {
		pr_err("%s: bus is NULL\n", __func__);
		return;
	}
	if (!adata) {
		pr_err("%s: channel has no association data\n", __func__);
		return;
	}
	dev = adata->chan_data[0];
	if (dev < 1 || dev > 7) {
		pr_err("%s: invalid node mcuio device number\n", __func__);
		return;
	}
	put_dev(bus, dev);
}

static const struct lininoio_proto_ops mcuio_ops = {
	.connect = lininoio_mcuio_connect,
	.inbound_packet = lininoio_mcuio_inbound_packet,
	.disconnect = lininoio_mcuio_disconnect,
};

static const struct lininoio_proto_handler_plugin_data plugin_data = {
	.ops = &mcuio_ops,
	.proto_id = LININOIO_PROTO_MCUIO_V0,
};

DECLARE_LININOIO_PROTO_HANDLER(mcuio, "mcuio protocol handler", &plugin_data);
