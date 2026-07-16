#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vb {

// Opaque, engine-scoped signal handle. Valid only for the engine instance
// that returned it — the R4 equivalence checker pairs signals across engines
// by NAME, never by id. The strong type keeps raw integers from being passed
// as ids; it cannot detect an id from one engine being used with another
// (both hand out small dense integers), so cross-engine code must re-lookup
// by name.
enum class SignalId : uint32_t {};
inline constexpr SignalId kNoSignal{0xFFFFFFFFu};

struct SignalInfo {
  std::string name;    // as resolved: "led", "sw", "counter.count"
  uint32_t width = 0;  // bits; 1..64 in phase 1
  bool input = false;  // top-level input port -> poke() allowed
};

// The one abstraction everything goes through (see CLAUDE.md). Board models,
// GUI, CLI, and tests never touch Verilator or the netlist simulator directly.
//
// t=0 contract (R6 — both engines MUST implement this identically or the
// equivalence checker false-positives at cycle 0): construction leaves the
// engine at t = 0 with design initialization complete — registers hold their
// Verilog initial/declaration-assignment values, registers with no initial
// value are ZERO (2-state), $readmemh/$readmemb have run, all inputs are 0,
// combinational logic is settled — and no clock edge has occurred
// (now() == 0).
class SimEngine {
public:
  virtual ~SimEngine() = default;

  // --- signal identification -------------------------------------------
  // Resolve a name to a handle (one-time cost; callers cache the id).
  //  - Top-level port names ("sw", "led"): guaranteed resolvable by EVERY
  //    engine. The only names board models and golden tests may use.
  //  - Hierarchical internal names, rooted at the top module name
  //    ("counter.count"): best-effort, RTL debugging only — the gate-level
  //    engine may return kNoSignal after synthesis renames/dissolves them.
  //    A plain (undotted) name never resolves to an internal signal.
  // Returns kNoSignal for unknown names and unsupported signals (>64 bits,
  // memories/unpacked arrays, non-integer types, inout ports).
  virtual SignalId lookup(std::string_view name) = 0;

  // Metadata by value — no lifetime coupling to engine internals.
  // Throws std::invalid_argument on an invalid id.
  virtual SignalInfo info(SignalId id) const = 0;

  // All supported top-level ports, sorted by name. Ports the engine cannot
  // service through peek/poke (>64 bits, inout) are omitted. This is the
  // enumeration the R4 checker diffs and the phase-2 peek UI lists.
  virtual std::vector<SignalInfo> ports() const = 0;

  // --- simulation core ---------------------------------------------------
  // step: advance N rising edges of the master 100 MHz clock; each edge is
  //       10 ns of virtual time (R1). The engine owns the clock — it cannot
  //       be poked.
  // poke: drive a top-level input; value masked to width; takes effect at
  //       the next eval (peek or step), so a poke between steps is stable
  //       before the following rising edge. Throws std::invalid_argument
  //       for non-input ids, the clock, or invalid ids. Never advances time.
  // peek: read the current value; settles combinational logic first if
  //       inputs changed since the last eval (never toggles the clock,
  //       never advances time, never appears in the waveform trace).
  //       Immediately after step() it reads post-clock-edge committed
  //       state; peek of a just-poked input returns the poked value.
  //       Throws std::invalid_argument on an invalid id.
  // now:  rising edges since t = 0.
  virtual void step(uint64_t cycles) = 0;
  virtual uint64_t peek(SignalId id) = 0;
  virtual void poke(SignalId id, uint64_t v) = 0;
  virtual uint64_t now() const = 0;

  // --- bulk observation tap (milestone 1.6) ------------------------------
  // Advance `cycles` rising edges, packing the watched signals' settled
  // post-edge values once per cycle into out[i]: for cycle i, out[i] holds
  // each id's value masked to its info().width, laid LSB-first in id order
  // (id[0] at bit 0, id[1] above it, ...). The sum of widths must be <= 64.
  //
  // Semantics are EXACTLY { step(1); (pack peeks); } x cycles — SAME time
  // advance, SAME VCD dumps, SAME now(); it is a parallel observation
  // channel, not a step replacement. This is the fast path for pixel-rate
  // sampling (VGA: ~1.68M cycles/frame) that per-cycle peek()-through-SignalId
  // dispatch cannot afford. The DEFAULT implementation below IS the
  // step(1)+peek loop, so every engine — including the phase-3 NetlistEngine
  // — satisfies the contract unchanged; an engine overrides ONLY as an
  // optimization and must stay observably identical to the default (verified
  // in test_engine_contract, tracing on and off).
  //
  // Throws std::invalid_argument if any id is invalid or the width sum
  // exceeds 64 (the exception TYPE is guaranteed identical between the default
  // and any override; the message may differ on doubly-malformed input).
  // cycles == 0 is a no-op (out untouched). `out` must have room for `cycles`
  // entries.
  virtual void stepCapture(uint64_t cycles, const std::vector<SignalId>& ids,
                           uint64_t* out);

  // Pack the current settled values of `ids` into one word (LSB-first in id
  // order, each masked to width). This defines the canonical layout; the
  // default stepCapture calls it, and an override must produce a BIT-IDENTICAL
  // word (VerilatorEngine's does, via cached pointers for speed rather than
  // calling this — verified in test_engine_contract). Throws
  // std::invalid_argument if the width sum exceeds 64.
  uint64_t packSample(const std::vector<SignalId>& ids);

  // --- waveform tracing ---------------------------------------------------
  // setTraceFile: set the waveform (VCD) output path. Must be called before
  //       tracing first starts (throws std::logic_error afterwards — the
  //       trace writer binds to one file for the life of the engine).
  // trace(true): start/resume dumping; requires a path from setTraceFile()
  //       (or engine construction options), else throws std::logic_error.
  //       May be enabled at any cycle, not just t = 0.
  // trace(false): pause dumping and flush; the file stays open so tracing
  //       can resume into the same VCD.
  virtual void setTraceFile(std::string_view path) = 0;
  virtual void trace(bool enable) = 0;
};

}  // namespace vb
