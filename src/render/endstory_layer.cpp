// render/endstory_layer.cpp — what the End, the dragons, the Deep Dark and the story show on screen besides the
// creatures themselves (those are the creature layer's): the watcher (haunt.js Watcher.draws: the default-skinned
// figure, or now and then a boar with his white eyes), thrown ender pearls and eyes, the Deep Dark's darkness
// pulsing at the edge of sight (deepdark.js darkTick), the rift's dread (riftarc.js .fx-dread), the story's line in
// the quest track (riftarc.js questHud) and the dragon eggs' countdowns (batchb.js egg tags).
#include "client/session.h"
#include "client/app.h"
#include "render/creature_draw.h"
#include "render/creature_models.h"
#include "assets/item_models.h"
#include "sim/creature_rig.h"
#include "sim/endstory.h"
#include "sim/player.h"
#include "core/registry.h"
#include "ui/theme.h"
#include "render/endstory_layer.h"
#include <cstdio>

namespace dq {
using namespace ui;   // text, textSize, Z, F_MID, col:: (ui/theme.h)

namespace {
// a radial gradient over the screen (CSS radial-gradient(ellipse at center, ...)): stops (fraction, rgba)
void radialStops(ImDrawList* dl, float W, float H, const std::vector<std::pair<float, ImU32>>& stops) {
  const int N = 48;
  ImVec2 c(W * 0.5f, H * 0.5f), R(W * 0.5f * 1.4142f, H * 0.5f * 1.4142f);
  ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  for (size_t s = 0; s + 1 < stops.size(); s++) {
    float f0 = stops[s].first, f1 = stops[s + 1].first; ImU32 c0 = stops[s].second, c1 = stops[s + 1].second;
    if (((c0 >> 24) & 255) == 0 && ((c1 >> 24) & 255) == 0) continue;
    dl->PrimReserve(N * 6, N * 4);
    for (int i = 0; i < N; i++) {
      float a0 = (float)i / N * TAU, a1 = (float)(i + 1) / N * TAU;
      ImVec2 d0(std::cos(a0) * R.x, std::sin(a0) * R.y), d1(std::cos(a1) * R.x, std::sin(a1) * R.y);
      ImDrawIdx b = (ImDrawIdx)dl->_VtxCurrentIdx;
      dl->PrimWriteVtx(ImVec2(c.x + d0.x * f0, c.y + d0.y * f0), uv, c0); dl->PrimWriteVtx(ImVec2(c.x + d1.x * f0, c.y + d1.y * f0), uv, c0);
      dl->PrimWriteVtx(ImVec2(c.x + d1.x * f1, c.y + d1.y * f1), uv, c1); dl->PrimWriteVtx(ImVec2(c.x + d0.x * f1, c.y + d0.y * f1), uv, c1);
      dl->PrimWriteIdx(b); dl->PrimWriteIdx(b + 1); dl->PrimWriteIdx(b + 2);
      dl->PrimWriteIdx(b); dl->PrimWriteIdx(b + 2); dl->PrimWriteIdx(b + 3);
    }
  }
}
ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)std::round(clampv(a, 0.f, 1.f) * 255)); }
float jf(const json& j, const char* k) { auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<float>() : 0.f; }
}

