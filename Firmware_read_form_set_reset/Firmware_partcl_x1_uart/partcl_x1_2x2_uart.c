/*
 * PARTCL 2x2 -> X1 command port -> UART firmware demo.
 *
 * RTL-checked flow:
 *   1. RISC-V accesses the user project Wishbone map.
 *   2. PARTCL/Williams mat_mult_wb is selected at 0x3100_0000.
 *   3. A 2x2 matrix is packed into the top-left corner of the 8x8 PARTCL
 *      operand caches.
 *   4. PARTCL is started, C00/C01/C10/C11 are read from its C cache.
 *   5. X1 is selected at the exact Wishbone command address 0x3000_0004.
 *      The current X1 RTL is not a word-addressed RAM; it consumes command
 *      packets at this one address.
 *   6. The four C values are converted to 8-bit PWM/program values and sent
 *      to X1 cells (0,0), (0,1), (1,0), and (1,1).
 *   7. RISC-V sends X1 read packets for those cells, performs Wishbone reads
 *      from 0x3000_0004, and transmits those readback words on UART TX.
 *
 * UART RX is not used. The external side/TMR path only receives TX bytes.
 */

#include <stdint.h>

#ifndef X1_WB_ADDR
#define X1_WB_ADDR 0x30000004u
#endif

#ifndef PARTCL_BASE
#define PARTCL_BASE 0x31000000u
#endif

/* Verified mat_mult_wb map used by the PARTCL RTL/cocotb setup. */
#ifndef PARTCL_CTRL_OFFSET
#define PARTCL_CTRL_OFFSET 0x0000u
#endif

#ifndef PARTCL_STATUS_OFFSET
#define PARTCL_STATUS_OFFSET 0x0004u
#endif

#ifndef PARTCL_A_OFFSET
#define PARTCL_A_OFFSET 0x0100u
#endif

#ifndef PARTCL_B_OFFSET
#define PARTCL_B_OFFSET 0x0200u
#endif

#ifndef PARTCL_C_OFFSET
#define PARTCL_C_OFFSET 0x0400u
#endif

#ifndef PARTCL_CTRL_START
#define PARTCL_CTRL_START 0x00000105u
#endif

#ifndef PARTCL_STATUS_DONE
#define PARTCL_STATUS_DONE 0x00000008u
#endif

#ifndef PARTCL_TIMEOUT
#define PARTCL_TIMEOUT 1000000u
#endif

#ifndef X1_PACKET_GAP
#define X1_PACKET_GAP 64u
#endif

#ifndef X1_PROGRAM_DELAY
#define X1_PROGRAM_DELAY 100000u
#endif

#ifndef X1_READ_DELAY
#define X1_READ_DELAY 100000u
#endif

#ifndef SYS_CLK_HZ
#define SYS_CLK_HZ 50000000u
#endif

#ifndef UART_BAUD
#define UART_BAUD 115200u
#endif

/*
 * Default GPIO-style UART TX backend. Replace these macros with the board
 * UART/GPIO register map used by your firmware tree if different.
 */
#ifndef UART_GPIO_OUT_REG
#define UART_GPIO_OUT_REG 0x2600000cu
#endif

#ifndef UART_TX_BIT
#define UART_TX_BIT 6u
#endif

#define RESP_BYTE 0x5Au

#define RESP_C00 0x80u
#define RESP_C01 0x81u
#define RESP_C10 0x82u
#define RESP_C11 0x83u
#define RESP_ERR 0xFEu

#define PARTCL_ROWS 8u
#define PARTCL_COLS 8u
#define PARTCL_ELEMS_PER_WORD 4u
#define PARTCL_WORDS_PER_ROW (PARTCL_COLS / PARTCL_ELEMS_PER_WORD)

#define X1_MODE_PROGRAM_RESET 0u
#define X1_MODE_READ 1u
#define X1_MODE_PROGRAM_SET 2u
#define X1_MODE_COMPUTE 3u

#define X1_CFG_TARGET_SET 0x64720003u
#define X1_CFG_TARGET_RESET 0x000B462Bu
#define X1_CFG_TIMING 0x00050005u

static uint32_t g_partcl_c00;
static uint32_t g_partcl_c01;
static uint32_t g_partcl_c10;
static uint32_t g_partcl_c11;

static inline void wb_write32(uint32_t addr, uint32_t data)
{
    *((volatile uint32_t *)addr) = data;
}

static inline uint32_t wb_read32(uint32_t addr)
{
    return *((volatile uint32_t *)addr);
}

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles--) {
        __asm__ volatile ("nop");
    }
}

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
    return PARTCL_BASE + PARTCL_C_OFFSET + (((row * PARTCL_COLS) + col) * 4u);
}

static inline uint32_t pack4_i8(int8_t e0, int8_t e1, int8_t e2, int8_t e3)
{
    return ((uint32_t)(uint8_t)e0) |
           ((uint32_t)(uint8_t)e1 << 8) |
           ((uint32_t)(uint8_t)e2 << 16) |
           ((uint32_t)(uint8_t)e3 << 24);
}

static void uart_delay_bit(void)
{
    delay_cycles(SYS_CLK_HZ / UART_BAUD);
}

static void uart_tx_set(uint32_t bit)
{
    uint32_t value = wb_read32(UART_GPIO_OUT_REG);
    if (bit) {
        value |= (1u << UART_TX_BIT);
    } else {
        value &= ~(1u << UART_TX_BIT);
    }
    wb_write32(UART_GPIO_OUT_REG, value);
}

