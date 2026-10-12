// The Basys 3 top level for NimGame.sv. The ports are named as in Digilent's
// Basys3_Master.xdc, so VirtualBasys (or Vivado, with that file) binds them
// with no constraints file of its own:
//
//   virtualbasys run demos/nim/ChipInterface.sv demos/nim/NimGame.sv
//
// Controls
//   display  the four piles. Your cursor is the lit decimal point; the pile
//            the FPGA is about to take from blinks while it thinks.
//   sw[3:0]  how many stones to take, in binary
//   btnD     take that many stones from the pile at your cursor
//   btnL/R   move your cursor between piles
//   btnU     deal a new random game
//   btnC     reset: a new game of 3 4 5 6
//   sw[15]   hint: show the nim-sum (the XOR of the piles) on led[3:0]
//
//   led[15]  your turn            led[14]  the FPGA is thinking
//   led[13]  illegal take: no stones, or more than the pile holds
//   600d     you won ("good"), and every LED lights
//   dEAd     the FPGA won

`default_nettype none

module ChipInterface
  (input  logic        clk,
   input  logic        btnC, btnU, btnL, btnR, btnD,
   input  logic [15:0] sw,
   output logic [15:0] led,
   output logic [6:0]  seg,
   output logic        dp,
   output logic [3:0]  an);

  // btnC resets the game; a bounce just resets it again.
  logic [1:0] resetSync;
  logic reset;
  always_ff @(posedge clk)
    resetSync <= {resetSync[0], btnC};
  assign reset = resetSync[1];

  // The other buttons act once per press.
  logic cursorLeft, cursorRight, take, deal;
  ButtonPress left (.clock(clk), .button(btnL), .pressed(cursorLeft));
  ButtonPress right(.clock(clk), .button(btnR), .pressed(cursorRight));
  ButtonPress down (.clock(clk), .button(btnD), .pressed(take));
  ButtonPress up   (.clock(clk), .button(btnU), .pressed(deal));

  // A 16-bit maximal-length LFSR, stepped every cycle: when you press btnU
  // decides the deal. All zeros (the power-on state) would lock it up.
  logic [15:0] random;
  always_ff @(posedge clk)
    if (random == 16'd0)
      random <= 16'hACE1;
    else
      random <= {random[14:0], random[15] ^ random[13] ^ random[12] ^ random[10]};

  logic [3:0][3:0] piles;
  logic [1:0] cursor, target;
  logic [3:0] nimSum;
  logic yourTurn, thinking, playerWon, fpgaWon, badMove;
  NimGame game
    (.clock(clk), .reset, .cursorLeft, .cursorRight, .take, .deal,
     .amount(sw[3:0]), .random, .piles, .cursor, .target, .nimSum,
     .yourTurn, .thinking, .playerWon, .fpgaWon, .badMove);

  // While the FPGA thinks, its pile blinks: 42 ms on, 42 ms off.
  logic [22:0] blink;
  always_ff @(posedge clk)
    blink <= thinking ? blink + 23'd1 : '0;

  logic [3:0][3:0] digits;
  logic [3:0] blank, dots;
  always_comb begin
    digits = piles;
    blank = 4'b0000;
    dots = 4'b0000;
    if (playerWon)
      digits = {4'h6, 4'h0, 4'h0, 4'hD};  // 600d
    else if (fpgaWon)
      digits = {4'hD, 4'hE, 4'hA, 4'hD};  // dEAd
    else begin
      dots[cursor] = 1'b1;
      blank[target] = thinking && blink[22];
    end
  end

  SevenSegmentDisplay display
    (.clock(clk), .digits, .blank, .dots, .seg, .dp, .an);

  always_comb begin
    led = 16'h0000;
    if (playerWon)
      led = 16'hFFFF;
    else begin
      led[15] = yourTurn;
      led[14] = thinking;
      led[13] = badMove;
      if (sw[15])
        led[3:0] = nimSum;
    end
  end
endmodule : ChipInterface

// One pulse per press of a push button: synchronized to the clock, then
// debounced (the level must hold for STABLE_CYCLES, 10 ms at 100 MHz).
module ButtonPress
  #(parameter int STABLE_CYCLES = 1_000_000)
  (input  logic clock, button,
   output logic pressed);

  localparam int W = $clog2(STABLE_CYCLES);
  logic [1:0] sync;
  logic level;
  logic [W-1:0] count;

  always_ff @(posedge clock) begin
    sync <= {sync[0], button};
    pressed <= 1'b0;
    if (sync[1] == level)
      count <= '0;
    else if (count == W'(STABLE_CYCLES - 1)) begin
      count <= '0;
      level <= sync[1];
      pressed <= sync[1];
    end else
      count <= count + W'(1);
  end
endmodule : ButtonPress

// The four multiplexed digits, one at a time for 250 us each (a 1 kHz
// refresh). Segments, decimal point and anodes are active low; seg[0] is
// segment a, and an[0] is the rightmost digit.
module SevenSegmentDisplay
  (input  logic            clock,
   input  logic [3:0][3:0] digits,       // hex values; digits[3] is the leftmost
   input  logic [3:0]      blank, dots,  // per digit: dark, decimal point lit
   output logic [6:0]      seg,
   output logic            dp,
   output logic [3:0]      an);

  logic [14:0] count;
  logic [1:0] index;
  always_ff @(posedge clock)
    if (count == 15'd24_999) begin
      count <= '0;
      index <= index + 2'd1;
    end else
      count <= count + 15'd1;

  logic [6:0] lit;  // segments g..a, 1 = lit
  always_comb begin
    unique case (digits[index])
      4'h0: lit = 7'h3F;  4'h1: lit = 7'h06;  4'h2: lit = 7'h5B;  4'h3: lit = 7'h4F;
      4'h4: lit = 7'h66;  4'h5: lit = 7'h6D;  4'h6: lit = 7'h7D;  4'h7: lit = 7'h07;
      4'h8: lit = 7'h7F;  4'h9: lit = 7'h6F;  4'hA: lit = 7'h77;  4'hB: lit = 7'h7C;
      4'hC: lit = 7'h39;  4'hD: lit = 7'h5E;  4'hE: lit = 7'h79;  4'hF: lit = 7'h71;
    endcase
    seg = ~lit;
    dp = ~dots[index];
    an = 4'b1111;
    if (!blank[index])
      an[index] = 1'b0;
  end
endmodule : SevenSegmentDisplay
