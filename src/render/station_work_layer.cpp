// render/station_work_layer.cpp — what lies on the grindstone and the stonecutter (sim/station_work.h), and their scenes
// (ui/cinematic.h): the two pieces lifted to the wheel and stroked on it, the stone slid into the saw and the pieces out
// the other side, the shapes it cuts into on a dial over the blade (Cinematic Interactions' GrindstoneStation /
// StonecutterStation)
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "client/app.h"
#include "client/session.h"
#include "ui/cinematic.h"
#include "sim/station_work.h"
#include "core/tool_parts.h"
#include "core/item_glint.h"
#include "core/registry.h"

namespace dq {
std::string stackModelKey(const ItemStack& s);   // render/pile_layer.cpp

namespace {

double win(double t, double a, double b) { return clampv((t - a) / (b - a), 0.0, 1.0); }

// an item lying flat (or a block standing) at p, turned yaw about +y; size: a flat item's width (a block 0.6 of it)
void drawLying(VoxelRenderer& VR, const FrameCtx& f, const ItemStack& s, vec3 p, float yaw, float size, VoxelLight L) {
  if (s.empty()) return;
  const VoxelModel& im = itemModel(stackModelKey(s));
  if (!im.ready()) return;
  mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
  m = glm::rotate(m, yaw, vec3(0, 1, 0));
  if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.3f * size, 0)), vec3(0.6f * size));
  else if (im.stand) m = glm::scale(glm::translate(m, vec3(0, 0.36f * size, 0)), vec3(0.9f * size));
  else { m = glm::translate(m, vec3(0, (im.depth > 0 ? im.depth : 0.03f) * size + 0.004f, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(size)); }
  VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(s) ? 1.f : 0.f);
}

// an item upright facing you (a shape on the dial), turning slowly
void drawFloating(VoxelRenderer& VR, const FrameCtx& f, const ItemStack& s, vec3 p, float yaw, float size, VoxelLight L) {
  if (s.empty() || size <= 0.001f) return;
  const VoxelModel& im = itemModel(stackModelKey(s));
  if (!im.ready()) return;
  mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
  m = glm::rotate(m, yaw, vec3(0, 1, 0));
  m = glm::scale(m, vec3(im.kind == VoxelModel::Block ? size * 0.62f : size));
  if (im.kind != VoxelModel::Block) m = glm::translate(m, vec3(0, -0.5f, 0));   // (a flat item's origin is its foot)
  VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(s) ? 1.f : 0.f);
}

// the stone on the stonecutter's table: one block, two side by side (the second further from the saw), a third on them
void drawCutHeap(VoxelRenderer& VR, const FrameCtx& f, const ItemStack& st, vec3 lie, vec3 right, vec3 out, float yaw, VoxelLight L) {
  static const vec3 AT[3] = {{0, 0, 0}, {-0.15f, 0, 0.03f}, {-0.075f, 0.145f, 0.015f}};   // (right, up, out)
  for (int i = 0; i < std::min(st.n, 3); i++) {
    ItemStack one = st; one.n = 1;
    drawLying(VR, f, one, lie + right * AT[i].x + vec3(0, AT[i].y, 0) + out * AT[i].z, yaw + (i == 2 ? 0.3f : i * 0.12f), 0.24f, L);
  }
}

