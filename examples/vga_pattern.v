// VGA color-bar test pattern, 640x480@60 (VESA 800x525 timing) off a /4
// clock-enable from the 100 MHz master (25 MHz pixel tick — R1's derived-clock
// idiom; no MMCM). 8 vertical 80px color bars, plus a border that is
// deliberately NOT vertically symmetric: the TOP edge row is white and the
// BOTTOM edge row is blue, so a top<->bottom mirror bug is caught by the
// pixel-exact golden (vertical bars alone are flip-invariant). Left/right
// edges are white; the ordered bars pin horizontal orientation.
//
// btnC = synchronous reset (R6): clears the divider, both counters, and all
// registered sync/color outputs so the first frame's phase is deterministic.
// Counter idiom per the decision log: full 32-bit counters vs integer
// localparams, sized increments, no declaration initializers.
module vga_pattern(
  input  wire       clk,
  input  wire       btnC,
  output reg        Hsync,
  output reg        Vsync,
  output reg  [3:0] vgaRed,
  output reg  [3:0] vgaGreen,
  output reg  [3:0] vgaBlue
);
  localparam integer H_VIS = 640, H_FP = 16, H_SYNC = 96, H_TOTAL = 800;
  localparam integer V_VIS = 480, V_FP = 10, V_SYNC = 2,  V_TOTAL = 525;
  localparam integer CE_DIV = 4;  // 100 MHz / 4 = 25 MHz pixel tick

  reg [31:0] ce_cnt;
  wire pix_ce = (ce_cnt == CE_DIV - 1);

  reg [31:0] hcnt;  // 0..799
  reg [31:0] vcnt;  // 0..524

  wire visible = (hcnt < H_VIS) && (vcnt < V_VIS);
  wire top_edge    = visible && (vcnt == 0);
  wire bottom_edge = visible && (vcnt == V_VIS - 1);
  wire side_edge   = visible && ((hcnt == 0) || (hcnt == H_VIS - 1));

  // 8 bars x 80 px: white yellow cyan green magenta red blue black.
  reg [2:0] bar;
  always @* begin
    if      (hcnt < 80)  bar = 3'd0;
    else if (hcnt < 160) bar = 3'd1;
    else if (hcnt < 240) bar = 3'd2;
    else if (hcnt < 320) bar = 3'd3;
    else if (hcnt < 400) bar = 3'd4;
    else if (hcnt < 480) bar = 3'd5;
    else if (hcnt < 560) bar = 3'd6;
    else                 bar = 3'd7;
  end

  reg [11:0] rgb;  // {R[3:0], G[3:0], B[3:0]}
  always @* begin
    case (bar)
      3'd0: rgb = 12'hFFF;  // white
      3'd1: rgb = 12'hFF0;  // yellow
      3'd2: rgb = 12'h0FF;  // cyan
      3'd3: rgb = 12'h0F0;  // green
      3'd4: rgb = 12'hF0F;  // magenta
      3'd5: rgb = 12'hF00;  // red
      3'd6: rgb = 12'h00F;  // blue
      default: rgb = 12'h000;  // black
    endcase
    if (side_edge)   rgb = 12'hFFF;  // white sides
    if (top_edge)    rgb = 12'hFFF;  // white top    (asymmetry: distinguishes
    if (bottom_edge) rgb = 12'h00F;  //   blue bottom  top from bottom)
    if (!visible)    rgb = 12'h000;  // blanking
  end

  always @(posedge clk) begin
    if (btnC) begin
      ce_cnt   <= 32'd0;
      hcnt     <= 32'd0;
      vcnt     <= 32'd0;
      Hsync    <= 1'b1;
      Vsync    <= 1'b1;
      vgaRed   <= 4'h0;
      vgaGreen <= 4'h0;
      vgaBlue  <= 4'h0;
    end else begin
      ce_cnt <= (ce_cnt == CE_DIV - 1) ? 32'd0 : ce_cnt + 32'd1;
      if (pix_ce) begin
        if (hcnt == H_TOTAL - 1) begin
          hcnt <= 32'd0;
          vcnt <= (vcnt == V_TOTAL - 1) ? 32'd0 : vcnt + 32'd1;
        end else begin
          hcnt <= hcnt + 32'd1;
        end
        Hsync <= ~((hcnt >= H_VIS + H_FP) && (hcnt < H_VIS + H_FP + H_SYNC));
        Vsync <= ~((vcnt >= V_VIS + V_FP) && (vcnt < V_VIS + V_FP + V_SYNC));
        vgaRed   <= rgb[11:8];
        vgaGreen <= rgb[7:4];
        vgaBlue  <= rgb[3:0];
      end
    end
  end
endmodule
