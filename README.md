# RISC-V Firmware Remote Flash Notes

This repository contains chip firmware and helper scripts for the Caravel board
connected to the remote Ubuntu PC over Tailscale.

## Current Firmware

The current firmware source is:

```text
cocotb_scan_debug_firmware.c
```

It is a chip-side version of the cocotb `ram_word` scan test. It drives the
scan/debug GPIOs from the management CPU and prints progress over UART.

Behavior:

- Prints UART checkpoints prefixed with `[COCOTB-SCAN]`.
- Toggles the management GPIO LED at each major checkpoint.
- Configures scan pins as management outputs.
- Drives the cocotb scan sequence:
  - transaction `0x8000`
  - transaction `0x8822`
  - 16 bits, LSB first
  - `TM` high during the transaction
  - 4 extra `TM` high cycles after each shift
- Keeps `GPIO35` low as `ScanInCC`.
- Leaves Wishbone operations as placeholders because WB is not working on the
  currently connected chip.

The firmware does not access `0x30000004` unless built with:

```c
#define ENABLE_WB_TOUCHES 1
```

Default is safe for the current chip:

```c
#define ENABLE_WB_TOUCHES 0
```

## GPIO Mapping

```text
GPIO21 -> ScanInDR
GPIO22 -> ScanInDL
GPIO35 -> ScanInCC
GPIO36 -> TM
```

The firmware follows the executable cocotb source: `ScanInDR` is driven low
during idle, shifting, after-shift, and final idle. The old cocotb comment says
`ScanInDR` may go high after completion, but the actual Python code drives it
low.

## Remote Target

```text
Remote PC:       ubuntu-24
Tailscale IP:    100.98.132.51
SSH user:        ubuntu-24-04
Firmware dir:    ~/caravel_board/firmware/chipignite/scan_debug
Flash utility:   ~/caravel_board/firmware/chipignite/util/caravel_hkflash.py
Python venv:     ~/caravel_venv/bin/python3
```

Do not commit passwords, private keys, or sudo secrets.

## Hardware Jumper Rule

The UART and housekeeping SPI programming path are multiplexed by jumper `J2`.

- Remove `J2` before flashing.
- Put `J2` back after flashing if you want UART logs.
- Press board reset after putting `J2` back.

If `J2` is installed while flashing, the flash utility may find the FTDI device
but fail Caravel identification with values like `mfg=ffff` or `mfg=0000`.

Expected good Caravel ID:

```text
mfg        = 0456
product    = 11
project ID = 23097d48
project ID = 12be90c4
```

## Build On Remote PC

Copy the local firmware source to the remote scan-debug directory and rebuild:

```bash
scp cocotb_scan_debug_firmware.c ubuntu-24-04@100.98.132.51:/tmp/cocotb_scan_debug_firmware.c

ssh ubuntu-24-04@100.98.132.51 '
  set -e
  cd ~/caravel_board/firmware/chipignite/scan_debug
  cp scan_debug.c scan_debug.c.backup_$(date +%Y%m%d_%H%M%S)
  cp /tmp/cocotb_scan_debug_firmware.c scan_debug.c
  make clean hex
'
```

The build output is:

```text
~/caravel_board/firmware/chipignite/scan_debug/scan_debug.hex
```

## Flash

Remove `J2`, then run the stock remote flash flow:

```bash
ssh -tt ubuntu-24-04@100.98.132.51 '
  cd ~/caravel_board/firmware/chipignite/scan_debug &&
  ls -l scan_debug.hex &&
  BUSDEV=$(lsusb -d 0403:6014 | awk '\''{print $2"/"substr($4,1,3)}'\'') &&
  echo "$BUSDEV" &&
  sudo chmod a+rw "/dev/bus/usb/$BUSDEV" &&
  ~/caravel_venv/bin/python3 ../util/caravel_hkflash.py scan_debug.hex
'
```

Successful flash should end with all read compares passing.

Latest known-good run:

```text
scan_debug.hex size: 25110 bytes
programmed bytes:   8192
verify:             all read compares successful
pll_trim:           b'ffefff03'
```

## UART After Flashing

After flash verify succeeds:

1. Put `J2` back.
2. Press the board reset button.
3. Open the UART monitor on the remote PC.

The firmware prints messages like:

```text
[COCOTB-SCAN] firmware start: chip-side version of cocotb ram_word scan test
[COCOTB-SCAN][CP 0x00000001] configure scan GPIO as management outputs
[COCOTB-SCAN][WB-PLACEHOLDER] ... SKIPPED_current_chip_wb_not_working
[COCOTB-SCAN][TXN] start scan_transaction_0 data=0x00008000
[COCOTB-SCAN][TXN] start scan_transaction_1 data=0x00008822
[COCOTB-SCAN][DONE] complete flow executed; entering LED heartbeat
```

The LED pulses during checkpoints and then enters a heartbeat loop when the
complete flow has executed.

## Helper Script

This repo also includes:

```bash
./flash_remote_caravel.sh
```

It can flash an existing remote `scan_debug.hex`:

```bash
./flash_remote_caravel.sh
```

Or copy a local `.hex` to the remote PC as `scan_debug.hex` and flash it:

```bash
./flash_remote_caravel.sh path/to/firmware.hex
```

Use an interactive remote terminal if sudo needs a password:

```bash
REMOTE_TTY=1 ./flash_remote_caravel.sh path/to/firmware.hex
```
