# Hardware Firmware: Scan Debug Form Placeholder

File:

```text
scan_debug_form_firmware.c
```

This is a hardware firmware testbench for the FORM placeholder scan-debug case.
Current `ScanDebug.v` has only `op_set`, so FORM is not a real RTL opcode yet.
The firmware uses SET-style polarity while clearly labeling the UART output as
`FORM_PLACEHOLDER`.

Scan word:

```text
{op_set=1, sl=8, bl=7, wl=6}
```

Build on Ubuntu:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug
cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/common/scan_debug_hw_single_common.h .
cp /home/ubuntu-24-04/Downloads/Neuromorphic_X1-Sindhu/rtl_scan_debug_testbenches/firmware/form/scan_debug_form_firmware.c scan_debug.c
make clean hex
```
