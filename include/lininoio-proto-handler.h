#ifndef __LININOIO_PROTO_HANDLER_H__
#define __LININOIO_PROTO_HANDLER_H__

#include <stdint.h>
#include <sys/uio.h>
#include "plugin.h"
#include "lininoio-internal.h"

struct lininoio_proto_handler_plugin_data {
	const struct lininoio_proto_ops *ops;
	uint16_t proto_id;
};

struct lininoio_proto_handler {
	const struct lininoio_proto_handler_plugin_data *data;
	void *priv;
};

#define DECLARE_LININOIO_PROTO_HANDLER(n,d,pd)		\
    DECLARE_PLUGIN(n,MCUIOD_PLUGIN_CLASS_LININOIO_PROTO_HANDLER,d,pd)

extern struct lininoio_proto_handler *
load_lininoio_proto_handler(const char *path, uint16_t id);

#endif /* __LININOIO_PROTO_HANDLER_H__ */
