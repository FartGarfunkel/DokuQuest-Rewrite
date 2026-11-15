// render/hands_chests.cpp — a chest drawn as the hands-on stations' chest over ours (the chest renderer; worked by
// sim/hands_chest.cpp): its lid on its hinge a quarter of the way in (so it opens up over the chest, never back into the
// wall behind), the two buttons on the lid pressing in, the page on the tray in its five by three, the other page heaped
// loose at the bottom, the one you look at lifted and larger with its name and lines written inside the lid
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/hands_kit.h"
#include "render/content_draw.h"
#include "render/map_render.h"
#include "render/tile_box.h"
#include "assets/local_models.h"
#include "sim/hands.h"
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

struct Chest { ivec3 at; int facing; bool open; int page; std::vector<ItemStack> items; int press[2]; VoxelLight L; int hover = -1;
               ChestLayout lay;   // (a large chest: one chest over both halves, sim/hands.h)
               bool barrel = false; float trayY = 9.5f, floorY = 2.f, lr = 10.f; };   // (a barrel: the chest's workings under its own top)

class HandsChests : public RenderLayer {
public:
  const char* name() const override { return "hands-chests"; }
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

  // the chest's lid frame (its own sixteenths, as shut, hinge at y 10, z 12) where it is now; a barrel's on its own top
  static TPose lidPose(const FrameCtx& f, const Chest& c, float lidDeg) {
    TPose p = handsBlockPose(f, c.at, c.facing);
    if (c.barrel) p.t(0, c.lr / 16, 1).rx(lidDeg).t(0, -10.f / 16, -12.f / 16);
    else p.t(0.5f, 0.625f, 0.75f).rx(lidDeg).t(-0.5f, -0.625f, -0.75f);
    return p;
  }
  // what a chest's half shows: the whole chest's slots (a large one's 54, both halves'), its page and buttons (a large
  // chest's kept by its left half), the slot looked at on its tray
  static void fill(GameSession& s, World& w, Chest& c, const json* e, vec3 eye, vec3 dir) {
    if (!c.barrel) c.lay = handsChestLayout(w, c.at); else c.lay.key = c.at;   // (a barrel: its own record, the chest's page size)
    ivec3 hs[2]; const int nh = c.barrel ? 1 : chestHalves(w, c.at, hs);
    if (nh == 2) for (int h = 0; h < 2; h++) { const auto it = chestHalfItems(*s.level, s.player, w, hs[h]); c.items.insert(c.items.end(), it.begin(), it.end()); }
    else c.items = chestHalfItems(*s.level, s.player, w, c.at);   // (an ender chest: your own)
    c.items.resize(std::max<size_t>(c.items.size(), 27));
    const json* ke = c.lay.key == c.at ? e : w.blockEntity(c.lay.key.x, c.lay.key.y, c.lay.key.z);
    c.page = ke ? ke->value("page", 0) & 1 : 0;
    c.press[0] = ke ? ke->value("press0", 0) : 0; c.press[1] = ke ? ke->value("press1", 0) : 0;
    c.L = handsLight(w, c.at);
    if (c.open && s.target.hit && s.target.block == c.at) {
      const HandsChestAim a = c.barrel ? handsBarrelAim(w, c.at, eye, dir) : handsChestAim(w, c.at, eye, dir);
      if (a.what == HandsChestAim::Tray && c.page * c.lay.pg + c.lay.off + a.slot < c.lay.slots) c.hover = a.slot;
    }
  }
  // the lid without its sheet (its one flat part): a large chest's lid that hasn't the sheet
  std::map<const PackModel*, PackModel> bare;
  const PackModel* bareLid(const PackModel* m) {
    if (!m) return m;
    auto it = bare.find(m);
    if (it == bare.end()) {
      PackModel b; for (const PackModelBox& x : m->boxes) if (x.from.y != x.to.y) b.boxes.push_back(x);
      it = bare.emplace(m, std::move(b)).first;
    }
    return &it->second;
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player || !s.app.settings.handsStations) return;
    World& w = s.world();
    list.clear();
    const vec3 eye(s.player->pos.x, s.player->pos.y + s.player->eye, s.player->pos.z);
    const vec3 dir = lookVector(s.player->yaw, s.player->pitch);
    forBlockEntities(w, f, 24.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const ivec3 at(x, y, z);
      const bool barrel = handsBarrel(w, at);
      if (!barrel && (!stationModel(w.getBlock(x, y, z)) || !handsChest(w, at))) return;
      Chest c; c.at = at; c.facing = barrel ? handsBarrelFacing(w, at) : handsFacing(w, at); c.barrel = barrel;
      if (barrel) { c.lr = chestLidRow(w, at) / 2.f; c.trayY = c.lr - 1.f; c.floorY = chestFloorY(w, at) * 16 + 1.f; } c.open = chestLidOpen(w, at);
      fill(s, w, c, &e, eye, dir);
      list.push_back(c);
    });
    // a large chest's half with no record of its own yet (never opened by itself): drawn as its partner is, empty
    for (size_t i = 0, n = list.size(); i < n; i++) {
      if (list[i].barrel) continue;
      ivec3 hs[2]; if (chestHalves(w, list[i].at, hs) != 2) continue;
      const ivec3 o = hs[0] == list[i].at ? hs[1] : hs[0];
      if (w.blockEntity(o.x, o.y, o.z)) continue;
      Chest c; c.at = o; c.facing = handsFacing(w, o); c.open = chestLidOpen(w, o);
      fill(s, w, c, nullptr, eye, dir);
      list.push_back(c);
    }
    // chests with no record yet: their lid still drawn (shut)
    if (list.empty()) return;
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    for (const Chest& c : list) {
      const float lid = handsEase(c.at, 2, c.open, 0);
      if (lid <= 0.001f) continue;
      const TPose base = handsBlockPose(f, c.at, c.facing);
      for (int i = 0; i < 15; i++) {   // (renderGrid: the page on the tray)
        const int k = c.page * c.lay.pg + c.lay.off + i;
        if (k >= c.lay.slots || k >= (int)c.items.size() || c.items[k].empty()) continue;
        const bool hot = i == c.hover;
        TPose p = base; p.t(COL_X[i % 5] / 16, (c.trayY + (hot ? 2.f : 0.f)) / 16, ROW_Z[i / 5] / 16).rx(90).s(0.2f * (hot ? 1.3f : 1.f));
        ItemStack one = c.items[k]; one.n = 1;
        handsItem(one, p.m, c.L);
      }
      // (renderMess: the other page heaped loose on the bottom, where the chest's own seed throws them)
      u32 h = (u32)(c.at.x * 73856093) ^ (u32)(c.at.y * 19349663) ^ (u32)(c.at.z * 83492791) ^ 0x9E37u;
      auto rnd = [&] { h = h * 1664525u + 1013904223u; return (h >> 8 & 0xFFFF) / 65535.f; };
      for (int i = 0; i < 15; i++) {
        const float jx = (rnd() - 0.5f) * 10, jz = (rnd() - 0.5f) * 7, dy = rnd() * 2.5f, spin = rnd() * 360, tx = (rnd() - 0.5f) * 60, tz = (rnd() - 0.5f) * 60;
        const int k = (1 - c.page) * c.lay.pg + c.lay.off + i;
        if (k >= c.lay.slots || k >= (int)c.items.size() || c.items[k].empty()) continue;
        TPose p = base; p.t((8 + jx) / 16, (c.floorY + dy) / 16, (6.5f + jz) / 16).ry(spin).rx(90 + tx).rz(tz).s(0.28f);
        ItemStack one = c.items[k]; one.n = 1;
        handsItem(one, p.m, c.L);
      }
    }
    VR.end();
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    for (const Chest& c : list) {
      // its own lid and buttons, in its own wood (assets/local_models.h)
      const StationModel* SM = stationModel(w.getBlock(c.at.x, c.at.y, c.at.z));
      auto partOf = [&](const char* n) -> const PackModel* { if (SM) { auto it = SM->parts.find(n); if (it != SM->parts.end() && it->second && !it->second->boxes.empty()) return it->second.get(); } return handsPart(n); };
      const PackModel* lidM = partOf("chest_lid");
      const PackModel* sortM = partOf("chest_button_sort");
      const PackModel* swapM = partOf("chest_button_page");
      const float lid = handsEase(c.at, 2, c.open, dt) * (c.barrel ? 71.6f : 90.f);   // (a barrel's top: render/chest_layer.cpp's 1.25)
      const vec2 light(c.L.sky, c.L.block);
      auto part = [&](const PackModel* m, float press) {
        if (!m) return;
        TPose p = lidPose(f, c, lid);
        if (press != 0) p.t(0, press * 1.2f / 16, 0);
        handsPartDraw(s.app, cd, *m, p.m, light);
      };
      if (!c.barrel) part(c.lay.sheet ? lidM : bareLid(lidM), 0);   // (a barrel's own top is render/chest_layer.cpp's; a large chest's right lid has no sheet)
      if (lid > 0.1f && c.lay.sheet) {   // (the sheet inside the lid in our own paper: the maps' parchment, both faces, over hands-on's)
        TPose p = lidPose(f, c, lid);
        p.t(6.f / 16, 10.75f / 16, 7.f / 16).rx(90).s(6.f / 16, 8.f / 16, 1);
        cd.picture(mapPaperTexture(), p.m, vec4(0, 0, 1, 1), vec4(1), light, true);
      }
      if (lid > 0.1f && c.lay.buttons) { part(sortM, pressOf(c.at, 0, c.press[0], f.time)); part(swapM, pressOf(c.at, 1, c.press[1], f.time)); }
      if (lid > 0.1f && c.hover >= 0) {   // (the sheet: the item's name and lines written on the sheet: a large chest's on its left lid)
        const int k = c.page * c.lay.pg + c.lay.off + c.hover;
        const Chest* sheet = &c;
        if (!c.lay.sheet) { sheet = nullptr; for (const Chest& o : list) if (o.at == c.lay.key) sheet = &o; }
        if (sheet && k < c.lay.slots && k < (int)c.items.size() && !c.items[k].empty()) writeSheet(cd, f, *sheet, handsEase(sheet->at, 2, sheet->open, 0) * 90.f, c.items[k]);
      }
    }
    cd.end();
  }

  // a line of text in the UI font's own glyphs, on the lid (the hands-on sheet: both faces, black)
  void writeSheet(ContentDraw& cd, const FrameCtx& f, const Chest& c, float lid, const ItemStack& it) {
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
      const float h = name ? 2.f : 1.3f, scale = (name ? 0.005f : 0.0035f) * 9.f / fh;   // (the hands-on scale is for a 9-pixel font)
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
      for (float xr : {90.f}) {   // (ours are pictures with two faces: one copy reads right from the open side; hands-on wrote both)
        TPose p = lidPose(f, c, lid);
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

void registerHandsChests() { registerRenderLayer([](GameSession&) { return std::make_shared<HandsChests>(); }); }

} // namespace dq
