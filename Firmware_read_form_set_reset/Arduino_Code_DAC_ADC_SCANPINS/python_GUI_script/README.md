# ReRAM Teensy Firmware and Serial Dashboard

This repository contains firmware and utilities for controlling a ReRAM test setup with a Teensy 4.1. The main firmware drives DAC channels, reads shunt voltages through ADS1115 ADCs, calculates ReRAM resistance, and streams formatted measurement data over USB serial. The provided Python Tkinter dashboard connects to the Teensy, sends operation commands, displays live measurements, and logs parsed data to CSV.

## Main Files

| File | Purpose |
| --- | --- |
| `ReRAM_powerup_pulse_fixed.ino` | Main Teensy 4.1 ReRAM control firmware. Handles serial commands, DAC pulse generation, ADC reads, resistance calculation, and serial logging. |
| Python `TeensySerialGUI` dashboard | Tkinter GUI provided with this project. Selects COM port, sends commands, reads serial data, updates live values, and writes CSV logs. |
| `Teensy_ADC_DAC_Caravel_Setup.drawio` | Setup/wiring diagram for the Teensy, DAC, ADC, and Caravel/ReRAM test hardware. |
| `DAC81416_Teensy41_SPI/` | DAC81416 and Teensy SPI support/test sketch. |
| `teensy_scan_reset_00_5mhz/` | Additional Teensy scan/reset firmware. |
| `caravel_scan_debug_test/` | Caravel scan debug C test code. |
| `Neuromorphic_X1-Sindhu/` | RTL sources for the neuromorphic/scan-debug design. |

## Hardware Overview

- Controller: Teensy 4.1 over USB serial.
- DAC interface: DAC81416-style DAC access through `DAC_read.h`.
- ADC: ADS1115 at I2C address `0x48` using `GAIN_SIXTEEN` and `RATE_ADS1115_475SPS`.
- Shunt resistor: `1000 Ohm`.

## Software Requirements

For the Teensy firmware:

- Arduino IDE or Teensyduino.
- Teensy 4.1 board support.
- Project-local `DAC_read.h`.
- Libraries:
  - `Adafruit_ADS1X15`
  - `TeensyThreads`

For the Python dashboard:

- Python 3.
- `pyserial`.
- `tkinter`, usually included with Python on Windows.

Install the Python dependency:

```powershell
python -m pip install pyserial
```

## Firmware Setup

1. Open `ReRAM_powerup_pulse_fixed.ino` in Arduino IDE or Teensyduino.
2. Select board `Teensy 4.1`.
3. Confirm that `DAC_read.h` and the required libraries are available.
4. Upload the firmware to the Teensy.
5. Connect the Teensy USB port to the PC.

The firmware initializes serial with:

```cpp
Serial.begin(115200);
```

The Python GUI currently opens the selected COM port at `2000000` baud. On Teensy native USB serial, the configured baud value is usually not the real transfer speed, but keep both sides consistent if this code is moved to a hardware UART.

## Running the Python Dashboard

1. Start the Python GUI script that contains `TeensySerialGUI`.
2. Click `Refresh` to scan available COM ports.
3. Select the Teensy COM port.
4. Click `Read` before sending commands. This opens the serial port and starts the background reader thread.
5. Use `SET`, `RESET`, `LOOP`, or `VERIFY` to send commands to the Teensy.
6. Click `Stop` to stop reading, close the serial port, and close any active CSV file.

## Command and Operation Mapping

The GUI sends newline-terminated ASCII commands to the Teensy. The Teensy reads the command with `Serial.readStringUntil('\n')`, trims whitespace, and selects the operation from the command string.

Current usage note: the `SET` and `RESET`/`CLEAR` command branches are available in the firmware, but they are not the main commands being used right now. The current workflow mainly uses `LOOP` to run the selected pulse operation and `VERIFY` to read/verify the device state.

