#ifndef __PLUGIN_H__
#define __PLUGIN_H__

#include <stdlib.h>
#include <dlfcn.h>
#include "list.h"

#define MAX_PLUGIN_NAME_LENGTH 80
#define MAX_PLUGIN_CLASS_NAME_LENGTH 80
#define MAX_PLUGIN_DESCRIPTION_LENGTH 160


#define MCUIOD_PLUGIN_CLASS_RADIO_DRIVER "radio"
#define MCUIOD_PLUGIN_CLASS_SPI_DRIVER "spi"
#define MCUIOD_PLUGIN_CLASS_LININOIO_PROTO_HANDLER "lininoio-proto-handler"

struct plugin_data {
	const char *name;
	const char *class;
	const char *description;
	const void *private_data;
};

struct plugin {
	struct plugin_data data;
	int free_strings;
	int users;
	void *handler;
	void *priv;
	struct list_head list;
};

/*
 * find_plugins: find list of plugins with given class (and possibly load them)
 *
 * @dir: path of plugins directory
 * @class: class to look for
 * @do_load: if this flag is !0, load plugins, otherwise just build their
 *           plugin data structure
 * @out: pointer to output list
 *
 * Returns number of plugins found, -1 on error
 */
int find_plugins(const char *dir, const char *class, int do_load,
		 struct list_head *out);

int _find_plugin(const char *dir, const char *class,
		 int (*match)(struct plugin *candidate, const void *key),
		 const void *key, int do_load, struct plugin **out);

/*
 * find_plugin: find plugin given class and name (and possibly load it)
 *
 * @dir: path plugins directory
 * @class: class to look for
 * @name: name to look for
 * @do_load: if this flag is !0, load plugin, otherwise just build its
 *           plugin data structure
 * @out: pointer to location where pointer to struct plugin shall be written.
 * If out is NULL, the plugin is only searched, no struct plugin is instantiated
 *
 * Returns 1 if plugin found, 0 if not found, -1 on error
 */
int find_plugin(const char *dir, const char *class, const char *name,
		int do_load, struct plugin **out);

int for_each_plugin(const char *dir, const char *class, int do_load,
		    int (*cb)(struct plugin *p, void *data), void *data);

static inline void plugin_free(struct plugin *p)
{
	if (p->free_strings) {
		free((void *)p->data.name);
		free((void *)p->data.class);
		free((void *)p->data.description);
	}
	free(p);
}

static inline void plugin_unload(struct plugin *p)
{
	dlclose(p->handler);
}


#define DECLARE_PLUGIN(n,c,d,pd)				 \
	const struct plugin_data mcuio_plugin_data = {		 \
		.name = #n,					 \
		.class = c,					 \
		.description = d,				 \
		.private_data = pd,				 \
	}


#endif /* __PLUGIN_H__ */
