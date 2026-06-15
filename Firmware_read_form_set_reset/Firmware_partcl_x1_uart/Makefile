RISCV_PREFIX ?= riscv32-unknown-elf-
CC := $(RISCV_PREFIX)gcc
OBJCOPY := $(RISCV_PREFIX)objcopy
OBJDUMP := $(RISCV_PREFIX)objdump

CFLAGS ?= -march=rv32imc -mabi=ilp32 -Os -ffreestanding -nostdlib

PARTCL_DEFS ?= \
	-DX1_WB_ADDR=0x30000004 \
	-DPARTCL_CTRL_OFFSET=0x0000 \
	-DPARTCL_STATUS_OFFSET=0x0004 \
	-DPARTCL_A_OFFSET=0x0100 \
	-DPARTCL_B_OFFSET=0x0200 \
	-DPARTCL_C_OFFSET=0x0400 \
	-DPARTCL_CTRL_START=0x00000105 \
	-DPARTCL_STATUS_DONE=0x00000008

TARGET := partcl_x1_2x2_uart

all: $(TARGET).elf $(TARGET).bin $(TARGET).lst

$(TARGET).elf: partcl_x1_2x2_uart.c
	$(CC) $(CFLAGS) $(PARTCL_DEFS) $< -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).lst: $(TARGET).elf
	$(OBJDUMP) -D $< > $@

clean:
	rm -f $(TARGET).elf $(TARGET).bin $(TARGET).lst

.PHONY: all clean
