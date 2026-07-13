// Test fixture (not a shipped example): a design with an inout port, used to
// lock the SimEngine contract that inout ports are unsupported — omitted from
// ports(), unresolvable via lookup(). R5 defers tri-state support.
module ioport(
  input  wire clk,
  inout  wire io,
  output wire led
);
  assign led = 1'b0;
endmodule
