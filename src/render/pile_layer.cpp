// render/pile_layer.cpp — things set down in the world (game/entities.js blockEntityDraws): item piles
// (sneak + use with an item: blocks stack up, ingots lie in criss-crossed layers, tools lie flat, bottles stand,
// the rest in a little heap) and placed backpacks. The blocks themselves are invisible (render NONE); their
// block entities carry the stacks. Item frames show their item the same way.
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "client/app.h"
#include "assets/voxel_paint.h"
#include "world/shapes.h"
#include <regex>

namespace dq {
namespace {

class PileLayer : public RenderLayer {
public:
  const char* name() const override { return "item-piles"; }
  int order() const override { return 110; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    auto begin = [&] { if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; } };
    forBlockEntities(w, f, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const std::string type = e.value("type", "");
      if (type != "pile" && type != "backpack" && type != "frame") return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (glm::dot(rel - vec3(0.5f), rel - vec3(0.5f)) > 48.f * 48.f || !f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) return;
      u8 l = w.getLight(x, y, z);
      VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      // resting on a bottom half slab: sit half a block lower
      float yOff = 0;
      {
        BlockId bl = w.getBlock(x, y - 1, z);
        if (bl && reg().tShape[bl] == ShapeKind::Slab && !(w.getMeta(x, y - 1, z) & 7)) yOff = -0.5f;
      }
      mat4 base = glm::translate(mat4(1), rel + vec3(0, yOff, 0));
      if (type == "frame") {
        // an item frame: the item sits flat against the wall behind it, turned in eighths (glow frames light it fully)
        if (!e.contains("item") || !e["item"].is_object()) return;
        ItemStack st = ItemStack::fromJson(e["item"]);
        if (st.empty() || !st.def()) return;
        const VoxelModel& im = itemModel(st.id);
        if (!im.ready()) return;
        begin();
        static const float TURN[4] = {0, -PI / 2, PI, PI / 2};
        int f = w.getMeta(x, y, z) & 3;
        bool glow = reg().block(w.getBlock(x, y, z)).num("frame") == 2;
        mat4 m = glm::translate(mat4(1), rel + vec3(0.5f));
        m = glm::rotate(m, TURN[f], vec3(0, 1, 0));
        m = glm::translate(m, vec3(0, 0, -0.5f + std::max(0.09f, im.depth * 0.55f + 0.07f)));   // thick things stand out from the frame
        m = glm::rotate(m, -(float)e.value("rot", 0) * PI / 4, vec3(0, 0, 1));
        m = glm::scale(m, vec3(im.kind == VoxelModel::Block ? 0.36f : 0.55f));
        VR.draw(im.gl, m * im.matrix(), glow ? VoxelLight{1, 1} : L);
        return;
      }
      if (type == "backpack") {
        if (!e.contains("stack")) return;
        ItemStack st = ItemStack::fromJson(e["stack"]);
        if (st.empty() || !st.def()) return;
        const VoxelModel& im = itemModel(st.id);
        if (!im.ready()) return;
        begin();
        float yaw = e.value("yaw", 0.f);
        mat4 m = glm::translate(base, vec3(0.5f, 0.36f, 0.5f));
        m = glm::rotate(m, yaw + PI, vec3(0, 1, 0));   // standing up, its pouch toward you
        m = glm::scale(m, vec3(0.64f));
        VR.draw(im.gl, m * im.matrix(), L);
        return;
      }
      if (!e.contains("items") || !e["items"].is_array()) return;
      const json& items = e["items"];
      for (size_t k = 0; k < items.size() && k < 4; k++) {
        if (!items[k].is_object()) continue;
        ItemStack st = ItemStack::fromJson(items[k]);
        if (st.empty() || !st.def()) continue;
        const VoxelModel& im = itemModel(st.id);
        if (!im.ready()) continue;
        begin();
        float r0 = e.contains("rot") && e["rot"].is_array() && k < e["rot"].size() && e["rot"][k].is_number() && e["rot"][k].get<float>() != 0 ? e["rot"][k].get<float>() : (float)k * 0.7f;
        float ox = (float)(k % 2) * 0.5f + 0.25f, oz = (float)(k / 2) * 0.5f + 0.25f;
        static const std::regex BAR("ingot|brick|nugget|bar$");
        bool bar = std::regex_search(st.id, BAR), tool = im.kind != VoxelModel::Block && isHandheld(st.id);
        bool block = im.kind == VoxelModel::Block;
        int n = st.n;
        int copies = block ? std::min(4, (n + 15) / 16) : bar ? std::min(16, (n + 3) / 4) : tool ? 1 : std::min(5, (n + 11) / 12);
        for (int c = 0; c < copies; c++) {
          mat4 m = base;
          float j = (float)((k * 7 + c * 13) % 10) / 10.f - 0.5f;
          if (block) {
            m = glm::translate(m, vec3(ox, 0.125f + c * 0.25f, oz)); m = glm::rotate(m, r0 + c * 0.25f, vec3(0, 1, 0)); m = glm::scale(m, vec3(0.25f));
          } else if (bar) {
            int layer = c / 2; float side = c % 2 ? 1.f : -1.f;
            m = glm::translate(m, vec3(ox, 0.03f + layer * 0.055f, oz)); m = glm::rotate(m, r0 + (layer % 2) * PI / 2, vec3(0, 1, 0));
            m = glm::translate(m, vec3(side * 0.065f, 0, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.36f));
          } else if (tool) {
            m = glm::translate(m, vec3(ox, (im.depth > 0 ? im.depth : 0.03f) * 0.6f + 0.005f, oz)); m = glm::rotate(m, r0 + PI / 4, vec3(0, 1, 0));
            m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.6f));
          } else if (im.stand) {
            if (c >= 2) break;   // bottles, buckets and bowls stand up
            m = glm::translate(m, vec3(ox + (c ? 0.12f : 0.f), 0.225f, oz + (c ? -0.1f : 0.f))); m = glm::rotate(m, r0 + c * 1.3f, vec3(0, 1, 0)); m = glm::scale(m, vec3(0.45f));
          } else {
            float dz = (im.depth > 0 ? im.depth : 0.02f) * 0.45f;
            m = glm::translate(m, vec3(ox + j * 0.05f, dz + 0.005f + c * dz * 1.8f, oz - j * 0.05f)); m = glm::rotate(m, r0 + c * 0.5f + j, vec3(0, 1, 0));
            m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.45f));
          }
          VR.draw(im.gl, m * im.matrix(), L);
        }
      }
    });
    if (begun) VR.end();
  }
};

} // namespace

void registerPileLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<PileLayer>(); }); }

} // namespace dq
