// render/char_layer.cpp — players drawn as voxel characters (render/skins.js avatarDraws, game/anim.js,
// game/capes.js, game/carried.js, game/charparts.js, render/gear3d.js gearDraws).
#include "render/char_layer.h"
#include "render/item_layers.h"
#include "client/app.h"
#include "ui/screen.h"
#include "core/registry.h"
#include "assets/pack_armor.h"
#include <cstdlib>

namespace dq {

void registerCharacterLayers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<CharacterLayer>(); });
}

const VoxelModel* localPlayerArm() {
  App& a = app();
  if (!a.session || !a.session->player) return nullptr;
  return skinArmModel(*resolveCharacter(a.session->player->skin));
}

namespace {
float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
float turnTo(float a, float b, float k) { return a + wrapAngle(b - a) * std::min(1.f, k); }
std::string str(const json& d, const char* k) { return d.contains(k) && d[k].is_string() ? d[k].get<std::string>() : std::string(); }
bool hidden(const Player& p, int k) { const json& h = p.data.contains("hideGear") ? p.data["hideGear"] : json(); return h.is_array() && k < (int)h.size() && h[k].is_number() && h[k].get<int>() != 0; }

// skins.js heldMatrix: how a flat item sits in a fist (arm space: the arm hangs down -y, the chest faces -z)
mat4 heldMatrix(float tilt, float s, vec2 grip, bool upright) {
  vec3 X, Y;
  if (upright) { X = vec3(0, 0, -s); Y = vec3(0, s * std::cos(tilt), -s * std::sin(tilt)); }
  else {
    float r2 = std::sqrt(0.5f);
    vec3 F(0, std::sin(tilt), -std::cos(tilt)), Bn(0, -std::cos(tilt), -std::sin(tilt));
    X = (F - Bn) * r2 * s; Y = (F + Bn) * r2 * s;
  }
  vec3 Z(s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}
const mat4& heldMat(const std::string& id) {
  static const mat4 TOOL = heldMatrix(0.25f, 0.85f, {-0.34f, -0.24f}, false), ITEM = heldMatrix(0.2f, 0.55f, {0, -0.25f}, true), ROD = heldMatrix(0.75f, 0.85f, {-0.34f, -0.24f}, false);
  return id == "fishing_rod" || id == "fishing_rod_cast" || id == "fishing_rod_tp" ? ROD : isHandheld(id) ? TOOL : ITEM;
}
std::string toolType(const std::string& id) { const ItemDef* d = reg().item(id); return d && d->hasTool ? d->tool.type : std::string(); }
bool rx(const std::string& s, std::initializer_list<const char*> parts) { for (auto p : parts) if (s.find(p) != std::string::npos) return true; return false; }
}

// ------------------------------------------------------------------ test switches
void CharacterLayer::devSwitches(GameSession& s) {
  devFrames++;
  Player* p = s.player;
  if (devFrames == 1) {
    if (const char* v = std::getenv("DQ_VIEW")) s.viewMode = clampv(std::atoi(v), 0, 2);
    if (const char* v = std::getenv("DQ_SKIN")) {   // a skin reference, or "random[:seed]" = a fresh creator character "devchar"
      std::string ref = v;
      if (startsWith(ref, "random")) {
        CharLook L = ref.size() > 7 ? ccDefaultLook((u32)std::atoi(ref.c_str() + 7)) : ccDefaultLook(std::nullopt);
        if (const char* sp = std::getenv("DQ_DEV_SPECIES")) ccSetSpecies(L, sp);
        SkinImage sk; ccPaintSkin(L, sk);
        saveCharacter("devchar", L, sk);
        ref = "creator:devchar";
      }
      p->skin = ref; s.app.settings.skin = ref;
    }
    if (const char* v = std::getenv("DQ_DEV_POSE")) devPose = v;
    if (const char* v = std::getenv("DQ_DEV_YAW")) p->yaw = (float)std::atof(v);
    if (const char* v = std::getenv("DQ_DEV_PITCH")) p->pitch = (float)std::atof(v);
    if (const char* v = std::getenv("DQ_DEV_WEAR")) {   // "iron_helmet,leather_chestplate#a03030,red_cape,leather_backpack,lantern"
      for (auto& k0 : splitStr(v, ',')) {
        auto parts = splitStr(k0, '#'); std::string k = parts[0];
        const ItemDef* d = reg().item(k);
        if (!d) continue;
        if (d->props.contains("armor") && d->props["armor"].is_object()) {
          int slot = d->props["armor"].value("slot", 0);
          if (slot >= 0 && slot < (int)p->armor.size()) { p->armor[slot] = ItemStack(k, 1); if (parts.size() > 1) { p->armor[slot].tag = json::object(); p->armor[slot].tag["col"] = "#" + parts[1]; } }
        } else if (d->has("cape")) p->data["cape"] = k;
        else if (d->has("backpack")) p->data["back"] = k;
        else p->data["hook"] = k;
      }
    }
  }
  if (devFrames == 4) if (const char* v = std::getenv("DQ_OPEN")) {
    std::string w = v;
    if (w == "creator") s.app.pushScreen(makeCreatorScreen());
    else if (w == "skineditor") s.app.pushScreen(makeSkinEditorScreen());
  }
}

// ------------------------------------------------------------------ per-entity motion (player.js bob, local.js landing, anim.js, capes.js)
void CharacterLayer::update(GameSession& s, double dt) {
  devSwitches(s);
  // armour, capes and packs take their pictures from the resource pack: rebuild them after a change
  std::string stamp = s.app.tiles.packName + "#" + std::to_string(s.app.tiles.count);
  if (stamp != tilesStamp) { if (!tilesStamp.empty()) charModelsReset(); tilesStamp = stamp; }
  frameNo++;
  time += dt;
  float fdt = (float)std::min(dt, 0.1);
  if (fdt <= 0) return;
  // the local player's arm timers (the hand view keeps them)
  if (Player* lp = s.player) if (HeldItemLayer* h = s.layer<HeldItemLayer>()) { lp->swingT = h->swing; lp->eatT = std::max(0.f, h->eatAnim); }
  for (auto& up : s.level->entities()) {
    Entity* e = up.get();
    if (!e || e->removed || e->type != "player") continue;
    Player* p = dynamic_cast<Player*>(e);
    if (!p) continue;
    Anim& st = anims[e->id];
    st.seen = frameNo;
    if (!st.init) { st.init = true; st.lastPos = e->pos; st.bodyYaw = st.lastBody = e->yaw; st.fallStart = e->pos.y; }
    vec3 v = p == s.player ? e->vel : (e->pos - st.lastPos) / fdt;
    if (p != s.player) v = glm::mix(st.vel, v, std::min(1.f, fdt * 12));
    if (glm::length(e->pos - st.lastPos) > 8) v = vec3(0);   // a teleport
    st.vel = v; st.lastPos = e->pos;
    if (!devPose.empty() && p == s.player) {   // DQ_DEV_POSE: walk / run look for the screenshots
      if (devPose == "walk" || devPose == "run" || devPose == "sneak") v = vec3(0, 0, -(devPose == "run" ? 5.6f : devPose == "sneak" ? 1.3f : 4.3f));
      if (devPose == "run") p->sprinting = true;
      if (devPose == "sneak") p->sneaking = true;
      if (devPose == "swim") { p->swimming = true; v = vec3(0, 0, -2); }
      if (devPose == "fly") { p->flying = true; v = vec3(0, 0, -8); }
      if (devPose == "swing") { if (p->swingT <= 0) p->swingT = 0.0001f; p->swingT += fdt * 1.8f; if (p->swingT >= 1) p->swingT = 0.0001f; }
      if (devPose == "bow") { p->bowPull = std::min(1.f, p->bowPull + fdt); if (p->held().empty()) p->held() = ItemStack("bow", 1); }
      float c = std::cos(p->yaw), sn = std::sin(p->yaw);
      v = vec3(v.x * c + v.z * sn, v.y, -v.x * sn + v.z * c);
      st.vel = v;
    }
    float hs = std::hypot(v.x, v.z);
    bool ground = e->onGround || (!devPose.empty() && p == s.player && devPose != "fly");
    st.bobAmt += ((ground ? std::min(1.f, hs / 4.3f) : 0.f) - st.bobAmt) * std::min(1.f, fdt * 8);
    st.bob += hs * fdt * 1.6f;
    st.swimPh = std::fmod(st.swimPh + fdt * (e->inWater ? 2.4f + hs * 0.5f : 1.2f), 628.3f);
    // a little crouch on landing after a fall
    if (st.landT > 0) st.landT -= fdt;
    if (!e->onGround && !e->inWater && !p->flying) { if (!st.falling || e->pos.y > st.fallStart) st.fallStart = st.falling ? std::max(st.fallStart, e->pos.y) : e->pos.y; st.falling = true; }
    if (e->inWater || p->flying) st.falling = false;
    if (e->onGround && st.falling) { if (st.fallStart - e->pos.y > 1.2f) st.landT = 0.2f; st.falling = false; }
    st.deathT = p->dead ? st.deathT + fdt : 0;
    // ---- anim.js: which way the body faces, and the leans ----
    float aim = p->yaw, body = st.bodyYaw, legTurn = 0;
    bool busy = p->swimming || p->crawling || e->riding || p->data.value("sleeping", false) || p->data.value("hang", false);
    if (busy) body = aim;
    else if (hs > 0.7f) {
      float mv = std::atan2(-v.x, -v.z), rel = wrapAngle(mv - aim);
      if (std::fabs(rel) > PI / 2 + 0.05f) rel = wrapAngle(rel - PI * (rel > 0 ? 1 : -1));   // backing off: walk backwards, turned a little
      float want = aim + clampf(rel, -1.2f, 1.2f) * 0.62f;
      body = aim + wrapAngle(turnTo(body, want, fdt * 10) - aim);
      legTurn = clampf(wrapAngle(aim + rel - body), -0.7f, 0.7f);
      st.stillT = 0;
    } else {
      st.stillT += fdt;
      float off = wrapAngle(aim - body);
      if (std::fabs(off) > 0.9f) body = aim - (off > 0 ? 1 : -1) * 0.9f;
      if (st.stillT > 0.5f || p->flying) body = turnTo(body, aim, fdt * 3);
    }
    st.bodyYaw = body;
    st.legTurn += (legTurn - st.legTurn) * std::min(1.f, fdt * 10);
    float fwd = -(v.x * std::sin(body) + v.z * std::cos(body));
    float acc = (fwd - st.lastFwd) / std::max(fdt, 1e-3f); st.lastFwd = fwd;
    st.accLean += (clampf(acc * 0.012f, -0.18f, 0.18f) - st.accLean) * std::min(1.f, fdt * 6);
    float yawRate = wrapAngle(body - st.lastBody) / fdt; st.lastBody = body;
    st.turnLean += (clampf(yawRate * 0.06f * std::min(1.f, hs / 5), -0.2f, 0.2f) - st.turnLean) * std::min(1.f, fdt * 5);
    // ---- capes.js: out behind when moving forward, lifted when falling, flapping a little ----
    float cf = -(v.x * std::sin(p->yaw) + v.z * std::cos(p->yaw));
    float want = 0.06f + clampf(cf, 0, 8) * 0.13f + clampf(-v.y, 0, 20) * 0.04f + (p->sneaking ? 0.25f : 0);
    if (p->swimming || e->riding) want = 0.3f;
    want = std::min(want, 1.35f) + std::sin(st.bob * 2.2f) * 0.04f * st.bobAmt;
    st.capeA += (want - st.capeA) * std::min(1.f, fdt * 7);
    float side = v.x * std::cos(p->yaw) - v.z * std::sin(p->yaw);
    st.capeSway = clampf(-side * 0.05f, -0.25f, 0.25f) + std::sin((float)time * 3.1f) * 0.02f;
  }
  for (auto it = anims.begin(); it != anims.end();) it = it->second.seen + 120 < frameNo ? anims.erase(it) : std::next(it);
}

// ------------------------------------------------------------------ drawing
void CharacterLayer::drawOpaque(GameSession& s, const FrameCtx& f) {
  if (!s.player) return;
  bool begun = false;
  for (auto& up : s.level->entities()) {
    Entity* e = up.get();
    if (!e || e->removed || e->type != "player" || e->realm != s.player->realm) continue;
    Player* p = dynamic_cast<Player*>(e);
    if (!p) continue;
    bool local = p == s.player, fp = false;
    if (local && s.viewMode == 0) {
      if (!s.app.settings.fpBody || p->dead || p->data.value("sleeping", false)) continue;
      fp = true;
    }
    vec3 rel = f.cam.rel(glm::dvec3(e->pos));
    if (glm::dot(rel, rel) > 160.f * 160.f || !f.cam.frustum.sphere(rel + vec3(0, 1, 0), 1.8f)) continue;
    auto it = anims.find(e->id);
    if (it == anims.end()) continue;
    if (!begun) { voxelRenderer().begin(f, f.cam.viewProj); begun = true; }
    drawPlayer(s, f, *p, it->second, fp);
  }
  if (begun) voxelRenderer().end();
}

void CharacterLayer::drawPlayer(GameSession& s, const FrameCtx& f, Player& p, Anim& st, bool fp) {
  VoxelRenderer& VR = voxelRenderer();
  const TileSet& ts = s.app.tiles;
  auto spec = resolveCharacter(p.skin);
  const CharModel& M = charModel(*spec);
  const GearLook* L = M.look ? &*M.look : nullptr;
  World& w = s.level->world(p.realm);
  u8 l = w.getLight(ifloor(p.pos.x), ifloor(p.pos.y + 1), ifloor(p.pos.z));
  VoxelLight light{(l >> 4) / 15.f, (l & 15) / 15.f};
  vec4 tint = p.hurtTime > 0 ? vec4(1, 0.15f, 0.1f, 0.4f) : vec4(0);
  bool sleeping = p.data.value("sleeping", false);
  // ---- the pose (local.js LocalPlayer.pose) ----
  CharPose ps;
  std::string heldId = p.held().empty() ? "" : p.held().id, offId = p.offhand.empty() || p.offhand[0].empty() ? "" : p.offhand[0].id;
  ps.yaw = p.yaw; ps.headPitch = p.pitch * 0.8f; ps.walk = st.bob * 1.4f; ps.walkAmt = st.bobAmt; ps.swing = p.swingT; ps.time = (float)time;
  ps.sneak = p.sneaking && !p.swimming; ps.sit = p.riding != 0; ps.sleep = sleeping; ps.swim = p.swimming || p.crawling;
  ps.swimPitch = p.swimming ? p.pitch : 0; ps.hang = p.data.value("hang", false); ps.climb = p.data.value("climb", 0.f);
  ps.held = !heldId.empty(); ps.off = !offId.empty(); ps.deathT = p.dead ? std::max(st.deathT, 0.01f) : 0;
  ps.air = !p.onGround && !p.flying && !p.inWater && !p.riding; ps.vy = st.vel.y; ps.sprint = p.sprinting; ps.land = std::max(0.f, st.landT);
  ps.bow = p.bowPull; ps.eat = p.eatT;
  ps.paddle = p.inWater && !p.onGround && !p.swimming && !p.flying && !p.riding;
  ps.swimPh = st.swimPh; ps.swimMv = std::min(1.f, std::hypot(st.vel.x, st.vel.z) / 2.2f);
  ps.hasBodyYaw = true; ps.bodyYaw = st.bodyYaw; ps.headTurn = wrapAngle(p.yaw - st.bodyYaw); ps.legTurn = st.legTurn;
  ps.accLean = p.onGround ? st.accLean : 0; ps.turnLean = st.turnLean;
  if (p.flying && !p.riding) { ps.fly = true; ps.flySpeed = std::min(1.f, std::hypot(st.vel.x, st.vel.z) / 14); ps.flyVy = st.vel.y; ps.air = false; ps.lean = 0.85f * ps.flySpeed; }
  if (!devPose.empty() && &p == s.player) { if (devPose == "sit") ps.sit = true; if (devPose == "sleep") ps.sleep = true; if (devPose == "hang") ps.hang = true; if (devPose == "eat") ps.eat = 0.3f; }
  ps.capeA = st.capeA; ps.capeSway = st.capeSway;
  ps.fp = fp;
  ps.fpLeftArm = fp && offId.empty() && !ps.hang && !(p.bowPull > 0);
  if (heldId == "bow" && p.bowPull > 0) heldId = p.bowPull < 0.45f ? "bow_pulling_0" : p.bowPull < 0.85f ? "bow_pulling_1" : "bow_pulling_2";
  // ---- what's worn (charparts.js: hidden gear still protects, nobody sees it) ----
  std::string AR[4], dye[4];
  for (int i = 0; i < 4 && i < (int)p.armor.size(); i++) {
    if (p.armor[i].empty() || hidden(p, i)) continue;
    AR[i] = p.armor[i].id;
    if (p.armor[i].tag.is_object() && p.armor[i].tag.contains("col") && p.armor[i].tag["col"].is_string()) { std::string c = p.armor[i].tag["col"]; dye[i] = c.size() == 7 ? c.substr(1) : ""; }
  }
  if (L && L->helmHide) AR[0].clear();
  const ItemDef* hd = AR[0].empty() ? nullptr : reg().item(AR[0]);
  bool hatItem = hd && hd->has("hat");
  std::string back = hidden(p, 4) ? "" : str(p.data, "back"), hook = str(p.data, "hook"), cape = hidden(p, 5) ? "" : str(p.data, "cape");
  if (!cape.empty()) { const ItemDef* cd = reg().item(cape); cape = cd ? cd->str("cape") : ""; }
  if (cape.empty() && !hidden(p, 5)) cape = spec->cape;
  PackOver ov;
  if (L && !back.empty()) ov = gearPackOver(L, AR[1], AR[0], !cape.empty());
  bool cloak = L && L->cloak && cape.empty();
  // ---- the rig ----
  mat4 base = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p.pos)));
  if (ps.sit) base = glm::translate(base, vec3(0, -0.45f, 0));
  CharRig R;
  charRig(M, ps, base, R, cloak, !back.empty());
  if (p.data.value("invisible", false)) return;
  // the 3D outer layer goes under armour: whatever a piece covers isn't drawn
  bool hid[CP_COUNT] = {};
  hid[CP_HEAD] = !AR[0].empty() && !hatItem;
  for (int i : {CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) hid[i] = !AR[1].empty();
  hid[CP_LEGR_U] = hid[CP_LEGL_U] = !AR[2].empty();
  hid[CP_LEGR_D] = hid[CP_LEGL_D] = !AR[2].empty() || !AR[3].empty();
  auto drawPart = [&](int i) {
    mat4 m = R.part[i] * R.scale[i];
    if (M.base[i].ready()) VR.draw(M.base[i].gl, m * M.base[i].local(), light, tint);
    if (!hid[i] && M.ovl[i].ready()) VR.draw(M.ovl[i].gl, m * M.ovl[i].local(), light, tint);
  };
  bool fpArmL = fp && R.fpTorso && ps.fpLeftArm && !ps.paddle;
  for (int i : {CP_LEGR_U, CP_LEGR_D, CP_LEGL_U, CP_LEGL_D}) drawPart(i);
  if (!fp || R.fpTorso) drawPart(CP_BODY);
  if (!fp) for (int i : {CP_HEAD, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) drawPart(i);
  else if (fpArmL) { drawPart(CP_ARML_U); drawPart(CP_ARML_D); }
  // ---- the look's shapes (far off, the detail is lost anyway: past 56 blocks only the skin is drawn) ----
  vec3 rel = f.cam.rel(glm::dvec3(p.pos));
  if (L && (fp || glm::dot(rel, rel) < 56.f * 56.f)) {
    const auto& G = M.gear(!AR[0].empty() && !hatItem, !AR[1].empty(), &ts);
    for (auto& [part, mesh] : G) {
      if (fp && (part == "head" || part == "neck" || part == "cape" || part == "capeLo" || part == "tail")) continue;
      if (fp && !R.fpTorso && part != "legR" && part != "legL" && part != "shinR" && part != "shinL") continue;
      if (fp && !fpArmL && (part == "armR" || part == "foreR" || part == "armL" || part == "foreL")) continue;
      if (fp && part == "armR") continue;
      if (fp && part == "foreR") continue;
      auto fm = R.gear.find(part);
      if (fm == R.gear.end() || !mesh.ready()) continue;
      VR.draw(mesh.gl, fm->second * mesh.local(), light, tint);
    }
  }
  // ---- armour ----
  static const std::map<std::string, int> PART = {{"head", CP_HEAD}, {"body", CP_BODY}, {"armR_u", CP_ARMR_U}, {"armR_d", CP_ARMR_D}, {"armL_u", CP_ARML_U},
                                                  {"armL_d", CP_ARML_D}, {"legR_u", CP_LEGR_U}, {"legR_d", CP_LEGR_D}, {"legL_u", CP_LEGL_U}, {"legL_d", CP_LEGL_D}};
  for (int slot = 0; slot < 4; slot++) {
    if (AR[slot].empty() || (slot == 0 && (fp || hatItem))) continue;
    const ArmorSet& A = armorMeshes(AR[slot], dye[slot], slot, M.slim, L != nullptr, ts);
    for (auto& [pn, list] : A.byPart) {
      int pi = PART.at(pn);
      if (fp) {
        if (pi == CP_HEAD || pi == CP_ARMR_U || pi == CP_ARMR_D) continue;
        if ((pi == CP_ARML_U || pi == CP_ARML_D) && !fpArmL) continue;
        if (pi == CP_BODY && !R.fpTorso) continue;
      }
      mat4 m = R.part[pi] * R.scale[pi];
      for (const CharMesh* cm : list) VR.draw(cm->gl, m * cm->local(), light, tint);
    }
  }
  // ---- a cape over the shoulders (under a backpack the hem below it swings) ----
  if (!cape.empty() && !fp && !ps.sleep) {
    const mat4& B = R.body;
    if (!back.empty()) {
      const CharMesh* upM = capeMesh(cape, 1, ts); const CharMesh* loM = capeMesh(cape, 2, ts);
      mat4 m = glm::translate(B, vec3(0, 12, ov.capeZ > 0 ? ov.capeZ : 2.03f));
      if (upM) VR.draw(upM->gl, m * upM->local(), light, tint);
      mat4 m2 = glm::translate(m, vec3(0, -11, 0));
      m2 = glm::rotate(m2, -std::min(1.2f, ps.capeA * 0.9f), vec3(1, 0, 0));
      m2 = glm::rotate(m2, ps.capeSway * 0.6f, vec3(0, 0, 1));
      if (loM) VR.draw(loM->gl, m2 * loM->local(), light, tint);
    } else if (const CharMesh* cm = capeMesh(cape, 0, ts)) {
      mat4 m = glm::translate(B, vec3(0, 12, 2.1f));
      m = glm::rotate(m, -ps.capeA, vec3(1, 0, 0));
      m = glm::rotate(m, ps.capeSway, vec3(0, 0, 1));
      VR.draw(cm->gl, m * cm->local(), light, tint);
    }
  }
  // ---- what's in their hands ----
  auto held = [&](const mat4& fore, std::string id) {
    if (id == "fishing_rod") id = "fishing_rod_tp";
    const VoxelModel& im = itemModel(id);
    if (!im.ready()) return;
    mat4 hm = glm::translate(fore, vec3(0, -6, -1));
    if (im.kind == VoxelModel::Block) {
      hm = glm::translate(hm, vec3(0, 0, -1.5f));
      hm = glm::scale(hm, vec3(1 / CHAR_PX));
      hm = glm::rotate(hm, -0.3f, vec3(1, 0, 0)); hm = glm::scale(hm, vec3(0.375f));
    } else hm = glm::scale(hm, vec3(1 / CHAR_PX)) * heldMat(id);
    VR.draw(im.gl, hm * im.matrix(), light, tint);
  };
  if (!fp && !heldId.empty()) held(ps.bow > 0 && heldId.rfind("bow", 0) == 0 ? R.part[CP_ARML_D] : R.part[CP_ARMR_D], heldId);
  if (!fp && !offId.empty()) held(R.part[CP_ARML_D], offId);
  // ---- a backpack: the pack itself, straps over the shoulders (in first person, just the straps down your chest) ----
  const mat4& B = R.body;
  if (!back.empty() && (!fp || R.fpTorso)) {
    mat4 m = B;
    if (!fp) m = glm::rotate(m, std::sin(ps.walk * 2) * 0.02f * ps.walkAmt, vec3(1, 0, 0));
    if (const CharMesh* pm = packWornMesh(back, fp, ov, ts)) VR.draw(pm->gl, m * pm->local(), light, tint);
  }
  // ---- a lantern (or torch) on the belt, or hung from the backpack's side ----
  if (!hook.empty() && (!fp || (R.fpTorso && back.empty()))) {
    const VoxelModel& im = itemModel(hook);
    if (im.ready()) {
      float walk = std::sin(ps.walk * 2) * ps.walkAmt, sway = walk * 0.3f;
      mat4 m = !back.empty() ? glm::translate(B, vec3(5.3f, 6.6f, 3.7f + ov.po)) : glm::translate(B, vec3(2.6f, 0.4f, -2.5f));
      m = glm::rotate(m, sway, vec3(1, 0, 0)); m = glm::rotate(m, walk * 0.12f, vec3(0, 0, 1));
      m = glm::scale(m, vec3(1 / CHAR_PX));   // blocks
      if (im.kind == VoxelModel::Block) {
        m = glm::translate(m, vec3(0, -0.12f, back.empty() ? -0.06f : 0)); m = glm::scale(m, vec3(0.27f));
        if (im.torch3d) { m = glm::rotate(m, 0.5f, vec3(0, 0, 1)); m = glm::translate(m, vec3(0, 0.2f, 0)); }
      } else { m = glm::translate(m, vec3(0, -3 * CHAR_PX, 0)); m = glm::rotate(m, 0.25f, vec3(0, 0, 1)); m = glm::scale(m, vec3(0.36f)); }
      VR.draw(im.gl, m * im.matrix(), light, tint);
    }
  }
  // ---- carried.js: what you carry shows on you (hotbar things not in your hand) ----
  if (!fp && !ps.swim && !ps.sleep && ps.deathT <= 0) {
    std::vector<std::string> ids;
    for (int i = 0; i < 9 && i < (int)p.inv.size(); i++) if (i != p.selected && !p.inv[i].empty()) ids.push_back(p.inv[i].id);
    std::string kb, kb2, hr, hl; std::vector<std::string> pots;
    for (auto& id : ids) { std::string t = toolType(id); if (kb.empty() && (t == "sword" || t == "staff" || rx(id, {"club", "spear", "trident", "mace"}))) kb = id; }
    for (auto& id : ids) if (toolType(id) == "bow") { if (!kb.empty()) kb2 = id; else kb = id; break; }
    for (auto& id : ids) if (hr.empty() && toolType(id) == "pickaxe") hr = id;
    for (auto& id : ids) if (hr.empty() && toolType(id) == "shovel") hr = id;
    for (auto& id : ids) if (hl.empty() && toolType(id) == "axe") hl = id;
    for (auto& id : ids) if (hl.empty() && toolType(id) == "hoe") hl = id;
    for (auto& id : ids) if ((id.rfind("potion_", 0) == 0 || id.rfind("splash_potion_", 0) == 0 || id == "honey_bottle") && pots.size() < 3) pots.push_back(id);
    auto put = [&](const std::string& id, const std::function<mat4(mat4)>& place) {
      if (!reg().item(id)) return;
      const VoxelModel& im = itemModel(id);
      if (!im.ready()) return;
      VR.draw(im.gl, place(glm::scale(B, vec3(1 / CHAR_PX))) * im.matrix(), light, tint);
    };
    const float PXb = CHAR_PX;
    float zb = (!back.empty() ? 7.4f : 2.7f) * PXb;
    if (!kb.empty()) put(kb, [&](mat4 m) { m = glm::translate(m, vec3(0, 7.4f * PXb, zb)); m = glm::rotate(m, PI - 0.12f, vec3(0, 0, 1)); return glm::scale(m, vec3(0.72f)); });
    if (!kb2.empty()) put(kb2, [&](mat4 m) { m = glm::translate(m, vec3(0, 6.8f * PXb, zb + 0.9f * PXb)); m = glm::rotate(m, PI / 2 + 0.12f, vec3(0, 0, 1)); return glm::scale(m, vec3(0.78f)); });
    if (!hr.empty()) put(hr, [&](mat4 m) { m = glm::translate(m, vec3(4.75f * PXb, 1.2f * PXb, 0.4f * PXb)); m = glm::rotate(m, PI / 2, vec3(0, 1, 0)); m = glm::rotate(m, PI / 4 + 0.08f, vec3(0, 0, 1)); return glm::scale(m, vec3(0.52f)); });
    if (!hl.empty()) put(hl, [&](mat4 m) { m = glm::translate(m, vec3(-4.75f * PXb, 1.2f * PXb, 0.4f * PXb)); m = glm::rotate(m, -PI / 2, vec3(0, 1, 0)); m = glm::rotate(m, PI / 4 + 0.08f, vec3(0, 0, 1)); return glm::scale(m, vec3(0.52f)); });
    for (size_t i = 0; i < pots.size(); i++) put(pots[i], [&](mat4 m) {
      if (!back.empty()) { m = glm::translate(m, vec3(-5.5f * PXb, 4.2f * PXb, (2.6f + i * 1.25f) * PXb)); m = glm::rotate(m, PI / 2, vec3(0, 1, 0)); return glm::scale(m, vec3(0.32f)); }
      m = glm::translate(m, vec3((-2.6f + i * 1.3f) * PXb, -0.6f * PXb, -2.45f * PXb)); return glm::scale(m, vec3(0.27f));
    });
  }
}

} // namespace dq
