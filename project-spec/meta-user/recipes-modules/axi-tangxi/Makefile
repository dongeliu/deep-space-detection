# https://stackoverflow.com/a/73509979/16027269
SRC := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
DRIVER_NAME := $(notdir $(SRC))
ifneq ($(KERNELRELEASE),)
	ccflags-y += -g -DDEBUG
	obj-m := $(DRIVER_NAME).o
	$(DRIVER_NAME)-objs := axi_tangxi.o axitangxi_dev.o
else
# https://docs.yoctoproject.org/1.9/ref-manual/ref-manual.html#var-KERNEL_SRC
ifeq ($(KERNEL_SRC),)
ifeq ($(shell test -d /usr/src/linux || echo 0),)
	KERNEL_SRC := /usr/src/linux
else
	KERNEL_SRC := /lib/modules/$(shell uname -r)/build
# https://nixos.wiki/wiki/Linux_kernel#Developing_out-of-tree_kernel_modules
ifeq ($(shell test -f /run/current-system/nixos-version || echo 0),)
	KERNEL_SRC := $(shell nix eval --impure --expr '"$${(import <nixpkgs> {}).linuxPackages_latest.kernel.dev}"')$(KERNEL_SRC)
endif
endif
endif
KERNEL_MAKE := $(MAKE) -C$(KERNEL_SRC) M=$(SRC)
RULES := all modules modules_prepare modules_check modules_install clean
.PHONY: default $(RULES) install uninstall reinstall
default:
	$(KERNEL_MAKE)
$(RULES):
	$(KERNEL_MAKE) $@
install: $(DRIVER_NAME).ko
	insmod $(DRIVER_NAME).ko
	grep -w $(DRIVER_NAME) /proc/devices | cut -d' ' -f1 | xargs -I{} mknod /dev/$(DRIVER_NAME) c {} 0
uninstall:
	$(RM) /dev/$(DRIVER_NAME)
	rmmod $(DRIVER_NAME)
reinstall: uninstall install
endif
