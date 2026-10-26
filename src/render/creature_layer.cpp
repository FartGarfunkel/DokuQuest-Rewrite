// render/creature_layer.cpp — drawing creatures and their things (see creature_layer.h).
// Ports the drawing half of game/entities.js (Mob.draws / drawRig: tints, wool colours, the death topple,
// held items; Projectile.glow; FallingTree.draws; telegraphDecal), game/dismember.js Gib.draws and
// game/blocks2.js Arrow.draws.
#include "render/creature_layer.h"
#include "render/creature_draw.h"
#include "render/creature_models.h"
#include "assets/item_models.h"
#include "assets/tileset.h"
#include "assets/voxel_paint.h"
#include "client/app.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include <cstdlib>

namespace dq {

static constexpr float RPX = 1.f / 16;

// ------------------------------------------------------------------ the shader
CreatureDraw& creatureDraw() { static CreatureDraw* d = new CreatureDraw(); return *d; }   // kept past exit (no GL calls after the context is gone)
bool CreatureDraw::begin(const FrameCtx& f) {
  if (!sh && !tried) { tried = true; sh = std::make_unique<Shader>(); if (!sh->load("creature.glsl", "creature.glsl")) { DQ_ERR("creature.glsl failed to compile"); sh.reset(); } }
  if (!sh || !sh->ok()) return false;
  sh->use();
  f.apply(*sh);
  sh->set("uViewProj", f.cam.viewProj);
  sh->set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  sh->set("uNeutral", 0.f);
  sh->set("uGlint", vec2(0));
  return true;
}
void CreatureDraw::draw(const VoxelMeshGL& m, const mat4& model, VoxelLight light, vec4 tint, float alpha) {
  if (!sh || !m.ready()) return;
  sh->set("uModel", model);
  sh->set("uLight", vec2(light.sky, light.block));
  sh->set("uTint", tint);
  sh->set("uAlpha", alpha);
  bool mirrored = glm::determinant(mat3(model)) < 0;
  if (mirrored) glFrontFace(GL_CW);
  m.mesh->draw();
  if (mirrored) glFrontFace(GL_CCW);
}
void CreatureDraw::end() { glBindVertexArray(0); }
VoxelLight lightAt(const World& w, vec3 p) { u8 l = w.getLight(ifloor(p.x), ifloor(p.y), ifloor(p.z)); return {(l >> 4) / 15.f, (l & 15) / 15.f}; }

// ------------------------------------------------------------------ small meshes (shadow disc, square, glowing balls)
struct SmallMeshes {
  VoxelMeshGL disc, square, fireball, spit, snow;
  bool built = false;
  void build() {
    if (built) return;
    built = true;
    VoxelMeshData md;
    VoxelGrid d(ivec3(16, 1, 16)), s(ivec3(16, 1, 16));
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      float dx = x - 7.5f, dz = z - 7.5f;
      if (dx * dx + dz * dz <= 64) d.set(x, 0, z, voxRGBA(0, 0, 0));
      s.set(x, 0, z, voxRGBA(255, 255, 255));
    }
    buildVoxelMesh(d, md); disc.upload(md);
    md = {}; buildVoxelMesh(s, md); square.upload(md);
    auto ball = [&](VoxelMeshGL& out, int n, const char* tile, bool glow) {
      VoxelGrid g; buildTileBox(g, ivec3(n), tile, glow);
      float r = n * 0.5f;
      for (int z = 0; z < n; z++) for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) { vec3 q = vec3(x, y, z) + 0.5f - r; if (glm::dot(q, q) > r * r) g.set(x, y, z, 0); }
      VoxelMeshData m2; buildVoxelMesh(g, m2); out.upload(m2);
    };
    ball(fireball, 8, "fireball", true); ball(spit, 4, "white", false); ball(snow, 6, "snow", false);
  }
};
static SmallMeshes& small() { static SmallMeshes* m = new SmallMeshes(); m->build(); return *m; }

