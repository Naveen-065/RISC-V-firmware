# Three-Caravel TMR UART and Reliable AI Cocotb Simulation

This directory contains a fast system-level cocotb simulation for the TMR UART
architecture around the PARTCL Caravel/X1 accelerator concept.

The testbench models three clock-synchronous Caravel endpoints. Each endpoint
contains:

- a UART firmware bridge connected to external GPIO-style UART pins,
- an X1/ReRAM-like memory array used for program/read storage,
- a behavioral 2x2 slice of the PARTCL systolic matrix multiplier.

The three endpoints receive the same external UART command through a ZES400
qual-channel voter. Their UART TX outputs are then voted into one external user
RX line. This is the same logical connection as the TMR diagram:

```text
external user TX lanes -> pRX1/pRX2/pRX3 voter -> all three Caravel RX pins
three Caravel TX pins  -> sTX1/sTX2/sTX3 voter -> external user RX
```

All three modeled Caravel UART endpoints share `clk_i`. The cocotb UART driver
changes bits on the falling edge and holds them for `CLKS_PER_BIT`, so all
lanes are synchronized before they enter the voter.

## PARTCL Connection

The remote PARTCL checkout at `~/partcl_neuromorphic_compute` includes:

- `verilog/rtl/user_project_wrapper.v`
  - maps `Neuromorphic_X1_wb` at `0x3000_0000`,
  - maps `mat_mult_wb` at `0x3100_0000`.
- `verilog/rtl/mat_mult_wb.v`
  - implements an 8x8 systolic array matrix multiplier,
  - accepts 8-bit signed/unsigned A and B operands,
  - writes 32-bit accumulated C results,
  - exposes A, B, and C caches through Wishbone.

This testbench uses a behavioral 2x2 slice of that dataflow so the TMR behavior
is easy to see in a short UART simulation. Conceptually, each replicated
Caravel performs:

```text
UART command
  -> RISC-V firmware bridge
  -> systolic matrix multiply
  -> store C words in X1/ReRAM result addresses
  -> read X1/ReRAM result words
  -> UART transmit through TMR voter
  -> external user
```

The 2x2 matrix result is stored in X1/ReRAM-like memory at:

```text
0x80: C00
0x81: C01
0x82: C10
0x83: C11
```

The model also stores packed A and B operand words at `0x20` and `0x24`, which
mirrors the idea of staging accelerator operands in local Caravel/X1-accessible
memory.

## UART Protocol

UART is 8N1, LSB-first, idle-high.

Program an arbitrary X1/ReRAM word:

```text
0xA5 0x50 <addr> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
```

Read an X1/ReRAM word:

```text
0xA5 0x52 <addr>
```

Run a 2x2 signed systolic matrix multiply:

```text
0xA5 0x4D <A00> <A01> <A10> <A11> <B00> <B01> <B10> <B11>
```

Responses are always six bytes:

```text
0x5A <addr> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
```

For matrix completion, the status response is:

```text
0x5A 0xF0 0xA1 0x00 0x00 0x01
```

## Reliable AI Test

The AI test sends:

```text
A = [[ 2, -1],
     [ 3,  4]]

B = [[ 5,  6],
     [-2,  7]]
```

Expected result:

```text
C = A * B
  = [[12,  5],
     [ 7, 46]]
```

The test checks three reliability cases:

1. Normal compute with one bad incoming user UART lane.
   - One replicated RX lane is inverted while the matrix command is sent.
   - The RX-side voter still delivers the correct command to all three
     Caravels.
   - C is stored to the X1/ReRAM result addresses and read back correctly.

2. One bad Caravel AI result.
   - One replicated Caravel intentionally stores `C00 = 13` instead of `12`.
   - The other two Caravels store `C00 = 12`.
   - The source TX voter returns the correct external word `0x0000000C`.

3. Two bad Caravel AI results.
   - Two replicated Caravels intentionally store `C00 = 13`.
   - The voter majority is now wrong, so the external user receives
     `0x0000000D`.
   - This demonstrates the expected TMR limit: one faulty lane is mitigated,
     two faulty lanes are not.

The data for the matrix and fault cases is recorded in
`data/ai_matmul_fault_cases.json`.

## Files

```text
rtl/tmr_caravel_uart_system.v
  ZES400-style bit voters, UART RX/TX blocks, three Caravel/X1 endpoint models,
  X1/ReRAM memory, and behavioral systolic matrix multiply.

tests/test_tmr_caravel_uart.py
  Cocotb tests for basic program/read UART TMR and reliable AI matrix output.

data/ai_matmul_fault_cases.json
  Machine-readable matrix operands, expected result words, UART response words,
  and fault-injection expectations.
```

## Run

On a machine with Icarus Verilog and cocotb:

```bash
cd caravel_tmr_uart_cocotb
python3 -m venv .venv
. .venv/bin/activate
pip install cocotb
make
```

Inside the remote Caravel workspace, use the existing cocotb environment:

```bash
cd ~/caravel_user_Neuromorphic_X1_32x32/caravel_tmr_uart_cocotb
PATH=~/caravel_user_Neuromorphic_X1_32x32/venv-cocotb/bin:$PATH make
```

Optional:

```bash
make clean
CLKS_PER_BIT=16 make
```

Expected result:

```text
TESTS=2 PASS=2 FAIL=0 SKIP=0
```

