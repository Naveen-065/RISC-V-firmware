#include <defs.h>
#include <stub.h>

/*
 * Hardware firmware testbench for X1 scan_debug mode.
 *
 * This runs on the Caravel management RISC-V and drives the scan-debug pins:
 *   GPIO21 -> ScanInDR / i_scan_se1, active low while shifting
 *   GPIO22 -> ScanInDL / i_scan_si1, scan data
 *   GPIO35 -> ScanInCC, held low placeholder
 *   GPIO36 -> TM / i_TM, scan-debug mux enable
 *
 * The current RTL ScanDebug.v only has one operation bit:
 *   op_set = 1 -> SET-style polarity
 *   op_set = 0 -> RESET-style polarity
 *
 * READ and FORM are therefore firmware placeholders that still prove the
 * scan capture/decode route and leave clear checkpoints for future RTL hooks.
 * Wishbone/status reads are disabled unless SCAN_DEBUG_STATUS_BASE is defined.
 */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define GPIO_SCAN_IN_DR 21u
#define GPIO_SCAN_IN_DL 22u
#define GPIO_SCAN_IN_CC 35u
#define GPIO_TM         36u

#ifndef SCAN_BIT_DELAY
#define SCAN_BIT_DELAY 8000
#endif

#ifndef SCAN_SETTLE_DELAY
#define SCAN_SETTLE_DELAY 40000
#endif

#ifndef SCAN_LED_DELAY
#define SCAN_LED_DELAY 120000
#endif

#ifndef SCAN_TRACE_EACH_BIT
#define SCAN_TRACE_EACH_BIT 1
#endif

#ifndef SCAN_DEBUG_STATUS_BASE
#define SCAN_DEBUG_HAS_STATUS 0
#else
#define SCAN_DEBUG_HAS_STATUS 1
#endif

#define SCAN_DBG_WL_ADDR_OFFSET   0x00u
#define SCAN_DBG_BL_ADDR_OFFSET   0x04u
#define SCAN_DBG_SL_ADDR_OFFSET   0x08u
#define SCAN_DBG_WL_DATA_OFFSET   0x0cu
#define SCAN_DBG_BL_DATA_OFFSET   0x10u
#define SCAN_DBG_SL_DATA_OFFSET   0x14u
#define SCAN_DBG_WL_FLOAT_OFFSET  0x18u
#define SCAN_DBG_BL_FLOAT_OFFSET  0x1cu
#define SCAN_DBG_SL_FLOAT_OFFSET  0x20u
#define SCAN_DBG_MUX_SEL_OFFSET   0x24u
#define SCAN_DBG_TM_O_OFFSET      0x28u

static u32 gpio_l_shadow;
static u32 gpio_h_shadow;
static u32 checkpoint_id;
static u32 error_count;

struct scan_case {
    const char *name;
    u8 op_set;
    u8 wl;
    u8 bl;
    u8 sl;
    u8 placeholder;
};

static const struct scan_case scan_cases[] = {
    {"READ_PLACEHOLDER",  0u,  3u,  4u,  5u, 1u},
    {"FORM_PLACEHOLDER",  1u,  6u,  7u,  8u, 1u},
    {"SET",               1u,  9u, 10u, 11u, 0u},
    {"RESET",             0u, 12u, 13u, 14u, 0u},
};

static void wait_timer(int ticks)
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
    int index;
    int nibble;

    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; index = index + 1) {
        nibble = (int)((value >> ((7 - index) * 4)) & 0xfu);
        text[index + 2] = (char)((nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10));
    }
    text[10] = '\0';
    print(text);
}

static void print_dec(u32 value)
{
    char text[11];
    char rev[10];
    int count;
    int index;

    if (value == 0u) {
        print("0");
        return;
    }

    count = 0;
    while ((value != 0u) && (count < 10)) {
        rev[count] = (char)('0' + (value % 10u));
        value = value / 10u;
        count = count + 1;
    }
    for (index = 0; index < count; index = index + 1) {
        text[index] = rev[count - index - 1];
    }
    text[count] = '\0';
    print(text);
}

static void led_write(u8 value)
{
    reg_gpio_out = (value != 0u) ? 1u : 0u;
}

static void led_pulse(void)
{
    led_write(1u);
    wait_timer(SCAN_LED_DELAY);
    led_write(0u);
    wait_timer(SCAN_LED_DELAY);
}

static void checkpoint(const char *tag, const char *label)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[SCAN-HW][");
    print(tag);
    print("][CP ");
    print_dec(checkpoint_id);
    print("] ");
    print(label);
    print("\n");
    led_pulse();
}

