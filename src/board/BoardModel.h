#pragma once
#include "board/SevenSeg.h"
#include "board/SimLog.h"
#include "board/Uart.h"
#include "board/Vga.h"
#include "constraints/PinBinding.h"
#include "engine/SimEngine.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace vb {

// The five momentary push buttons, in board order.
enum class Button : uint32_t { C = 0, U = 1, L = 2, R = 3, D = 4 };
inline constexpr std::array<const char*, 5> kButtonNames{"BTNC", "BTNU", "BTNL",
                                                         "BTNR", "BTND"};

// The virtual board the GUI renders: owns the pin binding and hides the
// engine behind board-level state. Draw code sees only BoardModel — the
// second wall behind the SimEngine rule. Peripherals are composed classes
// (SevenSeg, SimLog; UART/VGA join as siblings in 1.5/1.6).
//
// Milestone 1.4 scope: 16 switches, 16 LEDs, 4-digit seven-seg, 5 buttons,
// structured log.
class BoardModel {
public:
  static constexpr uint32_t kSwitchCount = 16;
  static constexpr uint32_t kLedCount = 16;
  static constexpr uint32_t kDigitCount = SevenSeg::kDigits;

  // Observation granularity for SLOW peripherals (seven-seg ~kHz mux, LEDs,
  // 1.5's UART at ~10.4k cycles/bit) — NOT a universal grid; 1.6's VGA
  // (4 cycles/pixel) will need an engine-side tap instead.
  // The grid is ABSOLUTE: outputs are sampled exactly when now() crosses a
  // multiple of this constant, regardless of how callers slice tick() — so
  // the structured log is a pure function of (stimulus schedule, total
  // cycles), never of frame sizes or driver refactors.
  // Capture contract (aliasing): a display digit is guaranteed captured if
  // its ACTIVE-anode window (dwell minus any ghost-prevention blanking)
  // spans >= 2 chunks (>= 20 us). Sub-chunk active windows whose mux period
  // is a chunk multiple phase-lock to permanently dark — documented, not
  // supported.
  static constexpr uint64_t kSampleChunkCycles = 1'000;  // 10 us @ 100 MHz

  // The engine must outlive the model. Board resources the XDC doesn't bind
  // for this design simply don't exist on the virtual board: has*() returns
  // false, setters are ignored, getters read 0/dark.
  BoardModel(SimEngine& engine, PinBinding binding);
  // Non-copyable: cached pointers into binding_'s map nodes.
  BoardModel(const BoardModel&) = delete;
  BoardModel& operator=(const BoardModel&) = delete;

  // Advance virtual time; samples outputs at absolute grid crossings.
  // The GUI calls this with a fixed cycle count per frame — never with
  // "cycles until N ms elapsed" (R1). Trailing cycles past the last grid
  // crossing are stepped but not sampled (the next tick's first crossing
  // observes them).
  void tick(uint64_t cycles);

  // --- switches (latching) -------------------------------------------------
  bool hasSwitch(uint32_t i) const;
  bool switchState(uint32_t i) const;
  void setSwitch(uint32_t i, bool on);  // takes effect at the next eval

  // --- buttons (momentary; caller owns press duration) ----------------------
  // State persists until explicitly cleared, so a press can span any number
  // of frames — debounce logic needs ~10 ms (1M cycles) of stable input.
  bool hasButton(Button b) const;
  bool buttonState(Button b) const;
  void setButton(Button b, bool pressed);

  // --- LEDs -----------------------------------------------------------------
  bool hasLed(uint32_t i) const;
  bool ledState(uint32_t i) const;

  // --- seven-segment display (fused, eye-emulated view) ---------------------
  bool hasDisplay() const { return displayBound_; }
  uint8_t digitSegments(uint32_t i) const;  // lit mask (bit0=CA..bit6=CG); 0 = dark
  bool digitDp(uint32_t i) const;
  char digitChar(uint32_t i) const;  // decodeSevenSeg(digitSegments(i))
  // Cycle of digit i's most recent active-anode observation (SevenSeg::
  // kNeverLit if none). Test/log support: mux-freshness bounds are far
  // tighter than the 20 ms decay window.
  uint64_t digitLastLit(uint32_t i) const;

