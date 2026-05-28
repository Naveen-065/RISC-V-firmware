# RISC-V Firmware Remote Flash Notes

This repository contains firmware intended to be flashed onto the Caravel board
connected to the remote Ubuntu PC over Tailscale.

## Remote Target

- Remote PC hostname: `ubuntu-24`
- Remote Tailscale IP: `100.98.132.51`
- Remote SSH user: `ubuntu-24-04`
- Remote firmware directory:
  `~/caravel_board/firmware/chipignite/scan_debug`
- Remote flash image:
  `~/caravel_board/firmware/chipignite/scan_debug/scan_debug.hex`
- Flash utility:
  `~/caravel_board/firmware/chipignite/util/caravel_hkflash.py`
- Python environment:
  `~/caravel_venv/bin/python3`

Do not commit passwords or private keys. The remote sudo password should be
entered only when prompted by SSH/sudo.

## Local Prerequisites

1. Tailscale must be running on this laptop and logged into the same tailnet as
   the remote PC.

   ```bash
   tailscale status
   tailscale ping --timeout=5s 100.98.132.51
   ```

2. SSH access from this laptop to the remote PC must work.

   ```bash
   ssh ubuntu-24-04@100.98.132.51 'hostname && whoami'
   ```

3. If SSH keys have not been installed yet, create/copy a key once:

   ```bash
   ssh-keygen -t ed25519 -a 100 -f ~/.ssh/id_ed25519
   ssh-copy-id -i ~/.ssh/id_ed25519.pub ubuntu-24-04@100.98.132.51
   ```

## Hardware Setup

The Caravel board is connected to the remote PC through an FTDI FT232H device.

Important jumper rule:

- Remove the UART mux jumper, `J2`, before flashing.
- Put `J2` back only when using UART/serial after flashing.

If `J2` is installed while flashing, the programming path is multiplexed away
from housekeeping SPI and `caravel_hkflash.py` may fail with invalid Caravel ID
reads such as:

```text
mfg        = ffff
product    = ff
project ID = 00000000
Incorrect MFG value, expected 0x0456.
```

or:

```text
mfg        = 0000
product    = 00
project ID = 00000000
Incorrect MFG value, expected 0x0456.
```

Expected ID when the board is correctly connected for flashing:

```text
mfg        = 0456
product    = 11
project ID = 23097d48
project ID = 12be90c4
```

## Cocotb-Style Scan Firmware

The current chip image is based on:

```text
cocotb_scan_debug_firmware.c
```

It is a chip-side C firmware version of the cocotb `ram_word` scan test found
on the remote PC at:

```text
~/caravel_user_Neuromorphic_X1_32x32/verilog/dv/cocotb/user_proj_tests/ram_word/ram_word.py
```

The firmware drives the same scan pins directly from the management CPU:

- `GPIO21`: `ScanInDR`
- `GPIO22`: `ScanInDL`
- `GPIO35`: `ScanInCC`, held low
- `GPIO36`: `TM`

The scan sequence follows the cocotb test flow:

1. Apply initial idle state.
2. Wait for stabilization.
3. Emit a firmware-ready checkpoint.
4. Run WB placeholders.
5. Run scan transaction `0x8000`.
6. Wait two idle cycles.
7. Run scan transaction `0x8822`.
8. Emit completion checkpoint and enter LED heartbeat.

UART prints are prefixed with `[COCOTB-SCAN]`, and the management GPIO LED is
pulsed at each major checkpoint so the code path is visible even without UART.

The current chip's Wishbone path is not working, so WB actions are placeholders
by default. The firmware prints the intended writes/reads but does not touch
`0x30000004` unless it is rebuilt with:

```c
#define ENABLE_WB_TOUCHES 1
```

Leave `ENABLE_WB_TOUCHES` at `0` for the currently connected chip.

### Build The Cocotb-Style Firmware

Copy the local source to the remote firmware directory and build:

```bash
scp cocotb_scan_debug_firmware.c ubuntu-24-04@100.98.132.51:/tmp/cocotb_scan_debug_firmware.c

ssh ubuntu-24-04@100.98.132.51 '
  cd ~/caravel_board/firmware/chipignite/scan_debug &&
  ts=$(date +%Y%m%d_%H%M%S) &&
  cp scan_debug.c scan_debug.c.backup_$ts &&
  cp /tmp/cocotb_scan_debug_firmware.c scan_debug.c &&
  make clean hex
'
```

The last flashed build used this remote backup name:

```text
scan_debug.c.backup_20260528_123418
```

After a successful build, the new image is:

```text
~/caravel_board/firmware/chipignite/scan_debug/scan_debug.hex
```

## Flash Using Helper Script

This repo includes a local helper:

```bash
./flash_remote_caravel.sh
```

That flashes the existing remote image:

```text
ubuntu-24-04@100.98.132.51:~/caravel_board/firmware/chipignite/scan_debug/scan_debug.hex
```

To copy a local `.hex` file to the remote PC as `scan_debug.hex` and then flash:

```bash
./flash_remote_caravel.sh path/to/your_firmware.hex
```

If the USB device permissions need sudo and the script needs an interactive
remote terminal:

```bash
REMOTE_TTY=1 ./flash_remote_caravel.sh path/to/your_firmware.hex
```

If more than one FTDI device is attached, select the bus/device manually:

```bash
USB_BUSDEV=002/003 ./flash_remote_caravel.sh path/to/your_firmware.hex
```

## Flash Using The Stock Remote Commands

This is the exact known-good command sequence run on the remote PC:

```bash
cd ~/caravel_board/firmware/chipignite/scan_debug

ls -l scan_debug.hex

BUSDEV=$(lsusb -d 0403:6014 | awk '{print $2"/"substr($4,1,3)}')
echo "$BUSDEV"
sudo chmod a+rw "/dev/bus/usb/$BUSDEV"

~/caravel_venv/bin/python3 ../util/caravel_hkflash.py scan_debug.hex
```

To run it from this laptop in one SSH command:

```bash
ssh -tt ubuntu-24-04@100.98.132.51 'cd ~/caravel_board/firmware/chipignite/scan_debug && ls -l scan_debug.hex && BUSDEV=$(lsusb -d 0403:6014 | awk '\''{print $2"/"substr($4,1,3)}'\'') && echo "$BUSDEV" && sudo chmod a+rw "/dev/bus/usb/$BUSDEV" && ~/caravel_venv/bin/python3 ../util/caravel_hkflash.py scan_debug.hex'
```

## Known-Good Flash Result

A successful run should include:

```text
Success: Found one matching FTDI device at ftdi://ftdi:232h:2:3/1
Caravel data:
   mfg        = 0456
   product    = 11
   project ID = 23097d48
   project ID = 12be90c4

Resetting Flash...
status = 0x00
JEDEC = ef4016
Erasing chip...
...done
```

The write/verify should finish with all page compares successful. The exact
byte count depends on the firmware image. The older reset-sequence image was
`9216` bytes; the current cocotb-style firmware is `8192` bytes.

```text
total_bytes = 8192
verifying...
addr 0x0: read compare successful
...
addr 0x1f00: read compare successful

total_bytes = 8192
```

In the successful session, `pll_trim` printed:

```text
pll_trim = b'ffefff03'
```

After flashing, press the Caravel board reset button if the firmware does not
start automatically.

For UART logs after flashing:

1. Remove `J2` before flashing.
2. Flash and verify the image.
3. Put `J2` back for UART/serial.
4. Press reset on the board.
5. Watch for `[COCOTB-SCAN]` UART messages and the checkpoint LED pulses.
