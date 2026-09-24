# Native GD32F305VCT6 bring-up firmware -- standalone image, NOT part of
# the sibling pandabox-firmware project's APP1/APP2 slot scheme. See
# README.md.
#
# Usage:
#   make            # builds build/bringup.bin
#   make clean

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CPU     = -mcpu=cortex-m4 -mfloat-abi=soft -mthumb
INCLUDES = -Isrc -Ifreertos -Ifreertos/include -Ifreertos/portable/GCC/ARM_CM3_GD32

EXTRA_CFLAGS =
CFLAGS  = $(CPU) $(INCLUDES) -Os -g -Wall -ffunction-sections -fdata-sections \
          -fno-common -std=gnu11 $(EXTRA_CFLAGS)
LDFLAGS = $(CPU) -Tlinker/gd32f305vct6.ld -nostartfiles -Wl,--gc-sections -specs=nano.specs

SRCS = startup/startup_gd32f305vct6.s \
       src/main.c \
       src/clock.c \
       src/gpio.c \
       src/uart.c \
       src/rtt.c \
       src/log.c \
       src/yc1021.c \
       src/bt_init_table.c \
       src/ec25.c \
       src/spi0.c \
       src/gd25q.c \
       src/flash_store.c \
       src/tcs.c \
       src/cmd.c \
       src/cmd_selftest.c \
       src/nvic.c \
       src/fault.c \
       src/led_mgr.c \
       src/task_meter.c \
       src/task_telit.c \
       src/task_ota.c \
       src/systick.c \
       src/syscalls.c \
       freertos/tasks.c \
       freertos/queue.c \
       freertos/list.c \
       freertos/event_groups.c \
       freertos/portable/MemMang/heap_4.c \
       freertos/portable/GCC/ARM_CM3_GD32/port.c \
       freertos/freertos_hooks.c

OBJDIR = build
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(filter %.c,$(SRCS))) \
       $(patsubst %.s,$(OBJDIR)/%.o,$(filter %.s,$(SRCS)))

ELF = $(OBJDIR)/bringup.elf
BIN = $(OBJDIR)/bringup.bin

all: $(BIN)
	$(SIZE) $(ELF)

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -c $< -o $@

clean:
	rm -rf build

.PHONY: all clean