class StationWorkLayer : public RenderLayer {
public:
  const char* name() const override { return "station-work"; }
  int order() const override { return 111; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    auto begin = [&] { if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; } };
    GrindView gv; CutView cv;
    const bool grindOn = cinematicGrindView(gv), cutOn = cinematicCutView(cv);
    const float t = (float)f.time;
    forBlockEntities(w, f, 32.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const std::string type = e.value("type", "");
      if (type != "grindwork" && type != "cutwork") return;
      const ivec3 at(x, y, z);
      if ((grindOn && gv.at == at) || (cutOn && cv.at == at)) return;   // (the scene draws it, below)
      const vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) return;
      const u8 l = w.getLight(x, y, z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      vec3 out, right; stationWorkFrame(w, at, out, right);
      const float yaw = std::atan2(out.x, out.z);
      begin();
      if (type == "grindwork") {
        const std::vector<ItemStack> v = grindWorkItems(w, at);
        for (int k = 0; k < 2; k++) drawLying(VR, f, v[k], grindLiePos(w, at, k), yaw + (k ? -0.35f : 0.35f), 0.34f, L);
      } else {
        const ItemStack st = cutWorkItem(w, at);
        drawCutHeap(VR, f, st, cutLiePos(w, at), right, out, yaw, L);
      }
    });
    if (grindOn && w.getBlock(gv.at.x, gv.at.y, gv.at.z)) {
      begin();
      const ivec3 at = gv.at;
      const u8 l = w.getLight(at.x, at.y, at.z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      vec3 out, right; stationWorkFrame(w, at, out, right);
      const float yaw = std::atan2(out.x, out.z);
      if (gv.t < 0) {
        const std::vector<ItemStack> v = grindWorkItems(w, at);
        for (int k = 0; k < 2; k++) {
          const float lift = gv.hover == k ? 0.02f + 0.006f * std::sin(t * 4.f) : 0.f;   // (the one under the mouse rises a little)
          drawLying(VR, f, v[k], grindLiePos(w, at, k) + vec3(0, lift, 0), yaw + (k ? -0.35f : 0.35f), 0.34f, L);
        }
      } else if (gv.t < GRIND_LENGTH) {
        // both pieces up on the wheel; once the grinding has cleaned it (GRIND_CLEAN) the first is what it became, the second
        // gone if it went into it; let go (GRIND_LET_GO): the result back off the wheel toward you, smaller, into the bag
        const bool clean = gv.t >= GRIND_CLEAN;
        const float away = (float)win(gv.t, GRIND_LET_GO, GRIND_LENGTH);
        const vec3 toYou = vec3(std::sin(gv.viewYaw), 0, std::cos(gv.viewYaw));
        for (int k = 0; k < 2; k++) {
          const int resultSide = gv.before[0].empty() ? 1 : 0;   // (the result where the first piece was)
          const ItemStack st = !clean ? gv.before[k] : k == resultSide ? gv.after : ItemStack();
          if (st.empty()) continue;
          vec3 p = grindHeldPos(at, gv.viewYaw, k, std::min(gv.t, GRIND_LET_GO), grindLiePos(w, at, k));
          p += toYou * (0.45f * away) + vec3(0, 0.15f * away, 0);
          // stood up on its edge against the wheel while grinding (a flat piece), laid back down as it's let go
          const float stand = (float)(win(gv.t, 0.28, GRIND_CONTACT) * (1 - win(gv.t, GRIND_LET_GO - 0.1, GRIND_LET_GO + 0.2)));
          const VoxelModel& im = itemModel(stackModelKey(st));
          if (!im.ready()) continue;
          const float size = 0.34f * (1 - 0.6f * away);
          mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
          m = glm::rotate(m, gv.viewYaw + (k ? -0.25f : 0.25f), vec3(0, 1, 0));
          if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.3f * size, 0)), vec3(0.6f * size));
          else { m = glm::translate(m, vec3(0, 0.5f * size * stand + 0.01f, 0)); m = glm::rotate(m, -PI / 2 * (1 - stand), vec3(1, 0, 0)); m = glm::translate(m, vec3(0, -0.5f * size * stand, 0)); m = glm::scale(m, vec3(size)); }
          VR.draw(im.gl, m * im.matrix(), VoxelLight{L.sky, std::max(L.block, clean ? 0.f : 0.35f)}, vec4(0), itemGlints(st) && !clean ? 1.f : 0.f);
        }
      }
    }
    if (cutOn && w.getBlock(cv.at.x, cv.at.y, cv.at.z)) {
      begin();
      const ivec3 at = cv.at;
      const u8 l = w.getLight(at.x, at.y, at.z);
      const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      vec3 out, right; stationWorkFrame(w, at, out, right);
      const float yaw = std::atan2(out.x, out.z);
      const vec3 lie = cutLiePos(w, at);
      const vec3 blade = vec3(at) + vec3(0.5f, 0.5625f, 0.5f) - right * 0.25f;   // (the saw's plane, just as far along it)
      const ItemStack stone = cv.t >= 0 ? cv.input : cutWorkItem(w, at);
      if (cv.t < 0) {
        drawCutHeap(VR, f, stone, lie, right, out, yaw, L);
        // the dial: what it cuts into, on a half ring over the saw
        const auto opts = cutWorkOptions(w, at);
        const int n = (int)opts.size();
        for (int k = 0; k < n; k++) {
          const float shown = cutDialShown(k, n, cv.scroll);
          if (shown <= 0.01f) continue;
          const float size = 0.19f * shown * (cv.hover == k ? 1.3f : 1.f);
          const float spin = cv.hover == k ? t * 1.6f : 0.25f * std::sin(t * 0.7f + k);
          drawFloating(VR, f, ItemStack(opts[k].first, 1), cutDialPoint(at, cv.viewYaw, k, n, cv.scroll, t), cv.viewYaw + spin, size, VoxelLight{L.sky, std::max(L.block, 0.45f)});
        }
      } else {
        // the cut: a block (all of them, cutting all) slides from where it lay into the saw by CUT_BITE; the pieces come out
        // toward you on the far side, settle, then rise off to the bag
        const float len = (float)(cv.all ? CUT_ALL_LENGTH : CUT_LENGTH);
        const float slide = (float)(win(cv.t, 0.0, CUT_BITE));
        if (cv.t < CUT_BITE) {
          ItemStack one = stone; one.n = 1;
          const float judder = 0.006f * std::sin((float)cv.t * 90.f) * slide;
          drawLying(VR, f, one, lie + (blade - lie) * (float)(slide * slide) + right * judder, yaw, 0.24f, L);
        }
        if (cv.t >= CUT_BITE) {
          const float u = (float)win(cv.t, CUT_BITE, len);
          const float rise = (float)win(cv.t, len - 0.35f, len);
          const int pieces = std::clamp(cv.piece.n, 1, 4);
          for (int i = 0; i < pieces; i++) {
            ItemStack one = cv.piece; one.n = 1;
            const vec3 p = blade + right * (0.16f + 0.12f * std::min(1.f, u * 3.f) + i * 0.07f) + out * ((i - (pieces - 1) * 0.5f) * 0.14f * std::min(1.f, u * 3.f))
                         + vec3(0, 0.25f * rise, 0) + vec3(std::sin(cv.viewYaw), 0, std::cos(cv.viewYaw)) * (0.4f * rise);
            drawLying(VR, f, one, p, yaw + i * 0.4f, 0.18f * (1 - 0.5f * rise), L);
          }
          if (stone.n > 1 && !cv.all) {   // (what's left stays on the table)
            ItemStack rest = stone; rest.n -= 1;
            drawCutHeap(VR, f, rest, lie, right, out, yaw, L);
          }
        }
      }
    }
    if (begun) VR.end();
  }
};

}  // namespace

void registerStationWorkLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<StationWorkLayer>(); }); }

}  // namespace dq
