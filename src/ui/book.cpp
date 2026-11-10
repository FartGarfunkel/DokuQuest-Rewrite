// ui/book.cpp — the open book (see book.h).
#include "ui/book.h"
#include "assets/gui_textures.h"
#include "client/app.h"
#include "client/session.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace dq {
namespace ui {

namespace {
// gui/book.png (a 256 sheet): the page from x 20 to 166, y 1 to 181 (its spine on the left, the paper's edge and the
// gold corners on the right); the arrows under it: forward (0,192) and backward (0,205), 23 x 13, lit at x + 23
constexpr float PX0 = 20, PX1 = 166, PY0 = 1, PY1 = 181, PW = PX1 - PX0, PH = PY1 - PY0;
constexpr float TX = 13, TXL = 20, TY = 14, TW = 113, TH = 136;   // the writing on a right page / a left (mirrored) one

GuiSprite pic(std::initializer_list<const char*> names, int sheetW = 0) {
  for (const char* n : names) {
    GuiSprite s = guiTexture(n);
    if (s.valid()) {
      if (sheetW) { s.scale = (float)s.w / sheetW; }
      return s;
    }
  }
  return {};
}
void image(ImDrawList* dl, const GuiSprite& s, ImVec2 a, ImVec2 b, float u0, float v0, float u1, float v1, ImU32 tint = IM_COL32_WHITE) {
  // u/v in the sheet's own pixels (s.w x s.h is the whole picture here)
  float sw = (float)s.w, sh = (float)s.h;
  dl->AddImage((ImTextureID)(intptr_t)s.tex, a, b, ImVec2(u0 / sw, v0 / sh), ImVec2(u1 / sw, v1 / sh), tint);
}
bool hovered(ImVec2 a, ImVec2 b) { ImVec2 m = ImGui::GetIO().MousePos; return m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y; }
}

static bool g_dark = false;
void setBookTheme(bool dark) {
  g_dark = dark;
  if (dark) {
    ink::text = col::rgb(0xe6dac2); ink::head = col::rgb(0xf2c46e); ink::muted = col::rgb(0xa89880); ink::key = col::rgb(0xe8b452);
    ink::good = col::rgb(0x9bd34a); ink::bad = col::rgb(0xff8a70); ink::faint = col::rgb(0xe6dac2, 46); ink::hilite = col::rgb(0xc8a050, 60);
    ink::field = col::rgb(0x2a211a); ink::fieldHi = col::rgb(0x3a2e24);
  } else {
    ink::text = col::rgb(0x3b2a1a); ink::head = col::rgb(0x6b2a0e); ink::muted = col::rgb(0x806b56); ink::key = col::rgb(0x8a5208);
    ink::good = col::rgb(0x2e6a26); ink::bad = col::rgb(0x9a2a1a); ink::faint = col::rgb(0x3b2a1a, 60); ink::hilite = col::rgb(0xc8a050, 90);
    ink::field = col::rgb(0xe8dcc0); ink::fieldHi = col::rgb(0xf6eedb);
  }
}
bool bookThemeDark() { return g_dark; }
ImU32 inkOf(ImU32 c) {
  if (g_dark) return c;
  float k = 0.55f;
  return IM_COL32((int)(((c >> IM_COL32_R_SHIFT) & 255) * k), (int)(((c >> IM_COL32_G_SHIFT) & 255) * k), (int)(((c >> IM_COL32_B_SHIFT) & 255) * k), (c >> IM_COL32_A_SHIFT) & 255);
}

ImVec2 bookTextOrigin(ImVec2 o, float k, int side) { return side == 0 ? ImVec2(o.x + TXL * k, o.y + TY * k) : ImVec2(o.x + (PW + TX) * k, o.y + TY * k); }

// the open book the station screens are written in (the inventory's look, ui/book.h): the spread over the dimmed world,
// the station on the left page and your bag on the right, dark ink on the paper and the slots pressed into it. Returns
// the slot size; bookPageRight() moves on to the right page, bookEnd() closes it.
namespace { float g_bookPW = 0, g_bookPH = 0; ImVec2 g_bookR; }
float bookPagesBegin(App& a, const char* id) {
  const float z = Z(), W = (float)a.width, H = (float)a.height;
  setBookTheme(a.settings.extra.value("bookDark", false));
  float k = std::min({(H - 110 * z) / BOOK_H, (W - 170 * z) / BOOK_W, 4.6f * z});
  k = std::max(1.f, std::floor(k * 4) / 4);
  const ImVec2 org(std::round((W - BOOK_W * k) * 0.5f), std::round(std::max(44 * z, (H - BOOK_H * k) * 0.5f - 4 * z)));
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 3, 2, 153));
  drawBookSpread(bg, org, k);
  const ImVec2 L = bookTextOrigin(org, k, 0);
  g_bookR = bookTextOrigin(org, k, 1); g_bookPW = BOOK_TW * k; g_bookPH = BOOK_TH * k;
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ink::text));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(ink::muted));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.20f, 0.10f, 0.22f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.38f, 0.16f, 0.35f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.45f, 0.18f, 0.5f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ink::faint));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertU32ToFloat4(ink::faint));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2 * z);
  ImGui::SetNextWindowPos(L); ImGui::SetNextWindowSize(ImVec2(g_bookPW, g_bookPH));
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  return std::floor((g_bookPW - 8 * 3 - 2) / 9.f);   // (nine across with the bag grid's 3 px gaps)
}
void bookPagesRight(const char* id) {
  ImGui::End();
  ImGui::SetNextWindowPos(g_bookR); ImGui::SetNextWindowSize(ImVec2(g_bookPW, g_bookPH));
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}
void bookPagesEnd() { ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(7); }
void bookPageTitle(App& a, const char* t) {   // the page's heading, a rule under it
  ImGui::PushFont(a.fontUiBig); ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ink::head)); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); ImGui::PopFont();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + std::max(1.f, Z())), ink::faint);
  ImGui::Dummy(ImVec2(1, 6 * Z()));
}

