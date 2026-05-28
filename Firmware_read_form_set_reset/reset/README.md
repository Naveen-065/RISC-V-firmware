# Hardware Firmware: Scan Debug Reset

File:

```text
scan_debug_reset_firmware.c
```

This is a hardware firmware testbench for the real current scan-debug RESET
polarity.

Scan word:

```text
{op_set=0, sl=14, bl=13, wl=12}
```

Build on Ubuntu:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug
cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/common/scan_debug_hw_single_common.h .
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/reset/scan_debug_reset_firmware.c scan_debug.c
make clean hex
```
