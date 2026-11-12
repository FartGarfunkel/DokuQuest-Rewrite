// launcher/process.h — child processes for the launcher: the game (Play), the system curl and tar
// (updater), a newer launcher (hand-off). posix_spawn on Linux/macOS, CreateProcessW on Windows.
#pragma once
#include "common/fs.h"

namespace dq {

struct ProcOptions {
  fs::path cwd;                  // empty = inherit
  fs::path stdoutFile;           // empty = inherit the launcher's stdout
  fs::path stderrFile;           // empty = inherit
  bool hidden = false;           // Windows: no console window (curl, tar)
  bool searchPath = false;       // exe is a bare name looked up on PATH (curl, tar)
};

class ChildProcess {
public:
  ChildProcess() = default;
  ~ChildProcess();               // does NOT kill a running child (the game outlives a closed launcher)
  ChildProcess(const ChildProcess&) = delete; ChildProcess& operator=(const ChildProcess&) = delete;
  // may be called again once the previous child has exited
  bool start(const fs::path& exe, const std::vector<std::string>& args, const ProcOptions& o = {});
  bool running();                // polls; false once it has exited (then exitCode() is valid)
  int wait();                    // blocks until it exits
  void kill();
  int exitCode() const { return code; }
  bool started() const { return startedFlag; }
  double startTime() const { return t0; }
  const std::string& error() const { return err; }
private:
  bool startedFlag = false, done = false;
  int code = -1;
  double t0 = 0;
  std::string err;
#ifdef _WIN32
  void* hProc = nullptr;
#else
  int pid = -1;
#endif
};

// executable file name for this platform ("dokuquest" → "dokuquest.exe" on Windows)
std::string exeName(const std::string& base);

} // namespace dq