void drawBookSpread(ImDrawList* dl, ImVec2 o, float k) {
  const GuiSprite book = pic({"gui/book.png"});
  const float bw = PW * 2 * k, bh = PH * k;
  const ImVec2 mid(o.x + PW * k, o.y);
  // dark pages: the cover a shade darker, the paper under a deep sepia wash
  const ImU32 tint = g_dark ? IM_COL32(150, 138, 126, 255) : IM_COL32_WHITE;
  if (book.valid() && book.w >= 166) {
    const float s = (float)book.w / 256.f;
    image(dl, book, o, ImVec2(mid.x, o.y + bh), PX1 * s, PY0 * s, PX0 * s, PY1 * s, tint);
    image(dl, book, mid, ImVec2(o.x + bw, o.y + bh), PX0 * s, PY0 * s, PX1 * s, PY1 * s, tint);
  } else {
    dl->AddRectFilled(o, ImVec2(o.x + bw, o.y + bh), col::rgb(0x6a2e14), 6 * k);
    dl->AddRectFilled(ImVec2(o.x + 6 * k, o.y + 6 * k), ImVec2(mid.x - 1 * k, o.y + bh - 7 * k), col::rgb(0xf3ead6));
    dl->AddRectFilled(ImVec2(mid.x + 1 * k, o.y + 6 * k), ImVec2(o.x + bw - 6 * k, o.y + bh - 7 * k), col::rgb(0xf3ead6));
    dl->AddRectFilled(ImVec2(mid.x - 1 * k, o.y + 4 * k), ImVec2(mid.x + 1 * k, o.y + bh - 5 * k), col::rgb(0x3c1a0a));
  }
  if (g_dark) {   // the paper of each page (inside the page's deckled edge)
    dl->AddRectFilled(ImVec2(o.x + 14 * k, o.y + 7 * k), ImVec2(mid.x - 6 * k, o.y + 173 * k), IM_COL32(24, 19, 15, 228), 4 * k);
    dl->AddRectFilled(ImVec2(mid.x + 6 * k, o.y + 7 * k), ImVec2(o.x + bw - 14 * k, o.y + 173 * k), IM_COL32(24, 19, 15, 228), 4 * k);
  }
  // the fold down the middle
  for (int i = 0; i < 4; i++) dl->AddRectFilled(ImVec2(mid.x - (4 - i) * k, o.y + 7 * k), ImVec2(mid.x + (4 - i) * k, o.y + bh - 8 * k), g_dark ? IM_COL32(0, 0, 0, 18 + i * 10) : IM_COL32(60, 30, 10, 10 + i * 6));
}

