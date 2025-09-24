# Top-level Makefile for virtio-drivers

# Name of the module (will build virtio-drivers.ko)
obj-m := virtio-drivers.o

# Objects that form the module
virtio-drivers-objs := \
    virtio-net/virtio_net.o \
    virtio-pci/virtio_pci.o

# Add include paths for headers
ccflags-y += -I$(src)/virtio-net -I$(src)/virtio-pci

# Kernel build system
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

