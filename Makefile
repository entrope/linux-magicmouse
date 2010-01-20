CFLAGS = -g -Wall -Wextra -Werror -fwhole-program $(OPT)
OPT = -O2
ALL = usb-bt-dump mtalk

all: $(ALL)
.PHONY: clean

usb-bt-dump: usb-bt-dump.c
mtalk: mtalk.c

clean:
	rm -f $(ALL)