| GUI control | Serial command sent | Teensy `mode` | Firmware branch | Selected operation |
| --- | --- | ---: | --- | --- |
| `Read` | None | N/A | GUI only | Opens the selected COM port at `2000000` baud, starts the read thread, resets GUI tracking, and begins parsing serial output. |
| `SET` | `SET\n` | `1` | `if (command == "SET")` | Runs a SET sweep. This branch exists in the firmware but is not the main command currently used. Word-line voltage steps from `0.0 V` to `< 0.9 V`; SET/bit-line voltage steps from `0.0 V` to `< 2.0 V`; source line is held at `0 V`. After each pulse it calls `read_all(1)` and `loggerOut()`. The routine halts when `res_u1bl1 <= 20000` and the resistance is nonzero. |
| `RESET` | `CLEAR\n` | `2` | `else if (command == "CLEAR")` | Runs a RESET sweep. This branch exists in the firmware but is not the main command currently used. Word-line voltage steps from `1.4 V` to `< 3.0 V`; reset/source-line voltage steps from `0.0 V` to `< 3.5 V`; bit line is held at `0 V`. After each pulse it calls `read_all(1)` and `loggerOut()`. The routine halts when `res_u1bl1 >= 375000`. |
| `LOOP` | `LOOP\n` | `3` | `else if (command == "LOOP")` | Enters a loop that currently calls `setting_constant_Pulse()`. The reset version, `resetting_constant_Pulse()`, exists but is commented out in the `LOOP` branch. |
| `VERIFY` | `VERIFY\n` | `4` | `else if (command == "VERIFY")` | Calls `read_all(1000000)` for repeated read pulses and serial logging. |
| `Stop` | None | N/A | GUI only | Stops the Python read loop, closes the serial port, updates the status to `Stopped`, and closes the active CSV file. |
| Unknown or empty command | None/other | `0` | final `else` | Calls `dac_stndby()` and leaves the firmware in standby mode. |

Important command naming detail: the GUI button is labeled `RESET`, but it sends the command string `CLEAR`. The Teensy firmware expects `CLEAR` for the reset branch.

## LOOP Mode Details

In the checked-in firmware, `LOOP` calls:

```cpp
setting_constant_Pulse();
```

That function:

- Calls `read_all_set(1)` before each SET pulse.
- Uses `res_u1bl1` as the in-loop resistance.
- Applies a constant SET pulse:
  - BL through `u1wl1`: `2.6 V`
  - WL through `u3sl1`: `1.7 V`
  - SL through `u2bl1`: `0 V`
- Logs each step through `loggerOut()`.
- Stops when `res_u1bl1 <= 20000` and nonzero.
- Then remains in a `while (1)` loop while continuing `dac_powerup_continuous_nonblocking()`.

The firmware also includes `resetting_constant_Pulse()`, but the `LOOP` command does not currently call it. To change the operation from SET to RESET, edit the `LOOP` branch by commenting the SET function and uncommenting the RESET function:

```cpp
//setting_constant_Pulse();
resetting_constant_Pulse();
```

To change back from RESET to SET, comment the RESET function and uncomment the SET function:

```cpp
setting_constant_Pulse();
//resetting_constant_Pulse();
```

After changing this selection, upload the updated firmware to the Teensy again. The GUI can continue using the same `LOOP` button; the selected operation is controlled by which function is active inside the firmware `LOOP` branch.

## Serial Output Format

The Teensy logs data through `loggerOut()` in one long line. The Python GUI parses lines that begin with `Vsh1:` and match this structure:

```text
Vsh1: <mV>mV Ish1: <uA>uA Vram1: <mV>mV RES1: <Ohms> Ohms
Vsh2: <mV>mV Ish2: <uA>uA Vram2: <mV>mV RES2: <Ohms> Ohms
Vsh3: <mV>mV Ish3: <uA>uA Vram3: <mV>mV RES3: <Ohms> Ohms
Vsh4: <mV>mV Ish4: <uA>uA Vram4: <mV>mV RES4: <Ohms> Ohms
Iter: <count> Steps Pass: <count> Cycles Mode: <mode>
U1Vbl1: <mV>mV U1Vbl2: <mV>mV U2Vbl1: <mV>mV U2Vbl2: <mV>mV
U1Vwl1: <mV>mV U2Vwl1: <mV>mV U1Vsl1: <mV>mV U2Vsl1: <mV>mV
LRS: <Ohms> Ohms HRS: <Ohms> Ohms OpState: <state>
```

The GUI displays four measurement channels:

- `Vsh` in mV.
- `Ish` in uA.
- `Vram` in mV.
- `RES` in Ohms.

