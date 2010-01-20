CFLAGS = -g -Wall -Wextra -Werror -fwhole-program $(OPT)
OPT = -O2
ALL = usb-bt-dump mtalk hid-parse

all: $(ALL)
.PHONY: clean

usb-bt-dump: usb-bt-dump.c
mtalk: mtalk.c
hid-parse: hid-parse.c

clean:
	rm -f $(ALL)