// ------------------------------------------------------------------ felled trees: a voxel model per tree
struct TreeMesh { VoxelMeshGL gl; ivec3 lo{0}; u32 id = 0; bool used = false; };
static std::unordered_map<u32, std::unique_ptr<TreeMesh>>& treeMeshes() { static auto* m = new std::unordered_map<u32, std::unique_ptr<TreeMesh>>(); return *m; }
static TreeMesh* treeMesh(const Entity& e) {
  auto& M = treeMeshes();
  auto it = M.find(e.id);
  if (it != M.end()) { it->second->used = true; return it->second.get(); }
  const json& bl = e.data.value("blocks", json::array());
  if (!bl.is_array() || bl.empty()) return nullptr;
  ivec3 lo(1 << 20), hi(-(1 << 20));
  for (auto& b : bl) { ivec3 p(b[0].get<int>(), b[1].get<int>(), b[2].get<int>()); lo = glm::min(lo, p); hi = glm::max(hi, p); }
  const int R = 8;
  VoxelGrid g((hi - lo + 1) * R);
  const TileSet& ts = app().tiles;
  for (auto& b : bl) {
    ivec3 p = ivec3(b[0].get<int>(), b[1].get<int>(), b[2].get<int>()) - lo;
    const BlockDef& d = reg().block(reg().blockId(b[3].get<std::string>()));
    for (int z = 0; z < R; z++) for (int y = 0; y < R; y++) for (int x = 0; x < R; x++) {
      vec3 n((x + 0.5f) / R, (y + 0.5f) / R, (z + 0.5f) / R);
      float dd[6] = {1 - n.x, n.x, 1 - n.y, n.y, 1 - n.z, n.z};
      int f = 0; for (int i = 1; i < 6; i++) if (dd[i] < dd[f]) f = i;
      float sx = f == 0 ? 1 - n.z : f == 1 ? n.z : f == 2 ? n.x : f == 3 ? 1 - n.x : f == 4 ? n.x : 1 - n.x, ty = f == 2 || f == 3 ? n.z : 1 - n.y;
      u32 c = tileVoxel(ts, d.faces[f], (int)(sx * ts.size), (int)(ty * ts.size));
      if (!c) continue;
      if (reg().tLeaves[d.id]) c = voxRGBA((int)((c & 255) * 0.55f), (int)(((c >> 8) & 255) * 0.8f), (int)(((c >> 16) & 255) * 0.45f));   // TINT_FOLIAGE-ish
      g.set(p.x * R + x, p.y * R + y, p.z * R + z, c | 0xFF000000u);
    }
  }
  auto tm = std::make_unique<TreeMesh>();
  VoxelMeshData md; buildVoxelMesh(g, md); tm->gl.upload(md);
  tm->lo = lo; tm->id = e.id; tm->used = true;
  auto* raw = tm.get(); M[e.id] = std::move(tm);
  return raw;
}

// ------------------------------------------------------------------ helpers
static vec4 dyeTint(const std::string& c) {
  static const std::unordered_map<std::string, u32> C = {{"white", 0xe9ecec}, {"orange", 0xf07613}, {"magenta", 0xbd44b3}, {"light_blue", 0x3aafd9}, {"yellow", 0xf8c627}, {"lime", 0x70b919},
    {"pink", 0xed8dac}, {"gray", 0x3e4447}, {"light_gray", 0x8e8e86}, {"cyan", 0x158991}, {"purple", 0x792aac}, {"blue", 0x35399d}, {"brown", 0x724728}, {"green", 0x546d1b}, {"red", 0xa12722}, {"black", 0x141519}};
  auto it = C.find(c);
  u32 h = it == C.end() ? 0xffffff : it->second;
  return vec4(((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f, 0.62f);
}
static mat4 heldMatrix(float tilt, float s, vec2 grip, bool upright) {
  vec3 X, Y;
  if (upright) { X = vec3(0, 0, -s); Y = vec3(0, s * std::cos(tilt), -s * std::sin(tilt)); }
  else {
    vec3 F(0, std::sin(tilt), -std::cos(tilt)), Bn(0, -std::cos(tilt), -std::sin(tilt));
    float r2 = std::sqrt(0.5f); X = (F - Bn) * r2 * s; Y = (F + Bn) * r2 * s;
  }
  vec3 Z(s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}
static const mat4& heldMat(const std::string& id) {
  static const mat4 TOOL = heldMatrix(0.25f, 0.85f, vec2(-0.34f, -0.24f), false), ITEM = heldMatrix(0.2f, 0.55f, vec2(0, -0.25f), true), ROD = heldMatrix(0.75f, 0.85f, vec2(-0.34f, -0.24f), false);
  if (id == "fishing_rod" || id == "fishing_rod_cast") return ROD;
  return isHandheld(id) ? TOOL : ITEM;
}
static float groundBelow(const World& w, vec3 p, float maxDrop) {
  int x = ifloor(p.x), z = ifloor(p.z);
  for (int y = ifloor(p.y + 0.2f); y > p.y - maxDrop; y--) if (reg().tSolid[w.getBlock(x, y - 1, z)]) return (float)y;
  return -1e9f;
}

// ------------------------------------------------------------------ the layer
CreatureLayer::CreatureLayer() : fauna(std::make_unique<Fauna>()) {}
CreatureLayer::~CreatureLayer() = default;

void registerCreatureLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<CreatureLayer>(); });
}