bool bookThemeButton(App& a, ImDrawList* dl, ImVec2 p, float s) {
  (void)a;
  ImVec2 p1(p.x + s, p.y + s), c(p.x + s * 0.5f, p.y + s * 0.5f);
  bool hot = hovered(p, p1);
  dl->AddRectFilled(p, p1, hot ? col::rgb(0x4a3a2c) : col::rgb(0x2b2017), s * 0.2f);
  dl->AddRect(p, p1, col::rgb(0x8a6a48), s * 0.2f, 0, std::max(1.f, s * 0.05f));
  if (g_dark) {   // a sun: switch to light pages
    dl->AddCircleFilled(c, s * 0.17f, col::rgb(0xffd860));
    for (int i = 0; i < 8; i++) { float an = i * 0.785398f; dl->AddLine(ImVec2(c.x + std::cos(an) * s * 0.24f, c.y + std::sin(an) * s * 0.24f), ImVec2(c.x + std::cos(an) * s * 0.34f, c.y + std::sin(an) * s * 0.34f), col::rgb(0xffd860), std::max(1.f, s * 0.06f)); }
  } else {        // a moon: switch to dark pages
    dl->AddCircleFilled(c, s * 0.28f, col::rgb(0xe8e0c8));
    dl->AddCircleFilled(ImVec2(c.x + s * 0.13f, c.y - s * 0.1f), s * 0.24f, hot ? col::rgb(0x4a3a2c) : col::rgb(0x2b2017));
  }
  bool click = hot && ImGui::IsMouseClicked(0);
  if (hot) {
    const char* tip = g_dark ? "Light pages" : "Dark pages";
    ImVec2 m = ImGui::GetIO().MousePos, ts = textSize(F_MID, tip); float z = Z();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(ImVec2(m.x + 14 * z, m.y - 4 * z), ImVec2(m.x + 26 * z + ts.x, m.y + 4 * z + ts.y), col::rgb(0x1a120c, 235), 3 * z);
    text(fg, F_MID, ImVec2(m.x + 20 * z, m.y), col::rgb(0xf0e0b8), tip);
  }
  return click;
}

