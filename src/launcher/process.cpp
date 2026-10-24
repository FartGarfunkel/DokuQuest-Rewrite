#include "launcher/process.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
extern char** environ;
#endif

namespace dq {

std::string exeName(const std::string& base) {
#ifdef _WIN32
  return base + ".exe";
#else
  return base;
#endif
}

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
// CommandLineToArgvW-compatible quoting
static void appendArg(std::wstring& cmd, const std::wstring& a) {
  if (!cmd.empty()) cmd += L' ';
  if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) { cmd += a; return; }
  cmd += L'"';
  for (size_t i = 0;; i++) {
    size_t bs = 0;
    while (i < a.size() && a[i] == L'\\') { i++; bs++; }
    if (i == a.size()) { cmd.append(bs * 2, L'\\'); break; }
    if (a[i] == L'"') { cmd.append(bs * 2 + 1, L'\\'); cmd += L'"'; }
    else { cmd.append(bs, L'\\'); cmd += a[i]; }
  }
  cmd += L'"';
}
static HANDLE openOut(const fs::path& p) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  return CreateFileW(p.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool ChildProcess::start(const fs::path& exe, const std::vector<std::string>& args, const ProcOptions& o) {
  if (hProc) { CloseHandle((HANDLE)hProc); hProc = nullptr; }
  startedFlag = false; done = false; code = -1; err.clear();
  std::wstring cmd;
  appendArg(cmd, o.searchPath ? widen(exe.string()) : exe.wstring());
  for (auto& a : args) appendArg(cmd, widen(a));
  STARTUPINFOW si{}; si.cb = sizeof si;
  HANDLE hOut = nullptr, hErr = nullptr;
  if (!o.stdoutFile.empty() || !o.stderrFile.empty()) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    hOut = o.stdoutFile.empty() ? nullptr : openOut(o.stdoutFile);
    hErr = o.stderrFile.empty() ? nullptr : openOut(o.stderrFile);
    si.hStdOutput = hOut && hOut != INVALID_HANDLE_VALUE ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = hErr && hErr != INVALID_HANDLE_VALUE ? hErr : GetStdHandle(STD_ERROR_HANDLE);
  }
  PROCESS_INFORMATION pi{};
  std::wstring cwd = o.cwd.empty() ? std::wstring() : o.cwd.wstring();
  BOOL ok = CreateProcessW(o.searchPath ? nullptr : exe.wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE,
                           (o.hidden ? CREATE_NO_WINDOW : 0) | CREATE_UNICODE_ENVIRONMENT, nullptr,
                           cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
  if (hOut && hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
  if (hErr && hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
  if (!ok) { err = strFormat("could not start %s (error %lu)", exe.string().c_str(), GetLastError()); return false; }
  CloseHandle(pi.hThread);
  hProc = pi.hProcess; startedFlag = true; done = false; code = -1; t0 = nowSeconds();
  return true;
}
bool ChildProcess::running() {
  if (!startedFlag || done) return false;
  if (WaitForSingleObject((HANDLE)hProc, 0) == WAIT_OBJECT_0) {
    DWORD c = 0; GetExitCodeProcess((HANDLE)hProc, &c); code = (int)c; done = true;
    CloseHandle((HANDLE)hProc); hProc = nullptr;
    return false;
  }
  return true;
}
int ChildProcess::wait() {
  if (!startedFlag) return -1;
  if (!done) { WaitForSingleObject((HANDLE)hProc, INFINITE); running(); }
  return code;
}
void ChildProcess::kill() { if (startedFlag && !done && hProc) TerminateProcess((HANDLE)hProc, 1); }
ChildProcess::~ChildProcess() { if (hProc) CloseHandle((HANDLE)hProc); }

#else

bool ChildProcess::start(const fs::path& exe, const std::vector<std::string>& args, const ProcOptions& o) {
  startedFlag = false; done = false; code = -1; err.clear();
  std::vector<std::string> all; all.push_back(exe.string());
  all.insert(all.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (auto& a : all) argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
  if (!o.stdoutFile.empty()) posix_spawn_file_actions_addopen(&fa, 1, o.stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (!o.stderrFile.empty()) posix_spawn_file_actions_addopen(&fa, 2, o.stderrFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  // posix_spawn has no portable "chdir" action (glibc 2.29+ has _np); the launcher passes absolute paths, so the
  // child's working folder only matters for the game, which finds its data next to its own executable.
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
  if (!o.cwd.empty()) posix_spawn_file_actions_addchdir_np(&fa, o.cwd.c_str());
#endif
  pid_t p = -1;
  int rc = o.searchPath ? posix_spawnp(&p, all[0].c_str(), &fa, nullptr, argv.data(), environ)
                        : posix_spawn(&p, all[0].c_str(), &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  if (rc != 0) { err = strFormat("could not start %s (%s)", all[0].c_str(), strerror(rc)); return false; }
  pid = p; startedFlag = true; done = false; code = -1; t0 = nowSeconds();
  return true;
}
bool ChildProcess::running() {
  if (!startedFlag || done) return false;
  int status = 0;
  pid_t r = waitpid(pid, &status, WNOHANG);
  if (r == 0) return true;
  done = true;
  if (r == pid) code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
  // posix_spawnp reports a missing program as exit code 127 from the child
  return false;
}
int ChildProcess::wait() {
  if (!startedFlag) return -1;
  if (!done) {
    int status = 0;
    if (waitpid(pid, &status, 0) == pid) code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
    done = true;
  }
  return code;
}
void ChildProcess::kill() { if (startedFlag && !done && pid > 0) ::kill(pid, SIGTERM); }
ChildProcess::~ChildProcess() {}
#endif

} // namespace dq
