#include <defs.h>
#include <stub.h>

/*
 * RISC-V firmware version of:
 *   top_tb_scan_debug_reset_sequence.sv
 *
 * Caravel scan/debug pin map used by the RTL testbench:
 *   GPIO21 -> ScanInDR / scan valid, active low while shifting
 *   GPIO22 -> ScanInDL / scan data, shifted LSB first
 *   GPIO35 -> ScanInCC, held low by this test
 *   GPIO36 -> TM
 *
 * The SV testbench directly observes top_module_wr's internal WL/BL/SL buses.
 * Firmware cannot see those buses unless the RTL exposes them through readable
 * Wishbone/LA status registers. If such registers are added, compile with:
 *
 *   -DSCAN_DEBUG_STATUS_BASE=0x30000100u
 *
 * and override the SCAN_DBG_*_OFFSET values below if your map differs.
 */

typedef unsigned int  u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define GPIO_SCAN_IN_DR  21u
#define GPIO_SCAN_IN_DL  22u
#define GPIO_SCAN_IN_CC  35u
#define GPIO_TM          36u

#define SCAN_OP_SET      1u
#define RESET_DEFAULT_OP 0u
#define DEFAULT_ROW      0u
#define DEFAULT_COL      0u

#define SCAN_RESET_GPIO_NONE 255u

#ifndef SCAN_RESET_GPIO
#define SCAN_RESET_GPIO SCAN_RESET_GPIO_NONE
#endif

#ifndef SCAN_BIT_DELAY
#define SCAN_BIT_DELAY 2000
#endif

#ifndef SCAN_RESET_SETTLE_DELAY
#define SCAN_RESET_SETTLE_DELAY 20000
#endif

#ifndef SCAN_DEBUG_SETTLE_DELAY
#define SCAN_DEBUG_SETTLE_DELAY 8000
#endif

#ifndef SCAN_DBG_WL_ADDR_OFFSET
#define SCAN_DBG_WL_ADDR_OFFSET    0x00u
#endif
#ifndef SCAN_DBG_BL_ADDR_OFFSET
#define SCAN_DBG_BL_ADDR_OFFSET    0x04u
#endif
#ifndef SCAN_DBG_SL_ADDR_OFFSET
#define SCAN_DBG_SL_ADDR_OFFSET    0x08u
#endif
#ifndef SCAN_DBG_WL_DATA_OFFSET
#define SCAN_DBG_WL_DATA_OFFSET    0x0cu
#endif
#ifndef SCAN_DBG_BL_DATA_OFFSET
#define SCAN_DBG_BL_DATA_OFFSET    0x10u
#endif
#ifndef SCAN_DBG_SL_DATA_OFFSET
#define SCAN_DBG_SL_DATA_OFFSET    0x14u
#endif
#ifndef SCAN_DBG_WL_FLOAT_OFFSET
#define SCAN_DBG_WL_FLOAT_OFFSET   0x18u
#endif
#ifndef SCAN_DBG_BL_FLOAT_OFFSET
#define SCAN_DBG_BL_FLOAT_OFFSET   0x1cu
#endif
#ifndef SCAN_DBG_SL_FLOAT_OFFSET
#define SCAN_DBG_SL_FLOAT_OFFSET   0x20u
#endif
#ifndef SCAN_DBG_MUX_SEL_OFFSET
#define SCAN_DBG_MUX_SEL_OFFSET    0x24u
#endif
#ifndef SCAN_DBG_TM_O_OFFSET
#define SCAN_DBG_TM_O_OFFSET       0x28u
#endif

static u32 gpio_l_shadow;
static u32 gpio_h_shadow;
static u32 checkpoint_id;
static u32 checkpoint_errors;

