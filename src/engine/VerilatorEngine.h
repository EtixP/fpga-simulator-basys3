#pragma once
#include "engine/SimEngine.h"

#include "verilated.h"
#include "verilated_vcd_c.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vb {

struct VerilatorEngineOptions {
  std::string topModule;           // Verilog top module name, e.g. "counter"
  std::string clockName = "clk";   // master clock input port
  std::string traceFile;           // optional; may also come via setTraceFile()
};

namespace detail {

// Type-erased handles into the generated VModel. Only makeVerilatorEngine<>
// touches generated headers; VerilatorEngine itself compiles once, against
// the model-independent Verilator runtime.
struct ModelHooks {
  void* model = nullptr;
  void (*eval)(void*) = nullptr;
  // Null when the design was verilated without tracing support. In practice
  // every design in this project is verilated with --trace-vcd (the engine
  // links against the VCD runtime, so a non-traced design fails at link
  // time); the null check in trace() is defense-in-depth.
  void (*traceRegister)(void*, VerilatedVcdC*, int levels) = nullptr;
  void (*finalize)(void*) = nullptr;
  void (*destroy)(void*) = nullptr;
};

// RAII for the type-erased model: a fully-constructed member, so the model
// is finalized/destroyed even if the engine constructor throws afterwards.
class ModelOwner {
public:
  explicit ModelOwner(ModelHooks h) : h_(h) {}
  ~ModelOwner() {
    if (h_.model) {
      if (h_.finalize) h_.finalize(h_.model);
      if (h_.destroy) h_.destroy(h_.model);
    }
  }
  ModelOwner(const ModelOwner&) = delete;
  ModelOwner& operator=(const ModelOwner&) = delete;
  const ModelHooks& hooks() const { return h_; }
  void eval() const { h_.eval(h_.model); }

private:
  ModelHooks h_;
};

}  // namespace detail

// Phase-1 SimEngine over a Verilator-compiled design. Requirements on the
// verilation (enforced where detectable; see docs/versions.md):
//   --public-flat-rw   mandatory — signal access goes through the runtime
//                      symbol table, which is EMPTY without it
//   --trace-vcd        for trace() support
//   --timescale 1ns/1ns  so one cycle is an honest 10 ns in the VCD
//
// Poke invariant: writes go ONLY through the "TOP.TOP" ports scope (real
// port storage, real directions). Module-scope vars ("TOP.<module>.x") are
// change-trigger-synced shadow copies — poking them appears to work until
// the real port changes — so they are registered peek-only.
class VerilatorEngine final : public SimEngine {
public:
  // Use makeVerilatorEngine<VModel>() instead of calling this directly.
  VerilatorEngine(std::unique_ptr<VerilatedContext> ctx, detail::ModelHooks hooks,
                  VerilatorEngineOptions opts);
  ~VerilatorEngine() override;

  VerilatorEngine(const VerilatorEngine&) = delete;
  VerilatorEngine& operator=(const VerilatorEngine&) = delete;

  SignalId lookup(std::string_view name) override;
  SignalInfo info(SignalId id) const override;
  std::vector<SignalInfo> ports() const override;

  void step(uint64_t cycles) override;
  uint64_t peek(SignalId id) override;
  void poke(SignalId id, uint64_t v) override;
  uint64_t now() const override;
  void stepCapture(uint64_t cycles, const std::vector<SignalId>& ids,
                   uint64_t* out) override;

  void setTraceFile(std::string_view path) override;
  void trace(bool enable) override;

private:
  struct Entry {
    void* datap = nullptr;
    VerilatedVarType vltype = VLVT_UNKNOWN;
    uint32_t width = 0;
    std::optional<SignalInfo::PackedRange> packedRange;
    bool input = false;
    bool isClock = false;
    std::string name;
  };

  // A resolved packing lane for stepCapture: a cached Entry pointer and its
  // LSB offset in the packed word (no per-cycle SignalId dispatch).
  struct PackLane {
    const Entry* entry;
    uint32_t offset;
  };

  const Entry& entryFor(SignalId id) const;  // throws std::invalid_argument
  // The single per-cycle advance path shared by step() and stepCapture()
  // (see the .cpp): two evals + two trace dumps + two timeInc(5) + cycle_.
  void advanceCycle(const Entry& clk);
  // Point Verilator's thread-context at THIS engine's context. Required
  // before every eval and before teardown: parts of the runtime (scope
  // unregistration in ~VerilatedScope, $finish/$display plumbing) resolve
  // the context through the thread-local, and with several engines alive it
  // otherwise points at whichever context was constructed last — including
  // a freed one (crash on non-LIFO engine destruction).
  void bindThreadContext() const;
  SignalId registerVar(const VerilatedVar& var, std::string name, bool fromPortsScope);
  static bool varSupported(const VerilatedVar& var);
  static uint64_t load(const Entry& e);
  static void store(const Entry& e, uint64_t v);

  VerilatorEngineOptions opts_;
  // Declaration order is teardown order in reverse: the VCD writer closes
  // first, then the model is finalized/destroyed, then the context dies.
  std::unique_ptr<VerilatedContext> ctx_;
  detail::ModelOwner model_;
  std::unique_ptr<VerilatedVcdC> vcd_;

  std::vector<Entry> entries_;
  std::unordered_map<std::string, SignalId> byName_;  // memoizes kNoSignal too
  std::vector<SignalInfo> portList_;                  // sorted by name
  SignalId clockId_{kNoSignal};
  uint64_t cycle_ = 0;
  bool dirty_ = false;    // inputs poked since the last eval
  bool tracing_ = false;  // dump() calls enabled
};

// Per-design factory: the only code that sees the generated VModel header.
template <class VModel>
std::unique_ptr<SimEngine> makeVerilatorEngine(VerilatorEngineOptions opts) {
  auto ctx = std::make_unique<VerilatedContext>();
  // Explicit instance name: the engine derives scope names from "TOP.".
  auto model = std::make_unique<VModel>(ctx.get(), "TOP");

  detail::ModelHooks h;
  h.eval = [](void* m) { static_cast<VModel*>(m)->eval(); };
  if constexpr (requires(VModel& mm, VerilatedVcdC* v) { mm.trace(v, 99); }) {
    h.traceRegister = [](void* m, VerilatedVcdC* vcd, int levels) {
      static_cast<VModel*>(m)->trace(vcd, levels);
    };
  }
  h.finalize = [](void* m) { static_cast<VModel*>(m)->final(); };
  h.destroy = [](void* m) { delete static_cast<VModel*>(m); };
  h.model = model.release();  // ModelOwner inside the engine takes over

  return std::make_unique<VerilatorEngine>(std::move(ctx), h, std::move(opts));
}

}  // namespace vb
