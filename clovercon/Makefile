CURRENT = $(shell uname -r)
TARGET  = clovercon
OBJS    = clovercon.o
PWD     = $(shell pwd)
KDIR    ?= ..
obj-m   := $(TARGET).o

all: module

module:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- CFLAGS_MODULE=-fno-pic
	/sbin/modinfo $(TARGET).ko
	arm-linux-gnueabihf-strip --strip-unneeded $(TARGET).ko

$(TARGET).o: $(OBJS)
	$(LD) $(LD_RFLAG) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -r -o $@ $(OBJS)

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
	rm -f $(TARGET).ko

.PHONY: all module clean

-include $(KDIR)/Rules.make
