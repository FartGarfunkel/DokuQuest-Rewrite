// ui/book_ui.h — the menus in the journal: in first person, every menu (the inventory, the journal, the map, skills, the
// pause menu...) is drawn onto the pages of the book you hold open (render/item_layers.cpp, render/book_model.h) instead
// of over the screen. The screens draw as ever, into a picture of their own (an ImGui context of its own, as split
// screen's viewports have: client/split.cpp); the mouse is carried from where you point on the pages.
#pragma once
#include <functional>
#include "common/common.h"
#include <imgui.h>

namespace dq {
class App;
class Screen;

bool bookUiOn(App& a);                 // this frame's menus go into the book (first person, a menu that belongs in it, the option on)
bool isBookScreen(const Screen* s);
// draws the screens into the book's picture (App::frameLoop, in place of drawing them over the world)
void bookUiRender(App& a, bool station = false);
// at a station (ui/cinematic.h, seamless first person): the journal in your off hand, open at your bag — its pages drawn
// by bookUiRender(a, true) after the scene's own UI; the scene asks for it each frame it shows the bag (padCur: the pad's
// cursor in the bag's order, -1 none), and leaves the mouse alone while it's over the book
bool bookUiStationOn(App& a);
void bookUiStationBag(int padCur);
bool bookUiMouseOverBook();
// a tooltip that may reach past the pages (beside a ribbon): in the book it's shown on the screen at the pointer, not
// on the page picture (it was cut off there); outside the book, false (draw it as ever)
bool bookUiScreenTip(const std::string& text);
void bookUiDrawScreenTip();
// something drawn on the window itself after the book (an item's tooltip: on the pages it was cut off at their edge and
// drawn twice where it crossed it). Called from the book's drawing; false when not drawing the book (draw it yourself)
bool bookUiScreenDraw(std::function<void()> f);   // (App::frameLoop, after the book)
void bookUiDebugQuad(ImDrawList* dl);   // (dev: DQ_DEV_QUAD)
bool bookUiPointOverBook(vec2 p);   // a window point behind the off-hand journal (the station's marks there are hidden)
// where the picture's crop is on the window (top left, top right, bottom right, bottom left): a book held at an angle
void bookUiSetQuad(const vec2 q[4]);
// the book exactly: its frame and the hand view's projection (render/journal_model.h's frame: spread w x h, its pages'
// surface at top, each half turned up by tilt) — the mouse is cast onto the pages through them
void bookUiSetBook(const mat4& proj, const mat4& frame, float w, float h, float top, float tilt);
unsigned bookUiTexture();
bool bookUiDrawing();
// a screen's drawn book spread (ui/book.cpp drawBookSpread): in the hands it is the physical book, the picture cropped to it
void bookUiSpread(ImVec2 a, ImVec2 b);
// the part of the picture that is the book (u0, v0, u1, v1, v from the top), and whether a drawn book was there
bool bookUiCrop(vec4& uv);
vec2 bookUiSize();                     // the picture's size (logical pixels)                  // the screens are drawing into the book now (they leave out their own drawn book)              // the pages' picture (GL texture; 0 none yet)
// where the open pages are on the window (logical pixels, top left and bottom right), set by the hand view each frame
void bookUiSetRect(vec2 lo, vec2 hi);
// a bookmark ribbon's showing part, as drawn (ui/book.cpp drawBookTab): in the hands it is cloth of its own
void bookUiRibbon(ImVec2 a, ImVec2 b);
// the journal's picture for the hand view: the texture, the crop, the drawn book and the ribbons in it (false: none)
bool bookUiJournal(struct JournalPic& p);
std::string bookUiLayout();
// keeps a copy of the pages as they are now (the page about to be turned over: render/journal_model.h drawJournalLeaf)
bool bookUiSnapshot(struct JournalPic& p);   // the same, for the others (render/journal_model.h journalLayoutString)
bool bookUiDark(App& a);               // dark pages (Options "Dark Book Pages", the same as the book screens)

} // namespace dq
