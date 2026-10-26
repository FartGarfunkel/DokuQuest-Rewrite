// ui/file_pick.cpp — the system file chooser and dropped files (see file_pick.h).
#include "ui/file_pick.h"
#include "common/common.h"
#include "common/fs.h"
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#undef near   // (windows.h defines near and far as nothing; commdlg.h still needs them, so after it)
#undef far
#endif

namespace dq {

struct FilePicker::State {
  std::atomic<bool> done{false};
  std::string path, err;
};

namespace {
std::string toUpper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
#ifndef _WIN32
// a shell word in single quotes
std::string sq(const std::string& s) { std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
bool runCapture(const std::string& cmd, std::string& out, int& rc) {
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return false;
  char buf[4096]; size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  rc = pclose(f);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return true;
}
bool haveTool(const char* t) { std::string o; int rc = 0; return runCapture(std::string("command -v ") + t + " 2>/dev/null", o, rc) && rc == 0 && !o.empty(); }
#endif

void pickBlocking(const std::string& title, const std::string& ext, const std::string& dir, std::string& path, std::string& err) {
#ifdef _WIN32
  // the common Open dialog, loaded at run time (no extra link library)
  typedef BOOL(WINAPI * GetOpenFileNameW_t)(LPOPENFILENAMEW);
  HMODULE dll = LoadLibraryW(L"comdlg32.dll");
  auto fn = dll ? (GetOpenFileNameW_t)(void*)GetProcAddress(dll, "GetOpenFileNameW") : nullptr;
  if (!fn) { err = "The file chooser isn't available"; return; }
  auto widen = [](const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? n - 1 : 0, 0); if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); return w; };
  std::wstring filter = widen(toUpper(ext) + " files (*." + ext + ")"); filter.push_back(0);
  filter += widen("*." + ext); filter.push_back(0); filter += L"All files"; filter.push_back(0); filter += L"*.*"; filter.push_back(0); filter.push_back(0);
  std::wstring wt = widen(title), wd = widen(dir);
  wchar_t file[4096] = {0};
  OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.lpstrFilter = filter.c_str(); o.lpstrFile = file; o.nMaxFile = 4096;
  o.lpstrTitle = wt.c_str(); o.lpstrInitialDir = wd.empty() ? nullptr : wd.c_str(); o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (fn(&o)) { int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr); std::string s(n > 0 ? n - 1 : 0, 0); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file, -1, &s[0], n, nullptr, nullptr); path = s; }
#elif defined(__APPLE__)
  std::string out; int rc = 0;
  std::string script = "POSIX path of (choose file with prompt \"" + title + "\" of type {\"" + ext + "\"})";
  if (runCapture("osascript -e " + sq(script) + " 2>/dev/null", out, rc) && rc == 0) path = out;
#else
  std::string out; int rc = 0;
  std::string start = dir.empty() ? "" : dir + "/";
  if (haveTool("zenity")) {
    std::string cmd = "zenity --file-selection --title=" + sq(title) + " --file-filter=" + sq(toUpper(ext) + " files | *." + ext + " *." + toUpper(ext)) + " --file-filter=" + sq("All files | *") +
                      (start.empty() ? "" : " --filename=" + sq(start)) + " 2>/dev/null";
    if (runCapture(cmd, out, rc) && rc == 0) path = out;
  } else if (haveTool("kdialog")) {
    std::string cmd = "kdialog --title " + sq(title) + " --getopenfilename " + sq(start.empty() ? "." : start) + " " + sq("*." + ext + " *." + toUpper(ext) + "|" + toUpper(ext) + " files") + " 2>/dev/null";
    if (runCapture(cmd, out, rc) && rc == 0) path = out;
  } else if (haveTool("yad")) {
    std::string cmd = "yad --file --title=" + sq(title) + " --file-filter=" + sq("*." + ext) + " 2>/dev/null";
    if (runCapture(cmd, out, rc) && rc == 0) { path = out; if (!path.empty() && path.back() == '|') path.pop_back(); }
  } else err = "No file chooser found (install zenity or kdialog) \xE2\x80\x94 drop the file on the window or type its path";
#endif
}

// ---- dropped files ----
std::mutex g_dropMu;
std::deque<std::string>& drops() { static auto* d = new std::deque<std::string>(); return *d; }
int SDLCALL dropWatch(void*, SDL_Event* e) {
  if (e->type == SDL_DROPFILE && e->drop.file) {
    std::lock_guard<std::mutex> l(g_dropMu);
    drops().push_back(e->drop.file);
    if (drops().size() > 16) drops().pop_front();
  }
  return 1;
}
}

void FilePicker::open(const std::string& title, const std::string& ext, const std::string& startDir) {
  if (busy()) return;
  err.clear();
  st = std::make_shared<State>();
  auto s = st;
  std::thread([s, title, ext, startDir] { pickBlocking(title, ext, startDir, s->path, s->err); s->done = true; }).detach();
}
bool FilePicker::busy() const { return st && !st->done; }
bool FilePicker::take(std::string& path) {
  if (!st || !st->done) return false;
  path = st->path; err = st->err;
  st.reset();
  return true;
}

void watchDroppedFiles() {
  static bool on = false;
  if (on) return;
  on = true;
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  SDL_AddEventWatch(dropWatch, nullptr);
}
bool takeDroppedFile(std::string& path, const std::string& ext) {
  watchDroppedFiles();
  std::lock_guard<std::mutex> l(g_dropMu);
  for (auto it = drops().begin(); it != drops().end(); ++it) {
    std::string e = toLower(fs::path(*it).extension().string());
    if (e == "." + toLower(ext)) { path = *it; drops().erase(it); return true; }
  }
  return false;
}

} // namespace dq