void drawBookTab(ImDrawList* dl, ImVec2 p0, float ts, int idx, bool sel, bool hot, const std::string& icon, bool left) {
  // the ribbons' cloth: deep, slightly dusty colours that sit with the leather and the gold
  static const u32 CLOTH[] = {0x9b2d2a, 0x2f5b8a, 0x3f7a3a, 0xb8842a, 0x6d3f8a, 0x2f7f7a, 0xa8552a, 0x55606e, 0x8a6a3a, 0x7a2f5a, 0x4f6f2a, 0x3a4a8a};
  const u32 c = CLOTH[((idx % 12) + 12) % 12];
  auto shade = [&](float k, int a = 255) {
    auto ch = [&](int sh) { return (int)std::min(255.f, ((c >> sh) & 255) * k); };
    return IM_COL32(ch(16), ch(8), ch(0), a);
  };
  const float W = 32 * ts, bandH = 20 * ts, y0 = std::round(p0.y + (28 * ts - bandH) * 0.5f), y1 = y0 + bandH, ym = (y0 + y1) * 0.5f;
  const float notch = 6 * ts, grow = hot && !sel ? 2 * ts : 0;
  const float xi = left ? p0.x + W : p0.x, xo = left ? p0.x - grow : p0.x + W + grow, d = left ? -1.f : 1.f;   // inner end (under the page), outer end
  const float base = sel ? 1.12f : hot ? 1.06f : 0.92f;
  ImVec2 pts[5] = {ImVec2(xi, y0), ImVec2(xo, y0), ImVec2(xo - d * notch, ym), ImVec2(xo, y1), ImVec2(xi, y1)};
  if (left) std::reverse(pts, pts + 5);   // (the concave fill wants the points clockwise on screen: mirrored, they ran the other way and the cut filled in)
  dl->AddConcavePolyFilled(pts, 5, shade(base));
  // the weave: faint threads along it (kept off the cut end)
  const float xa = std::min(xi, xo - d * notch), xb = std::max(xi, xo - d * notch);
  for (float y = y0 + 2 * ts; y < y1 - ts; y += 3 * ts) dl->AddLine(ImVec2(xa, y), ImVec2(xb, y), shade(base * 0.86f, 120), std::max(1.f, ts * 0.5f));
  // light along the top, shade along the bottom, and a stitch down each side (gold on the open one)
  dl->AddLine(ImVec2(xa, y0 + ts * 0.6f), ImVec2(xb + (left ? 0 : notch * 0.2f), y0 + ts * 0.6f), shade(base * 1.35f, 200), std::max(1.f, ts));
  dl->AddLine(ImVec2(xa, y1 - ts * 0.6f), ImVec2(xb, y1 - ts * 0.6f), shade(base * 0.55f, 220), std::max(1.f, ts));
  const ImU32 thread = sel ? col::rgb(0xe8c060) : shade(base * 1.5f, 150);
  for (float x = xa + 2 * ts; x < xb - notch * 0.6f; x += 4 * ts) {
    dl->AddLine(ImVec2(x, y0 + 2.6f * ts), ImVec2(x + 2 * ts, y0 + 2.6f * ts), thread, std::max(1.f, ts * 0.6f));
    dl->AddLine(ImVec2(x, y1 - 2.6f * ts), ImVec2(x + 2 * ts, y1 - 2.6f * ts), thread, std::max(1.f, ts * 0.6f));
  }
  dl->AddPolyline(pts, 5, IM_COL32(26, 14, 8, 230), ImDrawFlags_Closed, std::max(1.f, ts * 0.8f));
  // where it comes out from under the page, the page's shadow on it
  const float edge = left ? p0.x + W - 9 * ts : p0.x + 9 * ts;
  if (left) dl->AddRectFilledMultiColor(ImVec2(edge - 4 * ts, y0), ImVec2(edge, y1), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 0));
  else dl->AddRectFilledMultiColor(ImVec2(edge, y0), ImVec2(edge + 4 * ts, y1), IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 110));
  // the section's icon on the part that shows
  const float is = std::min(16 * ts, bandH * 0.82f);
  const float shown0 = left ? xo + notch * 0.5f : edge, shown1 = left ? edge : xo - notch * 0.5f;
  drawIcon(dl, icon, ImVec2(std::round((shown0 + shown1 - is) * 0.5f), std::round(ym - is * 0.5f)), is, sel || hot ? 1.f : 0.88f);
}

std::vector<std::string> Book::wrap(FontId f, const std::string& t, float w) {
  std::vector<std::string> out;
  size_t s = 0;
  while (s <= t.size()) {
    size_t e = t.find('\n', s); if (e == std::string::npos) e = t.size();
    std::string para = t.substr(s, e - s), line;
    size_t i = 0;
    if (para.empty()) out.push_back("");
    while (i < para.size()) {
      size_t j = para.find(' ', i); if (j == std::string::npos) j = para.size();
      std::string word = para.substr(i, j - i);
      std::string tryL = line.empty() ? word : line + " " + word;
      if (!line.empty() && textSize(f, tryL.c_str()).x > w) { out.push_back(line); line = word; }
      else line = tryL;
      i = j + 1;
    }
    if (!line.empty()) out.push_back(line);
    s = e + 1;
  }
  return out;
}

