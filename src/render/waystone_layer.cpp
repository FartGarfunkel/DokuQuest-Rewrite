// render/waystone_layer.cpp — the waystones (a pillar two blocks tall: tools/port/polish_data.py) as the Waystones mod's
// look them: the runes cut down the shaft light up on the ones you're attuned to (a glowing inlay over each groove,
// breathing slowly), violet motes drift up round them, and close by the stone's name stands over it. One you haven't
// found keeps its runes dark, a mote now and then the only sign it's more than stone. The record of which ones you know
// is Player::stats["ways"] (sim/travel.h wayKnown); the stone's name and id are its block entity {type:"waystone"}.
#include "client/session.h"
#include "client/app.h"
#include "render/be_scan.h"
#include "render/item_layers.h"
#include "assets/voxel.h"
#include "sim/travel.h"
#include "core/registry.h"
#include "ui/theme.h"

namespace dq {
namespace {

// the glowing inlay over the rune grooves of the shaft's four faces, both blocks: a grid of 1/32 block, 32 x 64 x 32
// (data/waystone_runes.json: the cells of the "waystone_rune" tile, 16 x 16, y down, a block pixel each)
bool buildRuneGrid(VoxelGrid& g) {
  const std::optional<json> jf = readJson(dataDir() / "waystone_runes.json");
  if (!jf || !jf->is_object() || !jf->contains("cells")) return false;
  const json& j = *jf;
  g.resize(ivec3(32, 64, 32));
  const u32 lit = voxRGBA(170, 236, 255), core = voxRGBA(236, 252, 255);
  for (auto& c : j["cells"]) {
    const int cx = c[0].get<int>(), cy = c[1].get<int>();
    const int ypx = 15 - cy;   // the cell's block pixel from the bottom (v = 16 - y)
    for (int half = 0; half < 2; half++) {   // the lower block's shaft is y 5..16, the upper's 0..9 (the cap above)
      if (half == 0 ? ypx < 5 : ypx >= 9) continue;
      const int y0 = half * 32 + ypx * 2;
      for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
        const int y = y0 + b;
        const u32 col = (a + b) % 2 ? lit : core;
        g.set(26, y, 2 * (15 - cx) + a, col);   // +x: u = 16 - z
        g.set(5, y, 2 * cx + a, col);           // -x: u = z
        g.set(2 * cx + a, y, 26, col);          // +z: u = x
        g.set(2 * (15 - cx) + a, y, 5, col);    // -z: u = 16 - x
      }
    }
  }
  return true;
}

class WaystoneLayer : public RenderLayer {
public:
  const char* name() const override { return "waystones"; }
  int order() const override { return 121; }
  void update(GameSession& s, double dt) override {
    time += dt;
    if (!s.player) { stones.clear(); return; }
    World& w = s.world();
    static const BlockId lower = reg().blockId("waystone");
    if (!lower) return;
    if ((scanT -= dt) <= 0) {   // the stones near the camera, a few times a second
      scanT = 0.5;
      stones.clear();
      forBlockEntities(w, s.frame, 40.f, [&](const Chunk&, json& e, int x, int y, int z) {
        if (e.value("type", "") != "waystone" || w.getBlock(x, y, z) != lower) return;
        Stone st; st.at = ivec3(x, y, z); st.name = e.value("name", std::string("A waystone"));
        st.known = wayKnown(*s.player, wayIdAt(w, x, y, z));
        stones.push_back(st);
      });
    }
    moteAcc += dt;
    if (moteAcc < 0.06) return;
    moteAcc = 0;
    const vec3 cam = vec3(s.frame.cam.pos);
    Level& lv = *s.level;
    for (auto& st : stones) {
      const vec3 c = vec3(st.at) + vec3(0.5f, 0, 0.5f);
      if (glm::distance(c, cam) > 24.f) continue;
      if (frand() > (st.known ? 0.55f : 0.08f)) continue;
      const float a = frand() * TAU, r = 0.45f + frand() * 0.35f;
      lv.fx->particles("mana_arcane", c + vec3(std::cos(a) * r, 0.2f + frand() * 1.6f, std::sin(a) * r), 1,
        {{"vel", {std::cos(a) * 0.1f, 0.25f + frand() * 0.3f, std::sin(a) * 0.1f}}, {"life", 1.4f + frand() * 0.8f}, {"size", 0.05f + frand() * 0.03f},
         {"glow", true}, {"grav", 0}, {"shrink", true}});
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    bool any = false;
    for (auto& st : stones) if (st.known) any = true;
    if (!any) return;
    if (!runes) {
      runes = std::make_unique<VoxelMeshGL>();
      VoxelGrid g; VoxelMeshData d;
      if (buildRuneGrid(g)) { buildVoxelMesh(g, d); runes->upload(d); }
    }
    if (!runes->ready()) return;
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    const float breath = 0.5f + 0.5f * std::sin((float)time * 1.6f);
    for (auto& st : stones) {
      if (!st.known) continue;
      const vec3 rel = f.cam.rel(glm::dvec3(st.at.x, st.at.y, st.at.z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f, 1, 0.5f), 1.4f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      VR.draw(*runes, glm::scale(glm::translate(mat4(1), rel), vec3(1.f / 32)), VoxelLight{1, 1}, vec4(0.85f, 0.97f, 1.f, 0.15f + 0.3f * breath));
    }
    if (begun) VR.end();
  }
  void drawHud(GameSession& s) override {   // the stone's name over it, up close
    if (!s.player || s.hudHidden || stones.empty()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = (float)s.app.width, H = (float)s.app.height, z = ui::Z();
    Camera& cam = s.frame.cam;
    for (auto& st : stones) {
      const vec3 top = vec3(st.at) + vec3(0.5f, 2.35f, 0.5f);
      const float d = glm::distance(top, vec3(cam.pos));
      if (d > 10.f) continue;
      vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(top)), 1);
      if (c.w <= 0.1f) continue;
      const ImVec2 sp((c.x / c.w * 0.5f + 0.5f) * W, (1 - (c.y / c.w * 0.5f + 0.5f)) * H);
      const char* nm = st.name.c_str();
      const ImVec2 ts = ui::textSize(ui::F_MID, nm);
      const int alpha = (int)(255 * std::clamp((10.f - d) / 3.f, 0.f, 1.f));
      dl->AddRectFilled(ImVec2(sp.x - ts.x / 2 - 6 * z, sp.y - ts.y), ImVec2(sp.x + ts.x / 2 + 6 * z, sp.y), IM_COL32(0, 0, 0, alpha * 107 / 255), 2 * z);
      ui::textShadow(dl, ui::F_MID, ImVec2(sp.x - ts.x / 2, sp.y - ts.y), st.known ? IM_COL32(190, 240, 255, alpha) : IM_COL32(220, 220, 220, alpha), nm, ImVec2(z, z), IM_COL32(0, 0, 0, alpha));
    }
  }

private:
  struct Stone { ivec3 at; std::string name; bool known = false; };
  std::vector<Stone> stones;
  std::unique_ptr<VoxelMeshGL> runes;
  double time = 0, scanT = 0, moteAcc = 0;
};

} // namespace

void registerWaystoneLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<WaystoneLayer>(); });
}

} // namespace dq
