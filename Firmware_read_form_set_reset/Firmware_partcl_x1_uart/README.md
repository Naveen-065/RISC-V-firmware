# PARTCL 2x2 to X1 UART Firmware

This firmware is a RISC-V C skeleton for the Caravel management core. It has
been corrected against the local X1 RTL at:

```text
C:\Users\elesamj\Documents\GitHub\Neuromorphic_X1\RTL
```

Flow:

1. Select PARTCL/Williams `mat_mult_wb` at `0x3100_0000`.
2. Load a 2x2 signed matrix into the top-left of PARTCL's 8x8 operand caches.
3. Start PARTCL matrix multiplication and read C00/C01/C10/C11.
4. Select X1 at the exact Wishbone command address `0x3000_0004`.
5. Send X1 command packets that program the C values into cells `(0,0)`,
   `(0,1)`, `(1,0)`, and `(1,1)`.
6. Send X1 read packets for those cells.
7. Transmit the Wishbone readback words from X1 on UART TX.

UART RX is not used by this firmware. The RISC-V performs all Wishbone writes
and reads internally; the external UART/TMR path only receives transmitted data.

## Important X1 RTL Detail

The X1 RTL is not a random-access 32-bit RAM behind `0x3000_0004`.
`Wb_slave.v` only accepts Wishbone transactions when:

```verilog
i_wb_addr == 32'h3000_0004
```

Writes to that address are command packets:

```text
bits [31:30] mode
bits [29:25] row
bits [24:20] col
bit  [19]    read error register select
bits [7:0]   PWM/program data
```

The first three packets after reset are consumed as X1 configuration packets:

```text
0x64720003  target set thresholds
0x000B462B  target reset thresholds
0x00050005  timing values
```

Because the current X1 interface accepts only an 8-bit PWM/program value, the
firmware clamps each PARTCL 32-bit signed result into `0..255` before sending it
to X1. The X1 readback returned on UART is the RTL's scratchpad/TDC readback
word, not a direct digital copy of the original PARTCL C-cache word.

## PARTCL Map Used

The firmware defaults match the `mat_mult_wb` map used in the remote cocotb
setup:

```text
0x3100_0000 CTRL
0x3100_0004 STATUS
0x3100_0100 Matrix A cache, 16 packed words
0x3100_0200 Matrix B cache, 16 packed words
0x3100_0400 Matrix C cache, 64 result words
```

A and B store four signed 8-bit values per 32-bit Wishbone word. For the 2x2
example, the firmware writes only word-column 0 of rows 0 and 1.

Default matrix:

```text
A = [[ 2, -1],
     [ 3,  4]]

B = [[ 5,  6],
     [-2,  7]]

C = [[12,  5],
     [ 7, 46]]
```

For that default matrix, the concrete PARTCL writes are:

```text
0x3100_0100 = 0x0000_FF02  A row 0, cols 0..3 = [2, -1, 0, 0]
0x3100_0108 = 0x0000_0403  A row 1, cols 0..3 = [3,  4, 0, 0]
0x3100_0200 = 0x0000_0605  B row 0, cols 0..3 = [5,  6, 0, 0]
0x3100_0208 = 0x0000_07FE  B row 1, cols 0..3 = [-2, 7, 0, 0]
```

The concrete PARTCL result reads are:

```text
0x3100_0400  C00
0x3100_0404  C01
0x3100_0420  C10
0x3100_0424  C11
```

For the default result values, the concrete X1 packets after the three config
packets are:

```text
0x8000_000C  program/set row 0, col 0, PWM 12
0x8010_0005  program/set row 0, col 1, PWM 5
0x8200_0007  program/set row 1, col 0, PWM 7
0x8210_002E  program/set row 1, col 1, PWM 46

0x4000_0000  read row 0, col 0
0x4010_0000  read row 0, col 1
0x4200_0000  read row 1, col 0
0x4210_0000  read row 1, col 1
```

UART output format:

```text
0x5A <result-id> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
```

Result IDs:

```text
0x80 = X1 readback for C00 cell (row 0, col 0)
0x81 = X1 readback for C01 cell (row 0, col 1)
0x82 = X1 readback for C10 cell (row 1, col 0)
0x83 = X1 readback for C11 cell (row 1, col 1)
0xFE = error/status packet
```

Example compile command:

```bash
riscv32-unknown-elf-gcc -march=rv32imc -mabi=ilp32 \
  -Os -ffreestanding -nostdlib \
  partcl_x1_2x2_uart.c -o partcl_x1_2x2_uart.elf
```

In a real Caravel firmware tree, link this source with the normal Caravel
startup/linker files and replace `UART_GPIO_OUT_REG`/`UART_TX_BIT` if your board
uses a different UART TX register or pin.