void CreatureLayer::update(GameSession& s, double dt) {
  if (!zooDone) { zooDone = true; if (std::getenv("DQ_ZOO")) zoo(s); }
  if (!s.paused()) fauna->update(s, (float)dt);
  // trees that fell are done with their meshes
  auto& M = treeMeshes();
  for (auto it = M.begin(); it != M.end();) { if (!it->second->used) it = M.erase(it); else { it->second->used = false; ++it; } }
}

struct Interp { vec3 pos; float yaw, age; };
static Interp interp(const Entity& e, float a) {
  return {glm::mix(e.prevPos, e.pos, a), e.prevYaw + wrapAngle(e.yaw - e.prevYaw) * a, e.age + a * (float)TICK_DT};
}

void CreatureLayer::drawOpaque(GameSession& s, const FrameCtx& f) {
  if (!s.player) return;
  World& w = s.world();
  CreatureDraw& D = creatureDraw();
  if (!D.begin(f)) return;
  float a = clampv((float)(s.tickAcc / TICK_DT), 0.f, 1.f);
  drawn = 0;
  float maxD = std::min(128.f, (float)s.app.settings.renderDist * 16);
  for (auto& up : s.level->entities()) {
    Entity& e = *up;
    if (e.removed || e.realm != s.player->realm) continue;
    Interp ip = interp(e, a);
    vec3 rel = f.cam.rel(glm::dvec3(ip.pos));
    if (glm::dot(rel, rel) > maxD * maxD) continue;
    if (auto* m = dynamic_cast<Creature*>(&e)) {
      if (m->T->npc) continue;   // villagers are voxel people (render/villager_layer)
      if (!f.cam.frustum.sphere(rel + vec3(0, m->h * 0.5f, 0), std::max(m->h, m->w * 2) + 1)) continue;
      const RigModel* RM = rigModel(m->modelKey());
      const CreatureModelGL* CM = creatureModel(m->modelKey());
      if (!RM || !CM) continue;
      // pose at the interpolated moment
      float sy = m->yaw, sa = m->age; m->yaw = ip.yaw; m->age = ip.age;
      rigPose(*m, *RM, creatureBase(*m, true));
      m->yaw = sy; m->age = sa;
      drawn++;
      VoxelLight L = lightAt(w, ip.pos + vec3(0, std::min(1.5f, m->h * 0.6f), 0));
      if (m->type == "cinder_golem") L.block = std::max(L.block, 12 / 15.f);
      if (m->T->glows > 0) L = {1, 1};
      vec4 tint = m->T->hasTint ? m->T->tint : vec4(0);
      if (m->hurtTime > 0 || m->deathTime > 0) tint = vec4(1, 0.15f, 0.1f, 0.45f);
      if (m->type == "cinder_golem" && m->state == "tele") tint = vec4(1, 0.55f, 0.15f, 0.25f + 0.25f * std::sin(m->age * 30));
      if (m->fuse > 0 && std::sin(m->fuse * 18) > 0.2f) tint = vec4(1, 1, 1, 0.55f);
      vec4 wool = m->T->woolly && !m->color.empty() && m->color != "white" ? dyeTint(m->color) : vec4(0);
      mat4 T0 = glm::translate(mat4(1), rel);
      for (size_t i = 0; i < RM->parts.size(); i++) {
        const RigPart& p = RM->parts[i];
        const CreaturePartMesh& pm = *CM->parts[i];
        if (pm.hidden || !rigPartPosed(*m, (int)i) || !rigPartShown(*m, p) || p.alpha < 1) continue;
        mat4 model = T0 * m->mats[i] * pm.local;
        if (p.glow) { D.draw(pm.gl, model, {1, 1}, m->hurtTime > 0 ? tint : vec4(0)); continue; }
        D.draw(pm.gl, model, L, p.wool && wool.a > 0 && tint.a == 0 ? wool : tint);
      }
      // what it holds: the bow out in the left hand while aiming, else the right
      std::string held = m->heldItem;
      if (held == "bow" && m->aiming) { float k = clampv(1 - m->shootT / 1.6f, 0.f, 1.f); held = k > 0.9f ? "bow_pulling_2" : k > 0.6f ? "bow_pulling_1" : k > 0.3f ? "bow_pulling_0" : "bow"; }
      auto drawHeld = [&](const std::string& id, const char* handPart) {
        int hi = RM->find(handPart);
        if (id.empty() || hi < 0 || !rigPartPosed(*m, hi) || !reg().item(id.rfind("bow_", 0) == 0 ? "bow" : id)) return;
        const VoxelModel& im = itemModel(id == "fishing_rod" ? "fishing_rod_tp" : id);
        if (!im.ready()) return;
        const RigPart& hp = RM->parts[hi];
        vec3 hand = hp.hasHand ? hp.hand : vec3(0, -5, 0);
        mat4 hm = T0 * m->mats[hi];
        hm = glm::translate(hm, vec3(hand.x * RPX, hand.y * RPX, -0.5f * RPX));
        if (RM->heavy) hm = glm::scale(hm, vec3(2));
        if (im.kind == VoxelModel::Block) { hm = glm::translate(hm, vec3(0, 0, -1.5f * RPX)); hm = glm::rotate(hm, -0.3f, vec3(1, 0, 0)); hm = glm::scale(hm, vec3(0.375f)); }
        else hm = hm * heldMat(id);
        D.draw(im.gl, hm * im.matrix(), L, tint);
      };
      drawHeld(held, held.rfind("bow", 0) == 0 && m->aiming ? "foreL" : "foreR");
      drawHeld(m->offItem, "foreL");
      // Halloween: a carved pumpkin or jack-o'-lantern on its head (rolled in sim/creature.cpp tick); the jack glows
      if (m->data.contains("hat") && m->data["hat"].is_string() && !m->lost.count("head")) {
        const std::string hat = m->data["hat"].get<std::string>();
        int hi = RM->find("head");
        const VoxelModel& im = itemModel(hat);
        if (hi >= 0 && im.ready() && rigPartPosed(*m, hi) && rigPartShown(*m, RM->parts[hi])) {
          const RigPart& hp = RM->parts[hi];
          vec3 ctr = (hp.min + hp.max) * 0.5f, ext = glm::max(hp.max - hp.min, vec3(0.05f)) * 1.14f;
          mat4 hm = glm::scale(glm::rotate(glm::translate(T0 * m->mats[hi], ctr + vec3(0, 0.02f, 0)), PI, vec3(0, 1, 0)), ext);   // (the carved face looks out the front)
          D.draw(im.gl, hm * im.matrix(), hat == "jack_o_lantern" ? VoxelLight{1, 1} : L, tint);
        }
      }
      continue;
    }
    if (!f.cam.frustum.sphere(rel, 2.5f)) continue;
    const std::string& t = e.type;
    if (t == "arrow") {
      const VoxelModel& im = itemModel("arrow");
      if (!im.ready()) continue;
      mat4 m = glm::translate(mat4(1), rel);
      m = glm::rotate(m, e.yaw, vec3(0, 1, 0)); m = glm::rotate(m, e.pitch, vec3(1, 0, 0));
      m = glm::rotate(m, PI / 2, vec3(0, 1, 0)); m = glm::rotate(m, -PI / 4, vec3(0, 0, 1)); m = glm::scale(m, vec3(0.7f));
      D.draw(im.gl, m * im.matrix(), lightAt(w, ip.pos));
    } else if (t == "projectile") {
      std::string k = e.data.value("kind", "fireball");
      SmallMeshes& S = small();
      bool gust = k == "gust", big = k == "ghast";   // a zephyr's gust: a pale puff; a ghast's fireball: bigger
      const VoxelMeshGL& mesh = k == "spit" ? S.spit : k == "snow" || gust ? S.snow : S.fireball;
      float sz = k == "spit" ? 0.12f : k == "snow" ? 0.22f : gust ? 0.45f + std::sin(ip.age * 20) * 0.05f : big ? 0.7f : 0.35f;
      int n = k == "spit" ? 4 : k == "snow" || gust ? 6 : 8;
      mat4 m = glm::translate(mat4(1), rel);
      m = glm::rotate(m, ip.age * 6, vec3(0.3f, 1, 0.2f));
      m = glm::scale(glm::translate(m, vec3(-sz / 2)), vec3(sz / n));
      D.draw(mesh, m, k == "fireball" || big ? VoxelLight{1, 1} : lightAt(w, ip.pos));
    } else if (t == "hex_flask") {
      const VoxelModel& im = itemModel("hex_flask");
      if (!im.ready()) continue;
      mat4 m = glm::translate(mat4(1), rel);
      m = glm::rotate(m, ip.age * 9, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.4f));
      D.draw(im.gl, m * im.matrix(), lightAt(w, ip.pos));
    } else if (t == "gib") {
      const CreatureModelGL* CM = creatureModel(e.data.value("model", ""));
      const RigModel* RM = rigModel(e.data.value("model", ""));
      if (!CM || !RM) continue;
      int pi = RM->find(e.data.value("part", ""));
      if (pi < 0 || CM->parts[pi]->hidden) continue;
      mat3 rot(1); const json& r = e.data["rot"];
      if (r.is_array() && r.size() == 9) for (int c = 0; c < 3; c++) for (int k = 0; k < 3; k++) rot[c][k] = r[c * 3 + k].get<float>();
      mat4 m = glm::translate(mat4(1), rel + vec3(0, 0.08f, 0));
      m = glm::rotate(m, e.data.value("ax", 0.f), vec3(1, 0, 0)); m = glm::rotate(m, e.data.value("az", 0.f), vec3(0, 0, 1));
      m = m * mat4(rot);
      float fade = e.age > 23 ? 1 - (25 - e.age) / 2 : 0;
      const CreatureType* T = creatureType(e.data.value("from", ""));
      vec4 tint = fade > 0 ? vec4(0, 0, 0, fade * 0.6f) : (T && T->hasTint ? T->tint : vec4(0));
      D.draw(CM->parts[pi]->gl, m * CM->parts[pi]->local, lightAt(w, ip.pos + vec3(0, 0.2f, 0)), tint);
    } else if (t == "falling_tree") {
      TreeMesh* tm = treeMesh(e);
      if (!tm) continue;
      float k = std::min(1.f, e.data.value("t", 0.f) / 1.1f), ang = k * k * PI / 2, dy = e.data.value("dirYaw", 0.f);
      mat4 m = glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f));
      m = glm::rotate(m, dy, vec3(0, 1, 0)); m = glm::rotate(m, -ang, vec3(1, 0, 0)); m = glm::rotate(m, -dy, vec3(0, 1, 0));
      m = glm::translate(m, vec3(-0.5f, 0, -0.5f) + vec3(tm->lo));
      m = glm::scale(m, vec3(1.f / 8));
      D.draw(tm->gl, m, lightAt(w, ip.pos + vec3(0, 1, 0)));
    }
  }
  fauna->draw(s, f);
  D.end();
}

