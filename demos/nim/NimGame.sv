// Nim against the FPGA: the game, independent of the board.
//
// Four piles of stones. On your turn you take one or more stones from one
// pile; then the FPGA takes its turn. Whoever takes the last stone wins.
//
// The FPGA plays perfectly. The XOR of the four piles is the nim-sum: a player
// facing a nim-sum of zero cannot win against perfect play, and from any other
// position there is a move that makes it zero. So the FPGA makes the nim-sum
// zero whenever it can, and otherwise takes one stone and waits for a mistake.
// Every game starts with a non-zero nim-sum: if you, too, always leave the
// FPGA a nim-sum of zero, you win.
//
// ChipInterface.sv connects this module to the Basys 3.

`default_nettype none

module NimGame
  #(parameter int THINK_CYCLES = 30_000_000)  // the FPGA's pause before it moves: 0.3 s at 100 MHz
  (input  logic            clock, reset,
   // One-cycle pulses: move your cursor, take stones, deal a new game.
   input  logic            cursorLeft, cursorRight, take, deal,
   input  logic [3:0]      amount,            // how many stones to take from your pile
   input  logic [15:0]     random,            // free-running random bits, for a new deal
   output logic [3:0][3:0] piles,             // piles[3] is the leftmost
   output logic [1:0]      cursor,            // the pile you are taking from
   output logic [1:0]      target,            // the pile the FPGA takes from
   output logic [3:0]      nimSum,            // XOR of the piles
   output logic            yourTurn, thinking, playerWon, fpgaWon,
   output logic            badMove);          // your last take was not a legal move

  typedef enum logic [1:0] {PLAYER, THINK, WON, LOST} state_t;
  state_t state;

  localparam int THINK_W = $clog2(THINK_CYCLES + 1);
  logic [THINK_W-1:0] thinkCount;

  // Whether every pile except `keep` is empty.
  function automatic logic othersEmpty(logic [3:0][3:0] p, logic [1:0] keep);
    othersEmpty = 1'b1;
    for (int i = 0; i < 4; i++)
      if (2'(i) != keep && p[i] != 4'd0) othersEmpty = 1'b0;
  endfunction

  // The FPGA's move. Taking pile i down to (pile i XOR nim-sum) leaves a
  // nim-sum of zero; that is smaller than pile i exactly when pile i has the
  // nim-sum's highest bit set. With no such move, take one stone. Ties go to
  // the leftmost pile.
  logic [3:0] targetValue;
  always_comb begin
    nimSum = piles[0] ^ piles[1] ^ piles[2] ^ piles[3];
    target = 2'd0;
    targetValue = piles[0];
    if (nimSum != 4'd0) begin
      for (int i = 0; i < 4; i++)
        if ((piles[i] ^ nimSum) < piles[i]) begin
          target = 2'(i);
          targetValue = piles[i] ^ nimSum;
        end
    end else begin
      for (int i = 0; i < 4; i++)
        if (piles[i] != 4'd0) begin
          target = 2'(i);
          targetValue = piles[i] - 4'd1;
        end
    end
  end

  // A new deal: piles of 1 to 9 stones, with a non-zero nim-sum so that you
  // can win. Changing any one pile turns a zero nim-sum into a non-zero one.
  function automatic logic [3:0] stones(logic [3:0] bits);
    stones = (bits < 4'd9) ? bits + 4'd1 : bits - 4'd8;
  endfunction

  logic [3:0][3:0] dealt;
  always_comb begin
    for (int i = 0; i < 4; i++)
      dealt[i] = stones(random[4*i +: 4]);
    if ((dealt[0] ^ dealt[1] ^ dealt[2] ^ dealt[3]) == 4'd0)
      dealt[0] = (dealt[0] == 4'd9) ? 4'd8 : dealt[0] + 4'd1;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      piles <= {4'd3, 4'd4, 4'd5, 4'd6};  // shown as 3 4 5 6; nim-sum 4
      cursor <= 2'd3;
      state <= PLAYER;
      badMove <= 1'b0;
      thinkCount <= '0;
    end else if (deal) begin
      piles <= dealt;
      cursor <= 2'd3;
      state <= PLAYER;
      badMove <= 1'b0;
    end else begin
      unique case (state)
        PLAYER: begin
          if (cursorLeft)
            cursor <= cursor + 2'd1;  // a higher index is further left
          else if (cursorRight)
            cursor <= cursor - 2'd1;
          else if (take) begin
            if (amount != 4'd0 && amount <= piles[cursor]) begin
              piles[cursor] <= piles[cursor] - amount;
              badMove <= 1'b0;
              thinkCount <= '0;
              state <= (amount == piles[cursor] && othersEmpty(piles, cursor)) ? WON : THINK;
            end else
              badMove <= 1'b1;
          end
        end
        THINK: begin
          if (thinkCount == THINK_W'(THINK_CYCLES - 1)) begin
            piles[target] <= targetValue;
            state <= (targetValue == 4'd0 && othersEmpty(piles, target)) ? LOST : PLAYER;
          end else
            thinkCount <= thinkCount + THINK_W'(1);
        end
        WON, LOST: ;  // until a new deal or reset
      endcase
    end
  end

  assign yourTurn = state == PLAYER;
  assign thinking = state == THINK;
  assign playerWon = state == WON;
  assign fpgaWon = state == LOST;
endmodule : NimGame
