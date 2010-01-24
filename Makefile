ifneq ($(KERNELRELEASE),)
include Kbuild
else

KERNELDIR := /lib/modules/`uname -r`/build
CFLAGS = -g -Wall -Wextra -Werror -fwhole-program $(OPT)
OPT = -O2
ALL = usb-bt-dump mtalk hid-parse hid-magicmouse.ko

all: $(ALL)
.PHONY: clean

usb-bt-dump: usb-bt-dump.c
mtalk: mtalk.c
hid-parse: hid-parse.c
hid-magicmouse.ko: hid-magicmouse.c
	$(MAKE) -C $(KERNELDIR) M=`pwd` $@

clean:
	$(MAKE) -C $(KERNELDIR) M=`pwd` clean
	rm -f $(ALL)

endif
