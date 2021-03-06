

BASE := $(shell pwd)

# SUBDIRS:=util lininoio-protocol-handlers etherd test
SUBDIRS:=util test etherd lininoio-protocol-handlers

output_tar_name ?= $(shell echo lininoio-userspace-`date +%Y%m%d`.tar.bz2)

export BASE

all: subdirs

install: all subdirs_install

clean: subdirs_clean

subdirs: $(SUBDIRS)

subdirs_clean subdirs_install: subdirs_%: $(foreach s,$(SUBDIRS),$(s)_%)

$(SUBDIRS):
	make -C $@

$(foreach s,$(SUBDIRS),$(s)_clean): %_clean:
	make -C $* clean

$(foreach s,$(SUBDIRS),$(s)_install): %_install:
	make -C $* install

etherd: util


tar : clean
	@echo "BUILDING TAR $(output_tar_name)" && cd .. && \
        [ ! -f $(output_tar_name) ] && \
        tar jcf $(output_tar_name) `basename $(BASE)` \
        || { echo "!!! TAR IS ALREADY THERE " ; exit 1 ; }

tar_compact : clean
	@echo "BUILDING COMPACT TAR (no git) $(output_tar_name)" && cd .. && \
        [ ! -f $(output_tar_name) ] && \
        tar jcf $(output_tar_name) --exclude '.git' `basename $(BASE)` \
        || { echo "!!! TAR IS ALREADY THERE " ; exit 1 ; }


.PHONY: all subdirs $(SUBDIRS) tar clean $(foreach s,$(SUBDIRS),$(s)_clean) \
subdirs_all subdirs_clean subdirs_install
