#ifndef __LININOIO_PROTO_H__
#define __LININOIO_PROTO_H__

#include <linux/r2proc_ioctl.h>
#include "lininoio.h"

/*
 * Lininoio transport protocol functions
 * GPL v2 or later
 * Copyright Dog Hunter 2016
 * Author Davide Ciminaghi 2016
 */

struct lininoio_channel;
struct lininoio_node;

struct lininoio_proto_ops {
	/* Invoked on node creation */
	int (*connect)(struct lininoio_channel *, struct lininoio_node *);
	/* Invoked on reception from node */
	void (*inbound_packet)(struct lininoio_channel *c,
			       const struct lininoio_data_packet *p);

	/* Invoked on node's death */
	void (*disconnect)(struct lininoio_channel *, struct lininoio_node *);
	/* Pointer to handler's private data */
	void *priv;
};

struct lininoio_channel;

struct lininoio_channel {
	uint16_t protocol;
	uint8_t core_id;
	uint8_t id;
	const struct lininoio_proto_ops *ops;
	void *priv;
	struct lininoio_association_data null_adata;
	struct lininoio_association_data *adata;
	/* Filled in by channel connect method */
	int resources_len;
	struct fw_rsc_hdr *resources;
	struct list_head list;
};

/* This corresponds to a remote processor */
struct lininoio_core {
	/* Name of related remote processor */
	char rproc_name[33];
	struct r2p_processor_data pd;
	int nchannels;
	struct list_head channels;
	struct lininoio_node *node;
};

struct lininoio_node {
	char name[16];
	struct timeout *alive_to;
	struct lininoio_core *cores[LININOIO_MAX_NCORES];
	int nchannels;
	void *ll_data;
	int (*send_packet)(struct lininoio_node *,
			   const struct lininoio_packet *p);
	struct list_head list;
	struct lininoio_channel *channels[LININOIO_MAX_NCHANNELS];
};

extern const struct lininoio_proto_ops *
lininoio_find_proto_ops(uint16_t proto_id);

extern int lininoio_register_proto_handler(uint16_t proto_id,
					   struct lininoio_proto_ops *);

extern int lininoio_send_packet(struct lininoio_node *,
				const struct lininoio_packet *packet);

extern int lininoio_init(void);

/* FIXME: IS THIS CORRECT HERE ? */
#define R2PROC_MISC_DEV "/dev/r2proc"

#endif /* __LININOIO_PROTO_H__ */
