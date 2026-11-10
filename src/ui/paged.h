// ui/paged.h — Better than Adventure 8.0's paged menu screen (ScreenPaged / ScreenOptions), written for this engine's
// ImGui: the whole window, a title band on top and a button band at the bottom (the pack's options_background dirt,
// or see-through black over a world), the page list on the left (a quarter of the width, each page an item icon and
// its name, grey / white when chosen / yellow under the mouse), an 8-unit strip, and the page on the right: folding
// categories (a triangle and a grey name) holding 24-unit rows (the name on the left, a faint line, a 100-unit
// control on the right and a reset button beside it once the value isn't the default). Hover a row for its
// description. A Search page filters every row of every page by name.
// Sizes are BTA gui units: one unit is u() pixels (2 × the interface zoom, kept small enough for the window).
// Controllers: the d-pad moves between rows, A presses, left/right change a focused slider or choice, Y resets it,
// LB/RB turn the pages.
#pragma once
#include "ui/theme.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dq {
class App;
namespace ui {

struct PagedPage { std::string name; std::string icon; };   // icon: an item key (drawn with drawIcon)

class Paged {
public:
  // ---- the frame ----
  // pages: the left list (empty: no list, the page fills the width). rightFrac: a column kept free on the right
  // (BTA's server browser keeps a quarter for Join / Edit / Delete).
  // topU / bottomU: the bands' heights in units (ScreenPaged 24 / 28; the world list 32 / 64).
  void begin(App& a, const char* id, const std::string& title, const std::vector<PagedPage>& pages, float rightFrac = 0, float topU = 24, float bottomU = 28);
  // a screen without bands (Create New World, Delete World...): dirt over the whole window (or the dim over a world),
  // the title at 20 units. Place things with buttonAt / textAt / fieldAt, then end().
  void beginPlain(App& a, const char* id, const std::string& title);
  void beginContent();               // the scrolling page area; rows go between these two
  void endContent();
  void skipFocus() { focusFirst_ = false; }   // the caller focused something itself this page
  // the bottom band: buttons centred like ScreenPaged (200 - (n-1)*40 units wide each). Returns the one pressed.
  int footer(const std::vector<std::string>& labels, const std::vector<bool>& disabled = {});
  void end();

  // ---- rows (each returns true when its value changed) ----
  bool category(const std::string& name, const char* icon = nullptr);   // false when folded (skip its rows)
  bool toggle(const char* label, bool* v, bool def, const char* tip = nullptr);
  bool cycle(const char* label, int* cur, const std::vector<std::string>& names, int def, const char* tip = nullptr, float ctrlU = 100);
  // shown: the text on the slider. released: set when a drag ends or a pad step lands (apply heavy things then)
  bool range(const char* label, float* v, float mn, float mx, float step, const std::string& shown, float def, const char* tip = nullptr, bool* released = nullptr, float ctrlU = 100);
  bool action(const char* label, const char* btn, const char* tip = nullptr, bool disabled = false, float ctrlU = 100);
  bool textField(const char* label, std::string* s, const char* hint = nullptr, const char* tip = nullptr, float ctrlU = 160, int flags = 0, size_t maxLen = 0);
  // a row with the caller's own control: true if shown; draw the control at *ctrl (screen pos; the cursor is
  // there), 20 units high and at most ctrlU wide, then call customEnd(). With a reset callback the reset button
  // shows while !isDefault.
  bool custom(const char* label, const char* tip, float ctrlU, ImVec2* ctrl, bool isDefault = true, const std::function<void()>& reset = {});
  void customEnd();
  void note(const std::string& t, ImU32 c = 0);     // a paragraph across the page (never filtered out of its page)
  void gap(float units);
  bool searching() const { return !filter.empty(); }

  // ---- placing things yourself ----
  float u() const { return unit; }
  bool buttonAt(const char* label, ImVec2 pos, float wU, float hU = 20, bool disabled = false);   // a classic grey button
  bool rowButton(const char* label, float wU, bool disabled = false);   // full-width row button in the page (ShortcutComponent)
  // a selectable list row (worlds, servers): returns true when clicked; *dbl on a double click. wU > 0: that wide,
  // centred in the page (Minecraft's world list is 220 wide)
  bool listRow(const char* id, float hU, bool selected, bool* dbl = nullptr, float wU = 0);
  // text in the window: grey labels (0xA0A0A0) and centred lines, with Minecraft's shadow
  void textAt(const std::string& t, ImVec2 pos, ImU32 c = IM_COL32(160, 160, 160, 255), FontId f = F_BTN);
  void centerText(const std::string& t, float y, ImU32 c = IM_COL32(255, 255, 255, 255), FontId f = F_BTN);
  // Minecraft's text field (black, grey frame) placed in the window
  bool fieldAt(const char* id, std::string* s, ImVec2 pos, float wU, const char* hint = nullptr, int flags = 0, size_t maxLen = 0);
  ImVec2 rowMin() const { return lastRowMin_; }   // the last listRow's box
  ImVec2 rowMax() const { return lastRowMax_; }
  void tooltip(const std::string& t) { tipText = t; tipAtMouse = true; }

  // ---- state kept between frames ----
  int page = 0;
  std::string filter;                 // the Search page's term
  std::set<std::string> folded;       // "page/category"
  bool pageChanged = false;           // set for the frame a new page was picked (scroll to top)
  bool inWorld = false;
  // geometry, pixels (valid after begin)
  float W = 0, H = 0, top = 0, bottom = 0, leftW = 0, rightW = 0, cx = 0, cw = 0;

private:
  float unit = 2;
  std::string pendCat; const char* pendIcon = nullptr; bool catOpen = true; bool catShown = false;
  std::string curCat;
  std::string tipText; bool tipAtMouse = false; ImVec2 tipPos;
  int rowSeq = 0;
  bool contentOpen = false;
  float rowNextY_ = 0, rowX_ = 0;
  bool customOpen_ = false; std::string customTip_;
  bool focusFirst_ = true, plain_ = false;
  void takeFocus();
  ImVec2 lastRowMin_, lastRowMax_;
  bool matches(const char* label) const;
  bool rowBegin(const char* label, const char* tip, float ctrlU, bool isDefault, ImVec2& ctrlPos, bool& resetHit);
  void rowEnd(const char* tip, bool isDefault, bool* resetByPad);
  void flushCategory();
  void drawBands(App& a);
  void ownLeftRight();
};

// the launcher's look (launcher/ui.cpp), shared so the game's menus match its Options tab: its brown dirt darkened by
// `dark` (0..1), its grey button, and a page's picture ("block/x" / "item/x": the pack's flat picture, else an item key)
void classicDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, float dark);
bool classicButton(const char* label, ImVec2 size, bool disabled = false, FontId f = F_MID);
void pageIcon(ImDrawList* dl, const std::string& icon, ImVec2 p, float size);
// the classic BTA slider (a dark track, a button-faced handle, the value in the middle). Returns true when changed.
bool mcSlider(const char* id, float* v, float mn, float mx, float step, const std::string& shown, ImVec2 size, bool* released = nullptr);
// draws a tooltip box in BTA's style (dark violet, a violet frame) at p
void tooltipBox(ImDrawList* dl, ImVec2 p, const std::string& t, float maxW);
// the dirt background of BTA's menus (the pack's gui/options_background.png, else the dirt tile), tinted
void menuDirt(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint, float scrollY = 0);
float menuUnit(App& a);              // pixels per BTA gui unit for this window

} // namespace ui
} // namespace dq
