module sqrt #(
    parameter WIDTH = 32
)(
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire                    start,
    input  wire [(2*WIDTH)-1:0]    radicand,
    output reg                     busy,
    output reg                     done,
    output reg [WIDTH-1:0]         root,
    output reg [WIDTH-1:0]         remainder
);

    // FSM States
    localparam IDLE = 1'b0;
    localparam CALC = 1'b1;

    reg                        state;
    reg [7:0]                  count;     // Counts down from WIDTH to 0

    reg [WIDTH:0]              rem_reg;   // (WIDTH + 1)-bit working remainder
    reg [WIDTH-1:0]            root_reg;  // WIDTH-bit root accumulator shift register

    // Radix-2 Square Root Datapath:
    // 1. Extract next 2 bits of the radicand based on iteration count
    wire [1:0]     next_bits      = {radicand[{count[5:0], 1'b1}], radicand[{count[5:0], 1'b0}]};
    
    // 2. Shift partial remainder left by 2 and bring in the next 2 radicand bits
    wire [WIDTH:0] rem_shifted    = {rem_reg[WIDTH-2:0], next_bits};
    
    // 3. Trial subtrahend: (accumulated root shifted left by 2) with 2'b01 appended
    wire [WIDTH:0] sub_subtrahend = {root_reg[WIDTH-2:0], 2'b01};
    
    // 4. Strict WIDTH + 1 bit trial subtraction
    wire [WIDTH:0] sub_result     = rem_shifted - sub_subtrahend;
    wire           sub_borrow     = sub_result[WIDTH]; // High if rem_shifted < sub_subtrahend

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state     <= IDLE;
            busy      <= 1'b0;
            done      <= 1'b0;
            root      <= {WIDTH{1'b0}};
            remainder <= {WIDTH{1'b0}};
            rem_reg   <= {(WIDTH+1){1'b0}};
            root_reg  <= {WIDTH{1'b0}};
            count     <= 8'd0;
        end else begin
            case (state)
                IDLE: begin
                    done <= 1'b0;
                    if (start) begin
                        busy      <= 1'b1;
                        rem_reg   <= {(WIDTH+1){1'b0}};
                        root_reg  <= {WIDTH{1'b0}};
                        count     <= 8'(WIDTH); // Square root takes WIDTH cycles (2 bits/cycle)
                        state     <= CALC;
                    end
                end

                CALC: begin
                    if (count > 0) begin
                        if (!sub_borrow) begin
                            // rem_shifted >= subtrahend: update remainder, set root bit to 1
                            rem_reg  <= sub_result;
                            root_reg <= {root_reg[WIDTH-2:0], 1'b1};
                        end else begin
                            // rem_shifted < subtrahend: keep shifted remainder, set root bit to 0
                            rem_reg  <= rem_shifted;
                            root_reg <= {root_reg[WIDTH-2:0], 1'b0};
                        end
                        count <= count - 1'b1;
                    end else begin
                        // Finished all WIDTH iterations
                        busy      <= 1'b0;
                        done      <= 1'b1;
                        root      <= root_reg;
                        remainder <= rem_reg[WIDTH-1:0];
                        state     <= IDLE;
                    end
                end
            endcase
        end
    end

endmodule