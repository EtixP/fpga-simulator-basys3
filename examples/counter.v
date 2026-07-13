// Milestone 1.1-1.2 demo: a 16-bit counter that adds the switch value every
// clock cycle. Reset on btnC (project convention, R6). Registers carry no
// declaration initializers: the explicit reset covers them, and Verilator's
// -Wall (PROCASSINIT) rejects mixing both styles.
module counter(
  input  wire        clk,
  input  wire        btnC,     // synchronous reset
  input  wire [3:0]  sw,       // increment step
  output wire [15:0] led       // current count
);
  reg [15:0] count;

  always @(posedge clk) begin
    if (btnC) count <= 16'd0;
    else      count <= count + {12'd0, sw};
  end

  assign led = count;
endmodule