// ---- rows ----
void Book::heading(BookRows& r, const std::string& t, ImU32 c, bool newPage) {
  float h = textSize(F_H3, "Ag").y;
  BookRow row; row.keep = true; row.breakBefore = newPage; row.h = h + 8 * Z();
  row.draw = [t, c, h](ImDrawList* dl, ImVec2 p, float w) {
    text(dl, F_H3, p, c, t.c_str());
    dl->AddRectFilled(ImVec2(p.x, p.y + h + 1 * Z()), ImVec2(p.x + w, p.y + h + 2 * Z()), ink::faint);
  };
  r.push_back(std::move(row));
}
void Book::sub(BookRows& r, const std::string& t, ImU32 c) {
  BookRow row; row.keep = true; row.h = textSize(F_BTN, "Ag").y + 5 * Z();
  row.draw = [t, c](ImDrawList* dl, ImVec2 p, float) { text(dl, F_BTN, ImVec2(p.x, p.y + 3 * Z()), c, t.c_str()); };
  r.push_back(std::move(row));
}
void Book::para(BookRows& r, const std::string& t, ImU32 c, FontId f, float indent) {
  float lh = textSize(f, "Ag").y + 2 * Z();
  for (auto& ln : wrap(f, t, pw - indent)) {
    BookRow row; row.h = lh;
    row.draw = [ln, c, f, indent](ImDrawList* dl, ImVec2 p, float) { text(dl, f, ImVec2(p.x + indent, p.y), c, ln.c_str()); };
    r.push_back(std::move(row));
  }
  gap(r, 5 * Z());
}
void Book::keyRow(BookRows& r, const std::string& key, const std::string& what) {
  float kw = std::round(pw * 0.32f), gapx = 8 * Z();
  auto kl = wrap(F_BODY, key, kw), wl = wrap(F_BODY, what, pw - kw - gapx);
  float lh = textSize(F_BODY, "Ag").y + 2 * Z();
  BookRow row; row.h = std::max(kl.size(), wl.size()) * lh + 3 * Z();
  row.draw = [kl, wl, kw, gapx, lh](ImDrawList* dl, ImVec2 p, float) {
    for (size_t i = 0; i < kl.size(); i++) text(dl, F_BODY, ImVec2(p.x, p.y + i * lh), ink::key, kl[i].c_str());
    for (size_t i = 0; i < wl.size(); i++) text(dl, F_BODY, ImVec2(p.x + kw + gapx, p.y + i * lh), ink::text, wl[i].c_str());
  };
  r.push_back(std::move(row));
}
void Book::bullet(BookRows& r, const std::string& t, ImU32 c, const char* mark) {
  float ind = textSize(F_BODY, mark).x + 6 * Z(), lh = textSize(F_BODY, "Ag").y + 2 * Z();
  auto lines = wrap(F_BODY, t, pw - ind);
  for (size_t i = 0; i < lines.size(); i++) {
    BookRow row; row.h = lh; std::string ln = lines[i], m = i == 0 ? mark : "";
    row.draw = [ln, m, c, ind](ImDrawList* dl, ImVec2 p, float) { if (!m.empty()) text(dl, F_BODY, p, ink::key, m.c_str()); text(dl, F_BODY, ImVec2(p.x + ind, p.y), c, ln.c_str()); };
    r.push_back(std::move(row));
  }
  gap(r, 3 * Z());
}
void Book::iconRow(BookRows& r, const std::string& icon, const std::string& title, const std::string& txt, ImU32 tc, float alpha) {
  float lh = textSize(F_BODY, "Ag").y + 2 * Z(), is = std::round(lh * 1.8f), ind = is + 8 * Z();
  auto lines = txt.empty() ? std::vector<std::string>{} : wrap(F_BODY, txt, pw - ind);
  BookRow row; row.h = std::max(is, (1 + lines.size()) * lh) + 6 * Z();
  row.draw = [icon, title, lines, tc, alpha, lh, is, ind](ImDrawList* dl, ImVec2 p, float) {
    if (!icon.empty()) drawIcon(dl, icon, p, is, alpha);
    text(dl, F_BODY, ImVec2(p.x + ind, p.y), withAlpha(tc, std::max(0.55f, alpha)), title.c_str());
    for (size_t i = 0; i < lines.size(); i++) text(dl, F_BODY, ImVec2(p.x + ind, p.y + (i + 1) * lh), withAlpha(ink::muted, std::max(0.6f, alpha)), lines[i].c_str());
  };
  r.push_back(std::move(row));
}
void Book::bar(BookRows& r, float frac, ImU32 c, float indent) {
  BookRow row; row.h = 9 * Z();
  row.draw = [frac, c, indent](ImDrawList* dl, ImVec2 p, float w) {
    ImVec2 a(p.x + indent, p.y + 1 * Z()), b(p.x + w, p.y + 6 * Z());
    dl->AddRectFilled(a, b, ink::faint);
    dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * std::clamp(frac, 0.f, 1.f), b.y), c);
    dl->AddRect(a, b, withAlpha(ink::text, 0.55f), 0, 0, std::max(1.f, Z()));
  };
  r.push_back(std::move(row));
}
void drawStar(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 edge) {
  ImVec2 pts[10];
  for (int i = 0; i < 10; i++) { float a = -1.5707963f + i * 0.6283185f, rr = i % 2 ? r * 0.45f : r; pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr); }
  if (fill) dl->AddConcavePolyFilled(pts, 10, fill);
  if (edge) dl->AddPolyline(pts, 10, edge, ImDrawFlags_Closed, std::max(1.f, r * 0.16f));
}
void drawTick(ImDrawList* dl, ImVec2 p, float s, ImU32 c) {
  ImVec2 pts[3] = {ImVec2(p.x, p.y + s * 0.55f), ImVec2(p.x + s * 0.38f, p.y + s * 0.9f), ImVec2(p.x + s, p.y + s * 0.15f)};
  dl->AddPolyline(pts, 3, c, 0, std::max(1.5f, s * 0.18f));
}

