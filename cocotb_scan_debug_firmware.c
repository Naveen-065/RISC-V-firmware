#include <defs.h>
#include <stub.h>

/*
 * Chip firmware version of the cocotb ram_word scan test.
 *
 * The cocotb testbench drives:
 *   GPIO21 ScanInDR
 *   GPIO22 ScanInDL
 *   GPIO36 TM
 *
 * The RTL scan/debug path also uses GPIO35 as ScanInCC in the older SV
 * reset-sequence test, so this firmware drives it low for the whole run.
 *
 * The current chip has a non-working Wishbone path, so the WB actions from the
 * cocotb firmware are represented as UART checkpoints and guarded code.  The
 * firmware never touches 0x30000004 unless ENABLE_WB_TOUCHES is set to 1.
 */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define GPIO_SCAN_IN_DR 21u
#define GPIO_SCAN_IN_DL 22u
#define GPIO_SCAN_IN_CC 35u
#define GPIO_TM         36u

#define NEURO_ADDR 0x30000004u

#ifndef ENABLE_WB_TOUCHES
#define ENABLE_WB_TOUCHES 0
#endif

#ifndef SCAN_EDGE_DELAY
#define SCAN_EDGE_DELAY 8000
#endif

#ifndef SCAN_IDLE_DELAY
#define SCAN_IDLE_DELAY 8000
#endif

#ifndef LED_PULSE_DELAY
#define LED_PULSE_DELAY 120000
#endif

/*
 * Match the executable cocotb source, not the stale comment in that file:
 * GPIO_SCAN_IN_DR is driven low during initial idle, shifting, after-shift,
 * and final idle.  Override these macros if the intended hardware protocol
 * turns out to require a high done/idle value.
 */
#ifndef SCAN_DR_IDLE_VALUE
#define SCAN_DR_IDLE_VALUE 0u
#endif
#ifndef SCAN_DR_SHIFT_VALUE
#define SCAN_DR_SHIFT_VALUE 0u
#endif
#ifndef SCAN_DR_DONE_VALUE
#define SCAN_DR_DONE_VALUE 0u
#endif

static u32 gpio_l_shadow;
static u32 gpio_h_shadow;
static u32 checkpoint_id;

static void wait_timer(const int ticks)
{
    reg_timer0_config = 0;
    reg_timer0_data = ticks;
    reg_timer0_config = 1;

    reg_timer0_update = 1;
    while (reg_timer0_value > 0) {
        reg_timer0_update = 1;
    }
}

static void print_hex32(u32 value)
{
    char text[11];
    int nibble;
    int index;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; index = index + 1) {
        nibble = (int)((value >> ((7 - index) * 4)) & 0xfu);
        text[index + 2] = (char)((nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10));
    }
    text[10] = '\0';
    print(text);
}

static void print_hex16(u16 value)
{
    print_hex32((u32)value);
}

static void led_write(u8 value)
{
    reg_gpio_out = (value != 0u) ? 1u : 0u;
}

static void led_pulse(void)
{
    led_write(1u);
    wait_timer(LED_PULSE_DELAY);
    led_write(0u);
    wait_timer(LED_PULSE_DELAY);
}

static void checkpoint(const char *label)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[COCOTB-SCAN][CP ");
    print_hex32(checkpoint_id);
    print("] ");
    print(label);
    print("\n");
    led_pulse();
}

static void print_pin_state(const char *label)
{
    print("[COCOTB-SCAN][PINS] ");
    print(label);
    print(" TM=");
    print_hex32((gpio_h_shadow >> (GPIO_TM - 32u)) & 1u);
    print(" DR=");
    print_hex32((gpio_l_shadow >> GPIO_SCAN_IN_DR) & 1u);
    print(" DL=");
    print_hex32((gpio_l_shadow >> GPIO_SCAN_IN_DL) & 1u);
    print(" CC=");
    print_hex32((gpio_h_shadow >> (GPIO_SCAN_IN_CC - 32u)) & 1u);
    print("\n");
}

