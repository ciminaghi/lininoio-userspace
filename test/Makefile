
include $(BASE)/common.mk

OBJS := simple_r2proc_test.o udev-events.o -ludev

EXE := simple_r2proc_test

all: $(EXE)

simple_r2proc_test: $(OBJS)

$(eval $(call install_cmds,$(LIB),$(EXE),$(SCRIPTS)))

clean:
	rm -f $(EXE) *.o *~

etherd: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
