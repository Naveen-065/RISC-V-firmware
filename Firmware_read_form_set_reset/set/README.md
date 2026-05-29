# Hardware Firmware: Scan Debug Set

File:

```text
scan_debug_set_firmware.c
```

This is a hardware firmware testbench for the real current scan-debug SET
polarity.

Scan word:

```text
{op_set=1, sl=11, bl=10, wl=9}
```

Build on Ubuntu:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug
cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/set/scan_debug_set_firmware.c scan_debug.c
make clean hex
```
