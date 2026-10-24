// SPDX-License-Identifier: Apache-2.0
// mcu_ram: WORDS x 32 single-port RAM, synchronous read (1-cycle latency),
// byte write mask. With MCU_SRAM22 it is the SRAM22 sky130 macro
// sram22_512x32m4w8 (UC Berkeley); otherwise a behavioral model.
`default_nettype none

module mcu_ram #(
    parameter WORDS = 512,
    parameter AW    = 9
) (
`ifdef USE_POWER_PINS
    inout  wire          vccd1,
    inout  wire          vssd1,
`endif
    input  wire          clk,
    input  wire          rstb,
    input  wire          ce,
    input  wire [AW-1:0] idx,
    input  wire [3:0]    we,     // byte write mask, 0 = read
    input  wire [31:0]   wdata,
    output wire [31:0]   rdata
);
`ifdef MCU_SRAM22
    sram22_512x32m4w8 sram (
`ifdef USE_POWER_PINS
        .vdd(vccd1), .vss(vssd1),
`endif
        .clk(clk), .rstb(rstb), .ce(ce), .we(|we), .wmask(we),
        .addr(idx), .din(wdata), .dout(rdata)
    );
`else
    reg [31:0] mem [0:WORDS-1];
    reg [31:0] dout;
    integer i;
    always @(posedge clk) begin
        if (ce && rstb) begin
            if (|we) begin
                for (i = 0; i < 4; i = i + 1)
                    if (we[i]) mem[idx][8*i +: 8] <= wdata[8*i +: 8];
            end else begin
                dout <= mem[idx];
            end
        end
    end
    assign rdata = dout;
`endif
endmodule
`default_nettype wire
