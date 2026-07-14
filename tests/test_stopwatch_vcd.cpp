// Milestone 1.4 acceptance, VCD half: the trace opens cleanly in real tools
// with hierarchical names preserved (R3). Two ctest entries share this
// binary:
//   test_stopwatch_vcd structural <out.vcd>          — writes a 500k-cycle
//     trace, validates it structurally (streaming line-by-line), then proves
//     the validator's teeth by corrupting copies and asserting rejection.
//   test_stopwatch_vcd validate <file.vcd>           — validates an existing
//     file without writing (negative-regression entry point).
//   test_stopwatch_vcd surfer <out.vcd> <surfer|"">  — feeds the same file to
//     Surfer's headless server mode. Success = the "Loaded body" log line
//     (header AND body parsed); header corruption exits 1, body corruption
//     panics a loader thread ("Surfer crashed due to a panic") without
//     exiting — both are FAIL. Exits 77 (ctest SKIP) when the tool is
//     unavailable, so absence reads SKIP, never PASS.
#include "Vstopwatch.h"
#include "check.h"
#include "engine/VerilatorEngine.h"

#include <csignal>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr uint64_t kTraceCycles = 500'000;  // one full anode rotation is 100k;
                                            // 500k covers 5 rotations + init
                                            // at ~31 MB (5M cycles would be 400+)

int writeTrace(const char* vcdPath) {
  auto engine = vb::makeVerilatorEngine<Vstopwatch>({.topModule = "stopwatch"});
  engine->setTraceFile(vcdPath);
  engine->trace(true);
  engine->step(kTraceCycles);
  return 0;  // engine destructor closes the VCD
}

int validateStructure(const char* vcdPath) {
  std::ifstream in(vcdPath);
  CHECK(in.good());

  bool sawTimescale1ns = false, sawEndDefs = false;
  bool inTopScope = false, sawStopwatchScope = false;
  std::set<std::string> stopwatchVars;
  std::set<std::string> ids;
  long long lastTime = -1;
  size_t timestamps = 0;
  std::string line;
  bool pendingTimescale = false;

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!sawEndDefs) {
      // Definition-section lines are indented by scope depth: trim first.
      const size_t start = line.find_first_not_of(" \t");
      const std::string def = start == std::string::npos ? "" : line.substr(start);
      if (def.find("$timescale") != std::string::npos) {
        if (def.find("1ns") != std::string::npos) sawTimescale1ns = true;
        else pendingTimescale = true;  // value may be on the next line
        continue;
      }
      if (pendingTimescale) {
        if (def.find("1ns") != std::string::npos) sawTimescale1ns = true;
        pendingTimescale = false;
      }
      if (def.rfind("$scope", 0) == 0) {
        if (def.find(" TOP ") != std::string::npos) inTopScope = true;
        if (def.find(" stopwatch ") != std::string::npos) sawStopwatchScope = true;
        continue;
      }
      if (def.rfind("$var", 0) == 0) {
        // "$var wire 32 <id> db_cnt [31:0] $end" — id is the 4th field.
        std::string kind, type, width, id, name;
        std::istringstream ls(def);
        ls >> kind >> type >> width >> id >> name;
        CHECK(!id.empty() && !name.empty());
        ids.insert(id);
        if (sawStopwatchScope) stopwatchVars.insert(name);
        continue;
      }
      if (def.find("$enddefinitions") != std::string::npos) {
        sawEndDefs = true;
        continue;
      }
      continue;
    }
    // Post-definitions: timestamps and value changes only.
    if (line[0] == '#') {
      const long long t = std::stoll(line.substr(1));
      CHECK(t > lastTime);  // strictly increasing
      lastTime = t;
      ++timestamps;
    } else if (line[0] == 'b' || line[0] == 'B') {
      const size_t sp = line.find(' ');
      CHECK(sp != std::string::npos);
      CHECK(ids.count(line.substr(sp + 1)));  // every change id was declared
    } else if (line[0] == '0' || line[0] == '1' || line[0] == 'x' || line[0] == 'z') {
      CHECK(ids.count(line.substr(1)));
    } else if (line[0] == '$') {
      // $dumpvars / $end markers are fine
    } else {
      std::fprintf(stderr, "unexpected VCD line: %s\n", line.c_str());
      return 1;
    }
  }

  CHECK(sawTimescale1ns);  // R1: honest 10 ns cycles
  CHECK(sawEndDefs);
  CHECK(inTopScope);
  CHECK(sawStopwatchScope);  // hierarchy preserved (R3)
  // Internal signals visible under the module scope, not just ports.
  CHECK(stopwatchVars.count("running"));
  CHECK(stopwatchVars.count("sel"));
  CHECK(stopwatchVars.count("db_cnt"));
  CHECK(timestamps > 100);
  CHECK_EQ(static_cast<unsigned long long>(lastTime),
           kTraceCycles * 10 - 5);  // last half-cycle dump at 10 ns/cycle

  std::puts("test_stopwatch_vcd structural: PASS");
  return 0;
}

