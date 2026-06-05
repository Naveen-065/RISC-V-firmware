#include <defs.h>
#include <stub.h>
#include <stdint.h>

// ************************************************
//          *** Hardware Test Case for Program SET ***
//          ChipIgnite / Caravel RISC-V firmware
//          Wishbone user-area access
// ************************************************

#define REG32(addr) (*(volatile uint32_t *)(addr))

#define NEURO_ADDR 0x30000004

// --------------------------------------------------------
// Simple CPU delay
// --------------------------------------------------------
static inline void wait_cycles(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; i++) {
        __asm__ volatile ("nop");
    }
}

// --------------------------------------------------------
// Print 32-bit value as hex over UART
// Safe for rv32i + nostdlib
// --------------------------------------------------------
void print_hex32_local(uint32_t value)
{
    char hex[] = "0123456789ABCDEF";
    char buf[11];

    buf[0] = '0';
    buf[1] = 'x';

    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(value >> (28 - 4 * i)) & 0xF];
    }

    buf[10] = '\0';
    print(buf);
}

// --------------------------------------------------------
// Wishbone read helper
// --------------------------------------------------------
uint32_t read_wishbone(uint32_t addr)
{
    return REG32(addr);
}

// --------------------------------------------------------
// Configure Caravel IOs for ChipIgnite hardware
// --------------------------------------------------------
void configure_io(void)
{
    reg_mprj_io_0 = GPIO_MODE_MGMT_STD_ANALOG;

    // Keep IO[1:4] flash-safe
    reg_mprj_io_1 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_2 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_3 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_4 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;

    // UART pins
    reg_mprj_io_5 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;  // UART RX
    reg_mprj_io_6 = GPIO_MODE_MGMT_STD_OUTPUT;        // UART TX

    // User GPIOs not needed for this Wishbone-only test
    reg_mprj_io_7  = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_8  = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_9  = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_10 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_11 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_12 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_13 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_14 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_15 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_16 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_17 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_18 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_19 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_20 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_21 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_22 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_23 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_24 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_25 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_26 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_27 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_28 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_29 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_30 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_31 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_32 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_33 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_34 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_35 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_36 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_37 = GPIO_MODE_USER_STD_INPUT_NOPULL;

    // Apply IO configuration
    reg_mprj_xfer = 1;
    while (reg_mprj_xfer == 1);
}

// --------------------------------------------------------
// Main firmware
// --------------------------------------------------------
void main(void)
{
    volatile uint32_t temp;

    // -----------------------------
    // Management GPIO setup
    // Equivalent to ManagmentGpio_outputEnable()
    // -----------------------------
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien   = 1;
    reg_gpio_oe    = 1;

    // Equivalent to ManagmentGpio_write(0)
    reg_gpio_out = 0;

    // -----------------------------
    // Configure IOs
    // -----------------------------
    configure_io();

    // -----------------------------
    // Enable UART prints
    // -----------------------------
    reg_uart_enable = 1;

    print("\n[SET_MODE_WB] firmware start\n");

    // -----------------------------
    // Enable user Wishbone interface
    // Equivalent to User_enableIF(1)
    // -----------------------------
    reg_wb_enable = 1;

    wait_cycles(1000);

    // Signal setup complete
    reg_gpio_out = 1;

    print("[SET_MODE_WB] Wishbone enabled\n");

    // -----------------------------
    // Performing Program SET mode sequence
    // Converted from your cocotb firmware
    // -----------------------------

    print("[SET_MODE_WB] Writing command 1: 0x00036472\n");
    REG32(NEURO_ADDR) = 0x00036472;
    wait_cycles(500);

    print("[SET_MODE_WB] Writing command 2: 0x462B000B\n");
    REG32(NEURO_ADDR) = 0x462B000B;
    wait_cycles(500);

    print("[SET_MODE_WB] Writing command 3: 0x44001405\n");
    REG32(NEURO_ADDR) = 0x44001405;
    wait_cycles(500);

    print("[SET_MODE_WB] Writing command 4 SET: 0xD00888A2\n");
    REG32(NEURO_ADDR) = 0xD00888A2;
    wait_cycles(500);

    print("[SET_MODE_WB] Writing command 5 READ: 0x500888FF\n");
    REG32(NEURO_ADDR) = 0x500888FF;
    wait_cycles(900);

    // -----------------------------
    // Read back from user Wishbone address
    // -----------------------------
    print("[SET_MODE_WB] Reading back from 0x30000004\n");

    temp = read_wishbone(NEURO_ADDR);

    print("[SET_MODE_WB] Readback value = ");
    print_hex32_local(temp);
    print("\n");

    wait_cycles(50);

    // Test finished
    reg_gpio_out = 0;

    print("[SET_MODE_WB] test finished\n");

    while (1) {
        wait_cycles(100000);
    }
}
