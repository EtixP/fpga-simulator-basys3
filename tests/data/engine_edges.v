// Audit fixture: independently observable initialization, both edges, derived
// clock, asynchronous reset and all supported integer storage widths.
module engine_edges(
  input wire clk,
  input wire rst,
  input wire [7:0] din,
  input wire [15:0] in16,
  input wire [31:0] in32,
  input wire [63:0] in64,
  input wire [64:0] too_wide,
  output wire [7:0] comb,
  output reg [31:0] pos_count = 5,
  output reg [31:0] neg_count = 11,
  output reg [31:0] async_count,
  output reg [7:0] sampled,
  output reg derived_clk = 0,
  output reg [31:0] derived_count = 0,
  output wire [7:0] mem0,
  output wire [7:0] mem1,
  output wire [15:0] out16,
  output wire [31:0] out32,
  output wire [63:0] out64
);
  reg [7:0] memory [0:1];
  initial $readmemh("engine_init.hex", memory);
  assign mem0 = memory[0];
  assign mem1 = memory[1];
  assign comb = din ^ 8'hA6;
  assign out16 = in16;
  assign out32 = in32;
  assign out64 = in64;
  always @(posedge clk) begin
    pos_count <= pos_count + 1;
    sampled <= comb;
    derived_clk <= ~derived_clk;
  end
  always @(negedge clk) neg_count <= neg_count + 1;
  always @(posedge derived_clk) derived_count <= derived_count + 1;
  always @(posedge clk or posedge rst)
    if (rst) async_count <= 0;
    else async_count <= async_count + 1;
endmodule
