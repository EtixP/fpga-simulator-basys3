// UART echo: every 8N1 byte received on RsRx at 9600 baud is retransmitted
// on RsTx. led lights while the transmitter is busy (gives the structured
// log an LED event per echoed byte).
//
//   btnC : synchronous reset (R6 convention).
//
// Counter idiom: 32-bit counters compared
// against integer localparams, sized increments, no declaration initializers.
module uart_echo #(
  parameter integer BIT = 10417        // 100 MHz / 9600 baud
)(
  input  wire clk,
  input  wire btnC,
  input  wire RsRx,
  output wire RsTx,
  output wire led
);
  localparam integer BIT_HALF = BIT / 2;  // mid-bit offset

  // --- receiver -------------------------------------------------------------
  // After start-bit detection, sample bit n's center at
  // cnt == BIT + BIT_HALF + n*BIT (data n=0..7, stop n=8), tracked with a
  // running target register (no multiplier).
  reg        rx_ff1, rx_ff2;            // 2FF synchronizer
  reg        rx_busy;
  reg [31:0] rx_cnt;
  reg [31:0] rx_target;
  reg [3:0]  rx_bit;
  reg [7:0]  rx_shift;
  reg [7:0]  rx_byte;
  reg        rx_valid;                  // one-cycle strobe: clean byte received

  // --- transmitter ----------------------------------------------------------
  // One-deep pending buffer between RX and TX. At the end of a full stop
  // bit, hand a pending byte straight to TX: adding an idle cycle per frame
  // would make TX slower than continuous RX and eventually overflow this
  // buffer. Simultaneous consumption and reception preserve the new byte.
  reg        pend_valid;
  reg [7:0]  pend_byte;
  reg        tx_busy;
  reg [31:0] tx_cnt;
  reg [3:0]  tx_bit;                    // shifts emitted: 8 data + stop
  reg [8:0]  tx_shift;                  // {stop, data[7:0]}, LSB first
  reg        tx_out;

  assign RsTx = tx_busy ? tx_out : 1'b1;
  assign led = tx_busy;

  always @(posedge clk) begin
    if (btnC) begin
      rx_ff1 <= 1'b1; rx_ff2 <= 1'b1;
      rx_busy <= 1'b0; rx_cnt <= 32'd0; rx_target <= 32'd0;
      rx_bit <= 4'd0; rx_shift <= 8'd0; rx_byte <= 8'd0; rx_valid <= 1'b0;
      pend_valid <= 1'b0; pend_byte <= 8'd0;
      tx_busy <= 1'b0; tx_cnt <= 32'd0; tx_bit <= 4'd0;
      tx_shift <= 9'd0; tx_out <= 1'b1;
    end else begin
      rx_ff1 <= RsRx;
      rx_ff2 <= rx_ff1;
      rx_valid <= 1'b0;

      if (!rx_busy) begin
        if (!rx_ff2) begin  // start-bit falling edge
          rx_busy <= 1'b1;
          rx_cnt <= 32'd0;
          rx_target <= BIT + BIT_HALF;  // center of data bit 0
          rx_bit <= 4'd0;
        end
      end else begin
        rx_cnt <= rx_cnt + 32'd1;
        if (rx_cnt == rx_target) begin
          rx_target <= rx_target + BIT;
          if (rx_bit < 4'd8) begin
            rx_shift <= {rx_ff2, rx_shift[7:1]};  // LSB first
            rx_bit <= rx_bit + 4'd1;
          end else begin  // stop-bit center
            rx_busy <= 1'b0;
            if (rx_ff2) begin
              rx_byte <= rx_shift;
              rx_valid <= 1'b1;
            end
          end
        end
      end

      if (rx_valid) begin
        pend_valid <= 1'b1;
        pend_byte <= rx_byte;
      end

      if (!tx_busy) begin
        if (pend_valid) begin
          pend_valid <= rx_valid;
          tx_busy <= 1'b1;
          tx_shift <= {1'b1, pend_byte};
          tx_out <= 1'b0;  // start bit
          tx_cnt <= 32'd0;
          tx_bit <= 4'd0;
        end
      end else begin
        tx_cnt <= tx_cnt + 32'd1;
        if (tx_cnt == BIT - 1) begin
          tx_cnt <= 32'd0;
          if (tx_bit == 4'd9) begin
            // The previous stop bit has lasted BIT cycles. Starting the
            // next frame now sustains the receiver's 10*BIT-cycle cadence.
            if (pend_valid || rx_valid) begin
              pend_valid <= pend_valid && rx_valid;
              tx_shift <= {1'b1, pend_valid ? pend_byte : rx_byte};
              tx_out <= 1'b0;
              tx_bit <= 4'd0;
            end else begin
              tx_busy <= 1'b0;
              tx_out <= 1'b1;
            end
          end else begin
            tx_out <= tx_shift[0];
            tx_shift <= {1'b1, tx_shift[8:1]};
            tx_bit <= tx_bit + 4'd1;
          end
        end
      end
    end
  end
endmodule
