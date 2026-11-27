KDIR ?= /lib/modules/$(shell uname -r)/build
USER_CC ?= $(CROSS_COMPILE)gcc
USER_CFLAGS ?= -O2 -Wall -Wextra
USER_LDFLAGS ?= -static

SERIAL_TOOLS := tools/serial-get-counter tools/serial-reset-counter

.PHONY: all modules tools clean

all: modules tools

modules:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" modules

tools: $(SERIAL_TOOLS)

$(SERIAL_TOOLS): %: %.c
	$(USER_CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o "$@" "$<"

clean:
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" clean
	$(RM) $(SERIAL_TOOLS)
