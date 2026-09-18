// Range orientation and numeric HDL indices are independent of packed value
// bit positions. The multidimensional port is intentionally unsupported.
module binding_ranges(
    input wire clk,
    input wire [7:4] sw_off,
    input wire [0:3] sw_up,
    input wire [-1:-4] sw_neg,
    input wire [5:5] sw_one,
    input wire scalar,
    input wire [1:0][1:0] multi,
    output wire [4:0] led
);
    assign led = {scalar, sw_one[5], sw_neg[-2], sw_up[0], sw_off[4]};
endmodule
