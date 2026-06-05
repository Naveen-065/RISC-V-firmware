# Caravel ChipIgnite ReRAM Firmware Modes

This repository contains RISC-V management-CPU firmware for testing a ReRAM / neuromorphic user project on a Caravel ChipIgnite board. The firmware programs the user design through the Caravel user-area Wishbone interface.

The main user Wishbone address used by all modes is:

```c
#define NEURO_ADDR 0x30000004
```

All firmware examples write 32-bit command words to `0x30000004`, then optionally read back from the same address for debug.

---

## Directory Structure

Recommended structure:

```text
caravel_board/
└── firmware/
    └── chipignite/
        └── reram_prog/
            ├── read_mode_wb/
            ├── form_mode_wb/
            ├── set_mode_wb/
            └── reset_mode_wb/
```

Each mode folder contains:

```text
Makefile
client.py
<mode_name>.c
<mode_name>.hex   # generated after make
```

---

## Common Hardware Setup

Required hardware:

- Caravel ChipIgnite board
- USB cable for board flashing
- USB-to-TTL UART adapter if board UART is not routed directly to USB
- 3.3 V TTL UART level
- Common GND between Caravel board and USB-to-TTL adapter

UART connection:

```text
Caravel GPIO6 / UART TX  →  USB-to-TTL RX
Caravel GND              →  USB-to-TTL GND
```

Optional RX connection:

```text
Caravel GPIO5 / UART RX  ←  USB-to-TTL TX
```

Do not connect 5 V to Caravel GPIOs.

---

## Common Firmware Setup

Each firmware uses:

```c
#include <defs.h>
#include <stub.h>
#include <stdint.h>
```

The firmware performs these common steps:

1. Configure management GPIO.
2. Configure Caravel GPIO pads.
3. Enable UART using `reg_uart_enable = 1`.
4. Enable user Wishbone using `reg_wb_enable = 1`.
5. Write mode-specific command words to `0x30000004`.
6. Read back from `0x30000004`.
7. Print the readback value on UART.

---

## Mode 1: Read Mode

Folder:

```text
read_mode_wb/
```

Purpose:

Read mode sends the common command setup sequence and then performs a read command.

Command sequence:

```c
REG32(0x30000004) = 0x00036472;
REG32(0x30000004) = 0x462B000B;
REG32(0x30000004) = 0x44001405;
REG32(0x30000004) = 0x4003AAFF;
```

Then:

```c
temp = REG32(0x30000004);
```

Expected UART output:

```text
[READ_MODE_WB] firmware start
[READ_MODE_WB] Wishbone enabled
[READ_MODE_WB] Writing command 1: 0x00036472
[READ_MODE_WB] Writing command 2: 0x462B000B
[READ_MODE_WB] Writing command 3: 0x44001405
[READ_MODE_WB] Writing command 4: 0x4003AAFF
[READ_MODE_WB] Reading back from 0x30000004
[READ_MODE_WB] Readback value = 0xXXXXXXXX
[READ_MODE_WB] test finished
```

---

## Mode 2: Form Mode

Folder:

```text
form_mode_wb/
```

Purpose:

Form mode applies the forming command sequence, followed by a read command.

Command sequence:

```c
REG32(0x30000004) = 0x00036472;
REG32(0x30000004) = 0x462B000B;
REG32(0x30000004) = 0x84001405;
REG32(0x30000004) = 0x8003AAFF;
REG32(0x30000004) = 0x4003AAFF;
```

Then:

```c
temp = REG32(0x30000004);
```

Expected UART output:

```text
[FORM_MODE_WB] firmware start
[FORM_MODE_WB] Wishbone enabled
[FORM_MODE_WB] Writing command 1: 0x00036472
[FORM_MODE_WB] Writing command 2: 0x462B000B
[FORM_MODE_WB] Writing command 3: 0x84001405
[FORM_MODE_WB] Writing command 4: 0x8003AAFF
[FORM_MODE_WB] Writing command 5: 0x4003AAFF
[FORM_MODE_WB] Reading back from 0x30000004
[FORM_MODE_WB] Readback value = 0xXXXXXXXX
[FORM_MODE_WB] test finished
```

---

## Mode 3: Program SET Mode

Folder:

```text
set_mode_wb/
```

Purpose:

SET mode sends the common setup commands, applies a SET operation, then issues a read operation.

Command sequence:

```c
REG32(0x30000004) = 0x00036472;
REG32(0x30000004) = 0x462B000B;
REG32(0x30000004) = 0x44001405;
REG32(0x30000004) = 0xD00888A2;  // SET operation
REG32(0x30000004) = 0x500888FF;  // READ operation
```

Then:

```c
temp = REG32(0x30000004);
```

Expected UART output:

```text
[SET_MODE_WB] firmware start
[SET_MODE_WB] Wishbone enabled
[SET_MODE_WB] Writing command 1: 0x00036472
[SET_MODE_WB] Writing command 2: 0x462B000B
[SET_MODE_WB] Writing command 3: 0x44001405
[SET_MODE_WB] Writing command 4 SET: 0xD00888A2
[SET_MODE_WB] Writing command 5 READ: 0x500888FF
[SET_MODE_WB] Reading back from 0x30000004
[SET_MODE_WB] Readback value = 0xXXXXXXXX
[SET_MODE_WB] test finished
```

---

## Mode 4: Program RESET Mode

Folder:

```text
reset_mode_wb/
```

Purpose:

RESET mode sends the common setup commands, applies a RESET operation, then issues a read operation.

Command sequence:

```c
REG32(0x30000004) = 0x00036472;
REG32(0x30000004) = 0x462B000B;
REG32(0x30000004) = 0x44001405;
REG32(0x30000004) = 0x10088806;  // RESET operation
REG32(0x30000004) = 0x500888FF;  // READ operation
```

Then:

```c
temp = REG32(0x30000004);
```

Expected UART output:

```text
[RESET_MODE_WB] firmware start
[RESET_MODE_WB] Wishbone enabled
[RESET_MODE_WB] Writing command 1: 0x00036472
[RESET_MODE_WB] Writing command 2: 0x462B000B
[RESET_MODE_WB] Writing command 3: 0x44001405
[RESET_MODE_WB] Writing command 4 RESET: 0x10088806
[RESET_MODE_WB] Writing command 5 READ: 0x500888FF
[RESET_MODE_WB] Reading back from 0x30000004
[RESET_MODE_WB] Readback value = 0xXXXXXXXX
[RESET_MODE_WB] test finished
```

---

## Build Instructions

Go to the mode folder:

```bash
cd ~/caravel_board/firmware/chipignite/reram_prog/read_mode_wb
```

Build:

```bash
make clean
make
```

Expected output:

```text
read_mode_wb.hex
```

Build the other modes:

```bash
cd ~/caravel_board/firmware/chipignite/reram_prog/form_mode_wb
make clean && make

cd ~/caravel_board/firmware/chipignite/reram_prog/set_mode_wb
make clean && make

cd ~/caravel_board/firmware/chipignite/reram_prog/reset_mode_wb
make clean && make
```

---

## Flash Instructions

The mode folders are nested inside:

```text
firmware/chipignite/reram_prog/<mode_folder>
```

so the flasher path is:

```bash
../../util/caravel_hkflash.py
```

Example for read mode:

```bash
cd ~/caravel_board/firmware/chipignite/reram_prog/read_mode_wb

source ~/caravel_venv/bin/activate

BUSDEV=$(lsusb -d 0403:6014 | awk '{print $2"/"substr($4,1,3)}')
echo $BUSDEV
sudo chmod a+rw /dev/bus/usb/$BUSDEV

~/caravel_venv/bin/python3 ../../util/caravel_hkflash.py read_mode_wb.hex
```

Flash other modes:

```bash
~/caravel_venv/bin/python3 ../../util/caravel_hkflash.py form_mode_wb.hex
~/caravel_venv/bin/python3 ../../util/caravel_hkflash.py set_mode_wb.hex
~/caravel_venv/bin/python3 ../../util/caravel_hkflash.py reset_mode_wb.hex
```

Successful flashing should show:

```text
mfg = 0456
JEDEC = ef4016
read compare successful
```

---

## UART Terminal

Open UART:

```bash
sudo chmod a+rw /dev/ttyUSB0
picocom -b 9600 /dev/ttyUSB0
```

Press RESET on the Caravel board.

If using an external USB-to-TTL adapter, connect:

```text
Caravel GPIO6 / UART TX  →  USB-to-TTL RX
Caravel GND              →  USB-to-TTL GND
```

Expected terminal speed:

```text
9600 baud, 8 data bits, no parity, 1 stop bit
```

Exit picocom:

```text
Ctrl + A, then Ctrl + X
```

---

## Notes on Readback

The firmware reads back using:

```c
temp = REG32(0x30000004);
```

If the terminal prints:

```text
Readback value = 0xFFFFFFFF
```

this does not necessarily mean the ReRAM data is all ones. It may mean:

- The user RTL does not drive `wbs_dat_o` for reads at `0x30000004`.
- `0x30000004` is only a write command register.
- The actual read-data register is at another address.
- The Wishbone address decode does not match the firmware address.
- The default unmapped read value is all ones.

Recommended future register map:

```text
0x30000004 = command register
0x30000008 = status register
0x3000000C = read-data register
```

Then firmware should read actual data from:

```c
temp = REG32(0x3000000C);
```

---

## Debug Checklist

1. Confirm the firmware flashes successfully.
2. Open UART before pressing RESET.
3. Confirm UART prints the mode name.
4. Confirm `Wishbone enabled` is printed.
5. Confirm all command writes are printed.
6. Confirm readback value is printed.
7. If readback is invalid, inspect RTL `wbs_dat_o`, `wbs_ack_o`, and address decode.

---

## Common Troubleshooting

### `mfg = ffff`

Caravel chip is not responding through housekeeping SPI.

Check:

- Board power
- Chip seating
- Reset state
- VM USB passthrough
- Correct board type

### `JEDEC = ffffff`

External SPI flash is not responding.

Check:

- Flash power
- Flash connection
- Board reset
- Jumper settings

### `Permission denied` on USB

Fix USB permission:

```bash
BUSDEV=$(lsusb -d 0403:6014 | awk '{print $2"/"substr($4,1,3)}')
sudo chmod a+rw /dev/bus/usb/$BUSDEV
```

### `Permission denied` on UART

Fix serial port permission:

```bash
sudo chmod a+rw /dev/ttyUSB0
```

Permanent fix:

```bash
sudo usermod -aG dialout $USER
newgrp dialout
```

### UART visible on logic analyzer but not terminal

The board UART TX is on GPIO6. If the onboard USB interface is not routed to GPIO6, use an external USB-to-TTL adapter.

```text
Caravel GPIO6 / UART TX  →  USB-to-TTL RX
Caravel GND              →  USB-to-TTL GND
```

---

## Important Safety Notes

- Use 3.3 V TTL UART levels.
- Do not connect 5 V to Caravel GPIOs.
- Do not run OpenLane commands with `sudo`.
- Keep firmware folders separate for each mode to avoid flashing the wrong `.hex`.
- Always verify the exact `.hex` name before flashing.
