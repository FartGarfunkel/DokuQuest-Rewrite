// ui/treasure_map_screen.cpp — a treasure map opened (sim/treasure_maps.h: contentScreen "treasure_map" {slot, x, z}),
// written in the book like the map screen: the right page is the old map itself, the land round the X drawn as a filled
// map is (sim/treasure_maps.cpp treasureMapPicture), a red X in the middle, where you are (or an arrow at the edge the way
// you are), a compass rose and the hint inked along its foot; the left page reads the hint out and the controls.
// Mouse: wheel zooms, drag looks around. Keys: WASD look around, + / - zoom, Enter back to the X, M or Esc close.
// Controller: left stick looks around, X / Y zoom, A back to the X, B closes.
#include "ui/book_ui.h"
#include "ui/content_screens.h"
#include "ui/ui.h"
#include "ui/book.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "render/map_render.h"
#include "sim/treasure_maps.h"
#include <imgui.h>

namespace dq {

namespace {
ImU32 col32(const char* hex, int a = 255) { u32 c = hexRGBA(hex); return IM_COL32(c & 255, (c >> 8) & 255, (c >> 16) & 255, a); }
void inked(ImDrawList* dl, ImVec2 p, const std::string& text, ImU32 fill = IM_COL32(255, 243, 214, 255)) {   // centred above p, edged
  ImVec2 sz = ImGui::CalcTextSize(text.c_str());
  ImVec2 o(std::round(p.x - sz.x * 0.5f), std::round(p.y - sz.y));
  for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(ImVec2(o.x + dx, o.y + dy), IM_COL32(20, 14, 8, 216), text.c_str());
  dl->AddText(o, fill, text.c_str());
}
void markerAt(ImDrawList* dl, int cell, ImVec2 c, float sz, float rot) {
  float h = sz * 0.5f, cs = std::cos(rot), sn = std::sin(rot);
  auto P = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
  vec4 uv = mapCellUV(cell);
  dl->AddImageQuad((ImTextureID)(intptr_t)mapMarkTexture(), P(-h, -h), P(h, -h), P(h, h), P(-h, h), ImVec2(uv.x, uv.y), ImVec2(uv.z, uv.y), ImVec2(uv.z, uv.w), ImVec2(uv.x, uv.w));
}

class TreasureMapScreen : public Screen {
public:
  TreasureMapScreen(int slot, int tx, int tz) : slot(slot), tx(tx), tz(tz) {}
  const char* id() const override { return "treasure_map"; }
  bool pausesGame() const override { return false; }
  void onOpen(App& a) override { if (a.session && a.session->level) m = treasureMapPicture(*a.session->level, tx, tz); recentre(); }
  void onClose(App& a) override { if (a.session && a.session->player) sfxUi(*a.session->level, *a.session->player, "map_paper"); }

  void update(App& a, double dt) override {
    GameSession* s = a.session.get();
    if (!s || !s->player || s->player->dead) { a.popScreen(); return; }
    Input& in = a.input;
    if (in.pressed(Act::Map) || (inventoryClosePressed(a, true) && !in.padButtonPressed(3))) { a.popScreen(); return; }
    if (in.textActive) return;
    vec2 mv = in.moveAxis();
    if (mv.x || mv.y) { cx += mv.x * (float)dt * 300 / k; cz -= mv.y * (float)dt * 300 / k; }
    if (in.pressed(Act::MenuOk)) recentre();
    for (auto& pad : in.pads()) {
      auto pr = [&](int b) { return b < 17 && pad.b[b] && !pad.prevB[b]; };
      if (pr(SDL_CONTROLLER_BUTTON_X)) zoomBy(1 / 1.5f);
      if (pr(SDL_CONTROLLER_BUTTON_Y)) zoomBy(1.5f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) zoomBy(1.5f);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) zoomBy(1 / 1.5f);
  }

