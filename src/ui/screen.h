// ui/screen.h — a menu or overlay (title, world list, options, pause, inventory, chat...).
// Screens are drawn with Dear ImGui styled like the original (styles.css): dark Doku panels,
// classic grey menu buttons, Pixelify Sans / Jersey 15 fonts. Controllers navigate every screen.
#pragma once
#include "common/common.h"

namespace dq {

class App;

class Screen {
public:
  virtual ~Screen() = default;
  virtual const char* id() const = 0;
  virtual void update(App& a, double dt) {}
  virtual void draw(App& a) = 0;                 // ImGui calls
  virtual bool pausesGame() const { return true; }      // single player sim pauses underneath
  virtual bool showsWorld() const { return true; }      // world keeps rendering behind it
  virtual bool wantsMouse() const { return true; }      // release mouse capture
  virtual bool onBack(App& a) { return false; }         // Esc / B: true if handled (else the screen closes)
  virtual void onOpen(App& a) {}
  virtual void onClose(App& a) {}
};

// Built-in screen factories (implemented in ui/*.cpp)
std::unique_ptr<Screen> makeTitleScreen();
std::unique_ptr<Screen> makePauseScreen();

} // namespace dq
