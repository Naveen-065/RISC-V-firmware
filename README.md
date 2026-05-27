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

The write/verify should finish with all page compares successful:

```text
total_bytes = 9216
verifying...
addr 0x0: read compare successful
...
addr 0x2300: read compare successful

total_bytes = 9216
```

In the successful session, `pll_trim` printed:

```text
pll_trim = b'ffefff03'
```

After flashing, press the Caravel board reset button if the firmware does not
start automatically.
