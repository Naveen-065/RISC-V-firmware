`timescale 1ns / 1ps
`default_nettype none

module zes400_voter_bit (
    input  wire a_i,
    input  wire b_i,
    input  wire c_i,
    output wire y_o,
    output wire error_o
);
    assign y_o = (a_i & b_i) | (b_i & c_i) | (a_i & c_i);
    assign error_o = (a_i ^ b_i) | (b_i ^ c_i);
endmodule

module zes400_uart_tmr (
    input  wire [2:0] source_tx_i,
    output wire       peripheral_rx_o,
    output wire       source_tx_error_o,

    input  wire [2:0] peripheral_tx_i,
    output wire       source_rx_o,
    output wire       peripheral_tx_error_o
);
    zes400_voter_bit source_tx_voter (
        .a_i(source_tx_i[0]),
        .b_i(source_tx_i[1]),
        .c_i(source_tx_i[2]),
        .y_o(peripheral_rx_o),
        .error_o(source_tx_error_o)
    );

    zes400_voter_bit peripheral_tx_voter (
        .a_i(peripheral_tx_i[0]),
        .b_i(peripheral_tx_i[1]),
        .c_i(peripheral_tx_i[2]),
        .y_o(source_rx_o),
        .error_o(peripheral_tx_error_o)
    );
endmodule