void Book::gap(BookRows& r, float px) { BookRow row; row.h = px; r.push_back(std::move(row)); }
void Book::rule(BookRows& r) {
  BookRow row; row.h = 9 * Z();
  row.draw = [](ImDrawList* dl, ImVec2 p, float w) { dl->AddRectFilled(ImVec2(p.x + w * 0.2f, p.y + 4 * Z()), ImVec2(p.x + w * 0.8f, p.y + 5 * Z()), ink::faint); };
  r.push_back(std::move(row));
}

// ---- the frame ----
void Book::goTab(int t) { wantTab = t; }
void Book::showSel(int sel) { wantSel = sel; }

void Book::begin(App& a, const char* id, const std::vector<BookTab>& tabs) {
  app = &a; tabs_ = tabs; turned = false;
  setBookTheme(a.settings.extra.value("bookDark", false));
  const float z = Z(), W = (float)a.width, H = (float)a.height;
  // as big as fits between the menu tabs on top and the hint underneath, a little room for the bookmarks on the right
  float k = std::min({(H - 150 * z) / PH, (W - 120 * z) / (PW * 2), 4.f * z});
  kk = std::max(1.f, std::floor(k * 4) / 4);
  float bw = PW * 2 * kk, bh = PH * kk;
  org = ImVec2(std::round((W - bw) * 0.5f - 14 * z), std::round(std::max(44 * z, (H - bh) * 0.5f - 12 * z)));
  pw = TW * kk; ph = TH * kk;
  if (active && !a.input.textActive) {
    Input& in = a.input;
    if ((keysTurn && in.pressed(Act::MenuLeft)) || in.keyPressed(SDL_SCANCODE_PAGEUP)) wheelDir -= 1;
    if ((keysTurn && in.pressed(Act::MenuRight)) || in.keyPressed(SDL_SCANCODE_PAGEDOWN)) wheelDir += 1;
    if (((keysTab && in.pressed(Act::MenuX)) || in.keyPressed(SDL_SCANCODE_TAB)) && !tabs_.empty()) wantTab = (curTab + 1) % (int)tabs_.size();
  }
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(W, H));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
  if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) wheelDir += ImGui::GetIO().MouseWheel < 0 ? 1 : -1;
}

