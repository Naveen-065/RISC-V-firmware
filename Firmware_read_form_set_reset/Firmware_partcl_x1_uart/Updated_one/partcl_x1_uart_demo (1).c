#include <defs.h>
#include <stub.h>
#include <stdint.h>

/*
 * PARTCL 2x2 -> X1 command port -> UART firmware demo
 *
 * Flow:
 *   1. RISC-V enables user Wishbone.
 *   2. RISC-V selects PARTCL/Williams mat_mult_wb at 0x3100_0000.
 *   3. A 2x2 matrix is packed into the top-left corner of PARTCL 8x8 operand caches.
 *   4. PARTCL runs matrix multiplication.
 *   5. C00/C01/C10/C11 are read from PARTCL C cache.
 *   6. Results are converted to 8-bit X1 program/PWM values.
 *   7. Results are written to X1 cells (0,0), (0,1), (1,0), (1,1)
 *      through the X1 command port at 0x3000_0004.
 *   8. RISC-V sends X1 READ packets for those cells.
 *   9. Wishbone readback words are transmitted on Caravel UART TX.
 *
 * UART:
 *   GPIO5 = UART RX
 *   GPIO6 = UART TX
 *
 * For TMR:
 *   Connect Caravel GPIO6 / UART TX to the TMR receiver input.
 */

#define REG32(addr) (*(volatile uint32_t *)(addr))

// ========================================================
// Address map
// ========================================================

#define X1_WB_ADDR      0x30000004u
#define PARTCL_BASE     0x31000000u

// ========================================================
// PARTCL / Williams mat_mult_wb register map
// ========================================================

#define PARTCL_CTRL_OFFSET       0x0000u
#define PARTCL_STATUS_OFFSET     0x0004u
#define PARTCL_A_OFFSET          0x0100u
#define PARTCL_B_OFFSET          0x0200u
#define PARTCL_C_OFFSET          0x0400u

#define PARTCL_CTRL_START        0x00000105u
#define PARTCL_STATUS_DONE       0x00000008u

#define PARTCL_TIMEOUT           1000000u

#define PARTCL_ROWS              8u
#define PARTCL_COLS              8u
#define PARTCL_ELEMS_PER_WORD    4u
#define PARTCL_WORDS_PER_ROW     (PARTCL_COLS / PARTCL_ELEMS_PER_WORD)

// ========================================================
// X1 command format
// ========================================================
//
// Sindhu RTL decode in Wb_slave.sv:
//
//   bits [31:30] = mode
//   bits [29:25] = row address
//   bits [24:20] = column address
//   bit  [19]    = read error register select
//   bit  [18]    = read full row select
//   bits [7:0]   = PWM/data/read dummy
//
// Mode values in Sindhu top_module.sv:
//
//   00 = program reset
//   01 = read
//   10 = form
//   11 = program set

#define X1_MODE_PROGRAM_RESET    0u
#define X1_MODE_READ             1u
#define X1_MODE_PROGRAM_FORM     2u
#define X1_MODE_PROGRAM_SET      3u

#define X1_READ_NORMAL           0u
#define X1_READ_ERROR_REGISTER   1u
#define X1_READ_SINGLE_CELL      0u
#define X1_READ_FULL_ROW         1u

#define X1_CFG_TARGET_SET        0x00036472u
#define X1_CFG_TARGET_RESET      0x462B000Bu
#define X1_CFG_TIMING            0x84001405u

#define X1_PACKET_GAP            500u
#define X1_PROGRAM_DELAY         100000u
#define X1_READ_DELAY            100000u

// ========================================================
// UART response frame
// ========================================================
//
// Binary frame sent to TMR:
//
//   Start: 0xA5 0x5A
//
//   Then four result packets:
//
//   0x5A 0x80 R00[31:24] R00[23:16] R00[15:8] R00[7:0]
//   0x5A 0x81 R01[31:24] R01[23:16] R01[15:8] R01[7:0]
//   0x5A 0x82 R10[31:24] R10[23:16] R10[15:8] R10[7:0]
//   0x5A 0x83 R11[31:24] R11[23:16] R11[15:8] R11[7:0]
//
// Error:
//
//   0x5A 0xFE ERR[31:24] ERR[23:16] ERR[15:8] ERR[7:0]

#define RESP_START_0             0xA5u
#define RESP_START_1             0x5Au
#define RESP_BYTE                0x5Au

#define RESP_C00                 0x80u
#define RESP_C01                 0x81u
#define RESP_C10                 0x82u
#define RESP_C11                 0x83u
#define RESP_ERR                 0xFEu

