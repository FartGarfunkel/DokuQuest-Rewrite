// render/stand_layer.cpp — armour stands (render NONE blocks): a wooden figure on a stone plate, wearing whatever was
// put on it (the players' own armour meshes, assets/char_model armorMeshes) and holding a weapon in its right hand.
// The gear is the block entity's "items" (helmet, chest, legs, boots, hand; sim/build.cpp), "pose" 0..4.
// Meta & 3 = the way the placer looked; the stand faces back at them.
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "render/tile_box.h"
#include "client/app.h"
#include "assets/char_model.h"
#include "assets/item_models.h"
#include "core/itemstack.h"
#include "core/item_glint.h"

namespace dq {
namespace {

mat4 heldMatrix(float tilt, float s, vec2 grip, bool upright) {   // as render/creature_layer.cpp
  vec3 X, Y;
  if (upright) { X = vec3(0, 0, -s); Y = vec3(0, s * std::cos(tilt), -s * std::sin(tilt)); }
  else {
    vec3 F(0, std::sin(tilt), -std::cos(tilt)), Bn(0, -std::cos(tilt), -std::sin(tilt));
    float r2 = std::sqrt(0.5f); X = (F - Bn) * r2 * s; Y = (F + Bn) * r2 * s;
  }
  vec3 Z(s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}

struct Pose { vec3 armR, armL, head; };   // x pitch (+ swings forward), z roll (+ out to the side), in radians
const Pose POSES[5] = {
  {vec3(0.f, 0, 0.12f), vec3(0.f, 0, -0.12f), vec3(0)},                // standing easy
  {vec3(1.45f, 0, 0.05f), vec3(1.45f, 0, -0.05f), vec3(0)},             // arms out in front
  {vec3(2.9f, 0, 0.15f), vec3(0.f, 0, -0.1f), vec3(-0.2f, 0, 0)},       // one hand raised
  {vec3(0.f, 0, 1.45f), vec3(0.f, 0, -1.45f), vec3(0)},                 // arms out to the sides
  {vec3(0.9f, 0, 0.1f), vec3(-0.25f, 0, -0.45f), vec3(0.1f, 0, 0.12f)}, // the hero: blade forward, chin up
};

class StandLayer : public RenderLayer {
public:
  const char* name() const override { return "armour-stands"; }
  int order() const override { return 111; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    App& a = app();
    struct One { mat4 base; vec2 light; std::array<ItemStack, 5> gear; int pose; };
    std::vector<One> list;
    forBlockEntities(w, f, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("type", "") != "stand") return;
      if (!reg().block(w.getBlock(x, y, z)).has("armorStand")) return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f, 1, 0.5f), 1.6f)) return;
      u8 l = w.getLight(x, y, z), l2 = w.getLight(x, y + 1, z);
      vec2 light(std::max(l >> 4, l2 >> 4) / 15.f, std::max(l & 15, l2 & 15) / 15.f);
      int fdir = w.getMeta(x, y, z) & 3;
      mat4 base = glm::rotate(glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f)), PI - fdir * PI * 0.5f, vec3(0, 1, 0));
      One o{base, light, {}, clampv(e.value("pose", 0), 0, 4)};
      if (e.contains("items") && e["items"].is_array())
        for (size_t i = 0; i < 5 && i < e["items"].size(); i++) if (!e["items"][i].is_null()) o.gear[i] = ItemStack::fromJson(e["items"][i]);
      list.push_back(std::move(o));
    });
    if (list.empty()) return;
    // ---- the wooden figure ----
    ContentDraw& cd = contentDraw();
    static const BlockId PL = reg().blockId("planks"), ST = reg().blockId("smooth_stone");
    GLuint wood = tileTexture(a, reg().block(PL).faces[0]), stone = tileTexture(a, ST ? reg().block(ST).faces[2] : reg().block(PL).faces[2]);
    if (wood && cd.begin(f, f.cam.viewProj)) {
      for (auto& o : list) {
        const Pose& P = POSES[o.pose];
        mat4 b = glm::scale(o.base, vec3(1.f / 16));
        drawTileBox(cd, b, vec3(-6, 0, -6), vec3(6, 1, 6), stone, stone, stone, o.light, vec3(1), false);   // the plate
        for (float sx : {-2.f, 2.f}) drawTileBox(cd, b, vec3(sx - 1, 1, -1), vec3(sx + 1, 12.5f, 1), wood, wood, wood, o.light, vec3(1), false);
        drawTileBox(cd, b, vec3(-4, 12, -1), vec3(4, 14, 1), wood, wood, wood, o.light, vec3(1), false);    // hips
        drawTileBox(cd, b, vec3(-1, 14, -1), vec3(1, 23, 1), wood, wood, wood, o.light, vec3(1), false);    // spine
        drawTileBox(cd, b, vec3(-6, 22, -1.5f), vec3(6, 25, 1.5f), wood, wood, wood, o.light, vec3(1), false);   // shoulders
        drawTileBox(cd, part(b, vec3(0, 25, 0), P.head), vec3(-1, 0, -1), vec3(1, 7, 1), wood, wood, wood, o.light, vec3(1), false);   // neck and head stick
        drawTileBox(cd, part(b, vec3(6, 23, 0), P.armR), vec3(-1, -10, -1), vec3(1, 0, 1), wood, wood, wood, o.light, vec3(1), false);
        drawTileBox(cd, part(b, vec3(-6, 23, 0), P.armL, true), vec3(-1, -10, -1), vec3(1, 0, 1), wood, wood, wood, o.light, vec3(1), false);
      }
      cd.end();
    }
    // ---- what it wears and holds ----
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    for (auto& o : list) {
      const Pose& P = POSES[o.pose];
      mat4 b = glm::scale(o.base, vec3(1.f / 16));
      mat4 armR = part(b, vec3(6, 23, 0), P.armR), armL = part(b, vec3(-6, 23, 0), P.armL, true);
      auto partMat = [&](const std::string& pn) -> mat4 {
        if (pn == "head") return part(b, vec3(0, 25, 0), P.head);
        if (pn == "body") return glm::translate(b, vec3(0, 13, 0));
        if (pn == "armR_u") return armR;
        if (pn == "armR_d") return glm::translate(armR, vec3(0, -4, 0));
        if (pn == "armL_u") return armL;
        if (pn == "armL_d") return glm::translate(armL, vec3(0, -4, 0));
        if (pn == "legR_u") return glm::translate(b, vec3(2, 13, 0));
        if (pn == "legR_d") return glm::translate(b, vec3(2, 7, 0));
        if (pn == "legL_u") return glm::translate(b, vec3(-2, 13, 0));
        if (pn == "legL_d") return glm::translate(b, vec3(-2, 7, 0));
        return mat4(0);
      };
      VoxelLight L{o.light.x, o.light.y};
      for (int slot = 0; slot < 4; slot++) {
        const ItemStack& g = o.gear[slot];
        if (g.empty() || !reg().item(g.id)) continue;
        std::string dye;
        if (g.tag.is_object() && g.tag.contains("col") && g.tag["col"].is_string()) { std::string c = g.tag["col"]; dye = c.size() == 7 ? c.substr(1) : ""; }
        const ArmorSet& A = armorMeshes(g.id, dye, slot, false, false, a.tiles);
        for (auto& [pn, meshes] : A.byPart) {
          mat4 pm = partMat(pn);
          if (pm[3][3] == 0) continue;
          for (const CharMesh* cm : meshes) VR.draw(cm->gl, pm * cm->local(), L, vec4(0), itemGlints(g) ? 1.f : 0.f);
        }
      }
      const ItemStack& h = o.gear[4];
      if (!h.empty()) {
        const VoxelModel& im = itemModel(h.id);
        if (im.ready()) {
          static const mat4 TOOL = heldMatrix(0.25f, 0.85f, vec2(-0.34f, -0.24f), false), ITEM = heldMatrix(0.2f, 0.55f, vec2(0, -0.25f), true);
          mat4 hm = glm::translate(armR, vec3(0, -9, -0.5f));
          hm = glm::scale(hm, vec3(16));   // back to blocks for the item's own frame
          hm = hm * (isHandheld(h.id) ? TOOL : ITEM);
          VR.draw(im.gl, hm * im.matrix(), L, vec4(0), itemGlints(h) ? 1.f : 0.f);
        }
      }
    }
    VR.end();
  }
private:
  // a limb hung at pivot (px), pitched forward by r.x and swung out by r.z (mirrored for the left side)
  static mat4 part(const mat4& b, vec3 pivot, vec3 r, bool left = false) {
    mat4 m = glm::translate(b, pivot);
    (void)left;
    m = glm::rotate(m, r.z, vec3(0, 0, 1));
    m = glm::rotate(m, r.x, vec3(1, 0, 0));
    return m;
  }
};

} // namespace

void registerStandLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<StandLayer>(); }); }

} // namespace dq