  // --- UART (8N1 @ 9600 baud; board resources UART_TX / UART_RX) -------------
  // TX (design -> host): decoded on the observation grid, a SevenSeg-style
  // sibling observer. RX (host -> design): exact-cycle bit edges — tick()
  // splits stepping at each edge, so RX timing is input-precise, not
  // grid-quantized.
  bool hasUartTx() const { return uartTxPin_ != nullptr; }
  bool hasUartRx() const { return uartRxPin_ != nullptr; }
  const std::vector<uint8_t>& uartTxBytes() const { return uartTxBytes_; }
  // Parallel to uartTxBytes(): the grid cycle at which each byte's stop bit
  // was sampled — the same stamp as its "UART TX" log event, logged or not.
  const std::vector<uint64_t>& uartTxByteCycles() const { return uartTxByteCycles_; }
  // Grid cycles of TX frames whose stop bit sampled low — the "UART TX
  // framing error" log stamps — recorded whether or not logging is on.
  const std::vector<uint64_t>& uartTxFramingErrorCycles() const {
    return uartTxFramingErrorCycles_;
  }
  // Queues a byte and returns the cycle scheduled for its start bit: its
  // "UART RX" log stamp when edges are applied through tick() (a caller that
  // advances the engine directly applies overdue edges late). Returns
  // kNoUartCycle, queuing nothing, if UART_RX is unbound.
  static constexpr uint64_t kNoUartCycle = ~0ull;
  uint64_t sendUart(uint8_t byte);
  void sendUartText(std::string_view text);

  // --- VGA (640x480@60; board resources VGA_R/G/B0..3, VGA_HS, VGA_VS) -------
  // Engine-side pixel tap: tick() captures the sync/color pins every cycle via
  // SimEngine::stepCapture (the 1000-cycle grid is far too coarse for 4-cycle
  // pixels) and feeds a VgaFrameAssembler monitor model. Bound only if all 16
  // VGA resources resolve and their packed width fits 64 bits.
  bool hasVga() const { return vga_ != nullptr; }
  uint64_t vgaCompletedFrames() const;
  // Index of the latest completed frame (completedFrames - 1); framebuffer()
  // holds that frame as kWidth*kHeight*3 RGB888, row 0 = top.
  const std::vector<uint8_t>& vgaFramebuffer() const;
  uint64_t vgaLastFrameCycle() const;
  uint32_t vgaCyclesPerPixel() const;
  bool vgaOk() const;
  const std::string& vgaStatus() const;

  // --- signal inspection (read-only) -----------------------------------------
  // The design's supported top-level ports (SimEngine::ports(), sorted by
  // name) and best-effort hierarchical RTL names rooted at the top module
  // ("counter.count"). Reads have peek semantics: pending inputs settle, the
  // clock never toggles and time never advances. Signal handles are for C++
  // frontends' own use and must never reach QML.
  std::vector<SignalInfo> designPorts() const { return engine_.ports(); }
  SignalId findSignal(std::string_view name) { return engine_.lookup(name); }
  SignalInfo signalInfo(SignalId id) const { return engine_.info(id); }
  uint64_t readSignal(SignalId id) { return engine_.peek(id); }

  // --- structured log (R3) ---------------------------------------------------
  bool logEnabled() const { return log_.enabled(); }
  void setLogEnabled(bool on) { log_.setEnabled(on); }
  const std::vector<std::string>& structuredLog() const { return log_.lines(); }
  void clearLog() { log_.clear(); }

  uint64_t now() const { return engine_.now(); }
  const PinBinding& binding() const { return binding_; }

private:
  void sampleAtGridCrossing();
  void applyUartRxEdges();
  void setupVga();  // resolve VGA pins -> lanes; leaves vga_ null if unbindable
  bool readPin(const BoundSignal* bs) const;

  SimEngine& engine_;
  PinBinding binding_;
  SevenSeg sevenSeg_;
  SimLog log_{kSampleChunkCycles};
  UartTxDecoder uartTx_;
  UartRxDriver uartRx_;
  std::vector<uint8_t> uartTxBytes_;
  std::vector<uint64_t> uartTxByteCycles_;
  std::vector<uint64_t> uartTxFramingErrorCycles_;

  std::unique_ptr<VgaFrameAssembler> vga_;  // null unless all VGA pins bound
  std::vector<SignalId> vgaIds_;            // distinct watched ids, first-seen order
  std::vector<uint64_t> vgaBuf_;            // reused per-segment capture buffer

  std::array<const BoundSignal*, kSwitchCount> switches_{};
  std::array<const BoundSignal*, kLedCount> leds_{};
  std::array<const BoundSignal*, 5> buttons_{};
  std::array<const BoundSignal*, 7> segPins_{};   // SEG0..SEG6
  std::array<const BoundSignal*, 4> anPins_{};    // AN0..AN3
  const BoundSignal* dpPin_ = nullptr;
  const BoundSignal* uartTxPin_ = nullptr;  // UART_TX: design output, observed
  const BoundSignal* uartRxPin_ = nullptr;  // UART_RX: design input, driven
  bool displayBound_ = false;  // any anode bound

  std::array<bool, kSwitchCount> switchState_{};
  std::array<bool, 5> buttonState_{};

  // Previous observations, for transition logging at grid crossings.
  std::array<bool, kLedCount> prevLed_{};
  std::array<char, kDigitCount> prevDigitChar_{};
  std::array<bool, kDigitCount> prevDigitDp_{};
};

}  // namespace vb
