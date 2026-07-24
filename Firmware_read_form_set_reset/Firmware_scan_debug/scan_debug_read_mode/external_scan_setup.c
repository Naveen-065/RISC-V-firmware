#include <defs.h>
#include <stub.h>

/*
 * RISC-V firmware for scan_debug mode when scan pins are driven externally.
 *
 * External source drives:
 *   GPIO21 -> ScanInDR / i_scan_se1, active low while shifting
 *   GPIO22 -> ScanInDL / i_scan_si1, scan data
 *   GPIO35 -> ScanInCC, keep low unless RTL uses it
 *   GPIO36 -> TM / i_TM
 *
 * This firmware only configures the Caravel pads so those external GPIO
 * levels are routed into the user project. It does not drive the scan pins.
 *
 * Analog voltage pins are configured as analog pads:
 *   GPIO33 = Vcc_read      variable
 *   GPIO26 = Vcc_wl_read   variable
 *   GPIO27 = Vcc_set       variable
 *   GPIO30 = Vcc_wl_set    variable
 *   GPIO28 = Vcc_wl_reset  variable
 *   GPIO34 = Iref          0.5 V
 *   GPIO32 = Vcomp         0.9 V
 *   GPIO31 = Bias_comp2    0.6 V
 *   GPIO29 = Vbias         1.6 V
 *   GPIO25 = dc_bias       1.0 V
 */

typedef unsigned int u32;
typedef unsigned char u8;

#define GPIO_SCAN_IN_DR 21u
#define GPIO_SCAN_IN_DL 22u
#define GPIO_SCAN_IN_CC 35u
#define GPIO_TM         36u

#ifndef SCAN_LED_DELAY
#define SCAN_LED_DELAY 120000
#endif

#ifndef SCAN_POLL_DELAY
#define SCAN_POLL_DELAY 600000
#endif

static u32 checkpoint_id;

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

    print("[SCAN-EXT][CP ");
    print_dec(checkpoint_id);
    print("] ");
    print(label);
    print("\n");

    led_pulse();
}

static void configure_mgmt_core(void)
{
    /*
     * Management GPIO heartbeat.
     */
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien   = 1;
    reg_gpio_oe    = 1;
    reg_gpio_out   = 0;

    /*
     * Enable UART.
     * GPIO5 = UART RX
     * GPIO6 = UART TX
     */
    reg_uart_enable = 1;

    led_write(0u);
}

static void configure_user_gpio_inputs(void)
{
    /*
     * Clear management output data.
     * Since scan pins are externally driven, firmware must not drive them.
     */
    reg_mprj_datal = 0u;
    reg_mprj_datah = 0u;

    /*
     * GPIO0 management analog.
     */
    reg_mprj_io_0 = GPIO_MODE_MGMT_STD_ANALOG;

    /*
     * Housekeeping / flash-safe pins.
     */
    reg_mprj_io_1 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_2 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_3 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_4 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;

    /*
     * UART pins.
     */
    reg_mprj_io_5 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;  // UART RX
    reg_mprj_io_6 = GPIO_MODE_MGMT_STD_OUTPUT;        // UART TX

    /*
     * Unused / safe user inputs.
     */
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

    /*
     * GPIO18, GPIO19, GPIO20 are not listed in your external analog table,
     * so keep them as safe user inputs.
     */
    reg_mprj_io_18 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_19 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_20 = GPIO_MODE_USER_STD_INPUT_NOPULL;

    /*
     * External scan pins.
     *
     * IMPORTANT:
     * These are USER inputs because an external source drives them.
     *
     * GPIO21 = ScanInDR, active-low scan enable
     * GPIO22 = ScanInDL, scan serial data
     */
    reg_mprj_io_21 = GPIO_MODE_USER_STD_INPUT_NOPULL;  // external ScanInDR
    reg_mprj_io_22 = GPIO_MODE_USER_STD_INPUT_NOPULL;  // external ScanInDL

    /*
     * GPIO23, GPIO24 are not scan pins in your current mapping.
     */
    reg_mprj_io_23 = GPIO_MODE_USER_STD_INPUT_NOPULL;
    reg_mprj_io_24 = GPIO_MODE_USER_STD_INPUT_NOPULL;

    /*
     * Analog voltage pins.
     */
    reg_mprj_io_25 = GPIO_MODE_USER_STD_ANALOG;  // dc_bias       = 1.0 V
    reg_mprj_io_26 = GPIO_MODE_USER_STD_ANALOG;  // Vcc_wl_read  = variable
    reg_mprj_io_27 = GPIO_MODE_USER_STD_ANALOG;  // Vcc_set      = variable
    reg_mprj_io_28 = GPIO_MODE_USER_STD_ANALOG;  // Vcc_wl_reset = variable
    reg_mprj_io_29 = GPIO_MODE_USER_STD_ANALOG;  // Vbias        = 1.6 V
    reg_mprj_io_30 = GPIO_MODE_USER_STD_ANALOG;  // Vcc_wl_set   = variable
    reg_mprj_io_31 = GPIO_MODE_USER_STD_ANALOG;  // Bias_comp2   = 0.6 V
    reg_mprj_io_32 = GPIO_MODE_USER_STD_ANALOG;  // Vcomp        = 0.9 V
    reg_mprj_io_33 = GPIO_MODE_USER_STD_ANALOG;  // Vcc_read     = variable
    reg_mprj_io_34 = GPIO_MODE_USER_STD_ANALOG;  // Iref         = 0.5 V

    /*
     * External scan pins above GPIO31.
     *
     * GPIO35 = ScanInCC
     * GPIO36 = TM
     */
    reg_mprj_io_35 = GPIO_MODE_USER_STD_INPUT_NOPULL;  // external ScanInCC
    reg_mprj_io_36 = GPIO_MODE_USER_STD_INPUT_NOPULL;  // external TM
    

    reg_mprj_io_37 = GPIO_MODE_USER_STD_INPUT_NOPULL;

    /*
     * Apply pad configuration.
     */
    reg_mprj_xfer = 1;
    while (reg_mprj_xfer == 1) {
    }
}

