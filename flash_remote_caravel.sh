#!/usr/bin/env bash
set -euo pipefail

REMOTE="${REMOTE:-ubuntu-24-04@100.98.132.51}"
REMOTE_REL_DIR="${REMOTE_REL_DIR:-caravel_board/firmware/chipignite/scan_debug}"
REMOTE_HEX="${REMOTE_HEX:-scan_debug.hex}"
LOCAL_HEX="${1:-}"

usage() {
  cat <<'USAGE'
Usage:
  ./flash_remote_caravel.sh [local_hex]

Defaults:
  REMOTE=ubuntu-24-04@100.98.132.51
  REMOTE_REL_DIR=caravel_board/firmware/chipignite/scan_debug
  REMOTE_HEX=scan_debug.hex

Examples:
  # Flash the existing scan_debug.hex already on ubuntu-24:
  ./flash_remote_caravel.sh

  # Copy a local hex to ubuntu-24 as scan_debug.hex, then flash it:
  ./flash_remote_caravel.sh build/scan_debug.hex

  # If more than one FTDI 0403:6014 device is attached, select one manually:
  USB_BUSDEV=002/003 ./flash_remote_caravel.sh build/scan_debug.hex

  # Force an interactive remote terminal if sudo needs a password:
  REMOTE_TTY=1 ./flash_remote_caravel.sh build/scan_debug.hex
USAGE
}

if [[ "${LOCAL_HEX}" == "-h" || "${LOCAL_HEX}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ -n "${LOCAL_HEX}" ]]; then
  if [[ ! -f "${LOCAL_HEX}" ]]; then
    echo "Local hex not found: ${LOCAL_HEX}" >&2
    exit 1
  fi

  echo "== Copying ${LOCAL_HEX} to ${REMOTE}:${REMOTE_REL_DIR}/${REMOTE_HEX}"
  scp "${LOCAL_HEX}" "${REMOTE}:${REMOTE_REL_DIR}/${REMOTE_HEX}"
fi

quote() {
  printf "%q" "$1"
}

echo "== Flashing ${REMOTE}:${REMOTE_REL_DIR}/${REMOTE_HEX}"
ssh_tty_flag="-T"
if [[ "${REMOTE_TTY:-0}" == "1" ]]; then
  ssh_tty_flag="-tt"
fi

ssh "${ssh_tty_flag}" "${REMOTE}" \
  "REMOTE_REL_DIR=$(quote "${REMOTE_REL_DIR}") REMOTE_HEX=$(quote "${REMOTE_HEX}") USB_BUSDEV=$(quote "${USB_BUSDEV:-}") bash -s" <<'REMOTE_SCRIPT'
set -euo pipefail

cd "${HOME}/${REMOTE_REL_DIR}"

echo "== Remote: $(hostname) as $(whoami)"
echo "== Firmware image:"
ls -l "${REMOTE_HEX}"

if [[ -n "${USB_BUSDEV}" ]]; then
  busdev="${USB_BUSDEV}"
else
  mapfile -t ftdi_devices < <(lsusb -d 0403:6014 || true)

  if [[ "${#ftdi_devices[@]}" -eq 0 ]]; then
    echo "No FTDI 0403:6014 device found on the remote PC." >&2
    exit 2
  fi

  if [[ "${#ftdi_devices[@]}" -gt 1 ]]; then
    printf 'More than one FTDI 0403:6014 device found:\n' >&2
    printf '  %s\n' "${ftdi_devices[@]}" >&2
    echo "Set USB_BUSDEV, for example: USB_BUSDEV=002/003 ./flash_remote_caravel.sh" >&2
    exit 2
  fi

  busdev="$(printf '%s\n' "${ftdi_devices[0]}" | awk '{print $2"/"substr($4,1,3)}')"
fi

echo "== FTDI USB bus/dev: ${busdev}"
usb_path="/dev/bus/usb/${busdev}"
if [[ -r "${usb_path}" && -w "${usb_path}" ]]; then
  echo "== USB permissions already allow flashing: ${usb_path}"
else
  sudo chmod a+rw "${usb_path}"
fi

"${HOME}/caravel_venv/bin/python3" ../util/caravel_hkflash.py "${REMOTE_HEX}"
REMOTE_SCRIPT