class EndStoryLayer : public RenderLayer {
public:
  const char* name() const override { return "endstory"; }
  int order() const override { return 11; }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player || f.portalDepth > 0) return;
    CreatureDraw& D = creatureDraw();
    World& w = s.world();
    float a = clampv((float)(s.tickAcc / TICK_DT), 0.f, 1.f);
    bool begun = false;
    for (auto& up : s.level->entities()) {
      Entity& e = *up;
      if (e.removed || e.realm != s.player->realm) continue;
      if (e.type != "watcher" && e.type != "ender_orb") continue;
      vec3 ip = glm::mix(e.prevPos, e.pos, a);
      vec3 rel = f.cam.rel(glm::dvec3(ip));
      if (glm::dot(rel, rel) > 128 * 128) continue;
      if (!begun) { if (!D.begin(f)) return; begun = true; }
      if (e.type == "ender_orb") {
        std::string k = e.data.value("kind", "pearl");
        const VoxelModel& im = itemModel(k == "eye" ? "ender_eye" : "ender_pearl");
        if (!im.ready()) continue;
        mat4 m = glm::translate(mat4(1), rel);
        m = glm::rotate(m, e.age * 5, vec3(0, 1, 0)); m = glm::scale(m, vec3(k == "eye" ? 0.32f : 0.26f) * 1.4f);
        D.draw(im.gl, m * im.matrix(), VoxelLight{1, 1});
        continue;
      }
      drawWatcher(s, D, w, e, ip, rel);
    }
    if (begun) D.end();
  }

  void drawWatcher(GameSession& s, CreatureDraw& D, World& w, Entity& e, vec3 ip, vec3 rel) {
    bool boar = e.data.value("boar", false);
    Creature& M = dummy(boar ? "boar" : "herobrine");
    const RigModel* RM = rigModel(M.modelKey());
    const CreatureModelGL* CM = creatureModel(M.modelKey());
    if (!RM || !CM) return;
    Player& p = *s.player;
    float leaving = jf(e.data, "leaving");
    M.pos = ip; M.yaw = e.yaw; M.age = e.age; M.walk = jf(e.data, "walk"); M.walkAmt = leaving > 0 ? 1 : 0;
    float pitch = std::atan2(p.pos.y + 1.62f - (ip.y + 1.6f), std::hypot(p.pos.x - ip.x, p.pos.z - ip.z));
    M.headPitch = leaving > 0 ? 0 : clampv(pitch, -0.5f, 0.5f);
    rigPose(M, *RM, creatureBase(M, true));
    VoxelLight L = lightAt(w, ip + vec3(0, 1.2f, 0));
    mat4 T0 = glm::translate(mat4(1), rel);
    for (size_t i = 0; i < RM->parts.size(); i++) {
      const RigPart& part = RM->parts[i];
      const CreaturePartMesh& pm = *CM->parts[i];
      if (pm.hidden || !rigPartPosed(M, (int)i) || !rigPartShown(M, part)) continue;
      D.draw(pm.gl, T0 * M.mats[i] * pm.local, part.glow || part.unlit || part.eyes ? VoxelLight{1, 1} : L);
    }
    if (boar) {
      // the boar's eyes are white too
      Creature& H = dummy("herobrine");
      const RigModel* HM = rigModel(H.modelKey());
      const CreatureModelGL* HC = creatureModel(H.modelKey());
      if (!HM || !HC) return;
      for (size_t i = 0; i < HM->parts.size(); i++) if (HM->parts[i].eyes || HM->parts[i].name == "eyes") {
        mat4 m = glm::rotate(T0, e.yaw, vec3(0, 1, 0));
        m = glm::scale(glm::translate(m, vec3(0, 0.57f, -0.62f)), vec3(1.1f, 1, 1));
        D.draw(HC->parts[i]->gl, m * HC->parts[i]->local, VoxelLight{1, 1});
      }
    }
  }

  void drawHud(GameSession& s) override {
    if (!s.player) return;
    Player& p = *s.player;
    App& a = s.app;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float W = (float)a.width, H = (float)a.height, z = Z(), t = (float)a.time;
    // the darkness: the edges of your sight closing in, pulsing like a heartbeat
    float dk = jf(p.data, "darkT");
    float k = dk > 0 ? std::min(1.f, dk / 1.5f) * (0.62f + 0.38f * std::sin(t * 1.9f)) : 0;
    if (k > 0.01f) radialStops(dl, W, H, {{0, rgba(0, 0, 0, 0.15f * k)}, {0.08f, rgba(0, 0, 0, 0.15f * k)}, {0.48f, rgba(0, 0, 0, 0.82f * k)}, {0.75f, rgba(0, 0, 0, 0.97f * k)}, {1, rgba(0, 0, 0, 0.97f * k)}});
    // the dread: the screen closing in while he stares
    float dr = std::min(1.f, std::max(jf(p.data, "dreadT"), jf(p.data, "dreadAll")) / 0.6f);
    if (dr > 0.01f) radialStops(dl, W, H, {{0.25f, rgba(40, 0, 60, 0)}, {0.7f, rgba(40, 0, 60, 0.55f * dr)}, {1, rgba(10, 0, 20, 0.9f * dr)}});
    if (s.hudHidden) return;
    Level& lv = *s.level;
    // the story's line at the top of the quest track
    const json& R = lv.info.data.contains("rift") ? lv.info.data["rift"] : json();
    int stage = R.is_object() ? R.value("stage", 0) : 0;
    bool sealed = R.is_object() && R.contains("sealed") && R["sealed"] != false && R["sealed"] != 0;
    if (stage && (!sealed || (p.stats.value("realmSeen", 0) == 1 && p.realm == REALM_HIS))) {
      int n = std::min(RIFT_NEED, R.value("frags", 0));
      std::string b = sealed ? "The rift is sealed" : stage >= 3 ? "Enter the rift and face him" : "The Temporal Keys: Realm Fragments";
      std::string sp = sealed || stage >= 3 ? "" : std::to_string(n) + "/" + std::to_string(RIFT_NEED);
      ImVec2 bs = textSize(F_MID, b.c_str()), ss = textSize(F_DISP, sp.c_str(), 0, 0.5f);
      float bw = bs.x + (sp.empty() && !sealed ? 0 : ss.x + 10 * z + (sealed ? 14 * z : 0)) + 16 * z, bh = std::max(bs.y, ss.y) + 6 * z;
      ImVec2 p0(W - 10 * z - bw, 120 * z);
      dl->AddRectFilled(p0, p0 + ImVec2(bw, bh), IM_COL32(30, 6, 40, 179));
      dl->AddRectFilled(p0, p0 + ImVec2(3 * z, bh), sealed ? col::xp : col::rgb(0xc040ff));
      text(dl, F_MID, p0 + ImVec2(8 * z, (bh - bs.y) * 0.5f), col::rgb(0xfff3d6), b.c_str());
      if (!sp.empty()) text(dl, F_DISP, ImVec2(p0.x + bw - 8 * z - ss.x, p0.y + (bh - ss.y) * 0.5f), col::muted, sp.c_str(), 0, 0.5f);
      if (sealed) {   // ✓
        ImVec2 c(p0.x + bw - 14 * z, p0.y + bh * 0.5f);
        dl->AddLine(c + ImVec2(-5, 0) * z, c + ImVec2(-1.5f, 4) * z, col::xp, 2 * z); dl->AddLine(c + ImVec2(-1.5f, 4) * z, c + ImVec2(6, -5) * z, col::xp, 2 * z);
      }
    }
    // the dragon eggs: a countdown over each one close by
    const json& eggs = lv.info.data.contains("eggs") ? lv.info.data["eggs"] : json();
    if (eggs.is_array() && !eggs.empty()) {
      Camera& cam = s.frame.cam;
      World& w = s.world();
      for (auto& e : eggs) {
        if (!e.is_object() || e.value("realm", 0) != p.realm) continue;
        int x = e.value("x", 0), y = e.value("y", 0), zz = e.value("z", 0);
        if (glm::distance(glm::dvec3(x + 0.5, y, zz + 0.5), cam.pos) > 16) continue;
        std::string kind = reg().block(w.getBlock(x, y, zz)).str("dragonEgg");
        if (kind.empty()) continue;
        vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(x + 0.5, y + 1.35, zz + 0.5)), 1);
        if (c.w <= 0.1f) continue;
        ImVec2 sp((c.x / c.w * 0.5f + 0.5f) * W, (1 - (c.y / c.w * 0.5f + 0.5f)) * H);
        int tt = e.value("t", 0), left = std::max(0, 300 - tt);
        char mmss[16]; snprintf(mmss, sizeof mmss, "%d:%02d", left / 60, left % 60);
        bool warm = e.value("warm", false), sea = kind == "tide";
        std::string b = warm ? std::string("Hatching in ") + mmss : tt ? std::string("Paused ") + mmss + " left" : "Dragon Egg";
        std::string sub = warm ? (sea ? "kept wet" : "kept warm") : (sea ? "needs water beside it" : "needs fire, lava or a campfire within 2");
        ImVec2 bs = textSize(F_MID, b.c_str()), ss = textSize(F_SMALL, sub.c_str(), 0, 11 / 12.5f);
        float tot = bs.y + ss.y + 2 * z;
        ImVec2 b0(sp.x - bs.x * 0.5f - 6 * z, sp.y - tot);
        dl->AddRectFilled(b0, b0 + ImVec2(bs.x + 12 * z, bs.y), IM_COL32(0, 0, 0, 107), 2 * z);
        textShadow(dl, F_MID, b0 + ImVec2(6 * z, 0), col::rgb(0xffd890), b.c_str(), ImVec2(1, 1) * z, col::black);
        textShadow(dl, F_SMALL, ImVec2(sp.x - ss.x * 0.5f, b0.y + bs.y + 2 * z), col::gold, sub.c_str(), ImVec2(1, 1) * z, col::black, 0, 11 / 12.5f);
      }
    }
  }

private:
  std::map<std::string, std::unique_ptr<Creature>> dummies;
  Creature& dummy(const std::string& type) {
    auto& d = dummies[type];
    if (!d) d = std::make_unique<Creature>(type);
    return *d;
  }
};

void registerEndStoryLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<EndStoryLayer>(); });
}

} // namespace dq
