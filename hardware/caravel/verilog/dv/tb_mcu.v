// Testbench: acts like the Caravel management SoC on Wishbone.
// Loads firmware into MCU RAM, releases the CPU, waits for halt,
// dumps RAM, and decodes the UART TX pin.
`timescale 1ns/1ps
`default_nettype none

module tb_mcu;
`ifndef RAM_WORDS
`define RAM_WORDS 512
`endif
    localparam WORDS = `RAM_WORDS;

    reg clk = 0, rst = 1;
    always #20 clk = ~clk;   // 25 MHz

    reg         stb = 0, cyc = 0, we = 0;
    reg  [3:0]  sel = 4'hf;
    reg  [31:0] dat_i = 0, adr = 0;
    wire        ack;
    wire [31:0] dat_o;
    wire [127:0] la;
    reg  [37:0] io_in = 38'h0;
    wire [37:0] io_out, io_oeb;
    wire [2:0]  irq;

`ifdef GL
    supply1 vccd1; supply0 vssd1;
    mcu_top dut (
        .vccd1(vccd1), .vssd1(vssd1),
`else
    mcu_top #(.RAM_WORDS(WORDS)) dut (
`endif
        .wb_clk_i(clk), .wb_rst_i(rst),
        .wbs_stb_i(stb), .wbs_cyc_i(cyc), .wbs_we_i(we), .wbs_sel_i(sel),
        .wbs_dat_i(dat_i), .wbs_adr_i(adr), .wbs_ack_o(ack), .wbs_dat_o(dat_o),
        .la_data_out(la), .io_in(io_in), .io_out(io_out), .io_oeb(io_oeb), .irq(irq)
    );

    task wb_write(input [31:0] a, input [31:0] d);
    begin
        @(negedge clk); adr = a; dat_i = d; we = 1; stb = 1; cyc = 1; sel = 4'hf;
        @(posedge clk); while (!ack) @(posedge clk);
        @(negedge clk); stb = 0; cyc = 0; we = 0;
    end
    endtask

    task wb_read(input [31:0] a, output [31:0] d);
    begin
        @(negedge clk); adr = a; we = 0; stb = 1; cyc = 1;
        @(posedge clk); while (!ack) @(posedge clk);
        #1 d = dat_o;
        @(negedge clk); stb = 0; cyc = 0;
    end
    endtask

    // UART loopback: io[9] (rx) <= io[8] (tx) when +LOOPBACK
    reg loopback = 0;
    always @(*) io_in[9] = loopback ? io_out[8] : 1'b1;
    // GPIO input pattern on pins configured as input
    initial io_in[25:10] = 16'hA5C3;

    // UART TX monitor (divisor read from DUT config: bit time = (div+1) clocks)
    integer bit_ns;
    reg [7:0] ch;
    integer k;
    initial begin
        bit_ns = 40 * 9;
        if (!$value$plusargs("BITNS=%d", bit_ns)) bit_ns = 40 * 9;
        forever begin
            @(negedge io_out[8]);
            if (!rst) begin
                #(bit_ns / 2);
                for (k = 0; k < 8; k = k + 1) begin #(bit_ns); ch[k] = io_out[8]; end
                #(bit_ns);
                $display("UART: 0x%02x '%c'", ch, (ch >= 32 && ch < 127) ? ch : 8'h2e);
            end
        end
    end

    reg [31:0] prog [0:WORDS-1];
    reg [31:0] d;
    integer i, fd, cycles, nprog;
    reg [8*256-1:0] hexfile, dumpfile;
    initial begin
        if (!$value$plusargs("HEX=%s", hexfile)) hexfile = "prog.hex";
        if (!$value$plusargs("DUMP=%s", dumpfile)) dumpfile = "dump.hex";
        if (!$value$plusargs("NPROG=%d", nprog)) nprog = WORDS;
        loopback = $test$plusargs("LOOPBACK");
        for (i = 0; i < WORDS; i = i + 1) prog[i] = 32'h0;
        $readmemh(hexfile, prog);
        repeat (5) @(posedge clk);
        rst = 0;
        for (i = 0; i < WORDS; i = i + 1) wb_write(32'h3000_0000 + 4*i, prog[i]);
        // read-back check of a few words
        for (i = 0; i < 4; i = i + 1) begin
            wb_read(32'h3000_0000 + 4*i, d);
            if (d !== prog[i]) $display("ERROR: readback %0d %h != %h", i, d, prog[i]);
        end
        wb_write(32'h3000_1000, 32'h1);              // run
        cycles = 0;
        d = 0;
        while (!d[1] && cycles < 400000) begin
            repeat (50) @(posedge clk);
            cycles = cycles + 50;
            wb_read(32'h3000_1000, d);
        end
        if (!d[1]) $display("ERROR: CPU did not halt (pc=%h)", la[31:0]);
        else $display("CPU halted after ~%0d cycles, pc=%h irq=%b", cycles, la[31:0], irq);
        wb_read(32'h3000_1004, d);
        $display("MAILBOX = %h  (irq after read = %b)", d, irq);
        $display("GPIO out=%h oeb=%h", io_out[25:10], io_oeb[25:10]);
        fd = $fopen(dumpfile, "w");
        for (i = 0; i < WORDS; i = i + 1) begin
            wb_read(32'h3000_0000 + 4*i, d);
            $fdisplay(fd, "%h", d);
        end
        $fclose(fd);
        $finish;
    end
endmodule
