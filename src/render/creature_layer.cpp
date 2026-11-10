// render/creature_layer.cpp — drawing creatures and their things (see creature_layer.h).
// Ports the drawing half of game/entities.js (Mob.draws / drawRig: tints, wool colours, the death topple,
// held items; Projectile.glow; FallingTree.draws; telegraphDecal), game/dismember.js Gib.draws and
// game/blocks2.js Arrow.draws.
#include "render/creature_layer.h"
#include "render/pack_geom.h"
#include "sim/hallow.h"
#include "render/creature_draw.h"
#include "render/creature_models.h"
#include "render/char_anim.h"
#include "render/char_layer.h"
#include "assets/char_model.h"
#include "assets/char_creator.h"
#include "assets/item_models.h"
#include "assets/tileset.h"
#include "client/app.h"
#include "render/tile_box.h"
#include "render/mesher.h"
#include "world/shapes.h"
#include "sim/creature_rig.h"
#include "core/registry.h"
#include <cstdlib>

namespace dq {

// the model to draw: a "#state" variant while the creature shows another face (a ghast opening its mouth to fire)
static const CreatureModelGL* modelFor(const Creature& m) {
  if (m.type == "ghast") { auto it = m.data.find("chargeT"); if (it != m.data.end() && it->is_number() && it->get<float>() > 0) if (auto* v = creatureModel(m.modelKey() + "#shooting")) return v; }
  if (m.type == "bee" && (m.angryT > 0 || m.pollen)) {   // angry (red eyes), and/or with pollen on its legs
    const std::string k = m.modelKey() + (m.angryT > 0 && m.pollen ? "#angry_nectar" : m.angryT > 0 ? "#angry" : "#nectar");
    if (auto* v = creatureModel(k)) return v;
  }
  // a pig, cow or chicken of a cold or warm land: the pack's own picture of that one (sim/creature.cpp spawnCreature)
  if (auto it = m.data.find("climate"); it != m.data.end() && it->is_string() && *it != "temperate" && (m.type == "pig" || m.type == "cow" || m.type == "chicken"))
    if (auto* v = creatureModel(m.modelKey() + "#" + it->get<std::string>())) return v;
  return creatureModel(m.modelKey());
}


static constexpr float RPX = 1.f / 16;

// a mount's gear (sim/mount_gear.h) as boxes of a picture, 32 voxels to the block, kept per picture and size
struct GearBox { VoxelMeshGL gl; vec3 n{1}; };
static const GearBox& gearBox(const std::string& tile, vec3 size) {
  static auto& cache = *new std::unordered_map<std::string, std::unique_ptr<GearBox>>();   // (never freed: GL is gone by exit)
  const ivec3 n = glm::max(ivec3(glm::round(size * 32.f)), ivec3(1));
  const std::string key = tile + "|" + std::to_string(n.x) + "," + std::to_string(n.y) + "," + std::to_string(n.z);
  auto& slot = cache[key];
  if (!slot) {
    slot = std::make_unique<GearBox>(); slot->n = vec3(n);
    VoxelGrid g; buildTileBox(g, n, tile); VoxelMeshData md; buildVoxelMesh(g, md); slot->gl.upload(md);
  }
  return *slot;
}
// the box lo..hi (part space, blocks) of a rig part's frame, from that picture
static void drawGearBox(CreatureDraw& D, const mat4& partM, vec3 lo, vec3 hi, const std::string& tile, VoxelLight L, vec4 tint) {
  const vec3 size = glm::max(hi - lo, vec3(0.01f));
  const GearBox& B = gearBox(tile, size);
  if (!B.gl.ready()) return;
  D.draw(B.gl, partM * glm::scale(glm::translate(mat4(1), lo), size / B.n), L, tint);
}

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
  // Translucent decals (blob shadows, telegraphs) did not write a depth the pack can match.
  if (alpha >= 0.99f && m.mesh) packVoxelNote(m.mesh.get(), model, light.sky, light.block, tint);
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

// the foliage colour the chunk shader looks up for this climate (render/world_renderer.cpp's tint table)
static void drawFallingTrees(GameSession& s, const FrameCtx& f, float a);
static vec3 foliageTintAt(const World& w, int x, int y, int z) {
  u8 t = 128, h = 128;
  if (Chunk* ch = w.chunkAtBlock(x, y, z)) {
    int i = ((z & 15) * 16 + (x & 15)) * 2;
    t = ch->climate[i]; h = ch->climate[i + 1];
  }
  bool ok = false;
  u32 c = packTint("foliage", t, h, ok);
  if (!ok) {
    u32 g = climateTint("grass", t, h);
    auto sc = [](unsigned v, float k) { return clampv((int)std::lround(v * k), 0, 255) / 255.f; };
    return vec3(sc(g & 255u, 0.8f * 0.82f), sc((g >> 8) & 255u, 0.8f * 0.86f), sc((g >> 16) & 255u, 0.8f * 0.8f));
  }
  return vec3((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f);
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
// a bow carried by its grip at the side, as archers walk with one: level, its limbs fore and aft, held as it's shot with
// the arm lowered: its belly (the stave's curve, the sprite's upper left) down and the string on top, the fist round the
// middle of the stave.
// (The player's matrix, turned half round about the upright: in a creature's arm frame that one held it string out, the
// fletcher's and the skeletons' bows backwards.)
static mat4 bowHeldMatrix(float s, vec2 grip) {
  const float r2 = std::sqrt(0.5f);
  const vec3 X(0, r2 * s, r2 * s), Y(0, -r2 * s, r2 * s), Z(s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}
static bool isBowKey(const std::string& id) { return id == "bow" || id.rfind("bow_pulling", 0) == 0 || id.rfind("bow_pq", 0) == 0; }
// the bow drawn, the arm out at the target: upright across the arm (the chord along the arm's z, which the raised arm
// points up), its belly out along the arm toward the target and the string back toward the archer, the fist round the
// middle of the stave however far it's bent (it lay flat along the arm)
static mat4 bowDrawnMatrix(const std::string& id) {
  const float s = 0.8f, r2 = std::sqrt(0.5f);
  const float pull = id == "bow_pulling_0" ? 0.35f : id == "bow_pulling_1" ? 0.65f : id == "bow_pulling_2" ? 1.f : 0.f;
  const vec3 X(0, r2 * s, r2 * s), Y(0, -r2 * s, r2 * s), Z(s, 0, 0);
  const vec2 g = vec2(-0.235f, 0.235f) * (1 + 0.3f * pull);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(-(g.x * X + g.y * Y), 1));
}
static const mat4& heldMat(const std::string& id) {
  static const mat4 BOW = bowHeldMatrix(0.8f, {-0.235f, 0.235f});   // (the fist round the middle of the stave: it was 1.5 px off it)
  if (isBowKey(id)) return BOW;
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
}

struct Interp { vec3 pos; float yaw, age; };
static Interp interp(const Entity& e, float a) {
  return {glm::mix(e.prevPos, e.pos, a), e.prevYaw + wrapAngle(e.yaw - e.prevYaw) * a, e.age + a * (float)TICK_DT};
}

// a mob dressed in a player skin (mobs.json "skin": "<file>.png"): the voxel-character pipeline from
// render/char_layer.cpp (the same meshes, rig and face the players get), with the mob's own pose fields.
static void drawSkinMob(const Creature& m, const Interp& ip, World& w, const FrameCtx& f, bool& begun) {
  auto spec = resolveCharacter(m.T->skin);
  CharPose ps;
  ps.yaw = ip.yaw; ps.walk = m.walk * 1.4f; ps.walkAmt = std::min(1.f, m.walkAmt); ps.time = (float)m.age;
  ps.headPitch = m.headPitch;
  ps.swing = m.swingT > 0 ? std::sin(std::min(1.f, m.swingT / 0.3f) * PI) : 0;   // the same strike curve rigPose gives voxel mobs
  ps.hasBodyYaw = true; ps.bodyYaw = ip.yaw;
  if (m.deathTime > 0) ps.deathT = std::min(1.f, m.deathTime * 2);   // villagers clamp the topple the same way
  ps.held = !m.heldItem.empty(); ps.off = !m.offItem.empty();       // the pose flags must follow the wear, like char_layer
  CharWear wear;
  wear.held = m.heldItem; wear.off = m.offItem;
  VoxelLight L = lightAt(w, ip.pos + vec3(0, std::min(1.5f, m.h * 0.6f), 0));
  if (m.T->glows > 0) L = {1, 1};
  auto glowPart = [&](int i) {   // mobs.json "skinGlow": whole parts of the skin drawn unlit (a pumpkin-headed skin's face)
    for (const std::string& g : m.T->skinGlowParts) { if (g == "head" && i == CP_HEAD) return true; if (g == "body" && i == CP_BODY) return true; }
    return false; };
  vec4 tint = m.hurtTime > 0 || m.deathTime > 0 ? vec4(1, 0.15f, 0.1f, 0.45f) : (m.T->hasTint ? m.T->tint : vec4(0));
  if (tint.a == 0 && m.data.is_object() && m.data.value("corrupt", false)) {   // corrupted (sim/rift.cpp): his purple flicker
    float fl = 0.5f + 0.5f * std::sin(m.age * 23.f + (float)(m.id % 97));
    tint = vec4(0.42f, 0.08f, 0.62f, fl > 0.92f ? 0.6f : 0.28f + 0.12f * fl);
  }
  // draw it through the voxelRenderer pipeline exactly like villagers and players do — NOT CreatureDraw's
  // creature.glsl batch: that shader never gets uDynN/uDyn set (only world_renderer does), so under a lantern
  // the skin mob rendered black-on-black and looked invisible at night.
  VoxelRenderer& VR = voxelRenderer();
  if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
  vec3 rel = f.cam.rel(glm::dvec3(ip.pos));
  mat4 base = glm::translate(mat4(1), rel);
  CharRig R;
  charRig(charModel(*spec), ps, base, R, false, !wear.back.empty());
  const CharModel& M = charModel(*spec);
  bool hid[CP_COUNT] = {};
  hid[CP_HEAD] = !wear.armor[0].empty();
  for (int i : {CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) hid[i] = !wear.armor[1].empty();
  hid[CP_LEGR_U] = hid[CP_LEGL_U] = !wear.armor[2].empty();
  hid[CP_LEGR_D] = hid[CP_LEGL_D] = !wear.armor[2].empty() || !wear.armor[3].empty();
  for (int i = 0; i < CP_COUNT; i++) {
    mat4 pm = R.part[i] * R.scale[i];
    if (M.base[i].ready()) VR.draw(M.base[i].gl, pm * M.base[i].local(), glowPart(i) ? VoxelLight{1, 1} : L, tint);
    if (!hid[i] && M.ovl[i].ready()) VR.draw(M.ovl[i].gl, pm * M.ovl[i].local(), glowPart(i) ? VoxelLight{1, 1} : L, tint);
    if (i == CP_HEAD) {
      // the classic net's white eye pixels, unlit like the stock rig's eyes (a skinned herobrine glows only in the
      // eyes); mobs.json "skinGlow" parts stay fully unlit instead — a pumpkin-headed skin wants that.
      if (!glowPart(i) && M.eyesBand.ready()) VR.draw(M.eyesBand.gl, pm * M.eyesBand.local(), VoxelLight{1, 1}, tint);
      drawCharFace(M, ps, pm, glowPart(i) ? VoxelLight{1, 1} : L, tint);   // the blink and the talking mouth (endstory dialogue)
    }
  }
}

void drawSkinFigure(const Creature& m, vec3 pos, float yaw, World& w, const FrameCtx& f) {
  bool begun = false;
  drawSkinMob(m, Interp{pos, yaw, m.age}, w, f, begun);
}

void CreatureLayer::drawOpaque(GameSession& s, const FrameCtx& f) {
  if (!s.player) return;
  World& w = s.world();
  CreatureDraw& D = creatureDraw();
  if (!D.begin(f)) return;
  bool skinBegun = false;   // the voxelRenderer batch for skin mobs (separate from CreatureDraw's)
  float a = clampv((float)(s.tickAcc / TICK_DT), 0.f, 1.f);
  drawn = 0;
  float maxD = std::min(128.f, (float)s.app.settings.renderDist * 16);
  for (auto& up : s.level->entities()) {
    Entity& e = *up;
    if (e.removed || e.realm != s.player->realm) continue;
    Interp ip = interp(e, a);
    vec3 rel = f.cam.rel(glm::dvec3(ip.pos));
    if (glm::dot(rel, rel) > maxD * maxD && !(e.type == "wither_storm" && glm::dot(rel, rel) < 480.f * 480.f)) continue;   // (a Storm is seen from afar)
    if (auto* m = dynamic_cast<Creature*>(&e)) {
      if (m->T->npc) continue;   // villagers are voxel people (render/villager_layer)
      // a mob whose mobs.json says "skin": "<file>.png" is drawn as a voxel character from that player skin,
      // exactly like the players (and villagers): so herobrine.png — or any regular Minecraft skin dropped in
      // userDir()/skins — dresses it. Its render rig is always the biped, whatever the skin's arms are.
      if (m->T->explicitSkin || (!m->T->skin.empty() && !reg().models.contains(m->modelKey()))) { drawSkinMob(*m, ip, w, f, skinBegun); continue; }
      if (!f.cam.frustum.sphere(rel + vec3(0, m->h * 0.5f, 0), std::max(m->h, m->w * 2) + 1)) continue;
      const RigModel* RM = rigModel(m->modelKey());
      const CreatureModelGL* CM = modelFor(*m);
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
      // corrupted (sim/rift.cpp): his purple flicker over it, and white eyes
      const bool corrupt = m->data.is_object() && m->data.value("corrupt", false);
      if (corrupt && tint.a == 0) { float fl = 0.5f + 0.5f * std::sin(m->age * 23.f + (float)(m->id % 97)); tint = vec4(0.42f, 0.08f, 0.62f, fl > 0.92f ? 0.6f : 0.28f + 0.12f * fl); }
      vec4 wool = m->T->woolly && !m->color.empty() && m->color != "white" ? dyeTint(m->color) : vec4(0);
      mat4 T0 = glm::translate(mat4(1), rel);
      for (size_t i = 0; i < RM->parts.size(); i++) {
        const RigPart& p = RM->parts[i];
        const CreaturePartMesh& pm = *CM->parts[i];
        if (pm.hidden || !rigPartPosed(*m, (int)i) || !rigPartShown(*m, p) || p.alpha < 1) continue;
        mat4 model = T0 * m->mats[i] * pm.local;
        if (p.glow) { D.draw(pm.gl, model, {1, 1}, m->hurtTime > 0 ? tint : vec4(0)); continue; }
        D.draw(pm.gl, model, L, (p.wool || p.undercoat) && wool.a > 0 && tint.a == 0 ? wool : tint);
      }
      // parts that are a real block (the Wither Storm's command block in its cage of ribs): the block itself, as it is in
      // the world (its own pictures, the pack's when there is one), filling the part's box
      for (size_t i = 0; i < RM->parts.size(); i++) {
        const RigPart& p = RM->parts[i];
        if (p.item.empty() || !rigPartPosed(*m, (int)i) || !rigPartShown(*m, p)) continue;
        const VoxelModel& im = itemModel(p.item);
        if (!im.ready()) continue;
        const vec3 ctr = (p.min + p.max) * 0.5f, ext = glm::max(p.max - p.min, vec3(0.01f));
        D.draw(im.gl, glm::scale(glm::translate(T0 * m->mats[i], ctr), ext) * im.matrix(), L, tint);
      }
      if (corrupt) {
        int hi = RM->find("head");
        const VoxelModel& eye = itemModel("white_concrete");
        if (hi >= 0 && eye.ready() && rigPartPosed(*m, hi) && rigPartShown(*m, RM->parts[hi]) && !m->lost.count("head")) {
          const RigPart& hp = RM->parts[hi];
          vec3 ctr = (hp.min + hp.max) * 0.5f, ext = glm::max(hp.max - hp.min, vec3(0.05f));
          for (float sx : {-1.f, 1.f}) {
            mat4 em = glm::translate(T0 * m->mats[hi], vec3(ctr.x + sx * ext.x * 0.22f, ctr.y + ext.y * 0.08f, hp.min.z - 0.004f));
            D.draw(eye.gl, glm::scale(em, vec3(ext.x * 0.16f, ext.y * 0.12f, 0.01f)) * eye.matrix(), VoxelLight{1, 1}, vec4(1, 1, 1, 1));
          }
        }
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
        else hm = hm * (isBowKey(id) && m->aiming ? bowDrawnMatrix(id) : heldMat(id));
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
      // armour (an armoured zombie or skeleton, sim Creature::rollArmour): the players' armour meshes, each made for a
      // Minecraft-sized part, stretched from that part's box onto the rig part's (a skeleton's thin bones get slim plates)
      if (m->data.contains("armor")) {
        struct AP { const char* charPart; const char* rigPart; vec3 lo, hi; };
        static const AP MAP[] = {
          {"head", "head", {-4, 0, -4}, {4, 8, 4}}, {"body", "body", {-4, 0, -2}, {4, 12, 2}},
          {"armR_u", "armR", {-2, -4, -2}, {2, 2, 2}}, {"armL_u", "armL", {-2, -4, -2}, {2, 2, 2}},
          {"armR_d", "foreR", {-2, -6, -2}, {2, 0, 2}}, {"armL_d", "foreL", {-2, -6, -2}, {2, 0, 2}},
          {"legR_u", "legR", {-2, -6, -2}, {2, 0, 2}}, {"legL_u", "legL", {-2, -6, -2}, {2, 0, 2}},
          {"legR_d", "shinR", {-2, -6, -2}, {2, 0, 2}}, {"legL_d", "shinL", {-2, -6, -2}, {2, 0, 2}}};
        for (int slot = 0; slot < 4; slot++) {
          const std::string& key = m->armourPiece(slot);
          if (key.empty() || !reg().item(key)) continue;
          const ArmorSet& A = armorMeshes(key, "", slot, false, false, app().tiles);
          for (auto& [pn, list] : A.byPart) {
            const AP* ap = nullptr; for (const AP& q : MAP) if (pn == q.charPart) ap = &q;
            int ri = ap ? RM->find(ap->rigPart) : -1;
            if (ri < 0 || !rigPartPosed(*m, ri) || m->lost.count(ap->rigPart)) continue;
            const RigPart& rp = RM->parts[ri];
            if (!rigPartShown(*m, rp)) continue;
            vec3 k = (rp.max - rp.min) / glm::max(ap->hi - ap->lo, vec3(0.01f));
            mat4 pm = glm::translate(T0 * m->mats[ri], rp.min) * glm::scale(mat4(1), k) * glm::translate(mat4(1), -ap->lo);
            for (const CharMesh* cm : list) D.draw(cm->gl, pm * cm->local(), L, tint);
          }
        }
      }
      // wolf armour (sim/bta_items.cpp): the material's helmet and chestplate plates stretched over the wolf's head and
      // body, a little larger than them
      if (m->data.contains("wolfArmor") && m->data["wolfArmor"].is_string()) {
        const ItemDef* wd = reg().item(m->data["wolfArmor"].get<std::string>());
        const std::string mat = wd && wd->props.contains("wolfArmor") ? wd->props["wolfArmor"].value("mat", std::string()) : std::string();
        struct WP { int slot; const char* charPart; const char* rigPart; vec3 lo, hi; };
        static const WP WMAP[] = {{0, "head", "head", {-4, 0, -4}, {4, 8, 4}}, {1, "body", "body", {-4, 0, -2}, {4, 12, 2}}};
        for (const WP& q : WMAP) {
          static const char* PIECE[] = {"helmet", "chestplate"};
          const std::string key = mat + "_" + PIECE[q.slot];
          if (mat.empty() || !reg().item(key)) continue;
          const ArmorSet& A = armorMeshes(key, "", q.slot, false, false, app().tiles);
          int ri = RM->find(q.rigPart);
          if (ri < 0 || !rigPartPosed(*m, ri)) continue;
          const RigPart& rp = RM->parts[ri];
          for (auto& [pn, list] : A.byPart) {
            if (pn != q.charPart) continue;
            const vec3 ctr = (rp.min + rp.max) * 0.5f, ext = (rp.max - rp.min) * 1.12f;
            vec3 kk = ext / glm::max(q.hi - q.lo, vec3(0.01f));
            mat4 pm = glm::translate(T0 * m->mats[ri], ctr - ext * 0.5f) * glm::scale(mat4(1), kk) * glm::translate(mat4(1), -q.lo);
            for (const CharMesh* cm : list) D.draw(cm->gl, pm * cm->local(), L, tint);
          }
        }
      }
      // a mount's gear (sim/mount_gear.h, synced as data "horseArmor" / "pack"): horse armour as barding over the body,
      // the neck and the top of the face; a chest on each flank (its latch outward); saddle bags as leather pouches
      if (!m->baby && m->data.is_object() && (m->data.contains("horseArmor") || m->data.contains("pack") || m->data.contains("bags"))) {
        const int bi = RM->find("body");
        const bool bodyOk = bi >= 0 && rigPartPosed(*m, bi) && rigPartShown(*m, RM->parts[bi]);
        const float e = 0.6f * RPX;
        if (m->data.contains("horseArmor") && m->data["horseArmor"].is_string()) {
          const ItemDef* ad = reg().item(m->data["horseArmor"].get<std::string>());
          const std::string mat = ad && ad->props.contains("horseArmor") ? ad->props["horseArmor"].value("mat", std::string()) : std::string();
          const std::string tile = mat == "leather" ? "saddle_leather" : mat == "gold" ? "gold_block" : mat == "diamond" ? "diamond_block" : mat == "steel" ? "steel_block" : "iron_block";
          const vec4 mt = tint.a > 0 ? tint : mat == "chainmail" ? vec4(0.42f, 0.45f, 0.5f, 0.45f) : mat == "leather" ? vec4(0.32f, 0.2f, 0.1f, 0.25f) : vec4(0);
          if (bodyOk) {
            const RigPart& b = RM->parts[bi]; const float h = b.max.y - b.min.y;
            drawGearBox(D, T0 * m->mats[bi], vec3(b.min.x - e, b.min.y + 0.3f * h, b.min.z - e), vec3(b.max.x + e, b.max.y + e, b.max.z + e * 0.5f), tile, L, mt);
          }
          if (int ni = RM->find("neck"); ni >= 0 && rigPartPosed(*m, ni) && rigPartShown(*m, RM->parts[ni])) {
            const RigPart& n = RM->parts[ni];
            drawGearBox(D, T0 * m->mats[ni], vec3(n.min.x - e, n.min.y, n.min.z - e), vec3(n.max.x + e, n.min.y + 0.85f * (n.max.y - n.min.y), n.max.z + e * 0.5f), tile, L, mt);
          }
          if (int hi = RM->find("head"); hi >= 0 && rigPartPosed(*m, hi) && rigPartShown(*m, RM->parts[hi]) && !m->lost.count("head")) {
            const RigPart& hp = RM->parts[hi]; const float hh = hp.max.y - hp.min.y, hd = hp.max.z - hp.min.z;
            drawGearBox(D, T0 * m->mats[hi], vec3(hp.min.x - e * 0.5f, hp.max.y - 0.3f * hh, hp.min.z - e), vec3(hp.max.x + e * 0.5f, hp.max.y + e, hp.max.z - 0.35f * hd), tile, L, mt);
          }
        }
        std::string pack = m->data.value("pack", std::string());
        if (pack.empty() && m->data.contains("bags")) pack = "bags";
        if (bodyOk && !pack.empty()) {
          const RigPart& b = RM->parts[bi];
          const mat4 PM = T0 * m->mats[bi];
          const float zc = (b.min.z + b.max.z) * 0.5f;
          for (float sx : {-1.f, 1.f}) {
            const float x0 = sx > 0 ? b.max.x : b.min.x;
            if (pack == "chest") {   // a chest, 8 long, 7 high, 3 thick, its front to the outside
              const VoxelModel& im = itemModel("chest");
              if (!im.ready()) continue;
              const vec3 ctr(x0 + sx * 1.5f * RPX, b.max.y - 4.5f * RPX, zc + 1.5f * RPX);
              mat4 cm = glm::rotate(glm::translate(PM, ctr), sx * PI * 0.5f, vec3(0, 1, 0));
              D.draw(im.gl, glm::scale(cm, vec3(8, 7, 3) * RPX) * im.matrix(), L, tint);
            } else {                 // a leather pouch with a brass buckle, behind the saddle
              const float zb = zc + 1.f * RPX;
              const vec3 lo(sx > 0 ? x0 : x0 - 2.5f * RPX, b.max.y - 7.5f * RPX, zb), hi(sx > 0 ? x0 + 2.5f * RPX : x0, b.max.y - 1.5f * RPX, zb + 6 * RPX);
              drawGearBox(D, PM, lo, hi, "saddle_leather", L, tint.a > 0 ? tint : vec4(0.2f, 0.12f, 0.06f, 0.3f));
              const float bx = sx > 0 ? hi.x : lo.x - 0.5f * RPX;
              drawGearBox(D, PM, vec3(bx, hi.y - 2.5f * RPX, zb + 2.25f * RPX), vec3(bx + 0.5f * RPX, hi.y - 1 * RPX, zb + 3.75f * RPX), "gold_block", L, tint);
            }
          }
        }
      }
      continue;
    }
    if (!f.cam.frustum.sphere(rel, e.type == "block_cluster" ? 5.f : 2.5f)) continue;
    const std::string& t = e.type;
    if (t == "arrow") {
      const VoxelModel& im = itemModel(e.data.value("item", std::string("arrow")));   // (a golden, flaming or purple arrow shows as itself)
      if (!im.ready()) continue;
      mat4 m = glm::translate(mat4(1), rel);
      m = glm::rotate(m, e.yaw, vec3(0, 1, 0)); m = glm::rotate(m, e.pitch, vec3(1, 0, 0));
      m = glm::rotate(m, PI / 2, vec3(0, 1, 0)); m = glm::rotate(m, -PI / 4, vec3(0, 0, 1)); m = glm::scale(m, vec3(0.7f));
      D.draw(im.gl, m * im.matrix(), lightAt(w, ip.pos));
    } else if (t == "falling_block" || t == "block_cluster") {   // blocks in flight: one falling, or a clump the Wither Storm
      // tore out of the world (trembling a moment where it was, then turning as it flies into a maw)
      const float spin = e.data.value("spin", 0.f), shake = e.data.value("shake", 0.f);
      const vec3 jit = shake > 0 ? vec3(std::sin(ip.age * 53 + e.id), std::sin(ip.age * 61 + e.id * 3.f), std::cos(ip.age * 47 + e.id)) * 0.07f : vec3(0);
      const bool cl = t == "block_cluster";
      const vec3 base = rel + jit + (cl ? vec3(0) : vec3(0, 0.5f, 0));
      const VoxelLight L = lightAt(w, ip.pos + vec3(0, 0.5f, 0));
      auto one = [&](const std::string& key, vec3 off) {
        const VoxelModel& im = itemModel(key); if (!im.ready()) return;
        mat4 m = glm::translate(mat4(1), base);
        if (cl && shake <= 0) m = glm::rotate(m, spin, glm::normalize(vec3(0.3f, 1, 0.2f)));
        m = glm::translate(m, off);
        D.draw(im.gl, m * im.matrix(), L);
      };
      if (!cl) one(e.data.value("block", std::string()), vec3(0));
      else if (auto bl = e.data.find("blocks"); bl != e.data.end() && bl->is_array())
        for (auto& b : *bl) if (b.is_array() && b.size() >= 4 && b[3].is_string()) one(b[3].get<std::string>(), vec3(b[0].get<float>(), b[1].get<float>(), b[2].get<float>()));
    } else if (t == "projectile" && (e.data.value("kind", "") == "cannonball" || e.data.value("kind", "") == "rocket")) {
      const std::string k = e.data.value("kind", "");
      const VoxelModel& im = itemModel(k);
      if (!im.ready()) continue;
      mat4 m = glm::translate(mat4(1), rel);
      if (k == "rocket") { vec3 v = e.vel; m = glm::rotate(m, std::atan2(-v.x, -v.z), vec3(0, 1, 0)); m = glm::rotate(m, std::atan2(v.y, std::hypot(v.x, v.z)) - PI / 4, vec3(1, 0, 0)); }
      else m = glm::rotate(m, ip.age * 8, vec3(1, 0, 0));
      m = glm::scale(m, vec3(k == "rocket" ? 0.6f : 0.35f));
      D.draw(im.gl, m * im.matrix(), lightAt(w, ip.pos));
    } else if (t == "projectile" && e.data.value("kind", "") == "wither_skull") {   // a Wither's skull: a little wither
      // skeleton's head, flying face first (a blue one lit from within)
      const VoxelModel& im = itemModel("wither_skeleton_skull");
      if (!im.ready()) continue;
      const vec3 v = e.vel; const float yaw = std::atan2(-v.x, -v.z), pitch = std::atan2(v.y, std::max(0.001f, std::hypot(v.x, v.z)));
      mat4 m = glm::translate(mat4(1), rel + vec3(0, 0.15f, 0));
      m = glm::rotate(m, yaw, vec3(0, 1, 0)); m = glm::rotate(m, pitch, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.5f));
      D.draw(im.gl, m * im.matrix(), e.data.value("blue", false) ? VoxelLight{1, 1} : lightAt(w, ip.pos));
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
    }
  }
  fauna->draw(s, f);
  D.end();
  drawFallingTrees(s, f, a);
}

// A felled tree used to be a nearest-face voxel copy: the top of a log wore the end-grain picture, and a leaf's
// holes became missing voxels with a flat green multiply. Each block stays a box of its own side, top and bottom tiles.
static void drawFallingTrees(GameSession& s, const FrameCtx& f, float a) {
  World& w = s.world();
  ContentDraw& cd = contentDraw();
  bool begun = false;
  const Registry& R = reg();
  for (auto& up : s.level->entities()) {
    Entity& e = *up;
    if (e.removed || e.type != "falling_tree" || e.realm != s.player->realm) continue;
    const json& bl = e.data.value("blocks", json::array());
    if (!bl.is_array() || bl.empty()) continue;
    Interp ip = interp(e, a);
    vec3 rel = f.cam.rel(glm::dvec3(ip.pos));
    if (glm::dot(rel, rel) > 160.f * 160.f) continue;
    if (!f.cam.frustum.sphere(rel + vec3(0, 8, 0), 18.f)) continue;
    if (!begun && !cd.begin(f, f.cam.viewProj)) return;
    begun = true;
    float k = std::min(1.f, e.data.value("t", 0.f) / 1.1f), ang = k * k * PI / 2, dy = e.data.value("dirYaw", 0.f);
    mat4 m = glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f));
    m = glm::rotate(m, dy, vec3(0, 1, 0));
    m = glm::rotate(m, -ang, vec3(1, 0, 0));
    m = glm::rotate(m, -dy, vec3(0, 1, 0));
    m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
    vec3 leaf = foliageTintAt(w, ifloor(e.pos.x), ifloor(e.pos.y), ifloor(e.pos.z));
    u8 lit = w.getLight(ifloor(ip.pos.x), ifloor(ip.pos.y) + 1, ifloor(ip.pos.z));
    vec2 light((lit >> 4) / 15.f, (lit & 15) / 15.f);
    for (auto& b : bl) {
      if (!b.is_array() || b.size() < 4 || !b[3].is_string()) continue;
      BlockId id = R.blockId(b[3].get<std::string>());
      if (!id) continue;
      const BlockDef& d = R.block(id);
      int sideI = d.faces[0], topI = d.faces[2], botI = d.faces[3];
      if (topI < 0) topI = sideI;
      if (botI < 0) botI = topI;
      GLuint side = tileTexture(s.app, sideI), top = tileTexture(s.app, topI), bot = tileTexture(s.app, botI);
      if (!side) continue;
      if (!top) top = side;
      if (!bot) bot = top;
      vec3 p((float)b[0].get<int>(), (float)b[1].get<int>(), (float)b[2].get<int>());
      bool leaves = R.tLeaves[id] || traits(id).tint == 2;
      drawTileBox(cd, m, p, p + vec3(1), side, top, bot, light, vec3(1), false, true, leaves ? leaf : vec3(1));
    }
  }
  if (begun) cd.end();
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
    const CreatureModelGL* CM = modelFor(*m);
    if (!RM || !CM || CM->parts.size() != m->mats.size()) continue;
    VoxelLight L = lightAt(w, ip.pos + vec3(0, std::min(1.5f, m->h * 0.6f), 0));
    vec4 tint = m->hurtTime > 0 || m->deathTime > 0 ? vec4(1, 0.15f, 0.1f, 0.45f) : vec4(0);
    // see-through parts (slime jelly, wraith cloth)
    for (size_t i = 0; i < RM->parts.size(); i++) {
      const RigPart& p = RM->parts[i];
      if (p.alpha >= 1 || CM->parts[i]->hidden || !rigPartPosed(*m, (int)i) || !rigPartShown(*m, p)) continue;
      // (glowing see-through parts are drawn full bright: the pumpkin creature's faint face, the ghost's eyes; the ghost fades with data["hw"].fa)
      D.draw(CM->parts[i]->gl, glm::translate(mat4(1), rel) * m->mats[i] * CM->parts[i]->local, p.glow ? VoxelLight{1, 1} : L, tint, p.alpha * (m->T->phase ? hwGet(*m, "fa", 1.f) : 1.f));
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
