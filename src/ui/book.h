// ui/book.h — the open book the Help, Journal, Skills, Spellbook and Stable screens are written in: Better than
// Adventure's guidebook (ScreenGuidebook: a two-page spread, a bookmark per section down the right edge, the pages
// turned with arrows in the bottom corners) in the DokuQuest Base pack's own look: its parchment page (gui/book.png,
// the left page the same one mirrored), its page arrows and its side tabs (tab_right_*), dark ink on the paper.
// A pack without them gets a plain paper page and ribbons drawn here.
//
// A screen hands the book one list of rows per bookmark; the book lays them onto pages (each section starts on a
// left page), shows the open spread, and runs straight on from one section to the next as the pages turn.
// Controls: the arrows, the mouse wheel, ← / → (d-pad left / right) turn the pages; a bookmark (click, or X / Tab
// for the next one) jumps to its section; LB / RB and Q / E stay the menu tabs. Rows with a `sel` id can be
// stepped through by the screen (up / down); showSel() turns to the page a row is on.
#pragma once
#include "ui/theme.h"
#include <functional>
#include <string>
#include <vector>

namespace dq {
class App;
namespace ui {

namespace ink {   // writing on the parchment (light pages; setBookTheme(true) swaps in light ink for the dark ones)
inline ImU32 text = col::rgb(0x3b2a1a), head = col::rgb(0x6b2a0e), muted = col::rgb(0x806b56), key = col::rgb(0x8a5208),
  good = col::rgb(0x2e6a26), bad = col::rgb(0x9a2a1a), faint = col::rgb(0x3b2a1a, 60), hilite = col::rgb(0xc8a050, 90),
  field = col::rgb(0xe8dcc0), fieldHi = col::rgb(0xf6eedb);   // a text field on the page
}
// the pages' theme: extra "bookDark" (Options > User Interface, or the moon on any open book)
void setBookTheme(bool dark);
bool bookThemeDark();
ImU32 inkOf(ImU32 brightColour);   // a colour picked for dark panels, as ink on the current pages

// the open book's picture on its own (the book inventory uses it): 292 x 180 book pixels at k screen px each
constexpr float BOOK_W = 292, BOOK_H = 180, BOOK_TW = 113, BOOK_TH = 136;   // the whole spread; one page's writing area
ImVec2 bookTextOrigin(ImVec2 org, float k, int side);   // top-left of the writing on the left (0) or right (1) page
void drawBookSpread(ImDrawList* dl, ImVec2 org, float k);
bool bookThemeButton(App& a, ImDrawList* dl, ImVec2 at, float size);   // the sun / moon: a click flips the theme
// a screen written on the open book like the inventory's (a station, the waystones, the places): the spread over the
// dimmed world, an ImGui window on the left page (dark ink, slots pressed into the paper); bookPagesRight() goes on to
// the right page; bookPagesEnd() closes both. Returns a slot size that fits nine across a page.
float bookPagesBegin(App& a, const char* id);
void bookPagesRight(const char* id);
void bookPagesEnd();
void bookPageTitle(App& a, const char* title);   // the page's heading in ink, a rule under it
// one bookmark, as BTA's guidebook has them: a cloth ribbon coming out from between the pages with a swallowtail cut at
// its end and the section's icon on it, in the book's own colours (idx picks the ribbon's). p0: the 32 x 28 box (ts
// screen px each) it lies in, its inner 9 under the page; sel: the open section (it sits further out, a gold thread
// down it); left: on the book's left edge, mirrored. Draw it before the pages so it comes out from under them.
void drawBookTab(ImDrawList* dl, ImVec2 p0, float ts, int idx, bool sel, bool hot, const std::string& icon, bool left = false);

struct BookTab { std::string name, icon; };   // icon: an item or block key

struct BookRow {
  float h = 0;                                                  // screen pixels
  std::function<void(ImDrawList*, ImVec2 at, float w)> draw;    // draws the row at its top-left, `w` the page's width
  bool keep = false;                                            // stays on the page of the row after it (headings)
  bool breakBefore = false;                                     // starts a new page
  int sel = -1;                                                 // the screen's id for a row it can select
};
using BookRows = std::vector<BookRow>;

// shapes for the marks the pixel font hasn't got
void drawStar(ImDrawList* dl, ImVec2 centre, float r, ImU32 fill, ImU32 edge = 0);
void drawTick(ImDrawList* dl, ImVec2 topLeft, float size, ImU32 c);

class Book {
public:
  // ---- the frame: begin() works out the size and takes the page-turning input; then build rows (pageW() is known),
  // draw() them, end()
  void begin(App& a, const char* id, const std::vector<BookTab>& tabs);
  void draw(const std::vector<BookRows>& sections);
  void hint(const std::string& t);   // a line under the book
  void end();
  float pageW() const { return pw; }
  float pageH() const { return ph; }
  float k() const { return kk; }      // screen pixels per pixel of the book picture

  // ---- rows ----
  void heading(BookRows& r, const std::string& t, ImU32 c = ink::head, bool newPage = false);
  void sub(BookRows& r, const std::string& t, ImU32 c = ink::head);                       // a smaller heading
  void para(BookRows& r, const std::string& t, ImU32 c = ink::text, FontId f = F_BODY, float indent = 0);
  void keyRow(BookRows& r, const std::string& key, const std::string& what);              // "Shift   Sneak ..." (two columns)
  void bullet(BookRows& r, const std::string& t, ImU32 c = ink::text, const char* mark = "\xE2\x80\xA2");
  void iconRow(BookRows& r, const std::string& icon, const std::string& title, const std::string& text, ImU32 tc = ink::head, float alpha = 1);
  void bar(BookRows& r, float frac, ImU32 c, float indent = 0);                          // a thin progress bar in ink
  void gap(BookRows& r, float px);
  void rule(BookRows& r);                                                                 // a faint line across
  static std::vector<std::string> wrap(FontId f, const std::string& t, float w);          // word-wrapped lines

  // ---- where the book is open ----
  int spread = 0;
  int tab() const { return curTab; }   // the section of the left page shown
  void goTab(int t);
  void showSel(int sel);               // turn to the page holding that row (after draw() has laid it out once)
  bool turned = false;                 // the pages turned this frame
  bool active = true;                  // false: the screen is busy (a text field): no page-turning keys
  bool keysTurn = true, keysTab = true; // ← → / the d-pad turn pages; X / Tab go to the next bookmark (a screen can keep them)
  std::vector<int> shownSel;           // the selectable rows on the open spread, in order (after draw())

private:
  App* app = nullptr;
  std::vector<BookTab> tabs_;
  float kk = 1, pw = 0, ph = 0, lineH = 0;
  ImVec2 org;                          // the book's top-left
  int curTab = 0, nSpreads = 1, wantTab = -1, wantSel = -1, wheelDir = 0;
  std::vector<int> tabSpread;          // the first spread of each section
  std::vector<std::pair<int, int>> selPage;   // sel id -> page
  std::string hint_;
};

} // namespace ui
} // namespace dq