static void uart_put_byte(uint8_t byte)
{
    uart_tx_set(0u);
    uart_delay_bit();

    for (uint32_t bit = 0; bit < 8u; bit++) {
        uart_tx_set((byte >> bit) & 1u);
        uart_delay_bit();
    }

    uart_tx_set(1u);
    uart_delay_bit();
}

static void uart_send_word_response(uint8_t addr, uint32_t data)
{
    uart_put_byte(RESP_BYTE);
    uart_put_byte(addr);
    uart_put_byte((uint8_t)(data >> 24));
    uart_put_byte((uint8_t)(data >> 16));
    uart_put_byte((uint8_t)(data >> 8));
    uart_put_byte((uint8_t)data);
}

static void partcl_clear_operand_caches(void)
{
    for (uint32_t row = 0; row < PARTCL_ROWS; row++) {
        for (uint32_t word_col = 0; word_col < PARTCL_WORDS_PER_ROW; word_col++) {
            wb_write32(partcl_operand_word_addr(PARTCL_A_OFFSET, row, word_col), 0u);
            wb_write32(partcl_operand_word_addr(PARTCL_B_OFFSET, row, word_col), 0u);
        }
    }
}

static void partcl_load_2x2(void)
{
    const int8_t a00 = 2;
    const int8_t a01 = -1;
    const int8_t a10 = 3;
    const int8_t a11 = 4;

    const int8_t b00 = 5;
    const int8_t b01 = 6;
    const int8_t b10 = -2;
    const int8_t b11 = 7;

    partcl_clear_operand_caches();

    wb_write32(partcl_operand_word_addr(PARTCL_A_OFFSET, 0u, 0u),
               pack4_i8(a00, a01, 0, 0));
    wb_write32(partcl_operand_word_addr(PARTCL_A_OFFSET, 1u, 0u),
               pack4_i8(a10, a11, 0, 0));

    wb_write32(partcl_operand_word_addr(PARTCL_B_OFFSET, 0u, 0u),
               pack4_i8(b00, b01, 0, 0));
    wb_write32(partcl_operand_word_addr(PARTCL_B_OFFSET, 1u, 0u),
               pack4_i8(b10, b11, 0, 0));
}

static uint32_t partcl_run_and_wait(void)
{
    wb_write32(partcl_reg_addr(PARTCL_STATUS_OFFSET), PARTCL_STATUS_DONE);
    wb_write32(partcl_reg_addr(PARTCL_CTRL_OFFSET), PARTCL_CTRL_START);

    for (uint32_t timeout = 0; timeout < PARTCL_TIMEOUT; timeout++) {
        uint32_t status = wb_read32(partcl_reg_addr(PARTCL_STATUS_OFFSET));
        if (status & PARTCL_STATUS_DONE) {
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
}

static uint8_t result_to_x1_pwm(uint32_t result)
{
    int32_t signed_result = (int32_t)result;

    if (signed_result <= 0) {
        return 0u;
    }
    if (signed_result >= 255) {
        return 255u;
    }

    return (uint8_t)signed_result;
}

static inline uint32_t x1_packet(uint32_t mode,
                                 uint32_t row,
                                 uint32_t col,
                                 uint32_t read_error,
                                 uint8_t pwm_data)
{
    return ((mode & 3u) << 30) |
           ((row & 0x1Fu) << 25) |
           ((col & 0x1Fu) << 20) |
           ((read_error & 1u) << 19) |
           (uint32_t)pwm_data;
}

static void x1_write_packet(uint32_t packet)
{
    wb_write32(X1_WB_ADDR, packet);
    delay_cycles(X1_PACKET_GAP);
}

static void x1_init_packet_interface(void)
{
    x1_write_packet(X1_CFG_TARGET_SET);
    x1_write_packet(X1_CFG_TARGET_RESET);
    x1_write_packet(X1_CFG_TIMING);
}

static void x1_program_cell(uint8_t row, uint8_t col, uint32_t result)
{
    uint8_t pwm = result_to_x1_pwm(result);
    x1_write_packet(x1_packet(X1_MODE_PROGRAM_SET, row, col, 0u, pwm));
    delay_cycles(X1_PROGRAM_DELAY);
}

static uint32_t x1_read_cell(uint8_t row, uint8_t col)
{
    x1_write_packet(x1_packet(X1_MODE_READ, row, col, 0u, 0u));
    delay_cycles(X1_READ_DELAY);
    return wb_read32(X1_WB_ADDR);
}

static void x1_program_c_2x2(void)
{
    x1_program_cell(0u, 0u, g_partcl_c00);
    x1_program_cell(0u, 1u, g_partcl_c01);
    x1_program_cell(1u, 0u, g_partcl_c10);
    x1_program_cell(1u, 1u, g_partcl_c11);
}

static void emit_x1_result_readbacks(void)
{
    uart_send_word_response(RESP_C00, x1_read_cell(0u, 0u));
    uart_send_word_response(RESP_C01, x1_read_cell(0u, 1u));
    uart_send_word_response(RESP_C10, x1_read_cell(1u, 0u));
    uart_send_word_response(RESP_C11, x1_read_cell(1u, 1u));
}

static void compute_store_read_and_transmit(void)
{
    partcl_load_2x2();
    if (!partcl_run_and_wait()) {
        uart_send_word_response(RESP_ERR, 0xBAD00001u);
        return;
    }

    partcl_read_c_2x2();
    x1_init_packet_interface();
    x1_program_c_2x2();
    emit_x1_result_readbacks();
}

int main(void);

#ifndef CARAVEL_HAS_STARTUP
void _start(void)
{
    (void)main();
    while (1) {
    }
}
#endif

int main(void)
{
    uart_tx_set(1u);
    compute_store_read_and_transmit();

    while (1) {
    }

    return 0;
}
