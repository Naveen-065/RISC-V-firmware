# Scan Debug Hardware Firmware

This folder contains firmware, not RTL simulation code.

Combined file:

```text
scan_debug_hw_firmware.c
```

Per-operation firmware files:

```text
read/scan_debug_read_firmware.c
form/scan_debug_form_firmware.c
set/scan_debug_set_firmware.c
reset/scan_debug_reset_firmware.c
common/scan_debug_hw_single_common.h
```

These run on the Caravel management RISC-V and drive the real hardware
scan-debug pins:

```text
GPIO21 -> ScanInDR / i_scan_se1, active low while shifting
GPIO22 -> ScanInDL / i_scan_si1
GPIO35 -> ScanInCC, held low
GPIO36 -> TM / i_TM
```

## What It Tests

The firmware runs four scan-debug cases:

```text
READ_PLACEHOLDER   op_set=0 wl=3  bl=4  sl=5
FORM_PLACEHOLDER   op_set=1 wl=6  bl=7  sl=8
SET                op_set=1 wl=9  bl=10 sl=11
RESET              op_set=0 wl=12 bl=13 sl=14
```

Current `ScanDebug.v` only has `op_set`, so SET and RESET are real scan-debug
polarities. READ and FORM are placeholders until dedicated scan-debug RTL hooks
are added.

The firmware prints UART checkpoints with prefix `[SCAN-HW]` and drives:

```text
TM high
ScanInDR low
16 scan bits LSB-first on ScanInDL
one extra capture edge
ScanInDR high while TM remains high for output observation
```

Wishbone is not needed for scan-debug mode. Optional RTL status reads are only
enabled if you compile with `SCAN_DEBUG_STATUS_BASE`.

## Build One Operation On Ubuntu VM

The folder is deployed here on the Ubuntu VM:

```bash
/home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware
```

Example for `set`:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug
cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/common/scan_debug_hw_single_common.h .
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/set/scan_debug_set_firmware.c scan_debug.c
make clean hex
```

Output:

```text
~/caravel_board/firmware/chipignite/scan_debug/scan_debug.hex
```

Swap the second `cp` command for the operation you want:

```bash
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/read/scan_debug_read_firmware.c scan_debug.c
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/form/scan_debug_form_firmware.c scan_debug.c
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/set/scan_debug_set_firmware.c scan_debug.c
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/reset/scan_debug_reset_firmware.c scan_debug.c
```

To build the combined all-operation firmware instead:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug
cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/scan_debug_hw_firmware.c scan_debug.c
make clean hex
```

## Hardware Check

After flashing and resetting the board, check UART for:

```text
[SCAN-HW] scan_debug hardware firmware testbench start
[SCAN-HW][READ_PLACEHOLDER]...
[SCAN-HW][FORM_PLACEHOLDER]...
[SCAN-HW][SET]...
[SCAN-HW][RESET]...
[SCAN-HW][SUMMARY][PASS] firmware scan_debug sequence completed
```

Use a logic analyzer or scope on GPIO21, GPIO22, GPIO35, and GPIO36 to confirm
the pin-level scan sequence.

For quieter UART logs:

```bash
make clean hex USER_DEFINES="-DSCAN_TRACE_EACH_BIT=0"
```

If future RTL exposes readable scan-debug status registers, compile with:

```bash
make clean hex USER_DEFINES="-DSCAN_DEBUG_STATUS_BASE=0x30000100u"
```