int surferGate(const char* vcdPath, const char* surferPath) {
  if (!surferPath || !*surferPath || access(surferPath, X_OK) != 0) {
    std::fprintf(stderr, "surfer not available — SKIP\n");
    return 77;
  }
  int fds[2];
  CHECK(pipe(fds) == 0);
  const pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[0]);
    close(fds[1]);
    execl(surferPath, "surfer", "server", "--file", vcdPath, "--port", "47653",
          static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  // Read the server's output until it either reports a loaded header
  // (success) or exits (parse failure / exec failure).
  FILE* out = fdopen(fds[0], "r");
  char buf[512];
  bool loadedBody = false, parseError = false;
  while (fgets(buf, sizeof buf, out)) {
    // "Loaded header" comes ~100 us in, BEFORE the body parse — waiting for
    // "Loaded body" is what actually gates the waveform content.
    if (std::strstr(buf, "Loaded body")) { loadedBody = true; break; }
    if (std::strstr(buf, "Failed to parse") || std::strstr(buf, "Error") ||
        std::strstr(buf, "panic") || std::strstr(buf, "crashed")) {
      parseError = true;
      break;
    }
  }
  kill(pid, SIGTERM);
  int status = 0;
  waitpid(pid, &status, 0);
  fclose(out);
  if (loadedBody) {
    std::puts("test_stopwatch_vcd surfer: PASS");
    return 0;
  }
  if (parseError) {
    std::fprintf(stderr, "surfer rejected the VCD (parse error or loader panic)\n");
    return 1;
  }
  std::fprintf(stderr, "surfer produced no verdict — SKIP\n");
  return 77;
}

// Corrupt `src` into `dst` per `mode`, then run <self> validate <dst> and
// expect a nonzero exit: proves the structural validator catches real damage.
int expectValidateFails(const char* self, const std::string& src,
                        const std::string& dst, int mode) {
  std::ifstream in(src, std::ios::binary);
  CHECK(in.good());
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  switch (mode) {
    case 0:  // truncate mid-line
      text.resize(text.size() * 2 / 3);
      break;
    case 1: {  // shuffle a timestamp backwards
      const size_t lastHash = text.rfind("\n#");
      CHECK(lastHash != std::string::npos);
      const size_t eol = text.find('\n', lastHash + 1);
      text.replace(lastHash, eol - lastHash, "\n#1");
      break;
    }
    case 2:  // value change for an undeclared id
      text += "0~~UNDECLARED~~\n";
      break;
    default:
      return 1;
  }
  {
    std::ofstream outF(dst, std::ios::binary);
    outF << text;
    CHECK(outF.good());
  }
  const pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execl(self, self, "validate", dst.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  const bool failedAsExpected = WIFEXITED(status) && WEXITSTATUS(status) != 0 &&
                                WEXITSTATUS(status) != 127;
  if (!failedAsExpected)
    std::fprintf(stderr, "validator ACCEPTED corrupt VCD (mode %d)\n", mode);
  std::remove(dst.c_str());
  return failedAsExpected ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc >= 3);
  if (std::strcmp(argv[1], "structural") == 0) {
    const int w = writeTrace(argv[2]);
    if (w != 0) return w;
    const int v = validateStructure(argv[2]);
    if (v != 0) return v;
    // Negative regressions: the validator must reject damaged copies.
    for (int mode = 0; mode < 3; ++mode) {
      const int r = expectValidateFails(argv[0], argv[2],
                                        std::string(argv[2]) + ".corrupt", mode);
      if (r != 0) return r;
    }
    std::puts("test_stopwatch_vcd corruption rejection: PASS");
    return 0;
  }
  if (std::strcmp(argv[1], "validate") == 0) return validateStructure(argv[2]);
  if (std::strcmp(argv[1], "surfer") == 0) return surferGate(argv[2], argc > 3 ? argv[3] : "");
  std::fprintf(stderr, "usage: %s structural|validate|surfer <vcd> [surfer-path]\n",
               argv[0]);
  return 1;
}
