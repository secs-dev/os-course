ifneq ($(KERNELRELEASE),)
obj-m := vtfs.o
vtfs-y := source/vtfs.o source/http.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
endif
