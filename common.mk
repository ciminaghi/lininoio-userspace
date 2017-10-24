
# Toolchain
HOSTCC ?= gcc
HOSTLD ?= ld
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
STRIP = $(CROSS_COMPILE)strip
INSTALL ?= /usr/bin/install
DEBUG ?= y

# Kernel headers
KERNEL_HEADERS := /kernel_headers

prefix := /usr
datadir := $(prefix)/share/lininoio/data
confdir := /etc

libdir := $(prefix)/lib/lininoio
bindir := $(prefix)/bin

INSTALL_DEFAULT_OWNER_GROUP := --owner=root --group=root
INSTALL_DEFAULT_MODE :=
#INSTALL_DEFAULT_STRIP := --strip --strip-program=$(STRIP)


# Common definitions for cflags and ldflags 
CFLAGS := -O2 -Wall -Werror -I$(BASE)/include/ -I. \
	-I$(KERNEL_HEADERS)/include\
	-DBASEDIR=\"$(BASE)\" -DPREFIX=\"$(prefix)/\" \
	-DBINDIR=\"$(bindir)/\" -DLIBDIR=\"$(libdir)/\" \
	-DDATADIR=\"$(datadir)/\" -DCONFDIR=\"$(confdir)/\"
LDFLAGS := -rdynamic -ldl -lininoio_util

ifeq ($(DEBUG),y)
CFLAGS += -g -DDEBUG
endif

CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

# Common target defines
define install_cmds
install: all lib_install bin_install

$(eval ifneq (,$(strip $(1)))
lib_install: libdir_install $(1)
	$(INSTALL) $(INSTALL_DEFAULT_OWNER_GROUP) $(INSTALL_DEFAULT_STRIP) \
	$(INSTALL_DEFAULT_MODE) $(foreach l,$(1),$l) $(DESTDIR)/$(libdir)
else
lib_install:
endif)

$(eval ifneq (,$(strip $(2)))
bin_install: bindir_install $(2)
	$(INSTALL) $(INSTALL_DEFAULT_OWNER_GROUP) $(INSTALL_DEFAULT_STRIP) \
	$(INSTALL_DEFAULT_MODE) $(foreach l,$(2),$l) $(DESTDIR)/$(bindir)
	[ -n "$(foreach l,$(3),$l)" ] && \
	$(INSTALL) $(INSTALL_DEFAULT_OWNER_GROUP) \
	$(INSTALL_DEFAULT_MODE) $(foreach l,$(3),$l) $(DESTDIR)/$(bindir) \
	|| exit 0
else
bin_install:
endif)

$(eval bindir_install libdir_install: %_install:
	$(INSTALL) -d $(DESTDIR)/$$($$*) $(INSTALL_DEFAULT_OWNER_GROUP) \
	$(INSTALL_DEFAULT_MODE))
endef
