// render/station_cards.cpp — what the enchanting table and the anvil show in the world while you work them (after
// the enchanting and anvil scenes): the three offers floating in a crown over the table, each a
// tablet of glyphs with its level and its lapis (the one you look at lifts and glows), the glyphs streaming from the
// bookshelves round it into the book, and the rite — the chosen tablet drawn into the item, the others falling away, a
// burst of glyphs; over the anvil, a card of what the work would become: the result, its wear before and after, its
// enchantments, what it costs (shaking no when it won't forge). Painted with the interface's own fonts and icons
// (ui/world_canvas.h) onto pictures that face you.
#include "render/item_layers.h"
#include "render/content_draw.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/cinematic.h"
#include "ui/world_canvas.h"
#include "ui/theme.h"
#include "sim/ench_work.h"
#include "sim/anvil_work.h"
#include "sim/enchanting.h"
#include "sim/player.h"
#include "sim/physics.h"
#include "core/registry.h"
#include <cmath>

namespace dq {
namespace {

int maxDur(const ItemStack& s) {
  const ItemDef* d = s.empty() ? nullptr : s.def();
  if (!d) return 0;
  if (d->hasTool) return d->tool.durability;
  if (d->props.contains("armor") && d->props["armor"].is_object()) return d->props["armor"].value("durability", 0);
  return 0;
}
std::string nameOfStack(const ItemStack& s) {
  if (s.empty()) return "";
  if (s.tag.is_object() && s.tag.contains("name") && s.tag["name"].is_string()) return s.tag["name"].get<std::string>();
  return s.def() ? s.def()->name : s.id;
}
// (scale: the font's own size times it; a negative scale is a height in pixels instead)
void text(ImDrawList* dl, ui::FontId f, float scale, ImVec2 at, ImU32 col, const std::string& t, bool shadow = true) {
  ImFont* fn = ui::font(f);
  const float sz = scale < 0 ? -scale : fn->FontSize * scale;
  if (shadow) dl->AddText(fn, sz, ImVec2(at.x + 2, at.y + 2), IM_COL32(0, 0, 0, 170), t.c_str());
  dl->AddText(fn, sz, at, col, t.c_str());
}
ImVec2 textSize(ui::FontId f, float scale, const std::string& t) { ImFont* fn = ui::font(f); return fn->CalcTextSizeA(scale < 0 ? -scale : fn->FontSize * scale, 1e9f, 0, t.c_str()); }

// a picture standing in the world facing the camera: centre p, w x h blocks, lifted toward you by `toward`
// (uv: the painted picture's own, 0..1 from its top left; the texture is upside down as GL draws into it)
void billboard(ContentDraw& cd, const FrameCtx& f, GLuint tex, vec3 p, float w, float h, vec4 tint = vec4(1), vec4 uv = vec4(0, 0, 1, 1)) {
  uv = vec4(uv.x, 1 - uv.y, uv.z, 1 - uv.w);
  const float yaw = f.cam.yaw, pitch = f.cam.pitch;
  const vec3 right(std::cos(yaw), 0, -std::sin(yaw)), fwd = lookVector(yaw, pitch), up = glm::cross(right, fwd);
  mat4 M(1); M[0] = vec4(right * w, 0); M[1] = vec4(up * h, 0); M[2] = vec4(-fwd, 0); M[3] = vec4(f.cam.rel(glm::dvec3(p)), 1);
  cd.picture(tex, M, uv, tint, vec2(1, 1), false);
}

// the glyphs in a strip: 26 letters, white strokes (tinted when drawn)
CanvasPic glyphStrip() {
  return canvasPaint("glyphs/strip", 1, 26 * 48, 48, [](ImDrawList* dl, ImVec2) {
    for (int c = 0; c < 26; c++) drawGlyph(dl, c, ImVec2(c * 48 + 8.f, 4), 40, IM_COL32(255, 255, 255, 255), 4.5f);
  });
}

class StationCards : public RenderLayer {
public:
  const char* name() const override { return "station-cards"; }
  int order() const override { return 132; }
  // the offers' tablets as they're drawn (eased toward hover / the rite)
  float lift[3] = {0, 0, 0};
  std::vector<ivec3> shelves; ivec3 shelvesAt{INT32_MIN}; double shelvesT = -10;
  void update(GameSession& s, double dt) override {
    EnchantView ev;
    if (!s.player || !cinematicEnchantView(ev)) { for (float& l : lift) l = 0; return; }
    for (int k = 0; k < 3; k++) lift[k] += ((ev.hover == k ? 1.f : 0.f) - lift[k]) * std::min(1.f, (float)dt * 10);
    const double now = s.frame.time;
    if (ev.at != shelvesAt || now - shelvesT > 1.0) {   // (the shelves round the table: the glyphs come from them)
      shelvesAt = ev.at; shelvesT = now; shelves.clear();
      World& w = s.world();
      for (int dy = 0; dy <= 1; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
        if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
        const ivec3 q = ev.at + ivec3(dx, dy, dz);
        const std::string& k = reg().block(w.getBlock(q.x, q.y, q.z)).key;
        if (k.find("bookshelf") != std::string::npos || k.find("bookcase") != std::string::npos) shelves.push_back(q);
      }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    EnchantView ev; AnvilView av;
    const bool en = cinematicEnchantView(ev), an = cinematicAnvilView(av);
    if (!en && !an) return;
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    if (en) drawEnchant(s, f, cd, ev);
    if (an) drawAnvil(s, f, cd, av);
    cd.end();
  }

  void drawEnchant(GameSession& s, const FrameCtx& f, ContentDraw& cd, const EnchantView& ev) {
    Player& p = *s.player; World& w = s.world();
    std::array<EnchOffer, 3> offers{};
    const bool has = enchWorkOffers(w, ev.at, p, offers);
    const std::vector<ItemStack> v = enchWorkItems(w, ev.at);
    const double clock = cinematicClock();
    const vec3 itemAt = vec3(ev.at) + vec3(0.5f, 0.75f + 0.42f, 0.5f);
    const CanvasPic gs = glyphStrip();
    // the glyphs from the shelves into the book (EnchantFx): a few in the air from each, always
    {
      const vec3 book = vec3(ev.at) + vec3(0.5f, 1.15f, 0.5f);
      for (size_t i = 0; i < shelves.size(); i++) for (int k = 0; k < 2; k++) {
        const float ph = (float)std::fmod(clock * 0.55 + i * 0.37 + k * 0.5, 1.0);
        const vec3 from = vec3(shelves[i]) + vec3(0.5f, 1.1f + 0.3f * std::sin(i * 3.1f + k), 0.5f);
        const vec3 mid = (from + book) * 0.5f + vec3(0, 0.7f, 0);
        const float u = ph;
        const vec3 at = (1 - u) * (1 - u) * from + 2 * (1 - u) * u * mid + u * u * book;
        const int letter = (int)((i * 7 + k * 13 + (int)(clock * 0.55 + i * 0.37 + k * 0.5)) % 26);
        const float sz = 0.09f * std::sin(u * 3.14159f);
        if (sz < 0.01f) continue;
        billboard(cd, f, gs.tex, at, sz, sz, vec4(0.78f, 0.62f, 1.f, 1), vec4(letter / 26.f, 0, (letter + 1) / 26.f, 1));
      }
    }
    if (!has) return;
    // the rite: the chosen tablet drawn down into the item (RITUAL_PEAK 0.5), the others dropping away; then a burst
    const double rt = ev.ritualT;
    for (int k = 0; k < 3; k++) {
      const EnchOffer& o = offers[k];
      const bool can = p.creative() || (p.level >= o.level && v[1].id == "lapis_lazuli" && v[1].n >= o.cost);
      const bool hot = ev.hover == k && rt < 0;
      // the tablet: glyphs (the offer's own, from its enchantments), its level, its lapis
      const u32 seed = (u32)std::hash<std::string>()(o.order.empty() ? std::to_string(k) : o.order[0]) + (u32)o.level * 977u;
      const std::string key = "ench/offer" + std::to_string(k);
      const u64 ver = (u64)seed * 8 + (hot ? 1 : 0) + (can ? 2 : 0) + (u64)o.cost * 1000003ull;
      const CanvasPic pic = canvasPaint(key, ver, 320, 256, [&](ImDrawList* dl, ImVec2 S) {
        const ImU32 edge = hot ? IM_COL32(255, 220, 140, 255) : can ? IM_COL32(170, 120, 230, 255) : IM_COL32(110, 100, 120, 255);
        dl->AddRectFilled(ImVec2(6, 6), ImVec2(S.x - 6, S.y - 6), can ? IM_COL32(34, 16, 58, 255) : IM_COL32(32, 30, 36, 255), 22);
        dl->AddRect(ImVec2(6, 6), ImVec2(S.x - 6, S.y - 6), edge, 22, 0, hot ? 7 : 4);
        dl->AddRect(ImVec2(18, 18), ImVec2(S.x - 18, S.y - 18), (edge & 0x00FFFFFF) | 0x60000000, 14, 0, 2);
        drawGlyphRun(dl, seed, 10, ImVec2(36, 30), 36, S.x - 72, can ? IM_COL32(220, 200, 255, 255) : IM_COL32(150, 145, 160, 255));
        const std::string lv = std::to_string(o.level);
        const ImVec2 ls = textSize(ui::F_DISP, -104, lv);
        text(dl, ui::F_DISP, -104, ImVec2(S.x * 0.33f - ls.x * 0.5f, S.y - ls.y - 18), can ? IM_COL32(128, 255, 32, 255) : IM_COL32(170, 80, 70, 255), lv);
        ui::drawItem(dl, ItemStack("lapis_lazuli", 1), ImVec2(S.x * 0.6f, S.y - 100), 64, can ? 1.f : 0.5f, false);
        text(dl, ui::F_BIG, -64, ImVec2(S.x * 0.6f + 70, S.y - 96), IM_COL32(140, 180, 255, 255), std::to_string(o.cost));
      });
      vec3 c = cinematicOfferPos(ev.at, ev.viewYaw, k, clock) + vec3(0, 0.06f * lift[k], 0);
      float sc = 1.f + 0.18f * lift[k];
      vec4 tint = vec4(1);
      if (rt >= 0) {
        if (k == ev.pick) { const float u = (float)std::min(1.0, rt / 0.5); c = c + (itemAt - c) * (u * u); sc *= 1 - u * 0.85f; if (rt > 0.5) continue; }
        else { const float u = (float)std::min(1.0, rt / 0.4); c.y -= 0.4f * u * u; sc *= 1 - u; if (u >= 1) continue; }
      }
      if (hot) tint = vec4(1.15f, 1.1f, 1.15f, 1);
      billboard(cd, f, pic.tex, c, 0.4f * sc, 0.32f * sc, tint);
    }
    if (rt >= 0.5 && rt < 1.5) {   // the burst: glyphs flung out from the item
      const float u = (float)((rt - 0.5) / 1.0);
      for (int i = 0; i < 24; i++) {
        const float a = i * 0.2618f + (float)rt, r = 0.15f + u * 0.9f;
        const vec3 at = itemAt + vec3(std::cos(a) * r, 0.25f * std::sin(i * 1.7f) + u * 0.4f, std::sin(a) * r);
        const float sz = 0.1f * (1 - u);
        if (sz < 0.012f) continue;
        billboard(cd, f, gs.tex, at, sz, sz, vec4(0.95f, 0.85f, 1.f, 1), vec4((i % 26) / 26.f, 0, (i % 26 + 1) / 26.f, 1));
      }
    }
  }

  void drawAnvil(GameSession& s, const FrameCtx& f, ContentDraw& cd, const AnvilView& av) {
    Player& p = *s.player; World& w = s.world();
    const std::vector<ItemStack> v = anvilWorkItems(w, av.at);
    if (v[0].empty()) return;
    const StationResult r = anvilWorkResult(w, av.at);
    // the card: what the work becomes
    const ItemStack& out = r.ok ? r.out : v[0];
    const int md = maxDur(out);
    json en = out.tag.is_object() && out.tag.contains("en") ? out.tag["en"] : json::object();
    std::vector<std::string> lines;
    if (en.is_object()) for (auto it = en.begin(); it != en.end(); ++it) if (it.value().is_number()) lines.push_back(enchName(it.key(), it.value().get<int>()));
    const bool afford = p.creative() || p.level >= r.lv;
    const u64 ver = std::hash<std::string>()(out.toJson().dump() + v[1].toJson().dump()) ^ ((u64)r.ok << 60) ^ ((u64)afford << 61) ^ ((u64)r.lv << 40);
    const int H = 200 + 50 * (int)lines.size() + (md > 0 ? 74 : 0) + (r.note.empty() ? 0 : 52) + (r.ok && r.lv > 0 ? 64 : 0);
    const CanvasPic pic = canvasPaint("anvil/card", ver, 560, H, [&](ImDrawList* dl, ImVec2 S) {
      dl->AddRectFilled(ImVec2(4, 4), ImVec2(S.x - 4, S.y - 4), IM_COL32(24, 20, 18, 240), 16);
      dl->AddRect(ImVec2(4, 4), ImVec2(S.x - 4, S.y - 4), r.ok ? IM_COL32(200, 160, 80, 255) : IM_COL32(170, 70, 60, 255), 16, 0, 4);
      ui::drawItem(dl, out, ImVec2(22, 22), 100, r.ok ? 1.f : 0.6f, false);
      text(dl, ui::F_BIG, -50, ImVec2(136, 24), r.ok ? IM_COL32(255, 235, 170, 255) : IM_COL32(220, 120, 110, 255), r.ok ? nameOfStack(out) : "It won't forge");
      text(dl, ui::F_MID, -34, ImVec2(136, 84), IM_COL32(190, 180, 165, 255), r.ok ? (v[1].empty() ? "as it is" : "with " + nameOfStack(v[1]) + (v[1].n > 1 ? " x" + std::to_string(r.useB ? r.useB : v[1].n) : "")) : "with what's beside it");
      float y = 142;
      if (md > 0) {   // its wear, before and after
        const float before = 1 - (float)v[0].d / md, after = 1 - (float)out.d / md;
        dl->AddRectFilled(ImVec2(24, y), ImVec2(S.x - 24, y + 26), IM_COL32(10, 10, 10, 255), 4);
        auto barCol = [](float k) { return IM_COL32((int)(255 * (1 - k)), (int)(220 * k + 30), 40, 255); };
        dl->AddRectFilled(ImVec2(26, y + 2), ImVec2(26 + (S.x - 52) * after, y + 24), barCol(after), 3);
        if (after > before + 0.001f) dl->AddRectFilled(ImVec2(26, y + 2), ImVec2(26 + (S.x - 52) * before, y + 24), (barCol(before) & 0x00FFFFFF) | 0x90000000, 3);
        text(dl, ui::F_MID, -32, ImVec2(26, y + 32), IM_COL32(170, 200, 150, 255), std::to_string(md - out.d) + " / " + std::to_string(md) + (out.d < v[0].d ? "   (+" + std::to_string(v[0].d - out.d) + ")" : ""));
        y += 74;
      }
      for (const std::string& l : lines) { text(dl, ui::F_MID, -40, ImVec2(28, y), IM_COL32(190, 160, 255, 255), l); y += 50; }
      if (!r.note.empty()) { text(dl, ui::F_MID, -34, ImVec2(28, y), IM_COL32(200, 190, 170, 255), r.note); y += 52; }
      if (r.ok && r.lv > 0) {
        const std::string c = "Costs " + std::to_string(r.lv) + (r.lv == 1 ? " level" : " levels");
        text(dl, ui::F_BIG, -46, ImVec2(28, S.y - 62), afford ? IM_COL32(128, 255, 32, 255) : IM_COL32(255, 96, 80, 255), c);
      }
    });
    vec3 c = vec3(av.at) + vec3(0.5f, 1.38f, 0.5f);
    if (av.denyT >= 0 && av.denyT < 0.5) {   // (no: a shake from side to side)
      const vec3 right(std::cos(av.viewYaw), 0, -std::sin(av.viewYaw));
      c += right * (0.035f * (float)std::sin(av.denyT * 45) * (float)(1 - av.denyT / 0.5));
    }
    const float wd = 0.7f, ht = wd * pic.h / (float)pic.w;
    vec4 tint = av.forgeT >= 0 ? vec4(0.7f, 0.7f, 0.7f, 1) : vec4(1);
    billboard(cd, f, pic.tex, c + vec3(0, ht * 0.5f, 0), wd, ht, tint);
  }
};
}  // namespace

void registerStationCards() { registerRenderLayer([](GameSession&) { return std::make_shared<StationCards>(); }); }

} // namespace dq
