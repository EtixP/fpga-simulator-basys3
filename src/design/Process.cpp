#include "design/Process.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace vb::design {
namespace {
// The only variables a build gets: finding programs, the user, temporary
// files, the locale, and VirtualBasys's own VB_ variables. Anything else, such
// as CXXFLAGS, MACOSX_DEPLOYMENT_TARGET or MAKEFLAGS, could silently change a
// cached module, whose key covers only what the loader controls.
bool buildNeutral(std::string_view entry) {
  for (const std::string_view name : {"PATH=", "HOME=", "USER=", "LOGNAME=", "SHELL=", "TMPDIR=", "LANG=",
                                      "LC_", "VB_"})
    if (entry.rfind(name, 0) == 0) return true;
  return false;
}

// The running child's process group, for cancelProcesses().
std::atomic<pid_t> running{0};
std::atomic<bool> cancelled{false};
static_assert(std::atomic<pid_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
              "cancelProcesses() must be async-signal-safe");

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}
}  // namespace

ProcessResult runProcess(const std::vector<std::string>& argv,
                         const std::filesystem::path& captureDir) {
  ProcessResult result;
  if (argv.empty() || cancelled) return result;
  const auto outPath = captureDir / "stdout.txt";
  const auto errPath = captureDir / "stderr.txt";
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 1, outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_addopen(&actions, 2, errPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  std::vector<char*> args;
  for (const auto& argument : argv) args.push_back(const_cast<char*>(argument.c_str()));
  args.push_back(nullptr);
  std::vector<char*> environment;
  for (char** entry = environ; entry && *entry; ++entry)
    if (buildNeutral(*entry)) environment.push_back(*entry);
  environment.push_back(nullptr);
  // A group of its own: a terminal's Ctrl-C reaches only this process, whose
  // handler cancels the whole build through cancelProcesses().
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attributes, 0);
  pid_t pid = 0;
  const int spawned = posix_spawn(&pid, args[0], &actions, &attributes, args.data(), environment.data());
  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);
  if (spawned != 0) return result;
  result.started = true;
  running = pid;
  if (cancelled) ::kill(-pid, SIGTERM);  // cancelled while it was starting
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  running = 0;
  if (cancelled) {
    // The group outlives its leader while make's and the compiler's own
    // children end; wait for them, killing them after a second.
    for (int waited = 0; waited < 300 && ::kill(-pid, 0) == 0; ++waited) {
      if (waited == 100) ::kill(-pid, SIGKILL);
      ::usleep(10'000);
    }
  }
  result.signaled = WIFSIGNALED(status);
  result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  result.output = readFile(outPath);
  result.errors = readFile(errPath);
  return result;
}

void cancelProcesses() {
  cancelled = true;
  if (const pid_t pid = running; pid > 0) ::kill(-pid, SIGTERM);
}

bool cancelRequested() { return cancelled; }

}  // namespace vb::design