#define ERR_PARTCL_TIMEOUT       0xBAD00001u

#define UART_BYTE_DELAY          3000u

#ifndef ENABLE_DEBUG_PRINTS
#define ENABLE_DEBUG_PRINTS      0
#endif

#if !ENABLE_DEBUG_PRINTS
#define print(msg) do { (void)(msg); } while (0)
#endif

// ========================================================
// Global PARTCL result storage
// ========================================================

static uint32_t g_partcl_c00;
static uint32_t g_partcl_c01;
static uint32_t g_partcl_c10;
static uint32_t g_partcl_c11;

// ========================================================
// Basic helpers
// ========================================================

static inline void wb_write32(uint32_t addr, uint32_t data)
{
    REG32(addr) = data;
}

static inline uint32_t wb_read32(uint32_t addr)
{
    return REG32(addr);
}

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles--) {
        __asm__ volatile ("nop");
    }
}

static void print_hex32_local(uint32_t value)
{
    char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;

    buf[0] = '0';
    buf[1] = 'x';

    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex[(value >> (28 - 4 * i)) & 0xF];
    }

    buf[10] = '\0';
    print(buf);
}

static void print_label_hex(const char *label, uint32_t value)
{
    print(label);
    print_hex32_local(value);
    print("\n");
}

// ========================================================
// Caravel UART helpers
// ========================================================

static void uart_put_byte(uint8_t byte)
{
    reg_uart_data = byte;
    delay_cycles(UART_BYTE_DELAY);
}

static void uart_send_u32_binary(uint32_t value)
{
    uart_put_byte((uint8_t)(value >> 24));
    uart_put_byte((uint8_t)(value >> 16));
    uart_put_byte((uint8_t)(value >> 8));
    uart_put_byte((uint8_t)value);
}

static void uart_send_word_response(uint8_t tag, uint32_t data)
{
    uart_put_byte(RESP_BYTE);
    uart_put_byte(tag);
    uart_send_u32_binary(data);
}

static void uart_send_start_frame(void)
{
    uart_put_byte(RESP_START_0);
    uart_put_byte(RESP_START_1);
}

// ========================================================
// Caravel IO setup
// ========================================================

static void configure_io(void)
{
    reg_mprj_io_0 = GPIO_MODE_MGMT_STD_ANALOG;

    reg_mprj_io_1 = GPIO_MODE_MGMT_STD_OUTPUT;
    reg_mprj_io_2 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_3 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;
    reg_mprj_io_4 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;

    // Caravel management UART
    reg_mprj_io_5 = GPIO_MODE_MGMT_STD_INPUT_NOPULL;  // UART RX
    reg_mprj_io_6 = GPIO_MODE_MGMT_STD_OUTPUT;        // UART TX

    // Other user GPIOs are not used in this Wishbone-only firmware
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

    reg_mprj_xfer = 1;
    while (reg_mprj_xfer == 1);
}

static void caravel_init(void)
{
    // Management GPIO setup
    reg_gpio_mode1 = 1;
    reg_gpio_mode0 = 0;
    reg_gpio_ien   = 1;
    reg_gpio_oe    = 1;
    reg_gpio_out   = 0;

    configure_io();

    // Enable UART print and binary TX
    reg_uart_enable = 1;

    // Enable user project Wishbone
    reg_wb_enable = 1;

    delay_cycles(1000);

    // Setup done indicator
    reg_gpio_out = 1;
}

// ========================================================
// PARTCL address helpers
// ========================================================

static inline uint32_t partcl_reg_addr(uint32_t offset)
{
    return PARTCL_BASE + offset;
}

static inline uint32_t partcl_operand_word_addr(uint32_t base_offset,
                                                uint32_t row,
                                                uint32_t word_col)
{
    return PARTCL_BASE + base_offset +
           (((row * PARTCL_WORDS_PER_ROW) + word_col) * 4u);
}

static inline uint32_t partcl_c_addr(uint32_t row, uint32_t col)
{
    return PARTCL_BASE + PARTCL_C_OFFSET +
           (((row * PARTCL_COLS) + col) * 4u);
}

static inline uint32_t pack4_i8(int8_t e0, int8_t e1, int8_t e2, int8_t e3)
{
    return ((uint32_t)(uint8_t)e0) |
           ((uint32_t)(uint8_t)e1 << 8) |
           ((uint32_t)(uint8_t)e2 << 16) |
           ((uint32_t)(uint8_t)e3 << 24);
}

