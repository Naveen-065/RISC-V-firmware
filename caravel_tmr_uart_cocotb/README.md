# Three-Caravel TMR UART Cocotb Simulation

This simulation models the UART-level system described by the ZES400
qual-channel voter diagram:

- Three clock-synchronous Caravel/X1 UART endpoints share one clock.
- The three Caravel TX lanes are voted into one external user RX lane.
- Three replicated external user TX lanes are voted into one Caravel RX lane.
- The voted Caravel RX lane is broadcast to all three Caravel/X1 endpoints.
- The external user can issue program and read commands to an X1-style memory.
- Single-lane UART input faults are mitigated; two-lane faults corrupt the
  voted output.

The Caravel endpoints are behavioral models of the RISC-V firmware bridge that
would sit behind GPIO UART pins and perform X1 program/read operations. This
keeps the system-level TMR UART behavior runnable with plain Icarus/cocotb even
when the full `caravel_cocotb` package and Caravel management SoC flow are not
installed.

All three modeled Caravel UART endpoints share `clk_i`; the cocotb UART driver
changes bits on the falling edge and holds them for `CLKS_PER_BIT`, so the three
UART lanes stay synchronized for the voter.

## Protocol

UART is 8N1, LSB-first, idle-high. Commands are sent by the external user:

```text
PROGRAM: 0xA5 0x50 <addr> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
READ:    0xA5 0x52 <addr>
```

Responses are:

```text
0x5A <addr> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
```

## Run

On Ubuntu:

```bash
cd caravel_tmr_uart_cocotb
python3 -m venv .venv
. .venv/bin/activate
pip install cocotb
make
```

Optional:

```bash
make clean
CLKS_PER_BIT=16 make
```

Inside the remote Caravel workspace, the same test can be run with the existing
cocotb environment:

```bash
cd ~/caravel_user_Neuromorphic_X1_32x32/caravel_tmr_uart_cocotb
PATH=~/caravel_user_Neuromorphic_X1_32x32/venv-cocotb/bin:$PATH make
```
