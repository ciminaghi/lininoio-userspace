DEBUG := y

include $(BASE)/common.mk

# FIXME: CFLAGS_LIBS ?
CFLAGS += -fpic -fPIC


PLUGINS := console.so
EXECUTABLES:=
SCRIPTS:=

all: $(PLUGINS) $(EXECUTABLES) $(SUBDIRS)

$(eval $(call install_cmds,$(PLUGINS),$(EXECUTABLES),$(SCRIPTS)))

$(SUBDIRS):
	make -C $@

clean:
	rm -f *.o *~ $(LIB) $(EXECUTABLES) $(PLUGINS)

$(PLUGINS): %.so: %.c
	$(CC) $(CFLAGS) -fpic -fPIC -o $*.so -shared -Wl,-soname,$@ $^

.PHONY: $(SUBDIRS) install all clean
