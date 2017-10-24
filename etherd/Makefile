
include $(BASE)/common.mk

OBJS := main.o lininoio-ether.o

EXE := etherd

LDFLAGS += -ludev

all: $(EXE)

$(eval $(call install_cmds,$(LIB),$(EXE),$(SCRIPTS)))

clean:
	rm -f $(EXE) *.o *~

etherd: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
