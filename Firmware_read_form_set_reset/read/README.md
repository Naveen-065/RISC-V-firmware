# Hardware Firmware: Scan Debug Read Placeholder

File:

```text
scan_debug_read_firmware.c
```

This is a hardware firmware testbench for the READ placeholder scan-debug case.
Current `ScanDebug.v` has only `op_set`, so READ is not a real RTL opcode yet.
The firmware still drives the scan pins and checks/logs the expected scan route.

Scan word:

```text
{op_set=0, sl=5, bl=4, wl=3}
```

Build on Ubuntu:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug
cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/read/scan_debug_read_firmware.c scan_debug.c
make clean hex
```
