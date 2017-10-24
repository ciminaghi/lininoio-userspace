
DEBUG := y

include $(BASE)/common.mk

LIBLININOIO_UTIL_OBJS := timeout.o logger.o daemonize.o fd_event.o plugin.o \
fd-over-socket.o lininoio.o  lininoio-proto-handler.o udev-events.o virtqueue.o virtio.o

# FIXME: CFLAGS_LIBS ?
CFLAGS += -fpic -fPIC


LIB := libininoio_util.so
EXECUTABLES:=
SCRIPTS:=

all: $(LIB) $(EXECUTABLES) $(SUBDIRS)

$(eval $(call install_cmds,$(LIB),$(EXECUTABLES),$(SCRIPTS)))

$(SUBDIRS):
	make -C $@

clean:
	rm -f *.o *~ $(LIB) $(EXECUTABLES)

$(LIB): $(LIBLININOIO_UTIL_OBJS)
	$(CC) -o $@ -shared -Wl,-soname,$@ $^
	#$(LD) -o $@ -shared -soname $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: $(SUBDIRS) install all clean
