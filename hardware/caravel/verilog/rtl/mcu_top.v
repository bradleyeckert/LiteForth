// SPDX-License-Identifier: Apache-2.0
// mcu_top: tiny RV32E microcontroller for the Caravel user area.
//
// Management SoC (Wishbone, base 0x3000_0000):
//   0x3000_0000..0x3000_07FF  RAM (512 x 32b SRAM22 macro), firmware is loaded here
//   0x3000_1000  CTRL    [0]=run (CPU out of reset)  RO:[1]=halted
//   0x3000_1004  MAILBOX RO, reading clears user_irq[0]
//   0x3000_1008  PC      RO
//
// CPU address map:
//   0x0000_0000..0x0000_07FF  RAM
//   0x8000_0000  GPIO_OUT   0x8000_0004 GPIO_OE   0x8000_0008 GPIO_IN
//   0x8000_0010  UART_DATA (W: send byte, R: received byte, clears valid)
//   0x8000_0014  UART_STAT [0]=tx_busy [1]=rx_valid
//   0x8000_0018  UART_DIV  (clk cycles per bit - 1)
//   0x8000_0020  TIMER     free-running 32-bit counter (writable)
//   0x8000_0030  MAILBOX   write -> visible to mgmt SoC, raises user_irq[0]
//
// Caravel pads: io[8]=UART TX, io[9]=UART RX, io[25:10]=GPIO[15:0]
`default_nettype none

module mcu_top #(
    parameter RAM_WORDS = 512
) (
`ifdef USE_POWER_PINS
    inout vccd1,
    inout vssd1,
`endif
    input  wire         wb_clk_i,
    input  wire         wb_rst_i,
    input  wire         wbs_stb_i,
    input  wire         wbs_cyc_i,
    input  wire         wbs_we_i,
    input  wire [3:0]   wbs_sel_i,
    input  wire [31:0]  wbs_dat_i,
    input  wire [31:0]  wbs_adr_i,
    output reg          wbs_ack_o,
    output reg  [31:0]  wbs_dat_o,

    output wire [127:0] la_data_out,

    input  wire [37:0]  io_in,
    output wire [37:0]  io_out,
    output wire [37:0]  io_oeb,

    output wire [2:0]   irq
);
    localparam AW = $clog2(RAM_WORDS);

    wire clk = wb_clk_i;
    // reset synchronizer: keeps the external reset input off the datapath
    reg  rst;
    always @(posedge clk) rst <= wb_rst_i;

    // ---------------- control ----------------
    reg         run;
    reg  [31:0] mbox_data;
    reg         mbox_irq;
    wire        cpu_rst = rst | ~run;

    // ---------------- CPU ----------------
    wire        c_valid, c_rd, halted;
    wire [31:0] c_addr, c_wdata, pc;
    wire [3:0]  c_wstrb;
    wire [31:0] c_rdata;
    wire        c_ready;

    mcu_cpu cpu (
        .clk(clk), .rst(cpu_rst),
        .mem_valid(c_valid), .mem_rd(c_rd), .mem_addr(c_addr),
        .mem_wdata(c_wdata), .mem_wstrb(c_wstrb),
        .mem_rdata(c_rdata), .mem_ready(c_ready),
        .pc_o(pc), .halted(halted)
    );

    // ---------------- Wishbone slave ----------------
    // Inputs are registered at the macro boundary (isolates the Caravel
    // I/O timing from the RAM array). ack_d blocks the stale request seen
    // one cycle after ack because of the input register.
    reg         wb_stb_r, wb_we_r, ack_d;
    reg  [3:0]  wb_sel_r;
    reg  [31:0] wb_dat_r;
    reg  [12:0] wb_adr_r;
    always @(posedge clk) begin
        if (rst) begin
            wb_stb_r <= 1'b0; ack_d <= 1'b0;
        end else begin
            wb_stb_r <= wbs_cyc_i & wbs_stb_i;
            ack_d    <= wbs_ack_o;
        end
        wb_we_r  <= wbs_we_i;
        wb_sel_r <= wbs_sel_i;
        wb_dat_r <= wbs_dat_i;
        wb_adr_r <= wbs_adr_i[12:0];
    end
    // 2-phase access: request cycle -> (SRAM read data valid) -> ack
    reg         wb_ph1, wb_ph1_ram;
    reg  [31:0] wb_reg_rdata;
    wire wb_req     = wb_stb_r & ~wb_ph1 & ~wbs_ack_o & ~ack_d;
    wire wb_ram     = wb_req & ~wb_adr_r[12];
    wire wb_reg     = wb_req &  wb_adr_r[12];

    // ---------------- RAM (synchronous SRAM, 1-cycle read latency) ----------------
    wire        c_is_io  = c_addr[31];
    wire        c_ram    = c_valid & ~c_is_io;
    assign      c_ready  = c_is_io | ~wb_ram;          // WB has priority on RAM

    wire [AW-1:0] ram_idx   = wb_ram ? wb_adr_r[AW+1:2] : c_addr[AW+1:2];
    wire [3:0]    ram_we    = wb_ram ? (wb_we_r ? wb_sel_r : 4'b0)
                                     : (c_ram ? c_wstrb : 4'b0);
    wire [31:0]   ram_wdata = wb_ram ? wb_dat_r : c_wdata;
    wire          ram_ce    = wb_ram | c_ram;
    wire [31:0]   ram_rdata;

    mcu_ram #(.WORDS(RAM_WORDS), .AW(AW)) ram (
`ifdef USE_POWER_PINS
        .vccd1(vccd1), .vssd1(vssd1),