module simple_uart_rx #(
    parameter integer CLKS_PER_BIT = 8
) (
    input  wire       clk_i,
    input  wire       rst_i,
    input  wire       rx_i,
    output reg  [7:0] data_o,
    output reg        valid_o
);
    localparam [1:0] STATE_IDLE  = 2'd0;
    localparam [1:0] STATE_START = 2'd1;
    localparam [1:0] STATE_DATA  = 2'd2;
    localparam [1:0] STATE_STOP  = 2'd3;

    reg [1:0] state_q;
    reg [15:0] clk_count_q;
    reg [2:0] bit_index_q;
    reg [7:0] data_q;

    always @(posedge clk_i) begin
        if (rst_i) begin
            state_q <= STATE_IDLE;
            clk_count_q <= 16'd0;
            bit_index_q <= 3'd0;
            data_q <= 8'h00;
            data_o <= 8'h00;
            valid_o <= 1'b0;
        end else begin
            valid_o <= 1'b0;

            case (state_q)
                STATE_IDLE: begin
                    clk_count_q <= 16'd0;
                    bit_index_q <= 3'd0;
                    if (!rx_i) begin
                        clk_count_q <= (CLKS_PER_BIT / 2);
                        state_q <= STATE_START;
                    end
                end

                STATE_START: begin
                    if (clk_count_q == 16'd0) begin
                        if (!rx_i) begin
                            clk_count_q <= CLKS_PER_BIT - 1;
                            state_q <= STATE_DATA;
                        end else begin
                            state_q <= STATE_IDLE;
                        end
                    end else begin
                        clk_count_q <= clk_count_q - 16'd1;
                    end
                end

                STATE_DATA: begin
                    if (clk_count_q == 16'd0) begin
                        data_q[bit_index_q] <= rx_i;
                        clk_count_q <= CLKS_PER_BIT - 1;

                        if (bit_index_q == 3'd7) begin
                            bit_index_q <= 3'd0;
                            state_q <= STATE_STOP;
                        end else begin
                            bit_index_q <= bit_index_q + 3'd1;
                        end
                    end else begin
                        clk_count_q <= clk_count_q - 16'd1;
                    end
                end

                STATE_STOP: begin
                    if (clk_count_q == 16'd0) begin
                        if (rx_i) begin
                            data_o <= data_q;
                            valid_o <= 1'b1;
                        end
                        state_q <= STATE_IDLE;
                    end else begin
                        clk_count_q <= clk_count_q - 16'd1;
                    end
                end

                default: begin
                    state_q <= STATE_IDLE;
                end
            endcase
        end
    end
endmodule

module simple_uart_tx #(
    parameter integer CLKS_PER_BIT = 8
) (
    input  wire       clk_i,
    input  wire       rst_i,
    input  wire [7:0] data_i,
    input  wire       valid_i,
    output reg        ready_o,
    output reg        tx_o
);
    localparam [1:0] STATE_IDLE  = 2'd0;
    localparam [1:0] STATE_START = 2'd1;
    localparam [1:0] STATE_DATA  = 2'd2;
    localparam [1:0] STATE_STOP  = 2'd3;

    reg [1:0] state_q;
    reg [15:0] clk_count_q;
    reg [2:0] bit_index_q;
    reg [7:0] data_q;

    always @(posedge clk_i) begin
        if (rst_i) begin
            state_q <= STATE_IDLE;
            clk_count_q <= 16'd0;
            bit_index_q <= 3'd0;
            data_q <= 8'h00;
            ready_o <= 1'b1;
            tx_o <= 1'b1;
        end else begin
            case (state_q)
                STATE_IDLE: begin
                    tx_o <= 1'b1;
                    ready_o <= 1'b1;
                    clk_count_q <= 16'd0;
                    bit_index_q <= 3'd0;

                    if (valid_i) begin
                        data_q <= data_i;
                        tx_o <= 1'b0;
                        ready_o <= 1'b0;
                        clk_count_q <= CLKS_PER_BIT - 1;
                        state_q <= STATE_START;
                    end
                end

                STATE_START: begin
                    ready_o <= 1'b0;
                    if (clk_count_q == 16'd0) begin
                        tx_o <= data_q[0];
                        clk_count_q <= CLKS_PER_BIT - 1;
                        bit_index_q <= 3'd0;
                        state_q <= STATE_DATA;
                    end else begin
                        clk_count_q <= clk_count_q - 16'd1;
                    end
                end

                STATE_DATA: begin
                    ready_o <= 1'b0;
                    if (clk_count_q == 16'd0) begin
                        clk_count_q <= CLKS_PER_BIT - 1;

                        if (bit_index_q == 3'd7) begin
                            tx_o <= 1'b1;
                            bit_index_q <= 3'd0;
                            state_q <= STATE_STOP;
                        end else begin
                            bit_index_q <= bit_index_q + 3'd1;
                            tx_o <= data_q[bit_index_q + 3'd1];
                        end
                    end else begin
                        clk_count_q <= clk_count_q - 16'd1;
                    end
                end

                STATE_STOP: begin
                    ready_o <= 1'b0;
                    if (clk_count_q == 16'd0) begin
                        tx_o <= 1'b1;
                        ready_o <= 1'b1;
                        state_q <= STATE_IDLE;
                    end else begin
                        clk_count_q <= clk_count_q - 16'd1;
                    end
                end

                default: begin
                    state_q <= STATE_IDLE;
                end
            endcase
        end
    end
endmodule

module caravel_x1_uart_model #(
    parameter integer CLKS_PER_BIT = 8,
    parameter integer RESPONSE_GAP_BITS = 4
) (
    input  wire clk_i,
    input  wire rst_i,
    input  wire uart_rx_i,
    input  wire ai_result_fault_i,
    output wire uart_tx_o
);
    localparam [7:0] SYNC_BYTE  = 8'hA5;
    localparam [7:0] RESP_BYTE  = 8'h5A;
    localparam [7:0] OP_PROGRAM = 8'h50;
    localparam [7:0] OP_READ    = 8'h52;
    localparam [7:0] OP_MATMUL  = 8'h4D;

    localparam [7:0] AI_STATUS_ADDR = 8'hF0;
    localparam [31:0] AI_DONE_WORD  = 32'hA100_0001;
    localparam [31:0] AI_ERR_WORD   = 32'hE000_0001;
    localparam [7:0] X1_AI_A_BASE   = 8'h20;
    localparam [7:0] X1_AI_B_BASE   = 8'h24;
    localparam [7:0] X1_AI_C_BASE   = 8'h80;

    localparam [31:0] MATMUL_BASE       = 32'h3100_0000;
    localparam [31:0] MATMUL_CTRL_ADDR  = MATMUL_BASE + 32'h0000_0000;
    localparam [31:0] MATMUL_STAT_ADDR  = MATMUL_BASE + 32'h0000_0004;
    localparam [31:0] MATMUL_A_BASE     = MATMUL_BASE + 32'h0000_0100;
    localparam [31:0] MATMUL_B_BASE     = MATMUL_BASE + 32'h0000_0200;
    localparam [31:0] MATMUL_C_BASE     = MATMUL_BASE + 32'h0000_0400;

    localparam [2:0] STATE_SYNC    = 3'd0;
    localparam [2:0] STATE_OP      = 3'd1;
    localparam [2:0] STATE_ADDR    = 3'd2;
    localparam [2:0] STATE_DATA    = 3'd3;
    localparam [2:0] STATE_AI_DATA = 3'd4;

    localparam [3:0] WB_IDLE         = 4'd0;
    localparam [3:0] WB_WRITE_A      = 4'd1;
    localparam [3:0] WB_WRITE_A_WAIT = 4'd2;
    localparam [3:0] WB_WRITE_B      = 4'd3;
    localparam [3:0] WB_WRITE_B_WAIT = 4'd4;
    localparam [3:0] WB_START        = 4'd5;
    localparam [3:0] WB_START_WAIT   = 4'd6;
    localparam [3:0] WB_POLL         = 4'd7;
    localparam [3:0] WB_POLL_WAIT    = 4'd8;
    localparam [3:0] WB_READ_C       = 4'd9;
    localparam [3:0] WB_READ_C_WAIT  = 4'd10;

    wire [7:0] rx_data;
    wire       rx_valid;
    reg  [7:0] tx_data;
    reg        tx_valid;
    wire       tx_ready;

    reg [2:0]  state_q;
    reg [7:0]  op_q;
    reg [7:0]  addr_q;
    reg [2:0]  byte_count_q;
    reg [31:0] data_shift_q;
    wire [31:0] next_data_word = {data_shift_q[23:0], rx_data};

    reg [31:0] x1_memory [0:255];
    reg [7:0]  ai_operand_q [0:7];
    reg [7:0]  response_q [0:5];
    reg [2:0]  response_index_q;
    reg        response_pending_q;
    reg [15:0] response_wait_q;
    reg [3:0]  wb_state_q;
    reg [3:0]  wb_word_index_q;
    reg [1:0]  wb_c_index_q;
    reg [7:0]  wb_poll_count_q;

    reg        mat_wbs_stb_i;
    reg        mat_wbs_cyc_i;
    reg        mat_wbs_we_i;
    reg [3:0]  mat_wbs_sel_i;
    reg [31:0] mat_wbs_dat_i;
    reg [31:0] mat_wbs_adr_i;
    wire [31:0] mat_wbs_dat_o;
    wire        mat_wbs_ack_o;
    wire        mat_irq_o;

    integer init_i;

    simple_uart_rx #(
        .CLKS_PER_BIT(CLKS_PER_BIT)
    ) uart_rx_inst (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .rx_i(uart_rx_i),
        .data_o(rx_data),
        .valid_o(rx_valid)
    );

    simple_uart_tx #(
        .CLKS_PER_BIT(CLKS_PER_BIT)
    ) uart_tx_inst (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .data_i(tx_data),
        .valid_i(tx_valid),
        .ready_o(tx_ready),
        .tx_o(uart_tx_o)
    );

    mat_mult_wb #(
        .BASE_ADDR(MATMUL_BASE)
    ) partcl_matmul_inst (
        .wb_clk_i(clk_i),
        .wb_rst_i(rst_i),
        .wbs_stb_i(mat_wbs_stb_i),
        .wbs_cyc_i(mat_wbs_cyc_i),
        .wbs_we_i(mat_wbs_we_i),
        .wbs_sel_i(mat_wbs_sel_i),
        .wbs_dat_i(mat_wbs_dat_i),
        .wbs_adr_i(mat_wbs_adr_i),
        .wbs_dat_o(mat_wbs_dat_o),
        .wbs_ack_o(mat_wbs_ack_o),
        .irq_o(mat_irq_o)
    );

    task queue_response;
        input [7:0] response_addr;
        input [31:0] response_data;
        begin
            response_q[0] <= RESP_BYTE;
            response_q[1] <= response_addr;
            response_q[2] <= response_data[31:24];
            response_q[3] <= response_data[23:16];
            response_q[4] <= response_data[15:8];
            response_q[5] <= response_data[7:0];
            response_index_q <= 3'd0;
            response_wait_q <= RESPONSE_GAP_BITS * CLKS_PER_BIT;
            response_pending_q <= 1'b1;
        end
    endtask

    function signed [31:0] sx8;
        input [7:0] value;
        begin
            sx8 = {{24{value[7]}}, value};
        end
    endfunction

    function [31:0] matmul_a_word;
        input [3:0] word_index;
        begin
            case (word_index)
                4'd0: matmul_a_word = {8'h00, 8'h00, ai_operand_q[1], ai_operand_q[0]};
                4'd2: matmul_a_word = {8'h00, 8'h00, ai_operand_q[3], ai_operand_q[2]};
                default: matmul_a_word = 32'h0000_0000;
            endcase
        end
    endfunction

    function [31:0] matmul_b_word;
        input [3:0] word_index;
        begin
            case (word_index)
                4'd0: matmul_b_word = {8'h00, 8'h00, ai_operand_q[5], ai_operand_q[4]};
                4'd2: matmul_b_word = {8'h00, 8'h00, ai_operand_q[7], ai_operand_q[6]};
                default: matmul_b_word = 32'h0000_0000;
            endcase
        end
    endfunction

    function [31:0] matmul_c_addr;
        input [1:0] c_index;
        begin
            case (c_index)
                2'd0: matmul_c_addr = MATMUL_C_BASE + 32'd0;
                2'd1: matmul_c_addr = MATMUL_C_BASE + 32'd4;
                2'd2: matmul_c_addr = MATMUL_C_BASE + 32'd32;
                default: matmul_c_addr = MATMUL_C_BASE + 32'd36;
            endcase
        end
    endfunction

    always @(posedge clk_i) begin
        if (rst_i) begin
            state_q <= STATE_SYNC;
            op_q <= 8'h00;
            addr_q <= 8'h00;
            byte_count_q <= 3'd0;
            data_shift_q <= 32'h0000_0000;
            tx_data <= 8'h00;
            tx_valid <= 1'b0;
            response_index_q <= 3'd0;
            response_pending_q <= 1'b0;
            response_wait_q <= 16'd0;
            wb_state_q <= WB_IDLE;
            wb_word_index_q <= 4'd0;
            wb_c_index_q <= 2'd0;
            wb_poll_count_q <= 8'd0;
            mat_wbs_stb_i <= 1'b0;
            mat_wbs_cyc_i <= 1'b0;
            mat_wbs_we_i <= 1'b0;
            mat_wbs_sel_i <= 4'h0;
            mat_wbs_dat_i <= 32'h0000_0000;
            mat_wbs_adr_i <= 32'h0000_0000;

            for (init_i = 0; init_i < 256; init_i = init_i + 1) begin
                x1_memory[init_i] <= 32'h0000_0000;
            end
            for (init_i = 0; init_i < 8; init_i = init_i + 1) begin
                ai_operand_q[init_i] <= 8'h00;
            end
            for (init_i = 0; init_i < 6; init_i = init_i + 1) begin
                response_q[init_i] <= 8'h00;
            end
        end else begin
            tx_valid <= 1'b0;

            case (wb_state_q)
                WB_IDLE: begin
                    mat_wbs_stb_i <= 1'b0;
                    mat_wbs_cyc_i <= 1'b0;
                    mat_wbs_we_i <= 1'b0;
                    mat_wbs_sel_i <= 4'h0;
                end

                WB_WRITE_A: begin
                    mat_wbs_stb_i <= 1'b1;
                    mat_wbs_cyc_i <= 1'b1;
                    mat_wbs_we_i <= 1'b1;
                    mat_wbs_sel_i <= 4'hF;
                    mat_wbs_adr_i <= MATMUL_A_BASE + {26'h0, wb_word_index_q, 2'b00};
                    mat_wbs_dat_i <= matmul_a_word(wb_word_index_q);
                    wb_state_q <= WB_WRITE_A_WAIT;
                end

                WB_WRITE_A_WAIT: begin
                    if (mat_wbs_ack_o) begin
                        mat_wbs_stb_i <= 1'b0;
                        mat_wbs_cyc_i <= 1'b0;
                        if (wb_word_index_q == 4'd15) begin
                            wb_word_index_q <= 4'd0;
                            wb_state_q <= WB_WRITE_B;
                        end else begin
                            wb_word_index_q <= wb_word_index_q + 4'd1;
                            wb_state_q <= WB_WRITE_A;
                        end
                    end
                end

                WB_WRITE_B: begin
                    mat_wbs_stb_i <= 1'b1;
                    mat_wbs_cyc_i <= 1'b1;
                    mat_wbs_we_i <= 1'b1;
                    mat_wbs_sel_i <= 4'hF;
                    mat_wbs_adr_i <= MATMUL_B_BASE + {26'h0, wb_word_index_q, 2'b00};
                    mat_wbs_dat_i <= matmul_b_word(wb_word_index_q);
                    wb_state_q <= WB_WRITE_B_WAIT;
                end

                WB_WRITE_B_WAIT: begin
                    if (mat_wbs_ack_o) begin
                        mat_wbs_stb_i <= 1'b0;
                        mat_wbs_cyc_i <= 1'b0;
                        if (wb_word_index_q == 4'd15) begin
                            wb_word_index_q <= 4'd0;
                            wb_state_q <= WB_START;
                        end else begin
                            wb_word_index_q <= wb_word_index_q + 4'd1;
                            wb_state_q <= WB_WRITE_B;
                        end
                    end
                end

                WB_START: begin
                    mat_wbs_stb_i <= 1'b1;
                    mat_wbs_cyc_i <= 1'b1;
                    mat_wbs_we_i <= 1'b1;
                    mat_wbs_sel_i <= 4'hF;
                    mat_wbs_adr_i <= MATMUL_CTRL_ADDR;
                    mat_wbs_dat_i <= 32'h0000_0105;
                    wb_poll_count_q <= 8'd0;
                    wb_state_q <= WB_START_WAIT;
                end

                WB_START_WAIT: begin
                    if (mat_wbs_ack_o) begin
                        mat_wbs_stb_i <= 1'b0;
                        mat_wbs_cyc_i <= 1'b0;
                        wb_state_q <= WB_POLL;
                    end
                end

                WB_POLL: begin
                    mat_wbs_stb_i <= 1'b1;
                    mat_wbs_cyc_i <= 1'b1;
                    mat_wbs_we_i <= 1'b0;
                    mat_wbs_sel_i <= 4'hF;
                    mat_wbs_adr_i <= MATMUL_STAT_ADDR;
                    wb_state_q <= WB_POLL_WAIT;
                end

                WB_POLL_WAIT: begin
                    if (mat_wbs_ack_o) begin
                        mat_wbs_stb_i <= 1'b0;
                        mat_wbs_cyc_i <= 1'b0;
                        if (mat_wbs_dat_o[3]) begin
                            wb_c_index_q <= 2'd0;
                            wb_state_q <= WB_READ_C;
                        end else if (wb_poll_count_q == 8'hFF) begin
                            queue_response(AI_STATUS_ADDR, AI_ERR_WORD);
                            wb_state_q <= WB_IDLE;
                        end else begin
                            wb_poll_count_q <= wb_poll_count_q + 8'd1;
                            wb_state_q <= WB_POLL;
                        end
                    end
                end

                WB_READ_C: begin
                    mat_wbs_stb_i <= 1'b1;
                    mat_wbs_cyc_i <= 1'b1;
                    mat_wbs_we_i <= 1'b0;
                    mat_wbs_sel_i <= 4'hF;
                    mat_wbs_adr_i <= matmul_c_addr(wb_c_index_q);
                    wb_state_q <= WB_READ_C_WAIT;
                end

                WB_READ_C_WAIT: begin
                    if (mat_wbs_ack_o) begin
                        mat_wbs_stb_i <= 1'b0;
                        mat_wbs_cyc_i <= 1'b0;
                        case (wb_c_index_q)
                            2'd0: x1_memory[X1_AI_C_BASE + 8'd0] <= ai_result_fault_i ? (mat_wbs_dat_o ^ 32'h0000_0001) : mat_wbs_dat_o;
                            2'd1: x1_memory[X1_AI_C_BASE + 8'd1] <= mat_wbs_dat_o;
                            2'd2: x1_memory[X1_AI_C_BASE + 8'd2] <= mat_wbs_dat_o;
                            default: x1_memory[X1_AI_C_BASE + 8'd3] <= mat_wbs_dat_o;
                        endcase

                        if (wb_c_index_q == 2'd3) begin
                            queue_response(AI_STATUS_ADDR, AI_DONE_WORD);
                            wb_state_q <= WB_IDLE;
                        end else begin
                            wb_c_index_q <= wb_c_index_q + 2'd1;
                            wb_state_q <= WB_READ_C;
                        end
                    end
                end

                default: begin
                    wb_state_q <= WB_IDLE;
                end
            endcase

            if (response_pending_q) begin
                if (response_wait_q != 16'd0) begin
                    response_wait_q <= response_wait_q - 16'd1;
                end else if (tx_valid) begin
                    if (response_index_q == 3'd5) begin
                        response_index_q <= 3'd0;
                        response_pending_q <= 1'b0;
                    end else begin
                        response_index_q <= response_index_q + 3'd1;
                    end
                end else if (tx_ready) begin
                    tx_data <= response_q[response_index_q];
                    tx_valid <= 1'b1;
                end
            end

            if (rx_valid) begin
                case (state_q)
                    STATE_SYNC: begin
                        if (rx_data == SYNC_BYTE) begin
                            state_q <= STATE_OP;
                        end
                    end

                    STATE_OP: begin
                        op_q <= rx_data;
                        if ((rx_data == OP_PROGRAM) || (rx_data == OP_READ)) begin
                            state_q <= STATE_ADDR;
                        end else if (rx_data == OP_MATMUL) begin
                            byte_count_q <= 3'd0;
                            state_q <= STATE_AI_DATA;
                        end else begin
                            state_q <= STATE_SYNC;
                        end
                    end

                    STATE_ADDR: begin
                        addr_q <= rx_data;
                        if (op_q == OP_READ) begin
                            queue_response(rx_data, x1_memory[rx_data]);
                            state_q <= STATE_SYNC;
                        end else begin
                            data_shift_q <= 32'h0000_0000;
                            byte_count_q <= 3'd0;
                            state_q <= STATE_DATA;
                        end
                    end

                    STATE_DATA: begin
                        data_shift_q <= next_data_word;
                        if (byte_count_q == 3'd3) begin
                            x1_memory[addr_q] <= next_data_word;
                            queue_response(addr_q, next_data_word);
                            byte_count_q <= 3'd0;
                            state_q <= STATE_SYNC;
                        end else begin
                            byte_count_q <= byte_count_q + 3'd1;
                        end
                    end

                    STATE_AI_DATA: begin
                        ai_operand_q[byte_count_q] <= rx_data;
                        if (byte_count_q == 3'd7) begin
                            x1_memory[X1_AI_A_BASE + 8'd0] <= {ai_operand_q[0], ai_operand_q[1], ai_operand_q[2], ai_operand_q[3]};
                            x1_memory[X1_AI_B_BASE + 8'd0] <= {ai_operand_q[4], ai_operand_q[5], ai_operand_q[6], rx_data};
                            wb_word_index_q <= 4'd0;
                            wb_c_index_q <= 2'd0;
                            wb_poll_count_q <= 8'd0;
                            wb_state_q <= WB_WRITE_A;
                            byte_count_q <= 3'd0;
                            state_q <= STATE_SYNC;
                        end else begin
                            byte_count_q <= byte_count_q + 3'd1;
                        end
                    end

                    default: begin
                        state_q <= STATE_SYNC;
                    end
                endcase
            end
        end
    end
endmodule

module tmr_caravel_uart_system #(
    parameter integer CLKS_PER_BIT = 8
) (
    input  wire       clk_i,
    input  wire       rst_i,

    input  wire [2:0] user_tx_lanes_i,
    output wire       user_rx_o,

    input  wire [2:0] caravel_tx_fault_mask_i,
    input  wire [2:0] caravel_ai_fault_mask_i,
    output wire [2:0] caravel_tx_raw_o,
    output wire [2:0] caravel_tx_faulted_o,
    output wire       voted_caravel_rx_o,
    output wire       tx_vote_error_o,
    output wire       rx_vote_error_o
);
    wire [2:0] caravel_tx_raw;
    wire [2:0] caravel_tx_faulted;
    wire       caravel_rx;

    assign caravel_tx_raw_o = caravel_tx_raw;
    assign caravel_tx_faulted = caravel_tx_raw ^ caravel_tx_fault_mask_i;
    assign caravel_tx_faulted_o = caravel_tx_faulted;
    assign voted_caravel_rx_o = caravel_rx;

    zes400_uart_tmr uart_tmr_inst (
        .source_tx_i(caravel_tx_faulted),
        .peripheral_rx_o(user_rx_o),
        .source_tx_error_o(tx_vote_error_o),
        .peripheral_tx_i(user_tx_lanes_i),
        .source_rx_o(caravel_rx),
        .peripheral_tx_error_o(rx_vote_error_o)
    );

    caravel_x1_uart_model #(
        .CLKS_PER_BIT(CLKS_PER_BIT)
    ) caravel_0 (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .uart_rx_i(caravel_rx),
        .ai_result_fault_i(caravel_ai_fault_mask_i[0]),
        .uart_tx_o(caravel_tx_raw[0])
    );

    caravel_x1_uart_model #(
        .CLKS_PER_BIT(CLKS_PER_BIT)
    ) caravel_1 (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .uart_rx_i(caravel_rx),
        .ai_result_fault_i(caravel_ai_fault_mask_i[1]),
        .uart_tx_o(caravel_tx_raw[1])
    );

    caravel_x1_uart_model #(
        .CLKS_PER_BIT(CLKS_PER_BIT)
    ) caravel_2 (
        .clk_i(clk_i),
        .rst_i(rst_i),
        .uart_rx_i(caravel_rx),
        .ai_result_fault_i(caravel_ai_fault_mask_i[2]),
        .uart_tx_o(caravel_tx_raw[2])
    );
endmodule

`default_nettype wire
