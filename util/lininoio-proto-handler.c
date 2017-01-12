/*
 * Copyright 2016 Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

#include <stdio.h>
#include <fcntl.h>
#include <pty.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <pty.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/tty.h>
#include "util.h"
#include "logger.h"
#include "plugin.h"
#include "lininoio-proto-handler.h"


static int lininoio_proto_match(struct plugin *candidate, const void *_id_ptr)
{
	uint16_t id = *(uint16_t *)_id_ptr;
	int ret;
	const struct lininoio_proto_handler_plugin_data *data =
	    candidate->data.private_data;

	ret = id == data->proto_id;
	pr_debug("%s: id = %u, data->proto_id = %u, return %d\n", __func__, id,
		 data->proto_id, ret);
	return ret;
}

struct lininoio_proto_handler *load_lininoio_proto_handler(const char *path,
							   uint16_t proto_id)
{
	struct plugin *p;
	struct lininoio_proto_handler *out;
	int stat;

	stat = _find_plugin(path, "lininoio-proto-handler",
			    lininoio_proto_match, &proto_id, 1, &p);
	pr_debug("%s: _find_plugin returns %d\n", __func__, stat);
	if (stat < 0)
		return NULL;
	out = malloc(sizeof(*out));
	if (!out) {
		plugin_unload(p);
		plugin_free(p);
		return NULL;
	}
	out->data = p->data.private_data;
	return out;
}
