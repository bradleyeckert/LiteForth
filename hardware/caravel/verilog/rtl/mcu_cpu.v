// SPDX-License-Identifier: Apache-2.0
// mcu_cpu: compact multi-cycle RV32E core (16 x 32-bit registers).
// Implements the full RV32I base integer instruction set (minus CSRs).
// FENCE is a no-op; ECALL/EBREAK and illegal opcodes halt the core.
// Memory has a 1-cycle read latency (synchronous SRAM): read data is valid
// in the cycle after a granted request. The fetched word is decoded directly
// from mem_rdata in EXEC and latched for the MEM/MWAIT states.
// Timing: ALU/branch/jump = 2 cycles, store = 3 cycles, load = 4 cycles.
`default_nettype none

module mcu_cpu (
    input  wire        clk,
    input  wire        rst,          // synchronous, active high

    // simple memory bus (combinational read, ready = grant)
    output wire        mem_valid,
    output wire        mem_rd,       // data read (load) - for read side effects
    output wire [31:0] mem_addr,
    output wire [31:0] mem_wdata,
    output wire [3:0]  mem_wstrb,
    input  wire [31:0] mem_rdata,
    input  wire        mem_ready,

    output wire [31:0] pc_o,
    output wire        halted
);
    localparam S_FETCH = 3'd0, S_EXEC = 3'd1, S_MEM = 3'd2, S_MWAIT = 3'd3, S_HALT = 3'd4;

    reg  [2:0]  state;
    reg  [31:0] pc;
    reg  [31:0] instr_q;
    reg  [31:0] rf [1:15];
    // current instruction: straight from memory in EXEC, latched afterwards
    wire [31:0] instr = (state == S_EXEC) ? mem_rdata : instr_q;

    // ---------------- decode ----------------
    wire [6:0] opcode = instr[6:0];
    wire [3:0] rd     = instr[10:7];   // RV32E: x0..x15
    wire [2:0] funct3 = instr[14:12];
    wire [3:0] rs1    = instr[18:15];
    wire [3:0] rs2    = instr[23:20];
    wire       f7b5   = instr[30];

    wire op_lui    = opcode == 7'b0110111;
    wire op_auipc  = opcode == 7'b0010111;
    wire op_jal    = opcode == 7'b1101111;
    wire op_jalr   = opcode == 7'b1100111;
    wire op_branch = opcode == 7'b1100011;
    wire op_load   = opcode == 7'b0000011;
    wire op_store  = opcode == 7'b0100011;
    wire op_imm    = opcode == 7'b0010011;
    wire op_reg    = opcode == 7'b0110011;
    wire op_fence  = opcode == 7'b0001111;
    wire legal     = op_lui | op_auipc | op_jal | op_jalr | op_branch |
                     op_load | op_store | op_imm | op_reg | op_fence;

    wire [31:0] imm_i = {{20{instr[31]}}, instr[31:20]};
    wire [31:0] imm_s = {{20{instr[31]}}, instr[31:25], instr[11:7]};
    wire [31:0] imm_b = {{19{instr[31]}}, instr[31], instr[7], instr[30:25], instr[11:8], 1'b0};
    wire [31:0] imm_u = {instr[31:12], 12'b0};
    wire [31:0] imm_j = {{11{instr[31]}}, instr[31], instr[19:12], instr[20], instr[30:21], 1'b0};

    wire [31:0] rs1v = (rs1 == 4'd0) ? 32'd0 : rf[rs1];
    wire [31:0] rs2v = (rs2 == 4'd0) ? 32'd0 : rf[rs2];

    // ---------------- ALU ----------------
    wire [31:0] alu_b   = op_reg ? rs2v : imm_i;
    wire        do_sub  = op_reg & f7b5;
    wire [31:0] addsub  = do_sub ? (rs1v - alu_b) : (rs1v + alu_b);
    wire [4:0]  shamt   = alu_b[4:0];
    wire        lt_s    = $signed(rs1v) < $signed(alu_b);
    wire        lt_u    = rs1v < alu_b;
    wire [31:0] sra_r   = $signed(rs1v) >>> shamt;
    reg  [31:0] alu_out;
    always @(*) begin
        case (funct3)
            3'b000: alu_out = addsub;
            3'b001: alu_out = rs1v << shamt;
            3'b010: alu_out = {31'd0, lt_s};
            3'b011: alu_out = {31'd0, lt_u};
            3'b100: alu_out = rs1v ^ alu_b;
            3'b101: alu_out = f7b5 ? sra_r : (rs1v >> shamt);
            3'b110: alu_out = rs1v | alu_b;
            default: alu_out = rs1v & alu_b;
        endcase
    end

    // ---------------- branches ----------------
    wire beq  = rs1v == rs2v;
    wire blt  = $signed(rs1v) < $signed(rs2v);
    wire bltu = rs1v < rs2v;
    reg  take;
    always @(*) begin
        case (funct3)
            3'b000: take = beq;
            3'b001: take = ~beq;
            3'b100: take = blt;
            3'b101: take = ~blt;
            3'b110: take = bltu;
            3'b111: take = ~bltu;
            default: take = 1'b0;
        endcase
    end

    wire [31:0] pc_plus4 = pc + 32'd4;
    wire [31:0] br_tgt   = pc + (op_jal ? imm_j : imm_b);
    wire [31:0] jalr_tgt = (rs1v + imm_i) & ~32'd1;

    // ---------------- load / store ----------------
    wire [31:0] ls_addr = rs1v + (op_store ? imm_s : imm_i);
    wire [1:0]  bo      = ls_addr[1:0];
    reg  [3:0]  st_strb;
    reg  [31:0] st_data;
    always @(*) begin
        case (funct3[1:0])
            2'b00: begin st_strb = 4'b0001 << bo;          st_data = {4{rs2v[7:0]}};  end
            2'b01: begin st_strb = 4'b0011 << {bo[1],1'b0}; st_data = {2{rs2v[15:0]}}; end
            default: begin st_strb = 4'b1111;              st_data = rs2v;            end
        endcase
    end
    wire [31:0] ld_shift = mem_rdata >> {bo, 3'b000};
    reg  [31:0] ld_data;
    always @(*) begin
        case (funct3)
            3'b000: ld_data = {{24{ld_shift[7]}},  ld_shift[7:0]};
            3'b001: ld_data = {{16{ld_shift[15]}}, ld_shift[15:0]};
            3'b100: ld_data = {24'd0, ld_shift[7:0]};
            3'b101: ld_data = {16'd0, ld_shift[15:0]};
            default: ld_data = ld_shift;
        endcase
    end

    // ---------------- bus ----------------
    assign mem_valid = (state == S_FETCH) | (state == S_MEM);
    assign mem_rd    = (state == S_MEM) & op_load;
    assign mem_addr  = (state == S_FETCH) ? pc : ls_addr;
    assign mem_wdata = st_data;
    assign mem_wstrb = ((state == S_MEM) & op_store) ? st_strb : 4'b0000;

    assign pc_o   = pc;
    assign halted = (state == S_HALT);

    // ---------------- writeback value (EXEC) ----------------
    reg [31:0] wb_val;
    always @(*) begin
        if (op_lui)                 wb_val = imm_u;
        else if (op_auipc)          wb_val = pc + imm_u;
        else if (op_jal | op_jalr)  wb_val = pc_plus4;
        else                        wb_val = alu_out;
    end
    wire exec_wr = op_lui | op_auipc | op_jal | op_jalr | op_imm | op_reg;

    // register file (no reset -> plain DFFs)
    always @(posedge clk) begin
        if (!rst && rd != 4'd0) begin
            if (state == S_EXEC && exec_wr)
                rf[rd] <= wb_val;
            else if (state == S_MWAIT)
                rf[rd] <= ld_data;
        end
    end

    // ---------------- control FSM ----------------
    always @(posedge clk) begin
        if (rst) begin
            state   <= S_FETCH;
            pc      <= 32'd0;
            instr_q <= 32'h0000_0013; // nop
        end else begin
            case (state)
                S_FETCH: if (mem_ready) state <= S_EXEC;
                S_EXEC: begin
                    instr_q <= mem_rdata;
                    if (!legal || opcode == 7'b1110011) begin
                        state <= S_HALT;                 // ecall/ebreak/illegal
                    end else if (op_load | op_store) begin
                        state <= S_MEM;
                    end else begin
                        state <= S_FETCH;
                        if (op_jal || (op_branch && take)) pc <= br_tgt;
                        else if (op_jalr)                  pc <= jalr_tgt;
                        else                               pc <= pc_plus4;
                    end
                end
                S_MEM: if (mem_ready) begin
                    if (op_load) state <= S_MWAIT;
                    else begin state <= S_FETCH; pc <= pc_plus4; end
                end
                S_MWAIT: begin
                    state <= S_FETCH;
                    pc    <= pc_plus4;
                end
                default: state <= S_HALT;
            endcase
        end
    end
endmodule
`default_nettype wire