void CreatureLayer::drawTranslucent(GameSession& s, const FrameCtx& f) {
  if (!s.player) return;
  World& w = s.world();
  CreatureDraw& D = creatureDraw();
  if (!D.begin(f)) return;
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  float a = clampv((float)(s.tickAcc / TICK_DT), 0.f, 1.f);
  SmallMeshes& S = small();
  for (auto& up : s.level->entities()) {
    auto* m = dynamic_cast<Creature*>(up.get());
    if (!m || m->removed || m->realm != s.player->realm || m->mats.empty()) continue;
    Interp ip = interp(*m, a);
    vec3 rel = f.cam.rel(glm::dvec3(ip.pos));
    if (glm::dot(rel, rel) > 64.f * 64.f || !f.cam.frustum.sphere(rel + vec3(0, m->h * 0.5f, 0), std::max(m->h, m->w * 2) + 1)) continue;
    const RigModel* RM = rigModel(m->modelKey());
    const CreatureModelGL* CM = creatureModel(m->modelKey());
    if (!RM || !CM || CM->parts.size() != m->mats.size()) continue;
    VoxelLight L = lightAt(w, ip.pos + vec3(0, std::min(1.5f, m->h * 0.6f), 0));
    vec4 tint = m->hurtTime > 0 || m->deathTime > 0 ? vec4(1, 0.15f, 0.1f, 0.45f) : vec4(0);
    // see-through parts (slime jelly, wraith cloth)
    for (size_t i = 0; i < RM->parts.size(); i++) {
      const RigPart& p = RM->parts[i];
      if (p.alpha >= 1 || CM->parts[i]->hidden || !rigPartPosed(*m, (int)i) || !rigPartShown(*m, p)) continue;
      D.draw(CM->parts[i]->gl, glm::translate(mat4(1), rel) * m->mats[i] * CM->parts[i]->local, L, tint, p.alpha);
    }
    // a soft round shadow on the ground under it
    if (!m->T->flyer || m->onGround) {
      float gy = groundBelow(w, ip.pos, 4);
      if (gy > -1e8f) {
        float lift = ip.pos.y - gy, k = clampv(1 - lift / 4, 0.f, 1.f) * (m->deathTime > 0 ? 1 - std::min(1.f, m->deathTime) : 1);
        float r = std::max(0.25f, m->w * (m->baby ? 0.7f : 1.2f) + 0.1f);
        vec3 c = f.cam.rel(glm::dvec3(ip.pos.x, gy + 0.015f, ip.pos.z));
        mat4 sm = glm::scale(glm::translate(mat4(1), c - vec3(r, 0, r)), vec3(2 * r / 16, 0.001f, 2 * r / 16));
        if (k > 0.02f) D.draw(S.disc, sm, {1, 1}, vec4(0, 0, 0, 1), 0.32f * k);
      }
    }
    // the Cinder Golem's warnings: where the slam lands, which way the charge goes
    if (m->type == "cinder_golem" && m->state == "tele" && m->attack.on) {
      float gy = (float)m->groundY(*s.level) + 0.02f;
      vec3 c = f.cam.rel(glm::dvec3(ip.pos.x, gy, ip.pos.z));
      float pr = m->attack.progress;
      if (m->attack.kind == "slam") {
        float R = 4.6f;
        D.draw(S.disc, glm::scale(glm::translate(mat4(1), c - vec3(R, 0, R)), vec3(2 * R / 16, 0.001f, 2 * R / 16)), {1, 1}, vec4(1, 0.25f, 0.1f, 1), 0.25f);
        float r2 = R * pr;
        D.draw(S.disc, glm::scale(glm::translate(mat4(1), c + vec3(0, 0.01f, 0) - vec3(r2, 0, r2)), vec3(2 * r2 / 16 + 1e-4f, 0.001f, 2 * r2 / 16 + 1e-4f)), {1, 1}, vec4(1, 0.25f, 0.1f, 1), 0.45f);
      } else if (m->attack.kind == "charge") {
        mat4 cm = glm::translate(mat4(1), c);
        cm = glm::rotate(cm, m->attack.yaw, vec3(0, 1, 0));
        cm = glm::translate(cm, vec3(-0.7f, 0, -14));
        D.draw(S.square, glm::scale(cm, vec3(1.4f / 16, 0.001f, 14 * pr / 16 + 1e-4f)), {1, 1}, vec4(1, 0.45f, 0.1f, 1), 0.5f);
        D.draw(S.square, glm::scale(cm, vec3(1.4f / 16, 0.001f, 14.f / 16)), {1, 1}, vec4(1, 0.45f, 0.1f, 1), 0.2f);
      }
    }
  }
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  D.end();
}