static void delay(const int d)
{
    reg_timer0_config = 0;
    reg_timer0_data = d;
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

static void print_dec(u32 value)
{
    char text[11];
    char reversed[10];
    int count;
    int index;

    if (value == 0u) {
        print("0");
        return;
    }

    count = 0;
    while ((value != 0u) && (count < 10)) {
        reversed[count] = (char)('0' + (value % 10u));
        value = value / 10u;
        count = count + 1;
    }

    for (index = 0; index < count; index = index + 1) {
        text[index] = reversed[count - index - 1];
    }
    text[count] = '\0';
    print(text);
}

static void checkpoint(const char *label, int condition)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[SCAN-RESET-SEQ][CP");
    print_dec(checkpoint_id);
    print("][");
    if (condition) {
        print("PASS] ");
    } else {
        checkpoint_errors = checkpoint_errors + 1u;
        print("FAIL] ");
    }
    print(label);
    print("\n");
}

static void check_eq1(const char *label, u32 actual, u32 expected)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[SCAN-RESET-SEQ][CP");
    print_dec(checkpoint_id);
    print("][");
    if ((actual & 1u) == (expected & 1u)) {
        print("PASS] ");
    } else {
        checkpoint_errors = checkpoint_errors + 1u;
        print("FAIL] ");
    }
    print(label);
    print(" actual=");
    print_dec(actual & 1u);
    print(" expected=");
    print_dec(expected & 1u);
    print("\n");
}

static void check_eq2(const char *label, u32 actual, u32 expected)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[SCAN-RESET-SEQ][CP");
    print_dec(checkpoint_id);
    print("][");
    if ((actual & 3u) == (expected & 3u)) {
        print("PASS] ");
    } else {
        checkpoint_errors = checkpoint_errors + 1u;
        print("FAIL] ");
    }
    print(label);
    print(" actual=");
    print_hex32(actual & 3u);
    print(" expected=");
    print_hex32(expected & 3u);
    print("\n");
}

static void check_eq32(const char *label, u32 actual, u32 expected)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[SCAN-RESET-SEQ][CP");
    print_dec(checkpoint_id);
    print("][");
    if (actual == expected) {
        print("PASS] ");
    } else {
        checkpoint_errors = checkpoint_errors + 1u;
        print("FAIL] ");
    }
    print(label);
    print(" actual=");
    print_hex32(actual);
    print(" expected=");
    print_hex32(expected);
    print("\n");
}

static u32 onehot32(u8 index)
{
    return 1u << (index & 31u);
}

static int count_ones32(u32 value)
{
    int count;
    int bit_index;

    count = 0;
    for (bit_index = 0; bit_index < 32; bit_index = bit_index + 1) {
        if ((value & (1u << bit_index)) != 0u) {
            count = count + 1;
        }
    }
    return count;
}

static u16 scan_word_for_cell(u8 row, u8 col, u8 op_set)
{
    return (u16)(((op_set & 1u) << 15) |
                 ((col & 31u) << 10) |
                 ((col & 31u) << 5) |
                 (row & 31u));
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

static u32 gpio_shadow_value(u8 gpio)
{
    if (gpio < 32u) {
        return (gpio_l_shadow >> gpio) & 1u;
    }
    return (gpio_h_shadow >> (gpio - 32u)) & 1u;
}

static void scan_pins_write(u8 tm, u8 scan_dr, u8 scan_dl)
{
    gpio_drive(GPIO_SCAN_IN_CC, 0u);
    gpio_drive(GPIO_TM, tm);
    gpio_drive(GPIO_SCAN_IN_DR, scan_dr);
    gpio_drive(GPIO_SCAN_IN_DL, scan_dl);
}

static void configure_io(void)
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

#if SCAN_RESET_GPIO < 38
    if (SCAN_RESET_GPIO == 0u) { reg_mprj_io_0 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 1u) { reg_mprj_io_1 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 2u) { reg_mprj_io_2 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 3u) { reg_mprj_io_3 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 4u) { reg_mprj_io_4 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 5u) { reg_mprj_io_5 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 6u) { reg_mprj_io_6 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 7u) { reg_mprj_io_7 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 8u) { reg_mprj_io_8 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 9u) { reg_mprj_io_9 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 10u) { reg_mprj_io_10 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 11u) { reg_mprj_io_11 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 12u) { reg_mprj_io_12 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 13u) { reg_mprj_io_13 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 14u) { reg_mprj_io_14 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 15u) { reg_mprj_io_15 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 16u) { reg_mprj_io_16 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 17u) { reg_mprj_io_17 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 18u) { reg_mprj_io_18 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 19u) { reg_mprj_io_19 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 20u) { reg_mprj_io_20 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 21u) { reg_mprj_io_21 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 22u) { reg_mprj_io_22 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 23u) { reg_mprj_io_23 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 24u) { reg_mprj_io_24 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 25u) { reg_mprj_io_25 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 26u) { reg_mprj_io_26 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 27u) { reg_mprj_io_27 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 28u) { reg_mprj_io_28 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 29u) { reg_mprj_io_29 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 30u) { reg_mprj_io_30 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 31u) { reg_mprj_io_31 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 32u) { reg_mprj_io_32 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 33u) { reg_mprj_io_33 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 34u) { reg_mprj_io_34 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 35u) { reg_mprj_io_35 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 36u) { reg_mprj_io_36 = GPIO_MODE_MGMT_STD_OUTPUT; }
    if (SCAN_RESET_GPIO == 37u) { reg_mprj_io_37 = GPIO_MODE_MGMT_STD_OUTPUT; }
#endif

    reg_mprj_xfer = 1;
    while (reg_mprj_xfer == 1) {
    }
}

static void configure_mgmt_core(void)
{
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien = 1;
    reg_gpio_oe = 1;
    reg_uart_enable = 1;
}

static void pulse_user_reset_if_available(void)
{
#if SCAN_RESET_GPIO < 38
    gpio_drive((u8)SCAN_RESET_GPIO, 1u);
    delay(SCAN_RESET_SETTLE_DELAY);
    gpio_drive((u8)SCAN_RESET_GPIO, 0u);
    delay(SCAN_RESET_SETTLE_DELAY);
#else
    delay(SCAN_RESET_SETTLE_DELAY);
#endif
}

static void print_expected_cell(const char *label, u8 row, u8 col, u8 op_set)
{
    u32 expected_wl;
    u32 expected_bl;
    u32 expected_sl;
    u32 expected_wl_data;
    u32 expected_bl_data;
    u32 expected_sl_data;

    expected_wl = onehot32(row);
    expected_bl = onehot32(col);
    expected_sl = onehot32(col);
    expected_wl_data = expected_wl;
    expected_bl_data = (op_set != 0u) ? expected_bl : 0u;
    expected_sl_data = (op_set != 0u) ? 0u : expected_sl;

    print("[SCAN-RESET-SEQ][EXPECT] ");
    print(label);
    print(" row=");
    print_dec(row);
    print(" col=");
    print_dec(col);
    print(" op_set=");
    print_dec(op_set);
    print(" wl_addr=");
    print_hex32(expected_wl);
    print(" bl_addr=");
    print_hex32(expected_bl);
    print(" sl_addr=");
    print_hex32(expected_sl);
    print(" wl_data=");
    print_hex32(expected_wl_data);
    print(" bl_data=");
    print_hex32(expected_bl_data);
    print(" sl_data=");
    print_hex32(expected_sl_data);
    print("\n");
}

#ifdef SCAN_DEBUG_STATUS_BASE
static u32 debug_read(u32 offset)
{
    return *((volatile u32 *)(SCAN_DEBUG_STATUS_BASE + offset));
}
#endif

static void checkpoint_selected_cell(const char *label, u8 row, u8 col, u8 op_set)
{
    u32 expected_wl;
    u32 expected_bl;
    u32 expected_sl;
    u32 expected_wl_data;
    u32 expected_bl_data;
    u32 expected_sl_data;

    expected_wl = onehot32(row);
    expected_bl = onehot32(col);
    expected_sl = onehot32(col);
    expected_wl_data = expected_wl;
    expected_bl_data = (op_set != 0u) ? expected_bl : 0u;
    expected_sl_data = (op_set != 0u) ? 0u : expected_sl;

#ifdef SCAN_DEBUG_STATUS_BASE
    {
        u32 wl_addr;
        u32 bl_addr;
        u32 sl_addr;
        u32 wl_data;
        u32 bl_data;
        u32 sl_data;
        u32 wl_float;
        u32 bl_float;
        u32 sl_float;

        wl_addr = debug_read(SCAN_DBG_WL_ADDR_OFFSET);
        bl_addr = debug_read(SCAN_DBG_BL_ADDR_OFFSET);
        sl_addr = debug_read(SCAN_DBG_SL_ADDR_OFFSET);
        wl_data = debug_read(SCAN_DBG_WL_DATA_OFFSET);
        bl_data = debug_read(SCAN_DBG_BL_DATA_OFFSET);
        sl_data = debug_read(SCAN_DBG_SL_DATA_OFFSET);
        wl_float = debug_read(SCAN_DBG_WL_FLOAT_OFFSET);
        bl_float = debug_read(SCAN_DBG_BL_FLOAT_OFFSET);
        sl_float = debug_read(SCAN_DBG_SL_FLOAT_OFFSET);

        check_eq1("TM_o is high in scan debug", debug_read(SCAN_DBG_TM_O_OFFSET), 1u);
        check_eq32("WL selects expected row", wl_addr, expected_wl);
        check_eq32("BL selects expected column", bl_addr, expected_bl);
        check_eq32("SL selects expected column", sl_addr, expected_sl);
        checkpoint("exactly one WL bit is selected", count_ones32(wl_addr) == 1);
        checkpoint("exactly one BL bit is selected", count_ones32(bl_addr) == 1);
        checkpoint("exactly one SL bit is selected", count_ones32(sl_addr) == 1);
        checkpoint("expected cell is selected",
                   ((wl_addr & expected_wl) != 0u) &&
                   ((bl_addr & expected_bl) != 0u) &&
                   ((sl_addr & expected_sl) != 0u));
        checkpoint("no non-target WL/BL/SL bits are selected",
                   ((wl_addr & ~expected_wl) == 0u) &&
                   ((bl_addr & ~expected_bl) == 0u) &&
                   ((sl_addr & ~expected_sl) == 0u));
        check_eq32("WL data matches selected row", wl_data, expected_wl_data);
        check_eq32("BL data matches op_set", bl_data, expected_bl_data);
        check_eq32("SL data matches op_set", sl_data, expected_sl_data);
        check_eq32("WL float is inverse of WL select", wl_float, ~expected_wl);
        check_eq32("BL float is inverse of BL select", bl_float, ~expected_bl);
        check_eq32("SL float is inverse of SL select", sl_float, ~expected_sl);
        check_eq2("mux select is scan-debug default", debug_read(SCAN_DBG_MUX_SEL_OFFSET), 0u);
    }
#else
    print_expected_cell(label, row, col, op_set);
#endif
}

static void reset_and_check_default(const char *label)
{
    scan_pins_write(1u, 1u, 0u);
    pulse_user_reset_if_available();
    delay(SCAN_DEBUG_SETTLE_DELAY);

    check_eq1("TM shadow is high in scan debug", gpio_shadow_value(GPIO_TM), 1u);
    check_eq1("scan valid shadow is idle-high", gpio_shadow_value(GPIO_SCAN_IN_DR), 1u);
    check_eq1("scan data shadow is idle-low", gpio_shadow_value(GPIO_SCAN_IN_DL), 0u);
    checkpoint_selected_cell(label, DEFAULT_ROW, DEFAULT_COL, RESET_DEFAULT_OP);
}

static void scan_shift_in(u16 bits, const char *label)
{
    int bit_index;

    checkpoint("scan shift starts with TM high", gpio_shadow_value(GPIO_TM) == 1u);
    check_eq1("pre-shift scan valid idle-high", gpio_shadow_value(GPIO_SCAN_IN_DR), 1u);

    scan_pins_write(1u, 0u, 0u);
    delay(SCAN_BIT_DELAY);
    check_eq1("dummy clock scan valid active-low", gpio_shadow_value(GPIO_SCAN_IN_DR), 0u);

    for (bit_index = 0; bit_index < 16; bit_index = bit_index + 1) {
        scan_pins_write(1u, 0u, (u8)((bits >> bit_index) & 1u));
        delay(SCAN_BIT_DELAY);
    }

    scan_pins_write(1u, 0u, 0u);
    delay(SCAN_BIT_DELAY);
    check_eq1("extra capture clock keeps scan valid active-low", gpio_shadow_value(GPIO_SCAN_IN_DR), 0u);

    scan_pins_write(1u, 1u, 0u);
    delay(SCAN_BIT_DELAY);
    check_eq1("scan valid returns idle-high after word", gpio_shadow_value(GPIO_SCAN_IN_DR), 1u);

    print("[SCAN-RESET-SEQ][SHIFTED] ");
    print(label);
    print(" word=");
    print_hex16(bits);
    print("\n");
}

static void scan_select_and_check(u8 row, u8 col, const char *label)
{
    u16 word;

    word = scan_word_for_cell(row, col, SCAN_OP_SET);
    scan_shift_in(word, label);
    delay(SCAN_DEBUG_SETTLE_DELAY);
    checkpoint_selected_cell(label, row, col, SCAN_OP_SET);
}

static void exit_scan_debug(void)
{
    scan_pins_write(0u, 0u, 0u);
    delay(SCAN_DEBUG_SETTLE_DELAY);

    check_eq1("exit TM shadow is low", gpio_shadow_value(GPIO_TM), 0u);
    check_eq1("exit scan valid shadow is low", gpio_shadow_value(GPIO_SCAN_IN_DR), 0u);
    check_eq1("exit scan data shadow is low", gpio_shadow_value(GPIO_SCAN_IN_DL), 0u);
}

static void checkpoint_summary(void)
{
#ifndef SCAN_DEBUG_STATUS_BASE
    print("[SCAN-RESET-SEQ][NOTE] Internal WL/BL/SL checks were expectation-only; ");
    print("define SCAN_DEBUG_STATUS_BASE after adding readable RTL debug registers for hard pass/fail checks.\n");
#endif

#if SCAN_RESET_GPIO >= 38
    print("[SCAN-RESET-SEQ][NOTE] No SCAN_RESET_GPIO was defined; firmware waited between reset phases ");
    print("but did not reassert wb_rst_i.\n");
#endif

    if (checkpoint_errors == 0u) {
        print("[SCAN-RESET-SEQ][SUMMARY][PASS] all ");
        print_dec(checkpoint_id);
        print(" executable checkpoints passed\n");
    } else {
        print("[SCAN-RESET-SEQ][SUMMARY][FAIL] ");
        print_dec(checkpoint_errors);
        print(" of ");
        print_dec(checkpoint_id);
        print(" executable checkpoints failed\n");
    }
}

void main(void)
{
    checkpoint_id = 0u;
    checkpoint_errors = 0u;

    configure_mgmt_core();
    configure_io();

    print("\n[SCAN-RESET-SEQ] RISC-V scan-debug reset-sequence firmware start\n");

    reset_and_check_default("R0 default after reset before first scan");
    scan_select_and_check(0u, 1u, "S1 scan select cell (0,1)");

    reset_and_check_default("R1 default after reset before second scan");
    scan_select_and_check(0u, 2u, "S2 scan select cell (0,2)");

    reset_and_check_default("R2 default after reset before third scan");
    scan_select_and_check(1u, 0u, "S3 scan select cell (1,0)");

    exit_scan_debug();
    checkpoint_summary();

    while (1) {
        reg_gpio_out = (checkpoint_errors == 0u) ? 0u : 1u;
        delay(800000);
        reg_gpio_out = (checkpoint_errors == 0u) ? 1u : 0u;
        delay(800000);
    }
}