static void print_route(void)
{
    print("[SCAN-EXT][ROUTE] external GPIO22 -> ScanInDL -> i_scan_si1\n");
    print("[SCAN-EXT][ROUTE] external GPIO21 -> ScanInDR -> i_scan_se1 active-low\n");
    print("[SCAN-EXT][ROUTE] external GPIO35 -> ScanInCC\n");
    print("[SCAN-EXT][ROUTE] external GPIO36 -> TM -> i_TM\n");
}

static void print_external_scan_protocol(void)
{
    print("\n[SCAN-EXT] scan_debug external GPIO mode ready\n");
    print("[SCAN-EXT] firmware does NOT drive ScanInDL, ScanInDR, ScanInCC, or TM\n");

    print_route();

    print("\n[SCAN-EXT] External scan pin mapping:\n");
    print("  GPIO36 = TM       = scan/test mux enable\n");
    print("  GPIO21 = ScanInDR = active-low scan enable\n");
    print("  GPIO22 = ScanInDL = scan serial data\n");
    print("  GPIO35 = ScanInCC = keep low unless RTL uses it\n");

    print("\n[SCAN-EXT] Scan word format:\n");
    print("  scan_word[15]    = op_set\n");
    print("  scan_word[14:10] = SL\n");
    print("  scan_word[9:5]   = BL\n");
    print("  scan_word[4:0]   = WL\n");

    print("\n[SCAN-EXT] External driving sequence:\n");
    print("  1. Idle: TM=0, ScanInDR=1, ScanInDL=0, ScanInCC=0\n");
    print("  2. Set TM=1\n");
    print("  3. Pull ScanInDR=0 to enable shifting\n");
    print("  4. Drive 16 scan bits on ScanInDL, LSB first\n");
    print("  5. Send one extra active sample/edge for capture\n");
    print("  6. Set ScanInDR=1 to hold outputs\n");
    print("  7. Keep TM=1 while observing/debugging\n");

    print("\n[SCAN-EXT] Cell (0,0) examples:\n");
    print("  op_set=0, SL=0, BL=0, WL=0 -> scan_word=0x0000\n");
    print("  op_set=1, SL=0, BL=0, WL=0 -> scan_word=0x8000\n");

    print("\n[SCAN-EXT] Analog pins configured as analog:\n");
    print("  GPIO33 = Vcc_read      variable\n");
    print("  GPIO26 = Vcc_wl_read   variable\n");
    print("  GPIO27 = Vcc_set       variable\n");
    print("  GPIO30 = Vcc_wl_set    variable\n");
    print("  GPIO28 = Vcc_wl_reset  variable\n");
    print("  GPIO34 = Iref          0.5 V\n");
    print("  GPIO32 = Vcomp         0.9 V\n");
    print("  GPIO31 = Bias_comp2    0.6 V\n");
    print("  GPIO29 = Vbias         1.6 V\n");
    print("  GPIO25 = dc_bias       1.0 V\n");

    print("\n[SCAN-EXT] Setup complete. External source can now drive scan pins.\n");
}

static void heartbeat_silent(void)
{
    print("[SCAN-EXT] heartbeat running silently\n");

    while (1) {
        led_write(1u);
        wait_timer(SCAN_POLL_DELAY);
        led_write(0u);
        wait_timer(SCAN_POLL_DELAY);
    }
}

void main(void)
{
    checkpoint_id = 0u;

    configure_mgmt_core();
    checkpoint("management UART and LED configured");

    configure_user_gpio_inputs();
    checkpoint("GPIO pads configured for external scan and analog biases");

    /*
     * Enable user Wishbone also.
     * Scan itself does not need Wishbone, but this keeps the user bus available.
     */
    reg_wb_enable = 1;

    print_external_scan_protocol();

    heartbeat_silent();
}