// ------------------------------------------------------------------ DQ_ZOO: a line-up for screenshots
void CreatureLayer::zoo(GameSession& s) {
  Level& lv = *s.level;
  Player& p = *s.player;
  World& w = s.world();
  std::string arg = std::getenv("DQ_ZOO");
  std::vector<std::pair<std::string, std::string>> list;   // type, variant
  std::vector<std::string> all;
  for (auto& [k, v] : reg().mobs.items()) all.push_back(k);
  bool page = !arg.empty() && std::all_of(arg.begin(), arg.end(), ::isdigit);
  if (arg == "all") for (auto& k : all) list.push_back({k, ""});
  else if (page) { int n = std::max(1, std::atoi(arg.c_str())); for (size_t i = (n - 1) * 16; i < all.size() && i < (size_t)n * 16; i++) list.push_back({all[i], ""}); }
  else {
    size_t a = 0;
    while (a <= arg.size()) { size_t b = arg.find(',', a); if (b == std::string::npos) b = arg.size(); std::string t = arg.substr(a, b - a);
      size_t c = t.find(':'); if (!t.empty()) list.push_back({t.substr(0, c), c == std::string::npos ? "" : t.substr(c + 1)}); a = b + 1; }
  }
  lv.info.rules["mobSpawning"] = false;
  lv.info.time = 300;
  for (auto& e : lv.entities()) if (!e->isPlayer()) e->removed = true;
  int X = ifloor(p.pos.x), Z = ifloor(p.pos.z), Y = ifloor(p.pos.y) + 2;
  int perRow = 8, rows = ((int)list.size() + perRow - 1) / perRow;
  BlockId grass = reg().blockId("grass"), water = reg().blockId("water"), glass = reg().blockId("glass");
  for (int x = X - 22; x <= X + 22; x++) for (int z = Z - 8 - rows * 6; z <= Z + 4; z++) {
    for (int y = Y; y < Y + 12; y++) w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
    w.setBlock(x, Y - 1, z, grass, 0, SetFlags::Urgent);
  }
  for (size_t i = 0; i < list.size(); i++) {
    int row = (int)i / perRow, col = (int)i % perRow, inRow = std::min(perRow, (int)list.size() - row * perRow);
    const CreatureType* T = creatureType(list[i].first);
    if (!T) { DQ_WARN("zoo: no creature '%s'", list[i].first.c_str()); continue; }
    float sp = 5.2f;
    float x = X + 0.5f + (col - (inRow - 1) * 0.5f) * sp, z = Z - 8.5f - row * 6;
    bool swimmer = T->swims && !T->walks;
    if (swimmer) for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) for (int y = Y - 3; y <= Y - 1; y++)
      w.setBlock(ifloor(x) + dx, y, ifloor(z) + dz, (std::abs(dx) == 1 || std::abs(dz) == 1) && y == Y - 1 ? glass : water, 0, SetFlags::Urgent);
    Creature* m = spawnCreature(lv, p.realm, list[i].first, vec3(x, swimmer ? Y - 2.2f : Y, z));
    if (!m) continue;
    if (!list[i].second.empty()) m->variant = list[i].second;
    m->calm = true; m->kept = true; m->persistent = false;
    m->yaw = PI - 0.45f;
    if (m->T->woolly && i % 2) m->color = "light_blue";
    if (list[i].first == "deer") m->stag = true;
    if (m->T->flyer || m->T->ai == "flyer") { m->noGravity = true; m->pos.y += 1; }
  }
  p.pos = vec3(X + 0.5f, Y + 3.5f, Z + 1.5f); p.prevPos = p.pos; p.vel = vec3(0);
  p.yaw = 0; p.pitch = -0.3f; p.flying = true; p.mode = "creative";
  DQ_LOG("zoo: %zu creatures in %d rows", list.size(), rows);
}

} // namespace dq
