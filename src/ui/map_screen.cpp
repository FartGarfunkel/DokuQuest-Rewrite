// ui/map_screen.cpp — looking at a map closely (game/maps.js MapView): every map of the same place and scale
// laid out together on the table, to scroll and zoom about and pin. Plain ImGui (the UI theme restyles it).
#include "ui/content_screens.h"
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
    ItemStack* st = mapStackAt(p, slot);
    if (!st || !isMapStack(*st)) return;
    m = mapStore(*s->level).forStack(p, *st);
    if (!m) { toast(*s->level, p, "This map is still on its way from the host"); return; }
    mapId = m->id;
    cx = m->x0 + m->span() / 2.f; cz = m->z0 + m->span() / 2.f;
    maps = mapStore(*s->level).sameScale(*m);
  }
  void onClose(App& a) override { if (a.session && a.session->player) sfxUi(*a.session->level, *a.session->player, "map_paper"); }

  void update(App& a, double dt) override {
    GameSession* s = a.session.get();
    if (!s || !s->player || !m || s->player->dead) { a.popScreen(); return; }
    Input& in = a.input;
    if (in.pressed(Act::Map) || in.pressed(Act::Inventory)) { a.popScreen(); return; }
    // a controller (or WASD): look around; X / Y zoom
    vec2 mv = in.moveAxis();
    if (in.down(Act::MenuLeft)) mv.x -= 1;
    if (in.down(Act::MenuRight)) mv.x += 1;
    if (in.down(Act::MenuUp)) mv.y += 1;
    if (in.down(Act::MenuDown)) mv.y -= 1;
    if (mv.x || mv.y) { cx += mv.x * (float)dt * 300 / k; cz -= mv.y * (float)dt * 300 / k; }
    for (auto& pad : in.pads()) {
      auto pr = [&](int b) { return pad.btn[b] && !pad.prevBtn[b]; };
      if (pr(SDL_CONTROLLER_BUTTON_X)) zoomBy(1 / 1.5f);
      if (pr(SDL_CONTROLLER_BUTTON_Y)) zoomBy(1.5f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) zoomBy(1.5f);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) zoomBy(1 / 1.5f);
  }

  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player || !m) return;
    Level& lv = *s->level;
    Player& P = *s->player;
    float W = std::min(a.width - 40.f, a.height * 1.0f - 20.f), Hh = std::min(a.height - 20.f, W + 110);
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W, Hh));
    ImGui::Begin("##mapview", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    int s1 = 1 << m->scale;
    ImGui::Text("Map \xC2\xB7 %s \xC2\xB7 1:%d", jsDimName(m->dim).c_str(), s1);
    ImGui::SameLine(); ImGui::TextDisabled("%d%% explored \xC2\xB7 %d blocks across", (int)std::lround(m->explored() * 100), m->span());
    ImGui::SameLine(ImGui::GetWindowWidth() - 34);
    if (ImGui::Button("X##close")) { ImGui::End(); a.popScreen(); return; }
    float box = std::max(120.f, std::min(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - 60));
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##sheet", ImVec2(box, box));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    baseK = box / (m->span() * 1.06f); k = baseK * zoom;
    auto sx = [&](float x) { return o.x + box / 2 + (x - cx) * k; };
    auto sz = [&](float z) { return o.y + box / 2 + (z - cz) * k; };
    ImGuiIO& io = ImGui::GetIO();
    auto toWorld = [&](ImVec2 q) { return vec2(cx + (q.x - o.x - box / 2) / k, cz + (q.y - o.y - box / 2) / k); };
    // mouse: wheel zooms about the pointer, drag looks around, a click sets or clears a pin
    std::optional<vec2> hover;
    if (hovered) {
      hover = toWorld(io.MousePos);
      if (io.MouseWheel != 0) {
        vec2 before = *hover; zoomBy(io.MouseWheel > 0 ? 1.25f : 0.8f); k = baseK * zoom;
        vec2 after = toWorld(io.MousePos); cx += before.x - after.x; cz += before.y - after.y;
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { dragging = true; moved = false; dragFrom = io.MousePos; dragC = vec2(cx, cz); }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { ImGui::End(); a.popScreen(); return; }
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
    // the table they're laid out on
    dl->PushClipRect(o, ImVec2(o.x + box, o.y + box), true);
    dl->AddRectFilled(o, ImVec2(o.x + box, o.y + box), col32("#3a2c1c"));
    for (float y = 0; y < box; y += 14) dl->AddLine(ImVec2(o.x, o.y + y), ImVec2(o.x + box, o.y + y), IM_COL32(0, 0, 0, 31));
    std::vector<PaperMap*> all = maps; all.push_back(m);
    for (PaperMap* mm : all) {
      float x = sx((float)mm->x0), y = sz((float)mm->z0), w = mm->span() * k;
      if (x > o.x + box || y > o.y + box || x + w < o.x || y + w < o.y) continue;
      dl->AddRectFilled(ImVec2(x + 3, y + 3), ImVec2(x + 3 + w, y + 3 + w), IM_COL32(0, 0, 0, 89));
      dl->AddImage((ImTextureID)(intptr_t)mapTexture(*mm, true), ImVec2(std::round(x), std::round(y)), ImVec2(std::round(x + w), std::round(y + w)));
    }
    { float x = sx((float)m->x0), y = sz((float)m->z0), w = m->span() * k; dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + w + 1), IM_COL32(255, 230, 160, 204), 0, 0, 2); }
    // markers: drawn at a steady size whatever the zoom
    float mk = 18;
    std::vector<std::pair<ImVec2, std::string>> labels;
    for (const MapMarker& q : mapMarkers(lv, *m, &P)) {
      if (q.small) continue;
      ImVec2 c(sx(m->x0 + q.u * s1), sz(m->z0 + q.v * s1));
      bool pin = q.cell >= 6 && q.cell <= 11;
      float big = q.cell >= 12 ? 1.3f : pin ? 1.1f : 1;
      markerAt(dl, q.cell, ImVec2(c.x, pin ? c.y - mk * 0.35f : c.y), mk * big, q.cell <= 2 ? -q.rot : 0);
      if (!q.label.empty()) labels.push_back({ImVec2(c.x, c.y + mk * 1.05f + 8), q.label});
    }
    for (auto& [p, t] : labels) tag(dl, p, t);
    // pins and people beyond the held map, and names
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
      float r = 26; ImVec2 c(o.x + box - r - 14, o.y + r + 14);
      dl->AddCircleFilled(c, r, IM_COL32(30, 20, 10, 140), 32);
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
    // the foot: where, and the buttons
    std::optional<vec2> at = hover;
    bool you = false;
    if (!at && jsDimOf(P) == m->dim) { at = vec2(P.pos.x, P.pos.z); you = true; }
    if (at) ImGui::Text("x %d \xC2\xB7 z %d%s", ifloor(at->x), ifloor(at->y), you ? " (you)" : "");
    else ImGui::TextUnformatted("You are not in this world");
    ImGui::SameLine(ImGui::GetWindowWidth() - 190);
    if (ImGui::Button(" - ")) zoomBy(1 / 1.5f);
    ImGui::SameLine(); if (ImGui::Button("Find me") && jsDimOf(P) == m->dim) { cx = P.pos.x; cz = P.pos.z; }
    ImGui::SameLine(); if (ImGui::Button(" + ")) zoomBy(1.5f);
    ImGui::TextDisabled("Scroll to zoom \xC2\xB7 drag to look around \xC2\xB7 click to set a pin, click it again to clear it \xC2\xB7 M or Esc to close \xC2\xB7 pad: X/Y zoom, LB/RB other menus");
    ImGui::End();
  }

private:
  int slot;
  PaperMap* m = nullptr;
  std::string mapId;
  std::vector<PaperMap*> maps;
  float zoom = 1, cx = 0, cz = 0, k = 1, baseK = 1;
  bool dragging = false, moved = false;
  ImVec2 dragFrom; vec2 dragC{0};
  void zoomBy(float f) { zoom = clampv(zoom * f, 0.2f, 10.f); k = baseK * zoom; }
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
