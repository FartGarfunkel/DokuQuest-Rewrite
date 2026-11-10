// ui/map_screen.cpp — looking at a map closely (game/maps.js MapView), written in the book like the other menu tabs
// (ui/book.h): the right page is the map (every map of the same place and scale laid out together, to drag about,
// zoom and pin); the left page lists the maps you carry (pick one, or step through them), with the zoom buttons,
// Find me, and the controls.
// Mouse: wheel zooms about the pointer, drag looks around, a click sets a pin (again: clears it), a map in the list
// shows it. Keys: WASD look around, +/- zoom, up/down the next map, Enter finds you. Controller: left stick looks
// around, X / Y zoom, d-pad up / down the next map, A finds you. M, Esc or B close; LB / RB (Q / E) change tab.
#include "ui/content_screens.h"
#include "ui/ui.h"
#include "ui/book.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "render/map_render.h"
#include "sim/maps.h"
#include <imgui.h>

namespace dq {

namespace {
ImU32 col32(const char* hex, int a = 255) { u32 c = hexRGBA(hex); return IM_COL32(c & 255, (c >> 8) & 255, (c >> 16) & 255, a); }
void tag(ImDrawList* dl, ImVec2 p, const std::string& text, ImU32 fill = IM_COL32(255, 243, 214, 255)) {
  ImVec2 sz = ImGui::CalcTextSize(text.c_str());
  ImVec2 o(p.x - sz.x * 0.5f, p.y - sz.y);
  for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(ImVec2(o.x + dx, o.y + dy), IM_COL32(20, 14, 8, 216), text.c_str());
  dl->AddText(o, fill, text.c_str());
}
// a marker from the strip, centred at c, sz across, turned by rot (radians, clockwise on screen)
void markerAt(ImDrawList* dl, int cell, ImVec2 c, float sz, float rot) {
  float h = sz * 0.5f, cs = std::cos(rot), sn = std::sin(rot);
  auto P = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
  vec4 uv = mapCellUV(cell);
  dl->AddImageQuad((ImTextureID)(intptr_t)mapMarkTexture(), P(-h, -h), P(h, -h), P(h, h), P(-h, h), ImVec2(uv.x, uv.y), ImVec2(uv.z, uv.y), ImVec2(uv.z, uv.w), ImVec2(uv.x, uv.w));
}

class MapScreen : public Screen {
public:
  explicit MapScreen(int slot) : slot(slot) {}
  const char* id() const override { return "map"; }
  bool pausesGame() const override { return false; }

  void onOpen(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player;
    if (slot == -1) slot = mapBestSlot(*s->level, p);
    gather(a);
    int pick = -1;
    for (size_t i = 0; i < list.size(); i++) if (list[i].slot == slot) pick = (int)i;
    if (pick < 0 && !list.empty()) pick = 0;
    if (pick >= 0) choose(a, pick);
    else if (ItemStack* st = mapStackAt(p, slot); st && isMapStack(*st)) toast(*s->level, p, "This map is still on its way from the host");
  }
  void onClose(App& a) override { if (a.session && a.session->player) sfxUi(*a.session->level, *a.session->player, "map_paper"); }

  void update(App& a, double dt) override {
    GameSession* s = a.session.get();
    if (!s || !s->player || !m || s->player->dead) { a.popScreen(); return; }
    Input& in = a.input;
    if (in.pressed(Act::Map) || (inventoryClosePressed(a, true) && !in.padButtonPressed(3))) { a.popScreen(); return; }   // pad Y zooms here; E is "next tab"
    if ((refreshT -= dt) <= 0) {   // (a map picked up or dropped meanwhile)
      refreshT = 1; std::string keep = m ? m->id : ""; gather(a);
      sel = 0; for (size_t i = 0; i < list.size(); i++) if (list[i].m->id == keep) sel = (int)i;
      if (list.empty()) { a.popScreen(); return; }
      if (list[sel].m->id != keep) choose(a, sel);
    }
    if (in.textActive) return;
    vec2 mv = in.moveAxis();   // look around: WASD / the left stick
    if (mv.x || mv.y) { cx += mv.x * (float)dt * 300 / k; cz -= mv.y * (float)dt * 300 / k; }
    const int n = (int)list.size();   // the next map: up / down
    if (n > 1 && in.pressed(Act::MenuDown)) { choose(a, (sel + 1) % n); ui::playClick(); }
    if (n > 1 && in.pressed(Act::MenuUp)) { choose(a, (sel + n - 1) % n); ui::playClick(); }
    if (in.pressed(Act::MenuOk)) findMe(a);
    for (auto& pad : in.pads()) {
      auto pr = [&](int b) { return b < 17 && pad.b[b] && !pad.prevB[b]; };   // standard layout: 2 = X, 3 = Y
      if (pr(SDL_CONTROLLER_BUTTON_X)) zoomBy(1 / 1.5f);
      if (pr(SDL_CONTROLLER_BUTTON_Y)) zoomBy(1.5f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) zoomBy(1.5f);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) zoomBy(1 / 1.5f);
  }