  void draw(App& a) override {
    using namespace ui;
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Level& lv = *s->level;
    Player& P = *s->player;
    m = treasureMapPicture(lv, tx, tz);   // (drawn finer each frame until it is done)
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    setBookTheme(a.settings.extra.value("bookDark", true));
    float bk = std::min({(H - 150 * z) / BOOK_H, (W - 120 * z) / BOOK_W, 4.f * z});
    bk = std::max(1.f, std::floor(bk * 4) / 4);
    const ImVec2 org(std::round((W - BOOK_W * bk) * 0.5f), std::round(std::max(44 * z, (H - BOOK_H * bk) * 0.5f - 12 * z)));
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##treasuremap", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!bookUiDrawing()) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 110));
    dropShadow(dl, org, org + ImVec2(BOOK_W * bk, BOOK_H * bk), 6 * z, 22 * z, 0.55f);
    drawBookSpread(dl, org, bk);
    const ImVec2 Lp = bookTextOrigin(org, bk, 0), Rp = bookTextOrigin(org, bk, 1);
    const float pw = BOOK_TW * bk, ph = BOOK_TH * bk, lh = textSize(F_BODY, "Ag").y + 2 * z, sh = textSize(F_SMALL, "Ag").y + 2 * z;
    const bool here = jsDimOf(P) == JS_OVERWORLD;
    if (here && (hint.empty() || glm::distance(vec2(P.pos.x, P.pos.z), hintAt) > 4)) { hint = treasureHint(lv, tx, tz, P.pos.x, P.pos.z); hintAt = vec2(P.pos.x, P.pos.z); }   // (read again as you walk)
    if (!here) hint.clear();
    const std::string shown = here ? hint : "The X is in the Overworld: read the map there";

    // ================= the right page: the map =================
    const float box = std::floor(std::min(pw, ph - lh * 1.6f));
    const ImVec2 o(Rp.x + std::floor((pw - box) * 0.5f), Rp.y);
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##sheet", ImVec2(box, box));
    const bool hovered = ImGui::IsItemHovered();
    const float span = (float)(MAP_N << TREASURE_MAP_SCALE);
    baseK = box / span; k = baseK * zoom;
    auto sx = [&](float x) { return o.x + box / 2 + (x - cx) * k; };
    auto sz = [&](float zz) { return o.y + box / 2 + (zz - cz) * k; };
    ImGuiIO& io = ImGui::GetIO();
    if (hovered) {
      if (io.MouseWheel != 0) zoomBy(io.MouseWheel > 0 ? 1.25f : 0.8f);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { dragging = true; dragFrom = io.MousePos; dragC = vec2(cx, cz); }
    }
    if (dragging) {
      cx = dragC.x - (io.MousePos.x - dragFrom.x) / k; cz = dragC.y - (io.MousePos.y - dragFrom.y) / k;
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging = false;
    }
    dl->PushClipRect(o, ImVec2(o.x + box, o.y + box), true);
    dl->AddRectFilled(o, ImVec2(o.x + box, o.y + box), col32("#c8b48a"));   // (old paper round the drawn land)
    if (m) {
      float x = sx((float)m->x0), y = sz((float)m->z0), w = m->span() * k;
      dl->AddImage((ImTextureID)(intptr_t)mapTexture(*m, true), ImVec2(std::round(x), std::round(y)), ImVec2(std::round(x + w), std::round(y + w)), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 238, 205, 255));
    }
    {   // the X: two thick red strokes, edged in ink
      const ImVec2 c(sx(tx + 0.5f), sz(tz + 0.5f)); const float r = std::round(11 * z);
      for (int pass = 0; pass < 2; pass++) {
        const ImU32 cl = pass == 0 ? IM_COL32(40, 14, 8, 230) : col32("#d42a1e");
        const float th = pass == 0 ? 7 * z : 4 * z;
        dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), cl, th);
        dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), cl, th);
      }
    }
    const float mk = std::round(18 * z);
    if (here) {   // you: on the sheet, or an arrow at its edge pointing your way
      const ImVec2 you(sx(P.pos.x), sz(P.pos.z)), c(o.x + box / 2, o.y + box / 2);
      const float pad = mk * 0.8f;
      if (you.x >= o.x + pad && you.y >= o.y + pad && you.x <= o.x + box - pad && you.y <= o.y + box - pad) { markerAt(dl, 0, you, mk, -P.yaw); inked(dl, ImVec2(you.x, you.y - mk * 0.6f), "You"); }
      else {
        const float dx = you.x - c.x, dy = you.y - c.y, half = box / 2 - pad;
        const float t = half / std::max(std::fabs(dx), std::fabs(dy));
        const ImVec2 e(c.x + dx * t, c.y + dy * t);
        const float ang = std::atan2(dy, dx), cs = std::cos(ang), sn = std::sin(ang);
        auto R = [&](float ax, float ay) { return ImVec2(e.x + ax * cs - ay * sn, e.y + ax * sn + ay * cs); };
        const float L2 = mk * 0.55f;
        dl->AddTriangleFilled(R(L2, 0), R(-L2, L2 * 0.75f), R(-L2, -L2 * 0.75f), IM_COL32(30, 20, 10, 220));
        dl->AddTriangleFilled(R(L2 * 0.7f, 0), R(-L2 * 0.7f, L2 * 0.5f), R(-L2 * 0.7f, -L2 * 0.5f), IM_COL32(255, 255, 255, 255));
        inked(dl, ImVec2(e.x - cs * mk * 1.1f, e.y - sn * mk * 1.1f + ImGui::GetFontSize() * 0.5f), "You");
      }
    }
    { // a compass rose in the corner: north is up
      float r = std::round(24 * z); ImVec2 c(o.x + box - r - 10 * z, o.y + r + 10 * z);
      dl->AddCircleFilled(c, r, IM_COL32(30, 20, 10, 150), 32);
      for (int i = 0; i < 8; i++) {
        float a2 = i * PI / 4, cs = std::cos(a2), sn = std::sin(a2), len = i % 2 ? 0.4f : 0.66f;
        auto R2 = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
        dl->AddTriangleFilled(R2(0, -r * len), R2(r * 0.14f, 0), R2(-r * 0.14f, 0), i == 0 ? col32("#e04a3a") : col32("#f2e6c8"));
      }
      const char* NESW[4] = {"N", "E", "S", "W"}; const float dxy[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
      for (int i = 0; i < 4; i++) inked(dl, ImVec2(c.x + dxy[i][0] * r * 0.82f, c.y + dxy[i][1] * r * 0.82f + ImGui::GetFontSize() * 0.5f), NESW[i]);
    }
    {   // the hint, inked along the foot of the sheet
      const auto lines = Book::wrap(F_SMALL, shown, box - 16 * z);
      float y = o.y + box - 8 * z - sh * lines.size();
      dl->AddRectFilled(ImVec2(o.x, y - 4 * z), ImVec2(o.x + box, o.y + box), IM_COL32(30, 20, 10, 120));
      for (auto& ln : lines) { y += sh; inked(dl, ImVec2(o.x + box / 2, y), ln); }
    }
    dl->PopClipRect();
    dl->AddRect(o - ImVec2(1, 1), ImVec2(o.x + box + 1, o.y + box + 1), withAlpha(ink::text, 0.6f), 0, 0, std::max(1.f, z));

    // ================= the left page: the hint and the controls =================
    float y = Lp.y;
    text(dl, F_H3, ImVec2(Lp.x, y), ink::head, "Treasure Map");
    y += textSize(F_H3, "Ag").y + 4 * z;
    dl->AddRectFilled(ImVec2(Lp.x, y), ImVec2(Lp.x + pw, y + std::max(1.f, z)), ink::faint);
    y += 8 * z;
    for (auto& ln : Book::wrap(F_BODY, shown, pw)) { text(dl, F_BODY, ImVec2(Lp.x, y), ink::text, ln.c_str()); y += lh; }
    y += 6 * z;
    for (auto& ln : Book::wrap(F_SMALL, "Walk to the red X and dig a few blocks down: a chest of treasure lies buried there.", pw)) { text(dl, F_SMALL, ImVec2(Lp.x, y), ink::muted, ln.c_str()); y += sh; }
    y += 8 * z;
    {
      const float bw = std::floor((pw - 6 * z) / 2), bh = std::round(22 * z);
      auto btn = [&](const char* l, float x, float yy) { ImGui::SetCursorScreenPos(ImVec2(x, yy)); return classicButton(l, ImVec2(bw, bh)); };
      if (btn("Zoom out  \xE2\x88\x92##zo", Lp.x, y)) zoomBy(1 / 1.5f);
      if (btn("Zoom in  +##zi", Lp.x + bw + 6 * z, y)) zoomBy(1.5f);
      y += bh + 5 * z;
      if (btn("The X##tx", Lp.x, y)) recentre();
      if (btn("Find me##fm", Lp.x + bw + 6 * z, y) && here) { cx = P.pos.x; cz = P.pos.z; zoom = std::min(zoom, 0.6f); k = baseK * zoom; }
      y += bh + 8 * z;
    }
    {
      const bool pad = a.input.usingPad();
      std::string how = pad ? "Left stick: look around \xC2\xB7 X / Y: zoom \xC2\xB7 A: back to the X \xC2\xB7 B: close"
                            : "Drag: look around (or WASD) \xC2\xB7 wheel or + / \xE2\x88\x92: zoom \xC2\xB7 Enter: back to the X \xC2\xB7 M or Esc: close";
      for (auto& ln : Book::wrap(F_SMALL, how, pw)) { if (y + sh > Lp.y + ph) break; text(dl, F_SMALL, ImVec2(Lp.x, y), ink::muted, ln.c_str()); y += sh; }
    }
    {
      const float s2 = std::round(18 * z);
      if (bookThemeButton(a, dl, ImVec2(org.x + BOOK_W * bk - s2, org.y + BOOK_H * bk + 6 * z), s2)) { a.settings.extra["bookDark"] = !bookThemeDark(); a.settings.save(); playClick(); }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

private:
  int slot, tx, tz;
  PaperMap* m = nullptr;
  std::string hint; vec2 hintAt{0};
  float zoom = 1, cx = 0, cz = 0, k = 1, baseK = 1;
  bool dragging = false;
  ImVec2 dragFrom; vec2 dragC{0};
  void recentre() { cx = tx + 0.5f; cz = tz + 0.5f; zoom = 1; k = baseK; }
  void zoomBy(float f) { zoom = clampv(zoom * f, 0.25f, 8.f); k = baseK * zoom; }
};
}

std::unique_ptr<Screen> makeTreasureMapScreen(int slot, int tx, int tz) { return std::make_unique<TreasureMapScreen>(slot, tx, tz); }

} // namespace dq
