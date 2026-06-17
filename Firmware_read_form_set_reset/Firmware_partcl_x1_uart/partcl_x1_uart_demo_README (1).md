# PARTCL X1 UART Demo Firmware

This firmware demonstrates a RISC-V controlled PARTCL-to-X1 flow using the
Caravel user Wishbone interface and UART TX output.

## What The Firmware Does

1. Enables Caravel user Wishbone.
2. Writes a 2x2 matrix into the PARTCL/Williams matrix multiplier at `0x3100_0000`.
3. Starts PARTCL and reads the 2x2 result matrix.
4. Converts each result to an 8-bit X1 PWM/program value.
5. Sends X1 command packets through Wishbone address `0x3000_0004`.
6. Reads the X1 cells back through Wishbone.
7. Sends a binary UART frame on Caravel UART TX/GPIO6 for the TMR receiver.

## X1 Wishbone Address

The X1 `Wb_slave.sv` accepts Wishbone transactions only when:

```verilog
i_wb_stb == 1
i_wb_cyc == 1
i_wb_addr == 32'h3000_0004
i_wb_sel == 4'b1111
```

So all X1 command writes and X1 readbacks use:

```c
#define X1_WB_ADDR 0x30000004u
```

## First Three X1 Config Packets

Before normal cell commands, the RTL consumes the first three packets as config.
The firmware sends:

```text
0x00036472  target_set2:target_set1
0x462B000B  target_reset2:target_reset1
0x84001405  dead_time=2, timeout=64, counter=5, cycles=5
```

## Normal X1 Packet Format

After the config packets, `Wb_slave.sv` decodes each packet as:

```text
[31:30] mode
[29:25] row address
[24:20] column address
[19]    rd_err_addr
[18]    rd_full_row
[17:8]  unused by Wb_slave
[7:0]   PWM/data/read dummy
```

The corrected firmware packet builder is:

```c
packet = ((mode & 0x3u) << 30) |
         ((row & 0x1Fu) << 25) |
         ((col & 0x1Fu) << 20) |
         ((rd_err & 0x1u) << 19) |
         ((rd_full_row & 0x1u) << 18) |
         ((uint32_t)data & 0xFFu);
```

## Mode Values

The X1 `top_module.sv` maps:

```text
00 = program reset
01 = read
10 = form
11 = program set
```

## Correct Packet Examples

```text
SET   row 0, col 0, data 0xA2  -> 0xC00000A2
READ  row 0, col 0, dummy 0xFF -> 0x400000FF
FORM  row 0, col 0, data 0xFF  -> 0x800000FF
RESET row 0, col 0, data 0x06  -> 0x00000006
```

The old packet `0xD00888A2` does not mean row 0, col 0 in this RTL. It decodes as:

```text
mode = 3
row = 8
col = 0
rd_err_addr = 1
data = 0xA2
```

That is why the packet builder had to be fixed.

## UART Output

Debug text is disabled by default:

```c
#define ENABLE_DEBUG_PRINTS 0
```

This avoids mixing human-readable logs into the binary UART stream. The emitted
binary frame is:

```text
0xA5 0x5A
0x5A 0x80 R00[31:24] R00[23:16] R00[15:8] R00[7:0]
0x5A 0x81 R01[31:24] R01[23:16] R01[15:8] R01[7:0]
0x5A 0x82 R10[31:24] R10[23:16] R10[15:8] R10[7:0]
0x5A 0x83 R11[31:24] R11[23:16] R11[15:8] R11[7:0]
```

Enable debug prints only when you are manually watching UART text and your TMR
receiver is not expecting a clean binary stream.

## Important Notes

- The X1 RTL defines and validates the command packet format used by this
  firmware.
- If PARTCL is integrated as `mat_mult_wb`, this firmware assumes the following
  register map:
  `CTRL=0x31000000`, `STATUS=0x31000004`, `A=0x31000100`,
  `B=0x31000200`, `C=0x31000400`.
- X1 receives only the low 8-bit PWM/data field from each command packet.
  The firmware clamps PARTCL's 32-bit result to `0..255` before programming X1.
- X1 readback is the RTL read FIFO/scratchpad/TDC readback, not a direct digital
  copy of the PARTCL C-cache word.

## Build

Compile this inside your normal Caravel firmware environment, where `defs.h`,
`stub.h`, linker script, startup code, and the RISC-V toolchain are available.

Typical shape:

```bash
riscv32-unknown-elf-gcc -march=rv32imc -mabi=ilp32 \
  -Os -ffreestanding -nostdlib \
  partcl_x1_uart_demo.c -o partcl_x1_uart_demo.elf
```

Use your existing Caravel firmware Makefile if it already builds examples that
include `defs.h` and `stub.h`.
