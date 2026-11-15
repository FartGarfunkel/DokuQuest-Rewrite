// render/tactile_chests.cpp — a chest drawn as Tactile Blocks' coffre over ours (CoffreBlockEntityRenderer; worked by
// sim/tactile_chest.cpp): its lid on its hinge a quarter of the way in (so it opens up over the chest, never back into the
// wall behind), the two buttons on the lid pressing in, the page on the tray in its five by three, the other page heaped
// loose at the bottom, the one you look at lifted and larger with its name and lines written inside the lid
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/tactile_kit.h"
#include "render/content_draw.h"
#include "render/map_render.h"
#include "render/tile_box.h"
#include "assets/local_models.h"
#include "sim/tactile.h"
#include "sim/chest_work.h"
#include "sim/physics.h"
#include "core/registry.h"
#include "world/world.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <map>
#include <tuple>

namespace dq {
std::vector<std::string> itemTooltipLines(const ItemStack& s);   // the item's name and lines (below, from its definition)

namespace {
const float COL_X[5] = {2.4f, 5.2f, 8.0f, 10.8f, 13.6f}, ROW_Z[3] = {9.5f, 6.5f, 3.5f};

struct Chest { ivec3 at; int facing; bool open; int page; ItemStack items[27]; int press[2]; VoxelLight L; int hover = -1; };

class TactileChests : public RenderLayer {
public:
  const char* name() const override { return "tactile-chests"; }
  std::vector<Chest> list;
  double dt = 0.016;
  struct Press { int seen = -1; double at = -10; };
  std::map<std::tuple<int, int, int, int>, Press> presses;
  void update(GameSession&, double d) override { dt = d; }

  float pressOf(ivec3 at, int k, int n, double now) {   // (buttonPress: a sine over four ticks after each press)
    Press& p = presses[{at.x, at.y, at.z, k}];
    if (p.seen < 0) p.seen = n;
    if (n != p.seen) { p.seen = n; p.at = now; }
    const double e = (now - p.at) * 20;
    return e < 0 || e > 4 ? 0.f : (float)std::sin(e / 4 * PI);
  }

