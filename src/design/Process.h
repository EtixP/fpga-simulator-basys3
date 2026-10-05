#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace vb::design {

struct ProcessResult {
  bool started = false;
  bool signaled = false;  // ended by a signal (for example Ctrl-C), not an exit
  int exitCode = -1;      // -1 unless the process exited normally
  std::string output;     // stdout
  std::string errors;     // stderr
};

// Runs argv[0] (an absolute path) without a shell, in the current working
// directory, with only PATH, HOME, USER, LOGNAME, SHELL, TMPDIR, the locale
// and VB_ variables from this process's environment: nothing else may change
// how a design module is compiled. stdin is /dev/null; stdout and stderr are captured through
// files in captureDir. The child and everything it starts form their own
// process group, which cancelProcesses() ends. Nothing starts once
// cancelProcesses() has been called.
ProcessResult runProcess(const std::vector<std::string>& argv,
                         const std::filesystem::path& captureDir);

// Ends the running process group, if any, and every later runProcess.
// Async-signal-safe: meant for SIGINT and SIGTERM handlers.
void cancelProcesses();
bool cancelRequested();

}  // namespace vb::design
