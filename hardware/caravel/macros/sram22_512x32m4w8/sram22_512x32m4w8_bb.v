/// sta-blackbox
// SRAM22 512x32 (write size 8) - blackbox for synthesis / STA
(* blackbox *)
module sram22_512x32m4w8 (
`ifdef USE_POWER_PINS
    inout vdd,
    inout vss,
`endif
    input clk, input rstb, input ce, input we,
    input [3:0] wmask, input [8:0] addr, input [31:0] din,
    output [31:0] dout
);
endmodule