`endif
        .clk(clk), .rstb(~rst), .ce(ram_ce), .idx(ram_idx), .we(ram_we),
        .wdata(ram_wdata), .rdata(ram_rdata)
    );

    // ---------------- peripherals ----------------
    reg  [15:0] gpio_out, gpio_oe;
    reg  [15:0] uart_div;
    reg  [31:0] timer;
    reg  [15:0] gpio_in_s1, gpio_in_s2;
    wire        uart_busy, uart_rx_valid;
    wire [7:0]  uart_rx_data;
    wire        txd;

    wire        io_sel  = c_valid & c_is_io;
    wire        io_wr   = io_sel & (c_wstrb != 4'b0);
    wire [5:0]  io_reg  = c_addr[7:2];
    wire        uart_tx_start = io_wr & (io_reg == 6'h04);
    wire        uart_rx_ack   = io_sel & c_rd & (io_reg == 6'h04);

    mcu_uart uart (
        .clk(clk), .rst(rst), .div(uart_div),
        .tx_start(uart_tx_start), .tx_data(c_wdata[7:0]), .tx_busy(uart_busy), .txd(txd),
        .rxd(io_in[9]), .rx_data(uart_rx_data), .rx_valid(uart_rx_valid), .rx_ack(uart_rx_ack)
    );

    reg [31:0] io_rdata;
    always @(*) begin
        case (io_reg)
            6'h00: io_rdata = {16'd0, gpio_out};
            6'h01: io_rdata = {16'd0, gpio_oe};
            6'h02: io_rdata = {16'd0, gpio_in_s2};
            6'h04: io_rdata = {24'd0, uart_rx_data};
            6'h05: io_rdata = {30'd0, uart_rx_valid, uart_busy};
            6'h06: io_rdata = {16'd0, uart_div};
            6'h08: io_rdata = timer;
            6'h0c: io_rdata = mbox_data;
            default: io_rdata = 32'd0;
        endcase
    end
    // IO reads are registered too, so every CPU read has 1-cycle latency
    reg [31:0] io_rdata_q;
    reg        last_io;
    always @(posedge clk) begin
        if (c_valid & c_ready) last_io <= c_is_io;
        if (io_sel) io_rdata_q <= io_rdata;
    end
    assign c_rdata = last_io ? io_rdata_q : ram_rdata;

    always @(posedge clk) begin
        if (rst) begin
            gpio_out <= 16'd0; gpio_oe <= 16'd0; uart_div <= 16'd216; // 115200 @ 25 MHz
            timer <= 32'd0; mbox_data <= 32'd0; mbox_irq <= 1'b0; run <= 1'b0;
            wbs_ack_o <= 1'b0; wbs_dat_o <= 32'd0; wb_ph1 <= 1'b0; wb_ph1_ram <= 1'b0;
            gpio_in_s1 <= 16'd0; gpio_in_s2 <= 16'd0;
        end else begin
            gpio_in_s1 <= io_in[25:10];
            gpio_in_s2 <= gpio_in_s1;
            timer <= timer + 32'd1;
            // CPU side register writes
            if (io_wr) begin
                case (io_reg)
                    6'h00: gpio_out <= c_wdata[15:0];
                    6'h01: gpio_oe  <= c_wdata[15:0];
                    6'h06: uart_div <= c_wdata[15:0];
                    6'h08: timer    <= c_wdata;
                    6'h0c: begin mbox_data <= c_wdata; mbox_irq <= 1'b1; end
                    default: ;
                endcase
            end
            // Wishbone side
            wb_ph1     <= wb_req;
            wb_ph1_ram <= wb_ram;
            wbs_ack_o  <= wb_ph1;
            if (wb_ph1) wbs_dat_o <= wb_ph1_ram ? ram_rdata : wb_reg_rdata;
            if (wb_reg) begin
                case (wb_adr_r[3:2])
                    2'd0: wb_reg_rdata <= {30'd0, halted, run};
                    2'd1: wb_reg_rdata <= mbox_data;
                    2'd2: wb_reg_rdata <= pc;
                    default: wb_reg_rdata <= 32'd0;
                endcase
                if (wb_we_r && wb_adr_r[3:2] == 2'd0 && wb_sel_r[0]) run <= wb_dat_r[0];
                if (!wb_we_r && wb_adr_r[3:2] == 2'd1) mbox_irq <= 1'b0;
            end
        end
    end

    // ---------------- pads ----------------
    assign io_out = {12'd0, gpio_out, 1'b0, txd, 8'd0};
    assign io_oeb = {12'hfff, ~gpio_oe, 1'b1, 1'b0, 8'hff};

    assign irq         = {2'b00, mbox_irq};
    assign la_data_out = {64'd0, mbox_data, pc};
endmodule
`default_nettype wire