  void draw(App& a) override {
    using namespace ui;
    GameSession* s = a.session.get();
    if (!s || !s->player || !m) return;
    Level& lv = *s->level;
    Player& P = *s->player;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    setBookTheme(a.settings.extra.value("bookDark", false));
    float bk = std::min({(H - 150 * z) / BOOK_H, (W - 120 * z) / BOOK_W, 4.f * z});
    bk = std::max(1.f, std::floor(bk * 4) / 4);
    const ImVec2 org(std::round((W - BOOK_W * bk) * 0.5f), std::round(std::max(44 * z, (H - BOOK_H * bk) * 0.5f - 12 * z)));
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##mapview", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 110));
    dropShadow(dl, org, org + ImVec2(BOOK_W * bk, BOOK_H * bk), 6 * z, 22 * z, 0.55f);
    drawBookSpread(dl, org, bk);
    const ImVec2 Lp = bookTextOrigin(org, bk, 0), Rp = bookTextOrigin(org, bk, 1);
    const float pw = BOOK_TW * bk, ph = BOOK_TH * bk, lh = textSize(F_BODY, "Ag").y + 2 * z, sh = textSize(F_SMALL, "Ag").y + 2 * z;
    const int s1 = 1 << m->scale;

    // ================= the right page: the map =================
    const float foot = lh * 1.6f, box = std::floor(std::min(pw, ph - foot));
    const ImVec2 o(Rp.x + std::floor((pw - box) * 0.5f), Rp.y);
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##sheet", ImVec2(box, box));
    const bool hovered = ImGui::IsItemHovered();
    baseK = box / (m->span() * 1.06f); k = baseK * zoom;
    auto sx = [&](float x) { return o.x + box / 2 + (x - cx) * k; };
    auto sz = [&](float zz) { return o.y + box / 2 + (zz - cz) * k; };
    ImGuiIO& io = ImGui::GetIO();
    auto toWorld = [&](ImVec2 q) { return vec2(cx + (q.x - o.x - box / 2) / k, cz + (q.y - o.y - box / 2) / k); };
    std::optional<vec2> hover;
    if (hovered) {
      hover = toWorld(io.MousePos);
      if (io.MouseWheel != 0) {
        vec2 before = *hover; zoomBy(io.MouseWheel > 0 ? 1.25f : 0.8f); k = baseK * zoom;
        vec2 after = toWorld(io.MousePos); cx += before.x - after.x; cz += before.y - after.y;
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { dragging = true; moved = false; dragFrom = io.MousePos; dragC = vec2(cx, cz); }
    }
    if (dragging) {
      float dx = io.MousePos.x - dragFrom.x, dy = io.MousePos.y - dragFrom.y;
      if (std::hypot(dx, dy) > 4) moved = true;
      if (moved) { cx = dragC.x - dx / k; cz = dragC.y - dy / k; }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging = false;
        if (!moved && hover) { std::string msg = mapTogglePin(lv, P, m->dim, hover->x, hover->y, 12 / k); if (!msg.empty()) toast(lv, P, msg); }
      }
    }
    // the maps laid out on the page
    dl->PushClipRect(o, ImVec2(o.x + box, o.y + box), true);
    dl->AddRectFilled(o, ImVec2(o.x + box, o.y + box), withAlpha(ink::text, 0.1f));
    std::vector<PaperMap*> all = maps; all.push_back(m);
    for (PaperMap* mm : all) {
      float x = sx((float)mm->x0), y = sz((float)mm->z0), w = mm->span() * k;
      if (x > o.x + box || y > o.y + box || x + w < o.x || y + w < o.y) continue;
      dl->AddRectFilled(ImVec2(x + 3, y + 3), ImVec2(x + 3 + w, y + 3 + w), IM_COL32(0, 0, 0, 70));
      dl->AddImage((ImTextureID)(intptr_t)mapTexture(*mm, true), ImVec2(std::round(x), std::round(y)), ImVec2(std::round(x + w), std::round(y + w)));
    }
    { float x = sx((float)m->x0), y = sz((float)m->z0), w = m->span() * k; dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + w + 1), IM_COL32(255, 230, 160, 204), 0, 0, 2); }
    const float mk = std::round(18 * z);
    std::vector<std::pair<ImVec2, std::string>> labels;
    for (const MapMarker& q : mapMarkers(lv, *m, &P)) {
      if (q.small) continue;
      ImVec2 c(sx(m->x0 + q.u * s1), sz(m->z0 + q.v * s1));
      bool pin = q.cell >= 6 && q.cell <= 11;
      float big = q.cell >= 12 ? 1.3f : pin ? 1.1f : 1;
      markerAt(dl, q.cell, ImVec2(c.x, pin ? c.y - mk * 0.35f : c.y), mk * big, q.cell <= 2 ? -q.rot : 0);
      if (!q.label.empty()) labels.push_back({ImVec2(c.x, c.y + mk * 1.05f + 8), q.label});
    }
    for (auto& [lp, t] : labels) tag(dl, lp, t);
    const json& D = lv.info.data;
    if (D.contains("mapPins") && D["mapPins"].is_array())
      for (auto& pn : D["mapPins"]) if (pn.value("d", 0) == m->dim && !m->has(pn.value("x", 0.0), pn.value("z", 0.0)))
        markerAt(dl, 6 + floormod(pn.value("c", 0), 6), ImVec2(sx(pn.value("x", 0.f)), sz(pn.value("z", 0.f)) - mk * 0.35f), mk * 1.1f, 0);
    if (jsDimOf(P) == m->dim) {
      for (Player* o2 : levelPlayers(lv, true)) {
        if (o2 == &P || o2->dead || jsDimOf(*o2) != m->dim) continue;
        if (!m->has(o2->pos.x, o2->pos.z)) markerAt(dl, 2, ImVec2(sx(o2->pos.x), sz(o2->pos.z)), mk, -o2->yaw);
        tag(dl, ImVec2(sx(o2->pos.x), sz(o2->pos.z) - mk * 0.75f), o2->name);
      }
      if (!m->has(P.pos.x, P.pos.z)) markerAt(dl, 0, ImVec2(sx(P.pos.x), sz(P.pos.z)), mk, -P.yaw);
    }
    { // (0.9.0.65) a compass rose in the corner: north is up
      float r = std::round(22 * z); ImVec2 c(o.x + box - r - 10 * z, o.y + r + 10 * z);
      dl->AddCircleFilled(c, r, IM_COL32(30, 20, 10, 150), 32);
      const float ang[4] = {0, PI, PI / 2, -PI / 2};
      for (int i = 0; i < 4; i++) {
        float a2 = ang[i], cs = std::cos(a2), sn = std::sin(a2);
        auto R2 = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
        dl->AddTriangleFilled(R2(0, -r * 0.62f), R2(r * 0.16f, 0), R2(-r * 0.16f, 0), i == 0 ? col32("#e04a3a") : col32("#f2e6c8"));
      }
      const char* NESW[4] = {"N", "E", "S", "W"}; const float dxy[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
      for (int i = 0; i < 4; i++) { ImVec2 t(c.x + dxy[i][0] * r * 0.8f, c.y + dxy[i][1] * r * 0.8f + ImGui::GetFontSize() * 0.5f); tag(dl, t, NESW[i]); }
    }
    dl->PopClipRect();
    dl->AddRect(o - ImVec2(1, 1), ImVec2(o.x + box + 1, o.y + box + 1), withAlpha(ink::text, 0.6f), 0, 0, std::max(1.f, z));
    {   // under it: where the pointer (or you) is
      std::optional<vec2> at = hover; bool you = false;
      if (!at && jsDimOf(P) == m->dim) { at = vec2(P.pos.x, P.pos.z); you = true; }
      std::string t = at ? "x " + std::to_string(ifloor(at->x)) + "  \xC2\xB7  z " + std::to_string(ifloor(at->y)) + (you ? "  (you)" : "") : std::string("You are not in this world");
      ImVec2 ts = textSize(F_BODY, t.c_str());
      text(dl, F_BODY, ImVec2(std::round(o.x + (box - ts.x) * 0.5f), std::round(o.y + box + lh * 0.35f)), at ? ink::text : ink::muted, t.c_str());
    }

    // ================= the left page: your maps and the controls =================
    float y = Lp.y;
    text(dl, F_H3, Lp, ink::head, "Your maps");
    { std::string c = std::to_string(list.size()) + (list.size() == 1 ? " map" : " maps"); ImVec2 cs = textSize(F_BODY, c.c_str());
      text(dl, F_BODY, ImVec2(Lp.x + pw - cs.x, y + 4 * z), ink::muted, c.c_str()); }
    y += textSize(F_H3, "Ag").y + 2 * z;
    dl->AddRectFilled(ImVec2(Lp.x, y), ImVec2(Lp.x + pw, y + std::max(1.f, z)), ink::faint);
    y += 5 * z;
    const float rowH = std::round(lh + 2 * sh + 9 * z), th = rowH - 8 * z;
    const int fit = std::max(1, std::min((int)list.size(), (int)((ph * 0.42f) / rowH)));
    const int first = std::clamp(sel - fit / 2, 0, std::max(0, (int)list.size() - fit));
    for (int i = first; i < first + fit && i < (int)list.size(); i++) {
      const Entry& e = list[i];
      ImVec2 r0(Lp.x, y), r1(Lp.x + pw, y + rowH - 2 * z);
      ImGui::SetCursorScreenPos(r0);
      if (ImGui::InvisibleButton(("##map" + std::to_string(i)).c_str(), ImVec2(pw, rowH - 2 * z)) && i != sel) { choose(a, i); playClick(); }
      const bool hot = ImGui::IsItemHovered();
      if (i == sel || hot) dl->AddRectFilled(ImVec2(r0.x - 4 * z, r0.y), ImVec2(r1.x + 2 * z, r1.y), i == sel ? ink::hilite : col::rgb(0xc8a050, 40), 3 * z);
      ImVec2 t0(r0.x, r0.y + 3 * z);
      dl->AddImage((ImTextureID)(intptr_t)mapTexture(*e.m, true), t0, t0 + ImVec2(th, th));
      dl->AddRect(t0 - ImVec2(1, 1), t0 + ImVec2(th + 1, th + 1), withAlpha(ink::text, 0.6f));
      const float tx = r0.x + th + 8 * z;
      std::string t1 = jsDimName(e.m->dim) + "  \xC2\xB7  1:" + std::to_string(1 << e.m->scale);
      std::string t2 = std::to_string((int)std::lround(e.m->explored() * 100)) + "% explored  \xC2\xB7  " + std::to_string(e.m->span()) + " blocks across";
      std::string t3 = e.slot == -2 ? "In your off hand" : e.slot == P.selected ? "In your hand" : e.slot < 9 ? "On your hotbar" : "In your bag";
      text(dl, F_BODY, ImVec2(tx, r0.y + 2 * z), i == sel ? ink::head : ink::text, t1.c_str());
      text(dl, F_SMALL, ImVec2(tx, r0.y + 2 * z + lh), ink::muted, t2.c_str());
      text(dl, F_SMALL, ImVec2(tx, r0.y + 2 * z + lh + sh), e.slot == P.selected || e.slot == -2 ? ink::key : ink::muted, t3.c_str());
      y += rowH;
    }
    if ((int)list.size() > fit) { std::string more = "\xE2\x86\x91 \xE2\x86\x93  " + std::to_string(sel + 1) + " of " + std::to_string(list.size()); text(dl, F_SMALL, ImVec2(Lp.x, y), ink::muted, more.c_str()); y += sh; }
    y += 6 * z;
    dl->AddRectFilled(ImVec2(Lp.x, y), ImVec2(Lp.x + pw, y + std::max(1.f, z)), ink::faint);
    y += 6 * z;
    {   // this map
      std::string t = "Covers x " + std::to_string(m->x0) + " to " + std::to_string(m->x0 + m->span()) + ", z " + std::to_string(m->z0) + " to " + std::to_string(m->z0 + m->span());
      for (auto& ln : Book::wrap(F_SMALL, t, pw)) { text(dl, F_SMALL, ImVec2(Lp.x, y), ink::text, ln.c_str()); y += sh; }
      if (!maps.empty()) { std::string t2 = std::to_string(maps.size()) + (maps.size() == 1 ? " other map" : " other maps") + " of this world at this scale " + (maps.size() == 1 ? "lies" : "lie") + " around it, where " + (maps.size() == 1 ? "it belongs" : "they belong") + ": drag or zoom out to see " + (maps.size() == 1 ? "it" : "them"); for (auto& ln : Book::wrap(F_SMALL, t2, pw)) { text(dl, F_SMALL, ImVec2(Lp.x, y), ink::muted, ln.c_str()); y += sh; } }
      y += 6 * z;
    }
    {   // the buttons (the pack's)
      const float bw = std::floor((pw - 6 * z) / 2), bh = std::round(22 * z);
      auto btn = [&](const char* l, float x, float yy) { ImGui::SetCursorScreenPos(ImVec2(x, yy)); return classicButton(l, ImVec2(bw, bh)); };
      if (btn("Zoom out  \xE2\x88\x92##zo", Lp.x, y)) zoomBy(1 / 1.5f);
      if (btn("Zoom in  +##zi", Lp.x + bw + 6 * z, y)) zoomBy(1.5f);
      y += bh + 5 * z;
      if (btn("Find me##fm", Lp.x, y)) findMe(a);
      if (btn("Whole map##wm", Lp.x + bw + 6 * z, y)) { zoom = 1; cx = m->x0 + m->span() / 2.f; cz = m->z0 + m->span() / 2.f; }
      y += bh + 8 * z;
    }
    {   // how
      const bool pad = a.input.usingPad();
      text(dl, F_BODY, ImVec2(Lp.x, y), ink::head, "Pins"); y += lh;
      std::string pins = "Click the map to set a pin; click it again to clear it. Pins show on every map of that place.";
      for (auto& ln : Book::wrap(F_SMALL, pins, pw)) { text(dl, F_SMALL, ImVec2(Lp.x, y), ink::text, ln.c_str()); y += sh; }
      y += 4 * z;
      std::string how = pad ? "Left stick: look around \xC2\xB7 X / Y: zoom \xC2\xB7 d-pad \xE2\x86\x91 \xE2\x86\x93: the next map \xC2\xB7 A: find me \xC2\xB7 B: close"
                            : "Drag: look around (or WASD) \xC2\xB7 wheel or + / \xE2\x88\x92: zoom \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93: the next map \xC2\xB7 Enter: find me \xC2\xB7 M or Esc: close";
      for (auto& ln : Book::wrap(F_SMALL, how, pw)) { if (y + sh > Lp.y + ph) break; text(dl, F_SMALL, ImVec2(Lp.x, y), ink::muted, ln.c_str()); y += sh; }
    }
    {   // the sun / moon under the book's corner
      const float s2 = std::round(18 * z);
      if (bookThemeButton(a, dl, ImVec2(org.x + BOOK_W * bk - s2, org.y + BOOK_H * bk + 6 * z), s2)) { a.settings.extra["bookDark"] = !bookThemeDark(); a.settings.save(); playClick(); }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

private:
  struct Entry { int slot; PaperMap* m; };
  int slot, sel = 0;
  PaperMap* m = nullptr;
  std::string mapId;
  std::vector<PaperMap*> maps;
  std::vector<Entry> list;
  double refreshT = 1;
  float zoom = 1, cx = 0, cz = 0, k = 1, baseK = 1;
  bool dragging = false, moved = false;
  ImVec2 dragFrom; vec2 dragC{0};
  void zoomBy(float f) { zoom = clampv(zoom * f, 0.2f, 10.f); k = baseK * zoom; }
  // every map you carry (the hotbar first, then the bag, then the off hand), once each
  void gather(App& a) {
    list.clear();
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player;
    std::vector<int> slots;
    for (int i = 0; i < (int)p.inv.size(); i++) slots.push_back(i);
    slots.push_back(-2);
    for (int sl : slots) {
      ItemStack* st = mapStackAt(p, sl);
      if (!st || !isMapStack(*st)) continue;
      PaperMap* pm = mapStore(*s->level).forStack(p, *st);
      if (!pm) continue;
      bool seen = false; for (auto& e : list) if (e.m->id == pm->id) seen = true;
      if (!seen) list.push_back({sl, pm});
    }
  }
  void choose(App& a, int i) {
    if (i < 0 || i >= (int)list.size()) return;
    sel = i; m = list[i].m; mapId = m->id; slot = list[i].slot;
    cx = m->x0 + m->span() / 2.f; cz = m->z0 + m->span() / 2.f; zoom = 1;
    if (a.session) maps = mapStore(*a.session->level).sameScale(*m);
    std::erase(maps, m);
  }
  void findMe(App& a) {
    GameSession* s = a.session.get();
    if (!s || !s->player || !m) return;
    if (jsDimOf(*s->player) == m->dim) { cx = s->player->pos.x; cz = s->player->pos.z; }
    else toast(*s->level, *s->player, "You're not in the world this map shows");
  }
};
}

std::unique_ptr<Screen> makeMapScreen(int slot) { return std::make_unique<MapScreen>(slot); }

bool canOpenMapScreen(App& a) {
  GameSession* s = a.session.get();
  if (!s || !s->player) return false;
  Player& p = *s->player;
  for (auto& st : p.inv) if (isMapStack(st)) return true;
  return !p.offhand.empty() && isMapStack(p.offhand[0]);
}

} // namespace dq