It also displays:

- `Iter`
- `Pass`
- `Mode`
- `Operation State`
- `LRS`
- `HRS`
- Target resistance, if a `ResIdx:` line is received.

## CSV Logging

The GUI writes CSV rows with these columns:

```text
Timestamp,
Vsh1 (mV), Ish1 (uA), Vram1 (mV), RES1 (Ohms),
Vsh2 (mV), Ish2 (uA), Vram2 (mV), RES2 (Ohms),
Vsh3 (mV), Ish3 (uA), Vram3 (mV), RES3 (Ohms),
Vsh4 (mV), Ish4 (uA), Vram4 (mV), RES4 (Ohms),
Iter, Pass, Mode,
V-U1BL1, V-U1BL2, V-U2BL1, V-U2BL2,
V-U1WL1, V-U2WL1, V-U1SL1, V-U2SL1,
LRS (Ohms), HRS (Ohms), OperationState
```

CSV files are currently written to this hard-coded folder in the GUI:

```text
C:/Users/SER7/Documents/GitHub/rram_tests/teensy/GUI_Python/
```

When the GUI receives:

```text
ResIdx:<index>
```

it maps `<index>` into the active `resistance_states` array, updates the target resistance label, and creates a file named:

```text
ReRAM_<resistance>ohm_<timestamp>.csv
```

The current active GUI target list is the 4-bit list:

```python
[8000, 8550, 9170, 9900, 10750, 11800, 13000, 14500,
 16400, 18900, 22200, 27000, 34500, 47600, 76900, 200000]
```

Note: in the provided Teensy firmware, `loggerOut()` does not print `ResIdx:`. If the firmware does not emit `ResIdx:` elsewhere, the GUI can still display live parsed measurements, but it will not create a per-resistance CSV file from `ResIdx:` events.

## Resistance and State Values

The firmware uses:

```cpp
const int resistance_values[3] = {20000, 50919, 66289};
int current_resistanceLRS = resistance_values[0];
int current_resistanceHRS = resistance_values[1];
```

These values are reported in each serial log line as `LRS` and `HRS`.

The operation state is printed as `OpState`:

- `-`: idle or no final operation state.
- `1`: SET success inside `setting_constant_Pulse()`.
- `0`: RESET success inside `resetting_constant_Pulse()`.

## Useful Code Notes

- The GUI command button labeled `RESET` sends `CLEAR`, not `RESET`.
- The firmware command parser is inside a `while(!Serial)` block. If commands are not processed after the GUI connects, check this condition first.
- Several SET/RESET routines intentionally end in `while (1)` after the target resistance condition is met. A Teensy reset or firmware change may be required before running another command.
- `srCycles` is checked in the `LOOP` branch, but the increment is commented out in the shown read routines. As written, `srCycles > 1000` may never become true.
- The GUI creates CSV files only when it sees `ResIdx:<index>`.
- The GUI regex expects the exact `loggerOut()` label order. If labels or spacing change in the firmware, update `self.data_pattern` in the Python GUI.

## Typical Workflow

1. Power and connect the ReRAM test hardware.
2. Upload `ReRAM_powerup_pulse_fixed.ino` to the Teensy 4.1.
3. Start the Python serial dashboard.
4. Select the Teensy COM port and click `Read`.
5. Click one operation button:
   - `LOOP` for the currently selected SET or RESET constant-pulse loop.
   - `VERIFY` for repeated read/verify logging.
   - `SET` and `RESET`/`CLEAR` are available firmware branches, but they are not the main commands currently used in this workflow.
6. Watch the live `Vsh`, `Ish`, `Vram`, `RES`, `Mode`, `LRS`, `HRS`, and `OpState` fields.
7. Click `Stop` when finished to close the serial connection and CSV file.

## Safety Checklist

Before running SET, RESET, LOOP, or VERIFY:

- Confirm the DUT and probe/card wiring matches `Teensy_ADC_DAC_Caravel_Setup.drawio`.
- Confirm DAC channel mappings match the comments in the active firmware branch.
- Confirm SET/RESET voltage limits are appropriate for the connected ReRAM device.
- Confirm the CSV output directory exists or update the GUI path before logging.
- Run one short read/verify first if the device state is unknown.
