/*
 * plugin.c : plugin related functions
 *
 * mcuiod util library
 * Copyright Davide Ciminaghi 2016
 * GPLv2 or later
 */

#include <stdlib.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "logger.h"
#include "plugin.h"

int for_each_plugin(const char *dir, const char *class, int do_load,
		    int (*cb)(struct plugin *p, void *data), void *data)
{
	DIR *d;
	struct dirent *de;
	char cwd[PATH_MAX + 1];
	int found;

	if (getcwd(cwd, sizeof(cwd)) < 0) {
		pr_err("%s: getcwd(): %s\n", __func__, strerror(errno));
		return -1;
	}
	pr_debug("chdir to %s\n", dir);
	if (chdir(dir) < 0) {
		pr_err("%s: chdir(): %s\n", __func__, strerror(errno));
		return -1;
	}
	d = opendir(".");
	if (!d)
		return -1;
	for (found = 0; ; ) {
		void *h, *s;
		struct plugin *p;
		struct plugin_data *pd;
		char path[PATH_MAX + 1];
		
		de = readdir(d);
		if (!de)
			break;
		if (de->d_type != DT_REG && de->d_type != DT_LNK)
			continue;
		strncpy(path, dir, sizeof(path));
		strncat(path, de->d_name, sizeof(path) - strlen(dir) - 1);
		
		h = dlopen(path, RTLD_LAZY|RTLD_GLOBAL);
		pr_err("dlopen(%s) returns %p (%s)\n", de->d_name, h,
		       dlerror());
		if (!h)
			continue;
		s = dlsym(h, "mcuio_plugin_data");
		if (!s) {
			pr_err("dlsym(): %s\n", dlerror());
			continue;
		}
		pd = s;
		pr_debug("plugin class = %s, requested class = %s\n",
			 pd->class, class);
		if (strcmp(pd->class, class)) {
			dlclose(h);
			continue;
		}
		found++;
		p = malloc(sizeof(*p));
		if (!p) {
			pr_err("%s: %s\n", __func__, strerror(errno));
			break;
		}
		p->free_strings = 0;
		if (do_load)
			p->data = *pd;
		else {
			p->data.name = strndup(pd->name,
					       MAX_PLUGIN_NAME_LENGTH);
			p->data.class = strndup(pd->class,
						MAX_PLUGIN_CLASS_NAME_LENGTH);
			p->data.description =
				strndup(pd->description,
					MAX_PLUGIN_DESCRIPTION_LENGTH);
			p->free_strings = 1;
		}
			
		p->handler = do_load ? h : NULL;
		if (!do_load)
			dlclose(h);
		if (cb(p, data))
			break;
	}
	closedir(d);
	chdir(cwd);
	pr_debug("%s returns %d\n", __func__, found);
	return found;
}


static int find_plugins_cb(struct plugin *p, void *data)
{
	struct list_head *l = data;

	list_add_tail(&p->list, l);
	return 0;
}

int find_plugins(const char *dir, const char *class, int do_load,
		 struct list_head *out)
{
	INIT_LIST_HEAD(out);
	return for_each_plugin(dir, class, do_load, find_plugins_cb, out);
}


struct find_plugin_data {
	const void *key;
	int (*match)(struct plugin *candidate, const void *key);
	const char *name;
	int do_load;
	struct plugin **out;
	int found;
};

static int _find_plugin_cb(struct plugin *p, void *data)
{
	struct find_plugin_data *fpd = data;
	int ret = fpd->match(p, fpd->key);

	pr_debug("%s: fpd->match = %d\n", __func__, ret);
	if (!ret && fpd->do_load) {
		plugin_unload(p);
		plugin_free(p);
	}
	if (ret) {
		if (fpd->out)
			*fpd->out = p;
		else
			plugin_free(p);
	}
	fpd->found = ret;
	pr_debug("fpd->found = %d\n", fpd->found);
	return ret;
}


int _find_plugin(const char *dir, const char *class,
		 int (*match)(struct plugin *candidate, const void *key),
		 const void *key,
		 int do_load, struct plugin **out)
{
	struct find_plugin_data fpd = {
		.key = key,
		.match = match,
		.do_load = do_load,
		.out = out,
		.found = 0,
	};
	int ret;

	if (do_load && !out) {
		pr_err("_find_plugin invoked with do_load && !out\n");
		return -1;
	}
	ret = for_each_plugin(dir, class, do_load, _find_plugin_cb, &fpd);
	if (ret < 0)
		return ret;
	return fpd.found ? 0 : -1;
}

static int plugin_name_match(struct plugin *candidate, const void *key)
{
	return !strcmp(candidate->data.name, (const char *)key);
}

int find_plugin(const char *dir, const char *class, const char *name,
		int do_load, struct plugin **out)
{
	return _find_plugin(dir, class, plugin_name_match, name, do_load, out);
}
