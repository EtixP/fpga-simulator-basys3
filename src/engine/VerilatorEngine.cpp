#include "engine/VerilatorEngine.h"

#include "verilated_syms.h"

#include <stdexcept>

namespace vb {

namespace {

uint64_t maskFor(uint32_t width) {
  return width >= 64 ? ~0ull : ((1ull << width) - 1);
}

}  // namespace

VerilatorEngine::VerilatorEngine(std::unique_ptr<VerilatedContext> ctx,
                                 detail::ModelHooks hooks, VerilatorEngineOptions opts)
    : opts_(std::move(opts)), ctx_(std::move(ctx)), model_(hooks) {
  if (opts_.topModule.empty())
    throw std::invalid_argument("VerilatorEngine: options.topModule must be set");
  if (opts_.topModule == "TOP")
    throw std::invalid_argument(
        "VerilatorEngine: a top module literally named 'TOP' collides with the "
        "ports scope; rename it");

  // Must precede any chance of tracing; enabling a trace later without this
  // hard-aborts the process inside Verilator.
  ctx_->traceEverOn(true);

  // First eval runs initial blocks/$readmemh and settles combinational logic:
  // this establishes the t=0 contract of SimEngine (R6).
  bindThreadContext();
  model_.eval();

  const VerilatedScope* portsScope = ctx_->scopeFind("TOP.TOP");
  if (!portsScope || !portsScope->varsp())
    throw std::runtime_error(
        "VerilatorEngine: no runtime symbol table — the design must be "
        "verilated with --public-flat-rw");

  const std::string moduleScope = "TOP." + opts_.topModule;
  if (!ctx_->scopeFind(moduleScope.c_str()))
    throw std::invalid_argument("VerilatorEngine: scope '" + moduleScope +
                                "' not found — check options.topModule against the "
                                "design's top module name");

  // Pre-register every supported top-level port. VerilatedVarNameMap is
  // strcmp-ordered, so registration (and ports()) is sorted by name.
  for (const auto& [namep, var] : *portsScope->varsp()) {
    if (!varSupported(var)) continue;
    // Contract: inout ports are omitted — a 2-state engine cannot service a
    // Z-capable pin through peek/poke (R5 defers tri-state anyway).
    if (var.vldir() == VLVD_INOUT) continue;
    registerVar(var, namep, /*fromPortsScope=*/true);
  }
  for (const SignalInfo& p : portList_) {
    if (p.name == opts_.clockName) clockId_ = byName_.at(p.name);
  }
  if (clockId_ == kNoSignal)
    throw std::invalid_argument("VerilatorEngine: clock port '" + opts_.clockName +
                                "' not found among top-level ports");
  Entry& clk = entries_[static_cast<uint32_t>(clockId_)];
  if (!clk.input || clk.width != 1)
    throw std::invalid_argument("VerilatorEngine: clock port '" + opts_.clockName +
                                "' must be a 1-bit input");
  clk.isClock = true;
}

VerilatorEngine::~VerilatorEngine() {
  // Teardown by member order after this body: vcd_ (closes the VCD), model_
  // (final + delete), ctx_ last — the model references the context until
  // destroyed. The thread-context bind makes the model's scopes unregister
  // from THEIR context, not from another engine's (possibly freed) one.
  bindThreadContext();
}

void VerilatorEngine::bindThreadContext() const {
  Verilated::threadContextp(ctx_.get());
}

bool VerilatorEngine::varSupported(const VerilatedVar& var) {
  if (var.udims() != 0) return false;  // memories/unpacked arrays: reject, don't
                                       // silently alias element 0
  if (var.pdims() > 1) return false;  // no multidimensional indexing contract
  switch (var.vltype()) {
    case VLVT_UINT8:
    case VLVT_UINT16:
    case VLVT_UINT32:
    case VLVT_UINT64:
      break;
    default:
      return false;  // WData (>64 bits), real, string, ...
  }
  const uint32_t bits = var.entBits();
  return bits >= 1 && bits <= 64;
}

SignalId VerilatorEngine::registerVar(const VerilatedVar& var, std::string name,
                                      bool fromPortsScope) {
  Entry e;
  e.datap = var.datap();
  e.vltype = var.vltype();
  e.width = var.entBits();
  if (var.pdims() == 1) {
    const auto& range = var.packedRanges().front();
    e.packedRange = SignalInfo::PackedRange{range.left(), range.right()};
  }
  // Direction is only trustworthy in the ports scope (module-scope vars are
  // all VLVD_NODIR) — and only ports-scope storage is safe to write anyway.
  e.input = fromPortsScope && var.vldir() == VLVD_IN;
  e.name = name;

  const SignalId id{static_cast<uint32_t>(entries_.size())};
  entries_.push_back(std::move(e));
  byName_.emplace(entries_.back().name, id);
  if (fromPortsScope)
    portList_.push_back(SignalInfo{entries_.back().name, entries_.back().width,
                                   entries_.back().input, entries_.back().packedRange});
  return id;
}

SignalId VerilatorEngine::lookup(std::string_view name) {
  const std::string key(name);
  if (auto it = byName_.find(key); it != byName_.end()) return it->second;

  // All ports were pre-registered, so an unknown plain name stays unknown:
  // plain names never resolve to internal signals (shadow copies, no
  // direction info, not portable to the gate-level engine).
  const size_t lastDot = key.rfind('.');
  if (lastDot == std::string::npos || lastDot == 0 || lastDot + 1 == key.size()) {
    byName_.emplace(key, kNoSignal);
    return kNoSignal;
  }

  // Hierarchical name rooted at the top module: "counter.count" ->
  // scope "TOP.counter", var "count". Registered peek-only. Names rooted
  // anywhere else (e.g. Verilator's own "TOP." instance prefix) are not part
  // of the contract and must not resolve.
  if (key.substr(0, key.find('.')) != opts_.topModule) {
    byName_.emplace(key, kNoSignal);
    return kNoSignal;
  }
  const std::string scopeName = "TOP." + key.substr(0, lastDot);
  const std::string varName = key.substr(lastDot + 1);
  const VerilatedScope* scope = ctx_->scopeFind(scopeName.c_str());
  const VerilatedVar* var = scope ? scope->varFind(varName.c_str()) : nullptr;
  if (!var || !varSupported(*var)) {
    byName_.emplace(key, kNoSignal);
    return kNoSignal;
  }
  return registerVar(*var, key, /*fromPortsScope=*/false);
}

const VerilatorEngine::Entry& VerilatorEngine::entryFor(SignalId id) const {
  const auto idx = static_cast<uint32_t>(id);
  if (idx >= entries_.size())
    throw std::invalid_argument("SimEngine: invalid SignalId");
  return entries_[idx];
}

SignalInfo VerilatorEngine::info(SignalId id) const {
  const Entry& e = entryFor(id);
  return SignalInfo{e.name, e.width, e.input, e.packedRange};
}

std::vector<SignalInfo> VerilatorEngine::ports() const { return portList_; }

uint64_t VerilatorEngine::load(const Entry& e) {
  uint64_t v = 0;
  switch (e.vltype) {
    case VLVT_UINT8: v = *static_cast<const uint8_t*>(e.datap); break;
    case VLVT_UINT16: v = *static_cast<const uint16_t*>(e.datap); break;
    case VLVT_UINT32: v = *static_cast<const uint32_t*>(e.datap); break;
    case VLVT_UINT64: v = *static_cast<const uint64_t*>(e.datap); break;
    default: throw std::logic_error("SimEngine: unreachable vltype in load");
  }
  return v & maskFor(e.width);
}

void VerilatorEngine::store(const Entry& e, uint64_t v) {
  v &= maskFor(e.width);
  switch (e.vltype) {
    case VLVT_UINT8: *static_cast<uint8_t*>(e.datap) = static_cast<uint8_t>(v); break;
    case VLVT_UINT16: *static_cast<uint16_t*>(e.datap) = static_cast<uint16_t>(v); break;
    case VLVT_UINT32: *static_cast<uint32_t*>(e.datap) = static_cast<uint32_t>(v); break;
    case VLVT_UINT64: *static_cast<uint64_t*>(e.datap) = v; break;
    default: throw std::logic_error("SimEngine: unreachable vltype in store");
  }
}

uint64_t VerilatorEngine::peek(SignalId id) {
  const Entry& e = entryFor(id);
  if (dirty_) {
    bindThreadContext();
    model_.eval();  // settle combinational logic; no clock edge, no time, no dump
    dirty_ = false;
  }
  return load(e);
}

void VerilatorEngine::poke(SignalId id, uint64_t v) {
  const Entry& e = entryFor(id);
  if (e.isClock)
    throw std::invalid_argument("poke: '" + e.name +
                                "' is the master clock — the engine owns it (R1)");
  if (!e.input)
    throw std::invalid_argument("poke: '" + e.name + "' is not a top-level input port");
  store(e, v);
  dirty_ = true;
}

// The single per-cycle time-advance path. step() and stepCapture() BOTH call
// this so the two evals, both trace dumps, and both timeInc(5)s live in ONE
// place — there is no second copy of the "advance 10 ns" logic that could
// drift from this one (R1 single-time-counter invariant). Assumes the thread
// context is already bound and clk is resolved by the caller.
void VerilatorEngine::advanceCycle(const Entry& clk) {
  // Low half-cycle first: pending pokes are applied in this eval, so they are
  // stable before the rising edge samples them.
  store(clk, 0);
  model_.eval();
  if (tracing_) vcd_->dump(ctx_->time());
  ctx_->timeInc(5);

  store(clk, 1);
  model_.eval();
  if (tracing_) vcd_->dump(ctx_->time());
  ctx_->timeInc(5);

  ++cycle_;
}

void VerilatorEngine::step(uint64_t cycles) {
  bindThreadContext();
  const Entry& clk = entryFor(clockId_);
  for (uint64_t i = 0; i < cycles; ++i) advanceCycle(clk);
  if (cycles > 0) dirty_ = false;
}

void VerilatorEngine::stepCapture(uint64_t cycles, const std::vector<SignalId>& ids,
                                  uint64_t* out) {
  // Resolve packed lanes once (cached datap pointers, no per-cycle SignalId
  // dispatch) — the whole point of the tap. Validates the same way the
  // shared default does: bad id or width sum > 64 throws before stepping.
  std::vector<PackLane> lanes;
  uint32_t offset = 0;
  for (const SignalId id : ids) {
    const Entry& e = entryFor(id);
    if (offset + e.width > 64)
      throw std::invalid_argument(
          "stepCapture: packed width of the watch set exceeds 64 bits");
    lanes.push_back(PackLane{&e, offset});
    offset += e.width;
  }

  bindThreadContext();
  const Entry& clk = entryFor(clockId_);
  for (uint64_t i = 0; i < cycles; ++i) {
    advanceCycle(clk);  // SAME path as step(): evals, dumps, timeInc, cycle_
    uint64_t word = 0;
    for (const PackLane& ln : lanes) word |= load(*ln.entry) << ln.offset;
    out[i] = word;
  }
  if (cycles > 0) dirty_ = false;
}

uint64_t VerilatorEngine::now() const { return cycle_; }

void VerilatorEngine::setTraceFile(std::string_view path) {
  if (vcd_)
    throw std::logic_error(
        "setTraceFile: tracing already started; the VCD writer binds to one "
        "file for the life of the engine");
  opts_.traceFile = std::string(path);
}

void VerilatorEngine::trace(bool enable) {
  bindThreadContext();  // keep the bind discipline uniform on runtime paths
  if (!enable) {
    tracing_ = false;
    if (vcd_) vcd_->flush();
    return;
  }
  if (!vcd_) {
    if (opts_.traceFile.empty())
      throw std::logic_error("trace: no output file set (use setTraceFile)");
    if (!model_.hooks().traceRegister)
      throw std::logic_error("trace: design was not verilated with tracing support "
                             "(--trace-vcd)");
    vcd_ = std::make_unique<VerilatedVcdC>();
    // Register the model exactly once per writer (a second registration or one
    // after the first dump is fatal inside Verilator), then open.
    model_.hooks().traceRegister(model_.hooks().model, vcd_.get(), 99);
    vcd_->open(opts_.traceFile.c_str());
    if (!vcd_->isOpen()) {
      vcd_.reset();
      throw std::runtime_error("trace: could not open '" + opts_.traceFile + "'");
    }
  }
  tracing_ = true;
}

}  // namespace vb
