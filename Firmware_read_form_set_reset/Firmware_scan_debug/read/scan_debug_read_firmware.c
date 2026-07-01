#define SCAN_FW_NAME "READ_PLACEHOLDER"
#define SCAN_FW_OP_SET 0u
#define SCAN_FW_WL 0u
#define SCAN_FW_BL 0u
#define SCAN_FW_SL 0u
#define SCAN_FW_PLACEHOLDER 1u
#include <defs.h>
#include <stub.h>

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef SCAN_FW_NAME
#define SCAN_FW_NAME "SCAN_UNKNOWN"
#endif

#ifndef SCAN_FW_OP_SET
#define SCAN_FW_OP_SET 0u
#endif

#ifndef SCAN_FW_WL
#define SCAN_FW_WL 0u
#endif

#ifndef SCAN_FW_BL
#define SCAN_FW_BL 0u
#endif

#ifndef SCAN_FW_SL
#define SCAN_FW_SL 0u
#endif

#ifndef SCAN_FW_PLACEHOLDER
#define SCAN_FW_PLACEHOLDER 0u
#endif

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

#ifdef SCAN_DEBUG_STATUS_BASE
#define SCAN_DEBUG_HAS_STATUS 1
#else
#define SCAN_DEBUG_HAS_STATUS 0
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

static void checkpoint(const char *label)
{
    checkpoint_id = checkpoint_id + 1u;
    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
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
    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("][FAIL] ");
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

static u16 make_scan_word(void)
{
    return (u16)(((SCAN_FW_OP_SET & 1u) << 15) |
                 ((SCAN_FW_SL & 31u) << 10) |
                 ((SCAN_FW_BL & 31u) << 5) |
                 (SCAN_FW_WL & 31u));
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
    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("][PINS] ");
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
    print("[SCAN-HW][RTL-ROUTE] GPIO21 ScanInDR -> top_module_wr.ScanInDR -> top_module.i_scan_se1 active-low\n");
    print("[SCAN-HW][RTL-ROUTE] GPIO36 TM -> top_module_wr.TM -> top_module.i_TM -> scan mux\n");
    print("[SCAN-HW][RTL-ROUTE] GPIO35 ScanInCC held low; Wishbone not required for scan_debug\n");
}

static void print_expected_outputs(u16 word)
{
    u32 wl;
    u32 bl;
    u32 sl;
    u32 bl_data;
    u32 sl_data;

    wl = onehot32((u8)SCAN_FW_WL);
    bl = onehot32((u8)SCAN_FW_BL);
    sl = onehot32((u8)SCAN_FW_SL);
    bl_data = (SCAN_FW_OP_SET != 0u) ? bl : 0u;
    sl_data = (SCAN_FW_OP_SET != 0u) ? 0u : sl;

    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("][EXPECT] word=");
    print_hex32((u32)word);
    print(" op_set=");
    print_dec((u32)SCAN_FW_OP_SET);
    print(" wl=");
    print_dec((u32)SCAN_FW_WL);
    print(" bl=");
    print_dec((u32)SCAN_FW_BL);
    print(" sl=");
    print_dec((u32)SCAN_FW_SL);
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

static void check_status_outputs(void)
{
    u32 wl;
    u32 bl;
    u32 sl;
    u32 bl_data;
    u32 sl_data;

    wl = onehot32((u8)SCAN_FW_WL);
    bl = onehot32((u8)SCAN_FW_BL);
    sl = onehot32((u8)SCAN_FW_SL);
    bl_data = (SCAN_FW_OP_SET != 0u) ? bl : 0u;
    sl_data = (SCAN_FW_OP_SET != 0u) ? 0u : sl;

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
static void check_status_outputs(void)
{
    checkpoint("RTL status placeholder check");
    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("][RTL-PLACEHOLDER] no SCAN_DEBUG_STATUS_BASE; verify pins/UART expected values on hardware\n");
}
#endif

static void shift_scan_word(u16 word)
{
    int bit_index;
    u8 bit_value;
    u16 shift_shadow;

    shift_shadow = 0u;

    checkpoint("shift setup: TM high, scan enable inactive");
    scan_pins_write(1u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("TM high, scan enable inactive");

    checkpoint("shift setup: assert ScanInDR active-low");
    scan_pins_write(1u, 0u, 0u);
    wait_timer(SCAN_BIT_DELAY);
    print_pin_state("scan enable active-low");

    checkpoint("shift data: begin 16 LSB-first bits");
    for (bit_index = 0; bit_index < 16; bit_index = bit_index + 1) {
        bit_value = (u8)((word >> bit_index) & 1u);
        scan_pins_write(1u, 0u, bit_value);
        shift_shadow = (u16)(((u16)bit_value << 15) | (shift_shadow >> 1));
#if SCAN_TRACE_EACH_BIT
        print("[SCAN-HW][");
        print(SCAN_FW_NAME);
        print("][BIT] index=");
        print_dec((u32)bit_index);
        print(" value=");
        print_dec((u32)bit_value);
        print(" shift_shadow=");
        print_hex32((u32)shift_shadow);
        print("\n");
#endif
        wait_timer(SCAN_BIT_DELAY);
    }

    checkpoint("shift data: all 16 payload bits driven");
    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("][SHIFT] final_shift_shadow=");
    print_hex32((u32)shift_shadow);
    print(" expected_word=");
    print_hex32((u32)word);
    print("\n");

    checkpoint("capture: send extra edge for r_scan_word_valid");
    scan_pins_write(1u, 0u, 0u);
    wait_timer(SCAN_BIT_DELAY);
    print("[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("][CAPTURE] extra capture edge sent for r_scan_word_valid\n");

    checkpoint("observe: hold scan_debug outputs");
    scan_pins_write(1u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("hold scan outputs");
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
    u16 word;

    checkpoint_id = 0u;
    error_count = 0u;
    
    configure_mgmt_core();
    checkpoint("configure management core UART and GPIO");
    checkpoint("configure scan GPIO pads");
    configure_scan_gpio();

    print("\n[SCAN-HW][");
    print(SCAN_FW_NAME);
    print("] scan_debug hardware firmware testbench start\n");
    print_route();
#if SCAN_DEBUG_HAS_STATUS
    print("[SCAN-HW][MODE] status register checking enabled at SCAN_DEBUG_STATUS_BASE\n");
#else
    print("[SCAN-HW][MODE] placeholder mode: scan pins are driven, RTL status reads are skipped\n");
#endif

    if (SCAN_FW_PLACEHOLDER != 0u) {
        print("[SCAN-HW][NOTE] ");
        print(SCAN_FW_NAME);
        print(" is a placeholder because current ScanDebug.v has only op_set\n");
    }

    checkpoint("drive initial scan idle");
    scan_pins_write(0u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("initial idle");

    word = make_scan_word();
    checkpoint("build read scan word");
    checkpoint("begin operation");
    print_expected_outputs(word);
    shift_scan_word(word);
    check_status_outputs();
    checkpoint("operation complete");

    checkpoint("return pins to final idle");
    scan_pins_write(0u, 1u, 0u);
    wait_timer(SCAN_SETTLE_DELAY);
    print_pin_state("final idle");

    if (error_count == 0u) {
        print("[SCAN-HW][");
        print(SCAN_FW_NAME);
        print("][SUMMARY][PASS] firmware scan_debug sequence completed\n");
    } else {
        print("[SCAN-HW][");
        print(SCAN_FW_NAME);
        print("][SUMMARY][FAIL] errors=");
        print_dec(error_count);
        print("\n");
    }

    final_heartbeat();
}