// ========================================================
// PARTCL operations
// ========================================================

static void partcl_clear_operand_caches(void)
{
    uint32_t row;
    uint32_t word_col;

    for (row = 0; row < PARTCL_ROWS; row++) {
        for (word_col = 0; word_col < PARTCL_WORDS_PER_ROW; word_col++) {
            wb_write32(partcl_operand_word_addr(PARTCL_A_OFFSET, row, word_col), 0u);
            wb_write32(partcl_operand_word_addr(PARTCL_B_OFFSET, row, word_col), 0u);
        }
    }
}

static void partcl_load_2x2(void)
{
    /*
     * A = [  2  -1 ]
     *     [  3   4 ]
     *
     * B = [  5   6 ]
     *     [ -2   7 ]
     *
     * Expected C = A x B:
     *
     * C00 = 2*5  + (-1)*(-2) = 12
     * C01 = 2*6  + (-1)*7    = 5
     * C10 = 3*5  + 4*(-2)    = 7
     * C11 = 3*6  + 4*7       = 46
     */

    const int8_t a00 = 2;
    const int8_t a01 = -1;
    const int8_t a10 = 3;
    const int8_t a11 = 4;

    const int8_t b00 = 5;
    const int8_t b01 = 6;
    const int8_t b10 = -2;
    const int8_t b11 = 7;

    partcl_clear_operand_caches();

    // A row 0: A00, A01, 0, 0
    wb_write32(partcl_operand_word_addr(PARTCL_A_OFFSET, 0u, 0u),
               pack4_i8(a00, a01, 0, 0));

    // A row 1: A10, A11, 0, 0
    wb_write32(partcl_operand_word_addr(PARTCL_A_OFFSET, 1u, 0u),
               pack4_i8(a10, a11, 0, 0));

    // B row 0: B00, B01, 0, 0
    wb_write32(partcl_operand_word_addr(PARTCL_B_OFFSET, 0u, 0u),
               pack4_i8(b00, b01, 0, 0));

    // B row 1: B10, B11, 0, 0
    wb_write32(partcl_operand_word_addr(PARTCL_B_OFFSET, 1u, 0u),
               pack4_i8(b10, b11, 0, 0));

    print("[PARTCL_X1] PARTCL 2x2 operands loaded\n");
}

static uint32_t partcl_run_and_wait(void)
{
    uint32_t timeout;
    uint32_t status;

    // Clear DONE if status is write-one-to-clear in RTL
    wb_write32(partcl_reg_addr(PARTCL_STATUS_OFFSET), PARTCL_STATUS_DONE);
    delay_cycles(100);

    // Start PARTCL
    wb_write32(partcl_reg_addr(PARTCL_CTRL_OFFSET), PARTCL_CTRL_START);

    for (timeout = 0; timeout < PARTCL_TIMEOUT; timeout++) {
        status = wb_read32(partcl_reg_addr(PARTCL_STATUS_OFFSET));

        if ((status & PARTCL_STATUS_DONE) != 0u) {
            return 1u;
        }
    }

    return 0u;
}

static void partcl_read_c_2x2(void)
{
    g_partcl_c00 = wb_read32(partcl_c_addr(0u, 0u));
    g_partcl_c01 = wb_read32(partcl_c_addr(0u, 1u));
    g_partcl_c10 = wb_read32(partcl_c_addr(1u, 0u));
    g_partcl_c11 = wb_read32(partcl_c_addr(1u, 1u));

    print("[PARTCL_X1] PARTCL C results:\n");
    print_label_hex("  C00 = ", g_partcl_c00);
    print_label_hex("  C01 = ", g_partcl_c01);
    print_label_hex("  C10 = ", g_partcl_c10);
    print_label_hex("  C11 = ", g_partcl_c11);
}

// ========================================================
// X1 helpers
// ========================================================

static uint8_t result_to_x1_pwm(uint32_t result)
{
    int32_t signed_result;

    signed_result = (int32_t)result;

    if (signed_result <= 0) {
        return 0u;
    }

    if (signed_result >= 255) {
        return 255u;
    }

    return (uint8_t)signed_result;
}

static uint32_t x1_make_packet(uint32_t mode,
                               uint32_t row,
                               uint32_t col,
                               uint32_t rd_err,
                               uint32_t rd_full_row,
                               uint8_t data)
{
    return ((mode & 0x3u) << 30) |
           ((row & 0x1Fu) << 25) |
           ((col & 0x1Fu) << 20) |
           ((rd_err & 0x1u) << 19) |
           ((rd_full_row & 0x1u) << 18) |
           ((uint32_t)data & 0xFFu);
}