static void gpio_drive(u8 gpio, u8 value)
{
    u32 mask;

    if (gpio < 32u) {
        mask = 1u << gpio;
        if (value != 0u) {
            gpio_l_shadow = gpio_l_shadow | mask;
        } else {
            gpio_l_shadow = gpio_l_shadow & ~mask;
        }
        reg_mprj_datal = gpio_l_shadow;
    } else {
        mask = 1u << (gpio - 32u);
        if (value != 0u) {
            gpio_h_shadow = gpio_h_shadow | mask;
        } else {
            gpio_h_shadow = gpio_h_shadow & ~mask;
        }
        reg_mprj_datah = gpio_h_shadow;
    }
}

static void scan_pins_write(u8 tm, u8 scan_dr, u8 scan_dl)
{
    gpio_drive(GPIO_SCAN_IN_CC, 0u);
    gpio_drive(GPIO_TM, tm);
    gpio_drive(GPIO_SCAN_IN_DR, scan_dr);
    gpio_drive(GPIO_SCAN_IN_DL, scan_dl);
}

static void configure_mgmt_core(void)
{
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien = 1;
    reg_gpio_oe = 1;
    led_write(0u);

    reg_uart_enable = 1;
}

static void configure_scan_gpio(void)
{
    gpio_l_shadow = 0u;
    gpio_h_shadow = 0u;
    reg_mprj_datal = gpio_l_shadow;
    reg_mprj_datah = gpio_h_shadow;

    reg_mprj_io_0 = GPIO_MODE_MGMT_STD_ANALOG;
    reg_mprj_io_1 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_2 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_3 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_4 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_5 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_6 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_7 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_8 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_9 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_10 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_11 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_12 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_13 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_14 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_15 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_16 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_17 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_18 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_19 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_20 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_21 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_22 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_23 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_24 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_25 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_26 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_27 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_28 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_29 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_30 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_31 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_32 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_33 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_34 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_35 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_36 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_37 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;

    reg_mprj_xfer = 1;
    while (reg_mprj_xfer == 1) {
    }
}

static void wait_cocotb_cycles(u32 cycles, const char *label)
{
    u32 index;

    print("[COCOTB-SCAN][WAIT] ");
    print(label);
    print(" cycles=");
    print_hex32(cycles);
    print("\n");

    for (index = 0u; index < cycles; index = index + 1u) {
        wait_timer(SCAN_IDLE_DELAY);
    }
}

static void wb_placeholder_write(u32 addr, u32 value, const char *label)
{
    print("[COCOTB-SCAN][WB-PLACEHOLDER] ");
    print(label);
    print(" write addr=");
    print_hex32(addr);
    print(" value=");
    print_hex32(value);

#if ENABLE_WB_TOUCHES
    print(" EXECUTED\n");
    *((volatile u32 *)addr) = value;
#else
    print(" SKIPPED_current_chip_wb_not_working\n");
#endif

    led_pulse();
}

static void wb_placeholder_read(u32 addr, const char *label)
{
    print("[COCOTB-SCAN][WB-PLACEHOLDER] ");
    print(label);
    print(" read addr=");
    print_hex32(addr);

#if ENABLE_WB_TOUCHES
    print(" value=");
    print_hex32(*((volatile u32 *)addr));
    print(" EXECUTED\n");
#else
    print(" SKIPPED_current_chip_wb_not_working\n");
#endif

    led_pulse();
}

static void run_wb_placeholders(void)
{
    checkpoint("WB placeholders begin");

#if ENABLE_WB_TOUCHES
    reg_wb_enable = 1;
    print("[COCOTB-SCAN][WB-PLACEHOLDER] reg_wb_enable=1 EXECUTED\n");
#else
    print("[COCOTB-SCAN][WB-PLACEHOLDER] User_enableIF(1) SKIPPED_current_chip_wb_not_working\n");
#endif

    wb_placeholder_write(NEURO_ADDR, 0x00036472u, "sim write 0");
    wb_placeholder_write(NEURO_ADDR, 0x462b000bu, "sim write 1");
    wb_placeholder_write(NEURO_ADDR, 0x44001405u, "sim write 2");
    wb_placeholder_write(NEURO_ADDR, 0x4003aaffu, "sim write 3");
    wait_cocotb_cycles(1u, "sim wait_cycles(500) placeholder");
    wb_placeholder_read(NEURO_ADDR, "sim readback temp");
    wait_cocotb_cycles(1u, "sim wait_cycles(50) placeholder");

    checkpoint("WB placeholders complete, continuing scan flow");
}

