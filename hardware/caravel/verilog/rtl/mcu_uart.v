// SPDX-License-Identifier: Apache-2.0
// mcu_uart: 8N1 UART, programmable divisor (clk cycles per bit).
`default_nettype none

module mcu_uart (
    input  wire        clk,
    input  wire        rst,
    input  wire [15:0] div,
    // tx
    input  wire        tx_start,
    input  wire [7:0]  tx_data,
    output wire        tx_busy,
    output wire        txd,
    // rx
    input  wire        rxd,
    output reg  [7:0]  rx_data,
    output reg         rx_valid,
    input  wire        rx_ack
);
    // ---------------- TX ----------------
    reg [9:0]  tx_sr;
    reg [3:0]  tx_cnt;
    reg [15:0] tx_tmr;
    assign tx_busy = tx_cnt != 4'd0;
    assign txd     = tx_busy ? tx_sr[0] : 1'b1;

    always @(posedge clk) begin
        if (rst) begin
            tx_sr <= 10'h3ff; tx_cnt <= 4'd0; tx_tmr <= 16'd0;
        end else if (!tx_busy) begin
            if (tx_start) begin
                tx_sr  <= {1'b1, tx_data, 1'b0};
                tx_cnt <= 4'd10;
                tx_tmr <= div;
            end
        end else if (tx_tmr == 16'd0) begin
            tx_sr  <= {1'b1, tx_sr[9:1]};
            tx_cnt <= tx_cnt - 4'd1;
            tx_tmr <= div;
        end else begin
            tx_tmr <= tx_tmr - 16'd1;
        end
    end

    // ---------------- RX ----------------
    reg [2:0]  rx_sync;
    reg [3:0]  rx_cnt;
    reg [15:0] rx_tmr;
    reg [8:0]  rx_sr;
    wire rx_in = rx_sync[2];

    always @(posedge clk) begin
        if (rst) begin
            rx_sync <= 3'b111; rx_cnt <= 4'd0; rx_tmr <= 16'd0;
            rx_sr <= 9'd0; rx_data <= 8'd0; rx_valid <= 1'b0;
        end else begin
            rx_sync <= {rx_sync[1:0], rxd};
            if (rx_ack) rx_valid <= 1'b0;
            if (rx_cnt == 4'd0) begin
                if (!rx_in) begin                  // start bit edge
                    rx_cnt <= 4'd10;
                    rx_tmr <= {1'b0, div[15:1]};   // sample mid-bit
                end
            end else if (rx_tmr == 16'd0) begin
                rx_tmr <= div;
                rx_cnt <= rx_cnt - 4'd1;
                rx_sr  <= {rx_in, rx_sr[8:1]};
                if (rx_cnt == 4'd10 && rx_in) begin
                    rx_cnt <= 4'd0;                // false start
                end else if (rx_cnt == 4'd1) begin
                    // rx_sr after this shift = {stop, d7..d0}
                    rx_data  <= rx_sr[8:1];
                    rx_valid <= 1'b1;
                end
            end else begin
                rx_tmr <= rx_tmr - 16'd1;
            end
        end
    end
endmodule
`default_nettype wire
