#include <defs.h>
#include <stub.h>
#include <stdint.h>

// ************************************************
//          *** Hardware Test Case for Read ***
//          ChipIgnite / Caravel RISC-V firmware
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
// No division/modulo, safe for rv32i + nostdlib
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
// Configure Caravel GPIOs for hardware
// --------------------------------------------------------
void configure_io(void)
{
    // GPIO0: keep safe/debug analog
    reg_mprj_io_0 = GPIO_MODE_MGMT_STD_ANALOG;

    // Keep IO[1:4] in the standard ChipIgnite blink style.
    // Changing these can interfere with flash programming.
    reg_mprj_io_1 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_2 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_3 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_4 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;

    // UART pins for print()
    reg_mprj_io_5 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;  // UART RX
    reg_mprj_io_6 = GPIO_MODE_MGMT_STD_OUTPUT;        // UART TX

    // User GPIOs. For this Wishbone test, keep them as user inputs.
    // They are not needed unless your RTL uses them.
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
    // Basic management GPIO setup
    // Equivalent to ManagmentGpio_outputEnable()
    // -----------------------------
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien   = 1;
    reg_gpio_oe    = 1;

    // Equivalent to ManagmentGpio_write(0)
    reg_gpio_out = 0;

    // -----------------------------
    // Configure Caravel IOs
    // -----------------------------
    configure_io();

    // -----------------------------
    // Enable UART prints
    // -----------------------------
    reg_uart_enable = 1;

    print("\n[TC_READ_HW] firmware start\n");

    // -----------------------------
    // Enable user Wishbone interface
    // Equivalent to User_enableIF(1)
    // -----------------------------
    reg_wb_enable = 1;

    wait_cycles(1000);

    // Signal setup complete, like cocotb ManagmentGpio_write(1)
    reg_gpio_out = 1;

    print("[TC_READ_HW] Wishbone enabled\n");

    // -----------------------------
    // Performing Write Operation
    // Same as cocotb firmware
    // -----------------------------
    print("[TC_READ_HW] Writing command 1: 0x00036472\n");
    REG32(NEURO_ADDR) = 0x00036472;
    wait_cycles(500);

    print("[TC_READ_HW] Writing command 2: 0x462B000B\n");
    REG32(NEURO_ADDR) = 0x462B000B;
    wait_cycles(500);

    print("[TC_READ_HW] Writing command 3: 0x44001405\n");
    REG32(NEURO_ADDR) = 0x44001405;
    wait_cycles(500);

    print("[TC_READ_HW] Writing command 4: 0x4003AAFF\n");
    REG32(NEURO_ADDR) = 0x4003AAFF;
    wait_cycles(500);

    // -----------------------------
    // Read back from user Wishbone address
    // -----------------------------
    print("[TC_READ_HW] Reading back from 0x30000004\n");

    temp = read_wishbone(NEURO_ADDR);

    print("[TC_READ_HW] Readback value = ");
    print_hex32_local(temp);
    print("\n");

    wait_cycles(50);

    // Test finished, like ManagmentGpio_write(0)
    reg_gpio_out = 0;

    print("[TC_READ_HW] test finished\n");

    while (1) {
        wait_cycles(100000);
    }
}
