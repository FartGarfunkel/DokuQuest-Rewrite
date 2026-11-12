// ui/file_pick.h — choosing a file like the original's <input type="file"> and dropping one on the window:
// the system's own file chooser (Windows: the common Open dialog; Linux: zenity / kdialog; macOS: osascript),
// run off the main thread so the game keeps drawing, and files dragged onto the game window (SDL_DROPFILE).
#pragma once
#include <memory>
#include <string>

namespace dq {

// the system file chooser for one file with an extension ("png", "zip"); poll take() every frame
class FilePicker {
public:
  void open(const std::string& title, const std::string& ext, const std::string& startDir = "");
  bool busy() const;
  // true once: the path picked ("" when cancelled; error() says why when no chooser could be shown)
  bool take(std::string& path);
  const std::string& error() const { return err; }
  struct State;
private:
  std::shared_ptr<State> st;
  std::string err;
};

// files dropped on the window: installs the watch on first use; true (once per file) for a file with that extension
bool takeDroppedFile(std::string& path, const std::string& ext);
void watchDroppedFiles();   // start watching (call when a screen that takes drops opens)

} // namespace dq
