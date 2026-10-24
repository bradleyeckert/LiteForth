// Full unsigned iterative divider courtesy of Gemini AI
module divider #(
    parameter WIDTH = 32
)(
    input  wire                     clk,
    input  wire                     rst_n,
    input  wire                     start,
    input  wire [(2*WIDTH)-1:0]     dividend,
    input  wire [WIDTH-1:0]         divisor,
    output reg                      busy,
    output reg                      done,
    output reg [(2*WIDTH)-1:0]      quotient,
    output reg [WIDTH-1:0]          remainder
);

    // FSM States
    localparam IDLE = 1'b0;
    localparam CALC = 1'b1;

    reg                        state;
    reg [7:0]                  count;     // Counts down from (2*WIDTH) to 0

    reg [WIDTH:0]              rem_reg;   // (WIDTH + 1)-bit partial remainder
    reg [(2*WIDTH)-1:0]        quo_reg;   // (2*WIDTH)-bit quotient shift register

    // (WIDTH + 1)-bit Datapath Logic:
    // Shift left current remainder and bring in the MSB of the quotient register
    wire [WIDTH:0]             rem_shifted = {rem_reg[WIDTH-1:0], quo_reg[(2*WIDTH)-1]};
    
    // (WIDTH + 1)-bit trial subtraction (strictly bound to WIDTH + 1 bits)
    wire [WIDTH:0]             sub_result  = rem_shifted - {1'b0, divisor};
    wire                       sub_borrow  = sub_result[WIDTH]; // High if rem_shifted < divisor

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state     <= IDLE;
            busy      <= 1'b0;
            done      <= 1'b0;
            quotient  <= {(2*WIDTH){1'b0}};
            remainder <= {WIDTH{1'b0}};
            rem_reg   <= {(WIDTH+1){1'b0}};
            quo_reg   <= {(2*WIDTH){1'b0}};
            count     <= 8'd0;
        end else begin
            case (state)
                IDLE: begin
                    done <= 1'b0;
                    if (start) begin
                        busy      <= 1'b1;
                        rem_reg   <= {(WIDTH+1){1'b0}};
                        quo_reg   <= dividend; // Dividend loaded into quotient register initially
                        count     <= 8'(2*WIDTH);
                        state     <= CALC;
                    end
                end

                CALC: begin
                    if (count > 0) begin
                        // Shift and trial subtraction ((WIDTH + 1)-bit operation)
                        if (!sub_borrow) begin
                            rem_reg <= sub_result;
                            quo_reg <= {quo_reg[(2*WIDTH)-2:0], 1'b1};
                        end else begin
                            rem_reg <= rem_shifted;
                            quo_reg <= {quo_reg[(2*WIDTH)-2:0], 1'b0};
                        end
                        count <= count - 1'b1;
                    end else begin
                        // Finished all (2*WIDTH) iterations
                        busy      <= 1'b0;
                        done      <= 1'b1;
                        quotient  <= quo_reg;
                        remainder <= rem_reg[WIDTH-1:0];
                        state     <= IDLE;
                    end
                end
            endcase
        end
    end

endmodule