static void fail_checkpoint(const char *label, u32 actual, u32 expected)
{
    error_count = error_count + 1u;
    print("[SCAN-HW][FAIL] ");
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

static u16 make_scan_word(u8 op_set, u8 wl, u8 bl, u8 sl)
{
    return (u16)(((op_set & 1u) << 15) |
                 ((sl & 31u) << 10) |
                 ((bl & 31u) << 5) |
                 (wl & 31u));
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

static void print_pin_state(const char *label)
{
    print("[SCAN-HW][PINS] ");
    print(label);
    print(" TM=");
    print_dec(gpio_shadow_value(GPIO_TM));
    print(" ScanInDR=");
    print_dec(gpio_shadow_value(GPIO_SCAN_IN_DR));
    print(" ScanInDL=");
    print_dec(gpio_shadow_value(GPIO_SCAN_IN_DL));
    print(" ScanInCC=");
    print_dec(gpio_shadow_value(GPIO_SCAN_IN_CC));
    print("\n");
}

static void configure_mgmt_core(void)
{
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien = 1;
    reg_gpio_oe = 1;
    reg_uart_enable = 1;
    led_write(0u);
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

static void print_route(void)
{
    print("[SCAN-HW][RTL-ROUTE] GPIO22 ScanInDL -> top_module_wr.ScanInDL -> top_module.i_scan_si1\n");
    print("[SCAN-HW][RTL-ROUTE] GPIO21 ScanInDR -> top_module_wr.ScanInDR -> top_module.i_scan_se1, active low\n");
    print("[SCAN-HW][RTL-ROUTE] GPIO36 TM -> top_module_wr.TM -> top_module.i_TM -> scan mux\n");
    print("[SCAN-HW][RTL-ROUTE] GPIO35 ScanInCC held low; Wishbone not required for scan_debug\n");
}

static void print_expected_outputs(const struct scan_case *tc, u16 word)
{
    u32 wl;
    u32 bl;
    u32 sl;
    u32 bl_data;
    u32 sl_data;

    wl = onehot32(tc->wl);
    bl = onehot32(tc->bl);
    sl = onehot32(tc->sl);
    bl_data = (tc->op_set != 0u) ? bl : 0u;
    sl_data = (tc->op_set != 0u) ? 0u : sl;

    print("[SCAN-HW][EXPECT] ");
    print(tc->name);
    print(" word=");
    print_hex32((u32)word);
    print(" op_set=");
    print_dec(tc->op_set);
    print(" wl=");
    print_dec(tc->wl);
    print(" bl=");
    print_dec(tc->bl);
    print(" sl=");
    print_dec(tc->sl);
    print(" WL_addr=");
    print_hex32(wl);
    print(" BL_addr=");
    print_hex32(bl);
    print(" SL_addr=");
    print_hex32(sl);
    print(" WL_data=");
    print_hex32(wl);
    print(" BL_data=");
    print_hex32(bl_data);
    print(" SL_data=");
    print_hex32(sl_data);
    print(" WL_float=");
    print_hex32(~wl);
    print(" BL_float=");
    print_hex32(~bl);
    print(" SL_float=");
    print_hex32(~sl);
    print("\n");
}

#if SCAN_DEBUG_HAS_STATUS
static u32 status_read(u32 offset)
{
    return *((volatile u32 *)(SCAN_DEBUG_STATUS_BASE + offset));
}

static void check_status_outputs(const struct scan_case *tc)
{
    u32 wl;
    u32 bl;
    u32 sl;
    u32 bl_data;
    u32 sl_data;

    wl = onehot32(tc->wl);
    bl = onehot32(tc->bl);
    sl = onehot32(tc->sl);
    bl_data = (tc->op_set != 0u) ? bl : 0u;
    sl_data = (tc->op_set != 0u) ? 0u : sl;

    if ((status_read(SCAN_DBG_TM_O_OFFSET) & 1u) != 1u) {
        fail_checkpoint("TM_o", status_read(SCAN_DBG_TM_O_OFFSET), 1u);
    }
    if (status_read(SCAN_DBG_WL_ADDR_OFFSET) != wl) {
        fail_checkpoint("WL_addr", status_read(SCAN_DBG_WL_ADDR_OFFSET), wl);
    }
    if (status_read(SCAN_DBG_BL_ADDR_OFFSET) != bl) {
        fail_checkpoint("BL_addr", status_read(SCAN_DBG_BL_ADDR_OFFSET), bl);
    }
    if (status_read(SCAN_DBG_SL_ADDR_OFFSET) != sl) {
        fail_checkpoint("SL_addr", status_read(SCAN_DBG_SL_ADDR_OFFSET), sl);
    }
    if (status_read(SCAN_DBG_WL_DATA_OFFSET) != wl) {
        fail_checkpoint("WL_data", status_read(SCAN_DBG_WL_DATA_OFFSET), wl);
    }
    if (status_read(SCAN_DBG_BL_DATA_OFFSET) != bl_data) {
        fail_checkpoint("BL_data", status_read(SCAN_DBG_BL_DATA_OFFSET), bl_data);
    }
    if (status_read(SCAN_DBG_SL_DATA_OFFSET) != sl_data) {
        fail_checkpoint("SL_data", status_read(SCAN_DBG_SL_DATA_OFFSET), sl_data);
    }
    if (status_read(SCAN_DBG_WL_FLOAT_OFFSET) != ~wl) {
        fail_checkpoint("WL_float", status_read(SCAN_DBG_WL_FLOAT_OFFSET), ~wl);
    }
    if (status_read(SCAN_DBG_BL_FLOAT_OFFSET) != ~bl) {
        fail_checkpoint("BL_float", status_read(SCAN_DBG_BL_FLOAT_OFFSET), ~bl);
    }
    if (status_read(SCAN_DBG_SL_FLOAT_OFFSET) != ~sl) {
        fail_checkpoint("SL_float", status_read(SCAN_DBG_SL_FLOAT_OFFSET), ~sl);
    }
    if ((status_read(SCAN_DBG_MUX_SEL_OFFSET) & 3u) != 0u) {
        fail_checkpoint("mux_sel", status_read(SCAN_DBG_MUX_SEL_OFFSET) & 3u, 0u);
    }
}
#else
static void check_status_outputs(const struct scan_case *tc)
{
    print("[SCAN-HW][RTL-PLACEHOLDER] ");
    print(tc->name);
    print(" no SCAN_DEBUG_STATUS_BASE; verify pins/UART expected values on hardware\n");
}
#endif

static void shift_scan_word(u16 word)
{
    int bit_index;
    u8 bit_value;
    u16 shift_shadow;

    shift_shadow = 0u;

    scan_pins_write(1u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("TM high, scan enable inactive");

    scan_pins_write(1u, 0u, 0u);
    wait_timer(SCAN_BIT_DELAY);
    print_pin_state("scan enable active-low");

    for (bit_index = 0; bit_index < 16; bit_index = bit_index + 1) {
        bit_value = (u8)((word >> bit_index) & 1u);
        scan_pins_write(1u, 0u, bit_value);
        shift_shadow = (u16)(((u16)bit_value << 15) | (shift_shadow >> 1));

#if SCAN_TRACE_EACH_BIT
        print("[SCAN-HW][BIT] index=");
        print_dec((u32)bit_index);
        print(" value=");
        print_dec((u32)bit_value);
        print(" shift_shadow=");
        print_hex32((u32)shift_shadow);
        print("\n");
#endif
        wait_timer(SCAN_BIT_DELAY);
    }

    print("[SCAN-HW][SHIFT] final_shift_shadow=");
    print_hex32((u32)shift_shadow);
    print(" expected_word=");
    print_hex32((u32)word);
    print("\n");

    scan_pins_write(1u, 0u, 0u);
    wait_timer(SCAN_BIT_DELAY);
    print("[SCAN-HW][CAPTURE] extra capture edge sent for r_scan_word_valid\n");

    scan_pins_write(1u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("hold scan outputs");
}

static void run_scan_case(const struct scan_case *tc)
{
    u16 word;

    word = make_scan_word(tc->op_set, tc->wl, tc->bl, tc->sl);
    checkpoint(tc->name, "begin");
    if (tc->placeholder != 0u) {
        print("[SCAN-HW][NOTE] ");
        print(tc->name);
        print(" is a placeholder because current ScanDebug.v has only op_set\n");
    }

    print_expected_outputs(tc, word);
    shift_scan_word(word);
    check_status_outputs(tc);
    checkpoint(tc->name, "complete");
}

static void final_heartbeat(void)
{
    while (1) {
        led_write((error_count == 0u) ? 1u : 0u);
        wait_timer(600000);
        led_write((error_count == 0u) ? 0u : 1u);
        wait_timer(600000);
    }
}

void main(void)
{
    unsigned int index;

    checkpoint_id = 0u;
    error_count = 0u;

    configure_mgmt_core();
    configure_scan_gpio();

    print("\n[SCAN-HW] scan_debug hardware firmware testbench start\n");
    print_route();
#if SCAN_DEBUG_HAS_STATUS
    print("[SCAN-HW][MODE] status register checking enabled at SCAN_DEBUG_STATUS_BASE\n");
#else
    print("[SCAN-HW][MODE] placeholder mode: scan pins are driven, RTL status reads are skipped\n");
#endif

    scan_pins_write(0u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("initial idle");

    for (index = 0u; index < (sizeof(scan_cases) / sizeof(scan_cases[0])); index = index + 1u) {
        run_scan_case(&scan_cases[index]);
        scan_pins_write(0u, 1u, 0u);
        wait_timer(SCAN_SETTLE_DELAY);
    }

    if (error_count == 0u) {
        print("[SCAN-HW][SUMMARY][PASS] firmware scan_debug sequence completed\n");
    } else {
        print("[SCAN-HW][SUMMARY][FAIL] errors=");
        print_dec(error_count);
        print("\n");
    }

    final_heartbeat();
}