static void scan_transaction(u16 data, u8 total_bits, u8 tm_extra_cycles, u8 idle_cycles, const char *label)
{
    u8 bit_index;
    u8 bit_value;

    print("[COCOTB-SCAN][TXN] start ");
    print(label);
    print(" data=");
    print_hex16(data);
    print(" total_bits=");
    print_hex32((u32)total_bits);
    print(" tm_extra_cycles=");
    print_hex32((u32)tm_extra_cycles);
    print("\n");
    led_pulse();

    checkpoint("transaction initial idle");
    scan_pins_write(0u, SCAN_DR_IDLE_VALUE, 0u);
    print_pin_state("initial_idle");
    wait_cocotb_cycles(1u, "initial idle");

    checkpoint("transaction TM high");
    scan_pins_write(1u, SCAN_DR_SHIFT_VALUE, 0u);
    print_pin_state("tm_high");
    wait_timer(SCAN_EDGE_DELAY);

    for (bit_index = 0u; bit_index < total_bits; bit_index = bit_index + 1u) {
        bit_value = (u8)((data >> bit_index) & 1u);
        scan_pins_write(1u, SCAN_DR_SHIFT_VALUE, bit_value);

        print("[COCOTB-SCAN][BIT] ");
        print(label);
        print(" bit=");
        print_hex32((u32)bit_index);
        print(" value=");
        print_hex32((u32)bit_value);
        print("\n");

        reg_gpio_out = bit_value;
        wait_timer(SCAN_EDGE_DELAY);
    }

    checkpoint("transaction data shift complete");
    scan_pins_write(1u, SCAN_DR_DONE_VALUE, 0u);
    print_pin_state("after_shift");
    wait_cocotb_cycles((u32)tm_extra_cycles, "TM high extra cycles");

    checkpoint("transaction final idle");
    scan_pins_write(0u, SCAN_DR_IDLE_VALUE, 0u);
    print_pin_state("final_idle");
    wait_cocotb_cycles((u32)idle_cycles, "post-transaction idle");

    print("[COCOTB-SCAN][TXN] done ");
    print(label);
    print("\n");
    led_pulse();
}

static void final_heartbeat(void)
{
    while (1) {
        led_write(1u);
        wait_timer(600000);
        led_write(0u);
        wait_timer(600000);
    }
}

void main(void)
{
    checkpoint_id = 0u;

    configure_mgmt_core();

    print("\n[COCOTB-SCAN] firmware start: chip-side version of cocotb ram_word scan test\n");
    print("[COCOTB-SCAN] NOTE: J2 must be installed for UART after flashing, removed for flashing.\n");
    print("[COCOTB-SCAN] NOTE: WB user accesses are placeholders unless ENABLE_WB_TOUCHES=1.\n");

    checkpoint("configure scan GPIO as management outputs");
    configure_scan_gpio();

    checkpoint("apply cocotb initial idle state");
    scan_pins_write(0u, SCAN_DR_IDLE_VALUE, 0u);
    print_pin_state("test_initial_idle");

    wait_cocotb_cycles(10u, "cocotb stabilization");

    checkpoint("firmware ready: mgmt_gpio=1");
    led_write(1u);

    checkpoint("cocotb release_csb placeholder on real chip");
    print("[COCOTB-SCAN][SIM-ONLY] release_csb has no firmware action on physical chip\n");
    wait_cocotb_cycles(5u, "GPIO stabilization after CSB release");

    run_wb_placeholders();

    scan_transaction(0x8000u, 16u, 4u, 2u, "scan_transaction_0");
    wait_cocotb_cycles(2u, "between cocotb scan transactions");
    scan_transaction(0x8822u, 16u, 4u, 2u, "scan_transaction_1");

    wait_cocotb_cycles(10u, "post-scan wait");

    checkpoint("test complete: mgmt_gpio=0");
    led_write(0u);
    print("[COCOTB-SCAN][DONE] complete flow executed; entering LED heartbeat\n");

    final_heartbeat();
}