  // an open barrel (sim/tactile.h tactileBarrel): its layer of nine on a shelf of its planks under the rim, the other layers
  // heaped below, its two buttons on the rim (Tactile's own, pressing in)
  struct Barrel { ivec3 at; int facing, page, press[2]; ItemStack items[27]; VoxelLight L; int hover = -1; };
  std::vector<Barrel> barrels;
  void drawBarrels(GameSession& s, const FrameCtx& f, World& w, vec3 eye, vec3 dir) {
    barrels.clear();
    forBlockEntities(w, f, 24.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const ivec3 at(x, y, z);
      if (!tactileBarrel(w, at) || !chestLidOpen(w, at)) return;
      Barrel b; b.at = at; b.facing = tactileBarrelFacing(w, at); b.page = e.value("page", 0); b.press[0] = e.value("press0", 0); b.press[1] = e.value("press1", 0);
      const json& it = e.contains("items") && e["items"].is_array() ? e["items"] : json::array();
      for (int k = 0; k < 27 && k < (int)it.size(); k++) b.items[k] = ItemStack::fromJson(it[k]);
      b.L = tactileLight(w, at);
      if (s.target.hit && s.target.block == at) { const TactileChestAim a = tactileBarrelAim(w, at, eye, dir); if (a.what == TactileChestAim::Tray) b.hover = a.slot; }
      barrels.push_back(b);
    });
    if (barrels.empty()) return;
    static const float COL[3] = {4.5f, 8.f, 11.5f}, ROW[3] = {11.5f, 8.f, 4.5f};
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    for (const Barrel& b : barrels) {
      const TPose base = tactileBlockPose(f, b.at, b.facing);
      for (int i = 0; i < 9; i++) {
        const int k = b.page * 9 + i;
        if (k >= 27 || b.items[k].empty()) continue;
        const bool hot = i == b.hover;
        TPose p = base; p.t(COL[i % 3] / 16, (14.6f + (hot ? 1.5f : 0.f)) / 16, ROW[i / 3] / 16).rx(90).s(0.28f * (hot ? 1.25f : 1.f));
        ItemStack one = b.items[k]; one.n = 1;
        tactileItem(one, p.m, b.L);
      }
      u32 h = (u32)(b.at.x * 73856093) ^ (u32)(b.at.y * 19349663) ^ (u32)(b.at.z * 83492791) ^ 0x51u;
      auto rnd = [&] { h = h * 1664525u + 1013904223u; return (h >> 8 & 0xFFFF) / 65535.f; };
      for (int k = 0; k < 27; k++) {   // (the other layers, heaped below the shelf)
        const float jx = (rnd() - 0.5f) * 9, jz = (rnd() - 0.5f) * 9, dy = rnd() * 6, spin = rnd() * 360, tx = (rnd() - 0.5f) * 60;
        if (k / 9 == b.page || b.items[k].empty()) continue;
        TPose p = base; p.t((8 + jx) / 16, (2 + dy) / 16, (8 + jz) / 16).ry(spin).rx(90 + tx).s(0.26f);
        ItemStack one = b.items[k]; one.n = 1;
        tactileItem(one, p.m, b.L);
      }
    }
    VR.end();
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    const PackModel* sortM = tactilePart("coffre_buttontri");
    const PackModel* swapM = tactilePart("coffre_buttonswap");
    for (const Barrel& b : barrels) {
      const vec2 light(b.L.sky, b.L.block);
      const TPose base = tactileBlockPose(f, b.at, b.facing);
      const std::string side = reg().block(w.getBlock(b.at.x, b.at.y, b.at.z)).props["tex"].value("side", std::string("barrel_side"));
      if (const GLuint t = tileTexture(s.app, side)) {   // (the shelf: one board across under the rim, its planks)
        TPose p = base; p.t(0.5f, 14.f / 16, 0.5f).rx(-90).s(12.f / 16, 12.f / 16, 1);
        cd.picture(t, p.m, vec4(0, 0, 1, 1), vec4(0.85f, 0.85f, 0.85f, 1), light, true);
      }
      auto button = [&](const PackModel* m, vec3 partMid, vec3 to, float press) {
        if (!m) return;
        TPose p = base; p.t((to.x - partMid.x) / 16, (to.y - partMid.y - press * 1.2f) / 16, (to.z - partMid.z) / 16);
        tactilePartDraw(s.app, cd, *m, p.m, light);
      };
      button(sortM, vec3(12.5f, 10.25f, 8.65f), vec3(3.5f, 16.4f, 1.2f), pressOf(b.at, 4, b.press[0], f.time));
      button(swapM, vec3(12.5f, 10.25f, 4.5f), vec3(12.5f, 16.4f, 1.2f), pressOf(b.at, 5, b.press[1], f.time));
    }
    cd.end();
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player || !s.app.settings.tactileStations) return;
    World& w = s.world();
    drawBarrels(s, f, w, vec3(s.player->pos.x, s.player->pos.y + s.player->eye, s.player->pos.z), lookVector(s.player->yaw, s.player->pitch));
    list.clear();
    const vec3 eye(s.player->pos.x, s.player->pos.y + s.player->eye, s.player->pos.z);
    const vec3 dir = lookVector(s.player->yaw, s.player->pitch);
    forBlockEntities(w, f, 24.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const ivec3 at(x, y, z);
      if (!stationModel(w.getBlock(x, y, z)) || !tactileChest(w, at)) return;
      Chest c; c.at = at; c.facing = tactileFacing(w, at); c.open = chestLidOpen(w, at); c.page = e.value("page", 0) & 1;
      { const std::vector<ItemStack> it = chestHalfItems(*s.level, s.player, w, at);   // (an ender chest: your own)
        for (int k = 0; k < 27 && k < (int)it.size(); k++) c.items[k] = it[k]; }
      c.press[0] = e.value("press0", 0); c.press[1] = e.value("press1", 0);
      c.L = tactileLight(w, at);
      if (c.open && s.target.hit && s.target.block == at) { const TactileChestAim a = tactileChestAim(w, at, eye, dir); if (a.what == TactileChestAim::Tray && c.page * 15 + a.slot < 27) c.hover = a.slot; }
      list.push_back(c);
    });
    // chests with no record yet: their lid still drawn (shut)
    if (list.empty()) return;
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    for (const Chest& c : list) {
      const float lid = tactileEase(c.at, 2, c.open, 0);
      if (lid <= 0.001f) continue;
      const TPose base = tactileBlockPose(f, c.at, c.facing);
      for (int i = 0; i < 15; i++) {   // (renderGrid: the page on the tray)
        const int k = c.page * 15 + i;
        if (k >= 27 || c.items[k].empty()) continue;
        const bool hot = i == c.hover;
        TPose p = base; p.t(COL_X[i % 5] / 16, (9.5f + (hot ? 2.f : 0.f)) / 16, ROW_Z[i / 5] / 16).rx(90).s(0.2f * (hot ? 1.3f : 1.f));
        ItemStack one = c.items[k]; one.n = 1;
        tactileItem(one, p.m, c.L);
      }
      // (renderMess: the other page heaped loose on the bottom, where the chest's own seed throws them)
      u32 h = (u32)(c.at.x * 73856093) ^ (u32)(c.at.y * 19349663) ^ (u32)(c.at.z * 83492791) ^ 0x9E37u;
      auto rnd = [&] { h = h * 1664525u + 1013904223u; return (h >> 8 & 0xFFFF) / 65535.f; };
      for (int i = 0; i < 15; i++) {
        const float jx = (rnd() - 0.5f) * 10, jz = (rnd() - 0.5f) * 7, dy = rnd() * 2.5f, spin = rnd() * 360, tx = (rnd() - 0.5f) * 60, tz = (rnd() - 0.5f) * 60;
        const int k = (1 - c.page) * 15 + i;
        if (k >= 27 || c.items[k].empty()) continue;
        TPose p = base; p.t((8 + jx) / 16, (2 + dy) / 16, (6.5f + jz) / 16).ry(spin).rx(90 + tx).rz(tz).s(0.28f);
        ItemStack one = c.items[k]; one.n = 1;
        tactileItem(one, p.m, c.L);
      }
    }
    VR.end();
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    for (const Chest& c : list) {
      // its own lid and buttons, in its own wood (assets/local_models.h)
      const StationModel* SM = stationModel(w.getBlock(c.at.x, c.at.y, c.at.z));
      auto partOf = [&](const char* n) -> const PackModel* { if (SM) { auto it = SM->parts.find(n); if (it != SM->parts.end() && it->second && !it->second->boxes.empty()) return it->second.get(); } return tactilePart(n); };
      const PackModel* lidM = partOf("coffre_lid");
      const PackModel* sortM = partOf("coffre_buttontri");
      const PackModel* swapM = partOf("coffre_buttonswap");
      const float lid = tactileEase(c.at, 2, c.open, dt) * 90.f;
      const vec2 light(c.L.sky, c.L.block);
      auto part = [&](const PackModel* m, float press) {
        if (!m) return;
        TPose p = tactileBlockPose(f, c.at, c.facing);
        p.t(0.5f, 0.625f, 0.75f).rx(lid).t(-0.5f, -0.625f, -0.75f);
        if (press != 0) p.t(0, press * 1.2f / 16, 0);
        tactilePartDraw(s.app, cd, *m, p.m, light);
      };
      part(lidM, 0);
      if (lid > 0.1f) {   // (the sheet inside the lid in our own paper: the maps' parchment, both faces, over Tactile's)
        TPose p = tactileBlockPose(f, c.at, c.facing);
        p.t(0.5f, 0.625f, 0.75f).rx(lid).t(-0.5f, -0.625f, -0.75f);
        p.t(6.f / 16, 10.75f / 16, 7.f / 16).rx(90).s(6.f / 16, 8.f / 16, 1);
        cd.picture(mapPaperTexture(), p.m, vec4(0, 0, 1, 1), vec4(1), light, true);
      }
      if (lid > 0.1f) { part(sortM, pressOf(c.at, 0, c.press[0], f.time)); part(swapM, pressOf(c.at, 1, c.press[1], f.time)); }
      if (lid > 0.1f && c.hover >= 0 && c.page * 15 + c.hover < 27) {   // (our chests hold 27: the second page has 12 spots)   // (renderAffiche: the item's name and lines written inside the lid)
        const ItemStack& it = c.items[c.page * 15 + c.hover];
        if (!it.empty()) affiche(cd, f, c, lid, it);
      }
    }
    cd.end();
  }

  // a line of text in the UI font's own glyphs, on the lid (Tactile's drawTextLines: both faces, black)
  void affiche(ContentDraw& cd, const FrameCtx& f, const Chest& c, float lid, const ItemStack& it) {
    ImFont* font = ImGui::GetIO().Fonts->Fonts.empty() ? nullptr : ImGui::GetIO().Fonts->Fonts[0];
    if (!font) return;
    const GLuint atlas = (GLuint)(intptr_t)ImGui::GetIO().Fonts->TexID;
    std::vector<std::string> lines = itemTooltipLines(it);
    if (lines.size() > 5) lines.resize(5);
    lines.push_back("\xC3\x97" + std::to_string(it.n));
    const float fh = font->FontSize;
    float z = 3.5f;
    for (size_t i = 0; i < lines.size(); i++) {
      const bool name = i == 0;
      const float h = name ? 2.f : 1.3f, scale = (name ? 0.005f : 0.0035f) * 9.f / fh;   // (Tactile's scale is for a 9-pixel font)
      const float maxW = 8.f * 0.98f / (scale * 16.f);
      // wrapped to the sheet's width (2..10 sixteenths)
      std::vector<std::string> wrapped; std::string cur, word;
      auto width = [&](const std::string& t) { return font->CalcTextSizeA(fh, 1e9f, 0, t.c_str()).x; };
      for (size_t k = 0; k <= lines[i].size(); k++) {
        const char ch = k < lines[i].size() ? lines[i][k] : ' ';
        if (ch != ' ') { word += ch; continue; }
        const std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && width(tryLine) > maxW) { wrapped.push_back(cur); cur = word; } else cur = tryLine;
        word.clear();
      }
      if (!cur.empty()) wrapped.push_back(cur);
      const float cz = z + h * 0.5f;
      for (float xr : {90.f}) {   // (ours are pictures with two faces: one copy reads right from the open side; Tactile wrote both)
        TPose p = tactileBlockPose(f, c.at, c.facing);
        p.t(0.5f, 0.625f, 0.75f).rx(lid).t(-0.5f, -0.625f, -0.75f);
        p.t(6.f / 16, 0.634375f, cz / 16).rx(xr).ry(180).s(scale, -scale, scale);
        const float total = wrapped.size() * fh;
        for (size_t l = 0; l < wrapped.size(); l++) {
          float x = -width(wrapped[l]) / 2;
          const float y = -total / 2 + l * fh;
          for (const char* q = wrapped[l].c_str(); *q;) {
            unsigned int cp = 0; const int n = ImTextCharFromUtf8(&cp, q, nullptr); q += std::max(1, n);
            const ImFontGlyph* g = font->FindGlyph((ImWchar)cp);
            if (!g) continue;
            if (g->Visible) {
              TPose gq = p; gq.t(x + (g->X0 + g->X1) / 2, y + (g->Y0 + g->Y1) / 2, 0).s(g->X1 - g->X0, g->Y1 - g->Y0, 1);
              cd.picture(atlas, gq.m, vec4(g->U0, g->V0, g->U1, g->V1), vec4(0.08f, 0.06f, 0.05f, 1.f), vec2(c.L.sky, c.L.block), true);
            }
            x += g->AdvanceX;
          }
        }
      }
      z += h;
    }
  }
};
}  // namespace

std::vector<std::string> itemTooltipLines(const ItemStack& s) {
  std::vector<std::string> out;
  const ItemDef* d = reg().item(s.id);
  out.push_back(s.tag.is_object() && s.tag.contains("name") && s.tag["name"].is_string() ? s.tag["name"].get<std::string>() : d ? d->name : s.id);
  if (d && d->props.contains("desc") && d->props["desc"].is_string()) out.push_back(d->props["desc"].get<std::string>());
  return out;
}

void registerTactileChests() { registerRenderLayer([](GameSession&) { return std::make_shared<TactileChests>(); }); }

} // namespace dq
