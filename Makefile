# LiteRTOS host-side checks. The kernel is tested as real AVR machine code on simavr.
# Requires: avr-gcc, avr-libc, gcc, libsimavr-dev, libelf-dev
#   Debian/Ubuntu: sudo apt install gcc-avr avr-libc build-essential libsimavr-dev libelf-dev
#
#   make test         build and run all simavr tests; any FAIL fails the make
#   make test LTO=1   same, with -flto (PlatformIO/Arduino builds use LTO by default)
#   make test-all     both of the above
#   make lint         compile the kernel with -Werror (plain and LTO)
#   make size         print kernel flash / RAM usage
#   make clean

SRC    := src
T      := test/simavr
LTO    ?= 0
BUILD  := build$(if $(filter 1,$(LTO)),-lto,)
CC     := avr-gcc
CFLAGS := -mmcu=atmega328p -DF_CPU=16000000UL -std=gnu11 -Wall -Wextra \
          -Os -ffunction-sections -fdata-sections -I$(T)/stub -I$(SRC) -I$(T)/tests
# T6 calls vPortYield from inline asm; LTO cannot see that reference, so keep it with -u
LDFLAGS := -mmcu=atmega328p -Wl,--gc-sections -Wl,-u,vPortYield
ifeq ($(LTO),1)
CFLAGS  += -flto
LDFLAGS += -flto
endif
TESTS  := 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
KERNEL := $(BUILD)/LiteRTOS.o $(BUILD)/LiteRTOS_port_avr.o \
          $(BUILD)/PortInitStack.o $(BUILD)/uxTaskGetStackHighWaterMark.o

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S
	@mkdir -p $(BUILD)
	$(CC) -mmcu=atmega328p -c $< -o $@

$(BUILD)/ktest%.elf: $(T)/tests/ktest.c $(T)/tests/sim_common.h $(KERNEL)
	$(CC) $(CFLAGS) -DTEST=$* $(T)/tests/ktest.c $(KERNEL) $(LDFLAGS) -o $@

$(BUILD)/runner: $(T)/tests/runner.c
	@mkdir -p $(BUILD)
	gcc -O1 -Wall $(T)/tests/runner.c -o $@ -lsimavr -lelf

test: $(BUILD)/runner $(TESTS:%=$(BUILD)/ktest%.elf)
	@fail=0; for t in $(TESTS); do \
	  out=$$(timeout 120 $(BUILD)/runner $(BUILD)/ktest$$t.elf 2>&1); \
	  echo "$$out"; \
	  echo "$$out" | grep -q "FAIL" && fail=1; \
	  echo "$$out" | grep -q "PASS" || fail=1; \
	done; exit $$fail

lint:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Werror -c $(SRC)/LiteRTOS.c -o /dev/null
	$(CC) $(CFLAGS) -Werror -c $(SRC)/LiteRTOS_port_avr.c -o /dev/null

size: $(KERNEL)
	avr-size $(KERNEL)

test-all:
	$(MAKE) test
	$(MAKE) test LTO=1

clean:
	rm -rf build build-lto

.PHONY: test test-all lint size clean