void Book::draw(const std::vector<BookRows>& sections) {
  App& a = *app;
  const float z = Z();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // ---- lay the rows onto pages ----
  struct Pg { std::vector<const BookRow*> rows; int sec = 0; };
  std::vector<Pg> pages;
  std::vector<int> secPage(sections.size(), 0);
  selPage.clear();
  for (size_t s = 0; s < sections.size(); s++) {
    pages.push_back({{}, (int)s});   // a section starts on a fresh page (its bookmark opens the spread it's on)
    secPage[s] = (int)pages.size() - 1;
    const BookRows& rows = sections[s];
    float y = 0;
    for (size_t i = 0; i < rows.size(); i++) {
      const BookRow& r = rows[i];
      float need = r.h;
      for (size_t j = i; j + 1 < rows.size() && rows[j].keep; j++) need += rows[j + 1].h;   // a heading keeps its first lines
      need = std::min(need, ph);
      if (y > 0 && (r.breakBefore || y + need > ph)) { pages.push_back({{}, (int)s}); y = 0; }
      if (y == 0 && !r.draw && r.sel < 0) continue;   // no gap at the top of a page
      pages.back().rows.push_back(&r);
      if (r.sel >= 0) selPage.push_back({r.sel, (int)pages.size() - 1});
      y += r.h;
    }
  }
  if (pages.empty()) pages.push_back({});
  nSpreads = std::max(1, (int)(pages.size() + 1) / 2);
  tabSpread.assign(sections.size(), 0);
  for (size_t s = 0; s < sections.size(); s++) tabSpread[s] = secPage[s] / 2;
  // ---- where to open ----
  int was = spread;
  if (wantTab >= 0 && wantTab < (int)tabSpread.size()) spread = tabSpread[wantTab];
  if (wantSel >= 0) for (auto& [id, pg] : selPage) if (id == wantSel) { spread = pg / 2; break; }
  spread += wheelDir;
  wantTab = wantSel = -1; wheelDir = 0;
  spread = std::clamp(spread, 0, nSpreads - 1);
  if (spread != was) { turned = true; playClick(); }
  curTab = pages[std::min((size_t)spread * 2, pages.size() - 1)].sec;

  // ---- the picture ----
  const GuiSprite book = pic({"gui/book.png"});
  const float k = kk, bw = PW * 2 * k, bh = PH * k;
  const ImVec2 o = org, mid(o.x + PW * k, o.y);
  dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)a.width, (float)a.height), IM_COL32(0, 0, 0, a.session ? 110 : 60));
  dropShadow(dl, o, ImVec2(o.x + bw, o.y + bh), 6 * z, 22 * z, 0.55f);
  // bookmarks: the chosen one in front of the pages, the rest behind them
  const int nt = (int)tabs_.size();
  const float ts = std::min(k * 0.8f, (bh - 16 * k) / std::max(1, nt) / 30.f), tw = 32 * ts, th = 28 * ts;
  auto tabRect = [&](int i, ImVec2& p0, ImVec2& p1) { p0 = ImVec2(o.x + bw - 9 * ts, o.y + 8 * k + i * (th + 2 * ts)); p1 = ImVec2(p0.x + tw, p0.y + th); };
  auto drawTab = [&](int i, bool sel, bool hot) {
    ImVec2 p0, p1; tabRect(i, p0, p1);
    if (sel) p0.x += 3 * ts;   // the open section's ribbon sits further out
    drawBookTab(dl, p0, ts, i, sel, hot, tabs_[i].icon, false);
  };
  int hotTab = -1;
  for (int i = 0; i < nt; i++) { ImVec2 p0, p1; tabRect(i, p0, p1); if (hovered(ImVec2(o.x + bw, p0.y), ImVec2(p1.x + 3 * ts, p1.y))) hotTab = i; }
  for (int i = 0; i < nt; i++) drawTab(i, i == curTab, i == hotTab && i != curTab);   // (all under the pages: they come out from between them)
  drawBookSpread(dl, o, k);
  // ---- the writing ----
  shownSel.clear();
  for (int side = 0; side < 2; side++) {
    size_t pi = (size_t)spread * 2 + side;
    ImVec2 ta(side == 0 ? o.x + TXL * k : mid.x + TX * k, o.y + TY * k);
    if (pi >= pages.size()) continue;
    dl->PushClipRect(ImVec2(ta.x - 2 * k, ta.y - 2 * k), ImVec2(ta.x + pw + 2 * k, ta.y + ph + 1 * k), true);
    float y = ta.y;
    int ri = 0;
    for (const BookRow* r : pages[pi].rows) {   // (ids by place on the page: the rows are made anew every frame)
      if (r->draw) { ImGui::PushID(r->sel >= 0 ? 1000000 + r->sel : (int)(pi * 4096 + ri)); r->draw(dl, ImVec2(ta.x, std::round(y)), pw); ImGui::PopID(); }
      if (r->sel >= 0) shownSel.push_back(r->sel);
      y += r->h; ri++;
    }
    dl->PopClipRect();
    if (!pages[pi].rows.empty()) {
      std::string n = std::to_string(pi + 1);
      ImVec2 ns = textSize(F_SMALL, n.c_str());
      text(dl, F_SMALL, ImVec2(std::round(ta.x + (pw - ns.x) * 0.5f), std::round(o.y + 158 * k)), ink::muted, n.c_str());
    }
  }
  // ---- the arrows ----
  auto arrow = [&](bool fwd, ImVec2 p) {
    ImVec2 p1(p.x + 23 * k, p.y + 13 * k);
    ImGui::SetCursorScreenPos(p);
    bool click = ImGui::InvisibleButton(fwd ? "##bookfwd" : "##bookback", ImVec2(23 * k, 13 * k));
    bool hot = ImGui::IsItemHovered();
    if (book.valid() && book.w >= 166) { const float s = (float)book.w / 256.f; image(dl, book, p, p1, (hot ? 23 : 0) * s, (fwd ? 192 : 205) * s, (hot ? 46 : 23) * s, (fwd ? 205 : 218) * s); }
    else {
      ImU32 c = hot ? ink::key : ink::muted; float cx = (p.x + p1.x) * 0.5f, cy = (p.y + p1.y) * 0.5f, d = fwd ? 1.f : -1.f;
      dl->AddTriangleFilled(ImVec2(cx + d * 7 * k, cy), ImVec2(cx - d * 5 * k, cy - 5 * k), ImVec2(cx - d * 5 * k, cy + 5 * k), c);
    }
    return click;
  };
  if (spread > 0 && arrow(false, ImVec2(o.x + 18 * k, o.y + 155 * k))) { spread--; turned = true; playClick(); }
  if (spread < nSpreads - 1 && arrow(true, ImVec2(o.x + bw - 41 * k, o.y + 155 * k))) { spread++; turned = true; playClick(); }
  // the bookmarks take clicks; their names show under the pointer
  for (int i = 0; i < nt; i++) {
    ImVec2 p0, p1; tabRect(i, p0, p1);
    ImGui::SetCursorScreenPos(ImVec2(o.x + bw, p0.y));
    if (ImGui::InvisibleButton(("##booktab" + std::to_string(i)).c_str(), ImVec2(std::max(4.f, p1.x + 3 * ts - (o.x + bw)), th))) { wantTab = i; playClick(); }
    if (ImGui::IsItemHovered()) {
      ImVec2 m = ImGui::GetIO().MousePos, s = textSize(F_MID, tabs_[i].name.c_str());
      ImVec2 b0(m.x + 14 * z, m.y - 4 * z), b1(b0.x + s.x + 12 * z, b0.y + s.y + 8 * z);
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      fg->AddRectFilled(b0, b1, col::rgb(0x1a120c, 235), 3 * z); fg->AddRect(b0, b1, col::rgb(0x6b5a3a), 3 * z);
      text(fg, F_MID, ImVec2(b0.x + 6 * z, b0.y + 4 * z), col::rgb(0xf0e0b8), tabs_[i].name.c_str());
    }
  }
  if (wantTab >= 0 && wantTab < (int)tabSpread.size()) { spread = tabSpread[wantTab]; wantTab = -1; turned = true; }
  {
    const float s = std::round(18 * z);
    if (bookThemeButton(a, dl, ImVec2(o.x + bw + 4 * z, o.y + bh - s - 6 * k), s)) {
      a.settings.extra["bookDark"] = !bookThemeDark(); a.settings.save(); setBookTheme(!bookThemeDark()); playClick();
    }
  }
}

void Book::hint(const std::string& t) { hint_ = t; }

void Book::end() {
  if (!hint_.empty() && app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float z = Z(), y = org.y + PH * kk + 10 * z;
    ImVec2 s = textSize(F_MID, hint_.c_str(), (float)app->width - 40 * z);
    textShadow(dl, F_MID, ImVec2(std::round((app->width - s.x) * 0.5f), std::round(std::min(y, (float)app->height - s.y - 6 * z))), col::rgb(0xd8c9a8), hint_.c_str(), ImVec2(1, 1) * z, col::black, (float)app->width - 40 * z);
  }
  hint_.clear();
  ImGui::End();
  ImGui::PopStyleVar(2);
}

} // namespace ui
} // namespace dq
