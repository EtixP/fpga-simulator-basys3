// SS.CC stopwatch (seconds.centiseconds) on the 4-digit seven-seg display.
//
//   btnC : synchronous reset — clears the time, stops counting, restarts the
//          display mux from digit 0 (R6 convention; while held, only digit 0
//          is strobed).
//   btnU : start/stop toggle, debounced — the button must be stable for
//          DEBOUNCE cycles (10 ms) before it registers, so an instantaneous
//          pulse is (correctly) ignored.
//   led  : running indicator.
//
// Counter idiom (see CLAUDE.md decision log): full 32-bit counters compared
// directly against integer localparams with sized increments — this is the
// verified -Wall-clean Verilog-2005 form; synthesis trims the dead upper
// bits. Registers carry no declaration initializers (PROCASSINIT).
module stopwatch(
  input  wire       clk,
  input  wire       btnC,
  input  wire       btnU,
  output reg  [6:0] seg,    // active-low cathodes CA..CG
  output reg        dp,     // active-low decimal point
  output reg  [3:0] an,     // active-low digit anodes
  output wire       led     // led[0] = running
);
  localparam integer DEBOUNCE    = 1000000;  // 10 ms of stable input
  localparam integer CENTI       = 1000000;  // 10 ms = one centisecond
  // 250 us per digit = all four digits driven once per 1 ms, "a refresh
  // frequency of about 1KHz" per the Basys 3 reference manual's recommended
  // 1-16 ms band.
  localparam integer DIGIT_DWELL = 25000;

  reg [31:0] db_cnt;
  reg        btnU_ff1, btnU_ff2, btnU_db, btnU_db_q;
  reg        running;
  reg [31:0] pre;
  reg [3:0]  c0, c1, s0, s1;   // BCD: centis ones/tens, seconds ones/tens
  reg [31:0] dwell;
  reg [1:0]  sel;

  assign led = running;

  always @(posedge clk) begin
    if (btnC) begin
      db_cnt   <= 32'd0;
      btnU_ff1 <= 1'b0;
      btnU_ff2 <= 1'b0;
      btnU_db  <= 1'b0;
      btnU_db_q<= 1'b0;
      running  <= 1'b0;
      pre      <= 32'd0;
      c0 <= 4'd0; c1 <= 4'd0; s0 <= 4'd0; s1 <= 4'd0;
      dwell    <= 32'd0;
      sel      <= 2'd0;
    end else begin
      // Two-flop synchronizer feeding a counting debouncer: btnU_db follows
      // btnU_ff2 only after DEBOUNCE cycles of disagreement.
      btnU_ff1 <= btnU;
      btnU_ff2 <= btnU_ff1;
      if (btnU_ff2 != btnU_db) begin
        if (db_cnt == DEBOUNCE - 1) begin
          btnU_db <= btnU_ff2;
          db_cnt  <= 32'd0;
        end else begin
          db_cnt <= db_cnt + 32'd1;
        end
      end else begin
        db_cnt <= 32'd0;
      end
      btnU_db_q <= btnU_db;
      if (btnU_db && !btnU_db_q) running <= !running;

      // Centisecond prescaler + BCD chain (wraps at 99.99).
      if (running) begin
        if (pre == CENTI - 1) begin
          pre <= 32'd0;
          if (c0 == 4'd9) begin
            c0 <= 4'd0;
            if (c1 == 4'd9) begin
              c1 <= 4'd0;
              if (s0 == 4'd9) begin
                s0 <= 4'd0;
                s1 <= (s1 == 4'd9) ? 4'd0 : s1 + 4'd1;
              end else begin
                s0 <= s0 + 4'd1;
              end
            end else begin
              c1 <= c1 + 4'd1;
            end
          end else begin
            c0 <= c0 + 4'd1;
          end
        end else begin
          pre <= pre + 32'd1;
        end
      end

      // Display mux.
      if (dwell == DIGIT_DWELL - 1) begin
        dwell <= 32'd0;
        sel   <= sel + 2'd1;
      end else begin
        dwell <= dwell + 32'd1;
      end
    end
  end

  // Digit select + active-low decode.
  reg [3:0] bcd;
  always @(*) begin
    case (sel)
      2'd0: begin bcd = c0; an = 4'b1110; end
      2'd1: begin bcd = c1; an = 4'b1101; end
      2'd2: begin bcd = s0; an = 4'b1011; end
      2'd3: begin bcd = s1; an = 4'b0111; end
    endcase
    dp = (sel == 2'd2) ? 1'b0 : 1'b1;  // the SS.CC point
    case (bcd)
      4'd0: seg = 7'b1000000;
      4'd1: seg = 7'b1111001;
      4'd2: seg = 7'b0100100;
      4'd3: seg = 7'b0110000;
      4'd4: seg = 7'b0011001;
      4'd5: seg = 7'b0010010;
      4'd6: seg = 7'b0000010;
      4'd7: seg = 7'b1111000;
      4'd8: seg = 7'b0000000;
      4'd9: seg = 7'b0010000;
      default: seg = 7'b1111111;  // blank (BCD never exceeds 9)
    endcase
  end
endmodule