static void x1_write_packet(uint32_t packet)
{
    wb_write32(X1_WB_ADDR, packet);
    delay_cycles(X1_PACKET_GAP);
}

static void x1_init_packet_interface(void)
{
    print("[PARTCL_X1] X1 init packets\n");

    x1_write_packet(X1_CFG_TARGET_SET);
    x1_write_packet(X1_CFG_TARGET_RESET);
    x1_write_packet(X1_CFG_TIMING);
}

static void x1_program_cell(uint32_t row, uint32_t col, uint32_t result)
{
    uint8_t pwm;
    uint32_t packet;

    pwm = result_to_x1_pwm(result);

    packet = x1_make_packet(X1_MODE_PROGRAM_SET,
                            row,
                            col,
                            X1_READ_NORMAL,
                            X1_READ_SINGLE_CELL,
                            pwm);

    print("[PARTCL_X1] X1 SET packet = ");
    print_hex32_local(packet);
    print("\n");

    x1_write_packet(packet);
    delay_cycles(X1_PROGRAM_DELAY);
}

static uint32_t x1_read_cell(uint32_t row, uint32_t col)
{
    uint32_t packet;
    uint32_t readback;

    packet = x1_make_packet(X1_MODE_READ,
                            row,
                            col,
                            X1_READ_NORMAL,
                            X1_READ_SINGLE_CELL,
                            0xFFu);

    print("[PARTCL_X1] X1 READ packet = ");
    print_hex32_local(packet);
    print("\n");

    x1_write_packet(packet);
    delay_cycles(X1_READ_DELAY);

    readback = wb_read32(X1_WB_ADDR);

    return readback;
}

static void x1_program_c_2x2(void)
{
    print("[PARTCL_X1] Programming PARTCL results into X1\n");

    x1_program_cell(0u, 0u, g_partcl_c00);
    x1_program_cell(0u, 1u, g_partcl_c01);
    x1_program_cell(1u, 0u, g_partcl_c10);
    x1_program_cell(1u, 1u, g_partcl_c11);
}

static void emit_x1_result_readbacks(void)
{
    uint32_t r00;
    uint32_t r01;
    uint32_t r10;
    uint32_t r11;

    print("[PARTCL_X1] Reading X1 cells back\n");

    r00 = x1_read_cell(0u, 0u);
    r01 = x1_read_cell(0u, 1u);
    r10 = x1_read_cell(1u, 0u);
    r11 = x1_read_cell(1u, 1u);

    print("[PARTCL_X1] X1 readback results:\n");
    print_label_hex("  R00 = ", r00);
    print_label_hex("  R01 = ", r01);
    print_label_hex("  R10 = ", r10);
    print_label_hex("  R11 = ", r11);

    // Binary UART frame for TMR
    uart_send_start_frame();
    uart_send_word_response(RESP_C00, r00);
    uart_send_word_response(RESP_C01, r01);
    uart_send_word_response(RESP_C10, r10);
    uart_send_word_response(RESP_C11, r11);
}

// ========================================================
// Full compute -> X1 -> UART sequence
// ========================================================

static void compute_store_read_and_transmit(void)
{
    partcl_load_2x2();

    print("[PARTCL_X1] Starting PARTCL\n");

    if (partcl_run_and_wait() == 0u) {
        print("[PARTCL_X1] ERROR: PARTCL timeout\n");

        uart_send_start_frame();
        uart_send_word_response(RESP_ERR, ERR_PARTCL_TIMEOUT);
        return;
    }

    print("[PARTCL_X1] PARTCL done\n");

    partcl_read_c_2x2();

    x1_init_packet_interface();

    x1_program_c_2x2();

    emit_x1_result_readbacks();

    print("[PARTCL_X1] PASS: PARTCL -> X1 -> UART complete\n");
}

// ========================================================
// Main firmware
// ========================================================

void main(void)
{
    caravel_init();

    print("\n[PARTCL_X1] firmware start\n");
    print("[PARTCL_X1] Wishbone enabled\n");
    print("[PARTCL_X1] PARTCL base = 0x31000000\n");
    print("[PARTCL_X1] X1 command address = 0x30000004\n");

    compute_store_read_and_transmit();

    reg_gpio_out = 0;

    while (1) {
        delay_cycles(100000);
    }
}
