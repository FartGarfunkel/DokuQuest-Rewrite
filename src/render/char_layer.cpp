// render/char_layer.cpp — players drawn as voxel characters (render/skins.js avatarDraws, game/anim.js,
// game/capes.js, game/carried.js, game/charparts.js, render/gear3d.js gearDraws).
#include "render/char_layer.h"
#include "assets/cape_tex.h"
#include "render/item_layers.h"
#include "client/app.h"
#include "ui/screen.h"
#include "core/registry.h"
#include "assets/pack_armor.h"
#include "render/map_render.h"
#include "sim/maps.h"
#include <cstdlib>
#include <cstdio>

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
float st0yaw(const CharPose& ps) { return ps.hasBodyYaw && !ps.fp ? ps.bodyYaw : ps.yaw; }
// skins.js held(): an item in a fist (fore = the forearm's frame, model pixels about the elbow; side -1 = the left hand)
void heldInHand(const mat4& fore, std::string id, float side, VoxelLight light, vec4 tint) {
  if (id == "fishing_rod") id = "fishing_rod_tp";
  const VoxelModel& im = itemModel(id);
  if (!im.ready()) return;
  // tuning aid: DQ_DEV_TPHELD="dy,dz,scale" (model pixels, scale added) for screenshots
  static float T[3] = {0, 0, 0}; static bool tl = false;
  if (!tl) { tl = true; if (const char* v = std::getenv("DQ_DEV_TPHELD")) std::sscanf(v, "%f,%f,%f", &T[0], &T[1], &T[2]); }
  mat4 hm = glm::translate(fore, vec3(0, -6 + T[0], -1 + T[1]));
  if (im.kind == VoxelModel::Block) {
    hm = glm::translate(hm, vec3(0, 0, -1.5f));
    hm = glm::scale(hm, vec3(1 / CHAR_PX));
    hm = glm::rotate(hm, -0.3f, vec3(1, 0, 0)); hm = glm::scale(hm, vec3(0.375f));
  } else {
    hm = glm::scale(hm, vec3((1 + T[2]) / CHAR_PX)) * heldMat(id);
  }
  (void)side;
  voxelRenderer().draw(im.gl, hm * im.matrix(), light, tint);
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
    st.ladderPh = std::fmod(st.ladderPh + std::fabs(v.y) * fdt * 4.2f, 628.3f);   // hand over hand up a ladder
    // a little crouch on landing after a fall
    if (st.landT > 0) st.landT -= fdt;
    if (!e->onGround && !e->inWater && !p->flying) { if (!st.falling || e->pos.y > st.fallStart) st.fallStart = st.falling ? std::max(st.fallStart, e->pos.y) : e->pos.y; st.falling = true; }
    if (e->inWater || p->flying) st.falling = false;
    if (e->onGround && st.falling) { if (st.fallStart - e->pos.y > 1.2f) st.landT = 0.2f; st.falling = false; }
    st.deathT = p->dead ? st.deathT + fdt : 0;
    // ---- anim.js: which way the body faces, and the leans ----
    float aim = p->yaw, body = st.bodyYaw, legTurn = 0;
    bool busy = p->swimming || p->crawling || e->riding || p->sleeping || p->data.value("sleeping", false) || (bool)p->hang || (bool)p->climb || p->gliding;
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
      if (!s.app.settings.fpBody || p->dead || p->sleeping || p->data.value("sleeping", false)) continue;
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
  // maps.js: the maps held up in third person, the real map on the side facing the holder
  if (!sheets.empty()) {
    ContentDraw& cd = contentDraw();
    if (cd.begin(f, f.cam.viewProj)) {
      for (auto& sh : sheets) drawMapSheet(cd, *s.level, sh.map, sh.holder, sh.M, sh.S, sh.light);
      cd.end();
    }
    sheets.clear();
  }
}

std::string wornItem(const Player& p, const char* slot) {
  // the inventory screen keeps these in Player::data (an array of one stack); using the item fills the member
  auto it = p.data.find(slot);
  if (it != p.data.end()) {
    if (it->is_string()) { if (!it->get<std::string>().empty()) return it->get<std::string>(); }
    else if (it->is_array() && !it->empty()) { ItemStack s = ItemStack::fromJson((*it)[0]); if (!s.empty()) return s.id; }
    else if (it->is_object()) { ItemStack s = ItemStack::fromJson(*it); if (!s.empty()) return s.id; }
  }
  std::string k = slot;
  const std::vector<ItemStack>* v = k == "cape" ? &p.cape : k == "back" ? &p.back : k == "hook" ? &p.hook : nullptr;
  return v && !v->empty() && !(*v)[0].empty() ? (*v)[0].id : std::string();
}
bool gearHidden(const Player& p, int k) { return hidden(p, k); }

CharWear charWearOf(const Player& p, const CharacterSpec& spec, bool withCarried) {
  CharWear w;
  for (int i = 0; i < 4 && i < (int)p.armor.size(); i++) {
    if (p.armor[i].empty() || hidden(p, i)) continue;
    w.armor[i] = p.armor[i].id;
    if (p.armor[i].tag.is_object() && p.armor[i].tag.contains("col") && p.armor[i].tag["col"].is_string()) { std::string c = p.armor[i].tag["col"]; w.dye[i] = c.size() == 7 ? c.substr(1) : ""; }
  }
  const ItemDef* hd = w.armor[0].empty() ? nullptr : reg().item(w.armor[0]);
  w.hatItem = hd && hd->has("hat");
  w.back = hidden(p, 4) ? "" : wornItem(p, "back");
  w.hook = wornItem(p, "hook");
  if (!hidden(p, 5)) {
    std::string c = wornItem(p, "cape");
    if (!c.empty()) { const ItemDef* cd = reg().item(c); w.cape = cd ? cd->str("cape") : ""; if (w.cape.empty() && cd) w.cape = c; }
    if (w.cape.empty()) {   // no cape item worn: the cape painted in the cape editor, else the creator's cosmetic cape
      auto cc = p.data.is_object() ? p.data.find("customCape") : p.data.end();
      if (p.data.is_object() && cc != p.data.end() && cc->is_string() && !cc->get<std::string>().empty()) w.cape = customCapeKey(cc->get<std::string>());
      else w.cape = spec.cape;
    }
  }
  if (!p.inv.empty() && p.selected >= 0 && p.selected < (int)p.inv.size() && !p.inv[p.selected].empty()) w.held = p.inv[p.selected].id;
  if (!p.offhand.empty() && !p.offhand[0].empty()) w.off = p.offhand[0].id;
  if (withCarried) for (int i = 0; i < 9 && i < (int)p.inv.size(); i++) if (i != p.selected && !p.inv[i].empty()) w.carried.push_back(p.inv[i].id);
  return w;
}

void CharacterLayer::drawPlayer(GameSession& s, const FrameCtx& f, Player& p, Anim& st, bool fp) {
  auto spec = resolveCharacter(p.skin);
  World& w = s.level->world(p.realm);
  u8 l = w.getLight(ifloor(p.pos.x), ifloor(p.pos.y + 1), ifloor(p.pos.z));
  bool sleeping = p.sleeping || p.data.value("sleeping", false);
  CharWear wear = charWearOf(p, *spec, !fp);
  // ---- the pose (local.js LocalPlayer.pose) ----
  CharPose ps;
  ps.yaw = p.yaw; ps.headPitch = p.pitch * 0.8f; ps.walk = st.bob * 1.4f; ps.walkAmt = st.bobAmt; ps.swing = p.swingT; ps.time = (float)time;
  ps.sneak = p.sneaking && !p.swimming; ps.sit = p.riding != 0; ps.sleep = sleeping; ps.swim = p.swimming || p.crawling;
  ps.swimPitch = p.swimming ? p.pitch : 0;
  // ledge grab (player.cpp LedgeHang / LedgeClimb): hands up on the edge, hauling yourself over, the shimmy
  ps.hang = (bool)p.hang || p.data.value("hang", false);
  ps.climb = p.climb ? std::min(1.f, p.climb->t / 0.55f) : p.data.value("climb", 0.f);
  if (p.hang) { ps.shimPh = p.hang->shimPh; ps.shimMv = p.hang->shimT > 0 ? 1.f : 0.f; }
  // on a ladder or vines: hand over hand as you go up or down
  ps.ladder = p.contacts.ladder && !p.onGround && !p.flying && !ps.hang && ps.climb <= 0 && !ps.swim;
  if (ps.ladder) ps.ladderPh = st.ladderPh, ps.ladderMv = std::min(1.f, std::fabs(st.vel.y) / 2.f);
  ps.held = !wear.held.empty(); ps.off = !wear.off.empty(); ps.deathT = p.dead ? std::max(st.deathT, 0.01f) : 0;
  ps.air = !p.onGround && !p.flying && !p.inWater && !p.riding && !ps.ladder && !ps.hang && ps.climb <= 0; ps.vy = st.vel.y; ps.sprint = p.sprinting; ps.land = std::max(0.f, st.landT);
  ps.bow = p.bowPull; ps.eat = p.eatT;
  ps.paddle = p.inWater && !p.onGround && !p.swimming && !p.flying && !p.riding && !ps.hang && ps.climb <= 0;
  ps.swimPh = st.swimPh; ps.swimMv = std::min(1.f, std::hypot(st.vel.x, st.vel.z) / 2.2f);
  ps.hasBodyYaw = true; ps.bodyYaw = st.bodyYaw; ps.headTurn = wrapAngle(p.yaw - st.bodyYaw); ps.legTurn = st.legTurn;
  ps.accLean = p.onGround ? st.accLean : 0; ps.turnLean = st.turnLean;
  if (p.flying && !p.riding) { ps.fly = true; ps.flySpeed = std::min(1.f, std::hypot(st.vel.x, st.vel.z) / 14); ps.flyVy = st.vel.y; ps.air = false; ps.lean = 0.85f * ps.flySpeed; }
  if (!devPose.empty() && &p == s.player) {
    if (devPose == "sit") ps.sit = true;
    if (devPose == "sleep") ps.sleep = true;
    if (devPose == "hang") ps.hang = true;
    if (devPose == "shimmy") { ps.hang = true; ps.shimPh = (float)time * 6; ps.shimMv = 1; }
    if (devPose == "climb") ps.climb = std::fmod((float)time * 0.6f, 1.f);
    if (devPose == "ladder") { ps.ladder = true; ps.ladderPh = (float)time * 5; ps.ladderMv = 1; ps.air = false; }
    if (devPose == "paddle") { ps.paddle = true; ps.swimMv = 0; ps.air = false; }
    if (devPose == "eat") ps.eat = 0.3f;
  }
  ps.capeA = st.capeA; ps.capeSway = st.capeSway;
  ps.fp = fp;
  ps.fpLeftArm = fp && wear.off.empty() && !ps.hang && !(p.bowPull > 0);
  if (wear.held == "bow" && p.bowPull > 0) wear.held = p.bowPull < 0.45f ? "bow_pulling_0" : p.bowPull < 0.85f ? "bow_pulling_1" : "bow_pulling_2";
  if (p.data.value("invisible", false)) return;
  vec3 rel = f.cam.rel(glm::dvec3(p.pos));
  // maps.js: a map in the hand is held up in front of the chest (both hands when the other is free)
  vec2 lt((l >> 4) / 15.f, (l & 15) / 15.f);
  if (!fp && !ps.sleep && !ps.swim && !ps.hang && ps.climb <= 0 && ps.deathT <= 0 && !ps.ladder) {
    ItemStack* off = p.offhand.empty() ? nullptr : &p.offhand[0];
    bool mainMap = isMapStack(p.held()), offMap = off && isMapStack(*off);
    if (mainMap || offMap) {
      MapStore& ms = mapStore(*s.level);
      ps.mapMain = mainMap; ps.mapOff = offMap;
      bool both = mainMap && !offMap && wear.off.empty();
      float drop = ps.sneak ? 0.22f : 0;
      auto sheet = [&](ItemStack& st, float side, float S) {
        mat4 M = glm::translate(mat4(1), rel + vec3(0, ps.sit ? -0.45f : 0, 0));
        M = glm::rotate(M, st0yaw(ps), vec3(0, 1, 0));
        M = glm::translate(M, vec3(side * 0.2f, 0.98f - drop - (ps.sneak ? 0.1f : 0), -0.5f));
        if (side != 0) M = glm::rotate(M, -side * 0.35f, vec3(0, 1, 0));
        M = glm::rotate(M, -0.95f, vec3(1, 0, 0));   // tipped up toward its holder's eyes
        sheets.push_back({ms.forStack(p, st), &p, M, S, lt});
      };
      if (both) sheet(p.held(), 0, 0.46f);
      else { if (mainMap) sheet(p.held(), 1, 0.34f); if (offMap) sheet(*off, -1, 0.34f); }
      if (mainMap) wear.held.clear();
      if (offMap) wear.off.clear();
    }
  }
  mat4 base = glm::translate(mat4(1), rel);
  if (ps.sit) base = glm::translate(base, vec3(0, -0.45f, 0));
  CharDrawOpts o;
  o.light = VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f};
  o.tint = p.hurtTime > 0 ? vec4(1, 0.15f, 0.1f, 0.4f) : vec4(0);
  o.fp = fp;
  o.detail = fp || glm::dot(rel, rel) < 56.f * 56.f;   // far off, the detail is lost anyway: past 56 blocks only the skin
  drawCharacter(*spec, ps, wear, base, o);
}

void drawCharacter(const CharacterSpec& spec, const CharPose& ps, const CharWear& wear, const mat4& base, const CharDrawOpts& o) {
  VoxelRenderer& VR = voxelRenderer();
  const TileSet& ts = app().tiles;
  const CharModel& M = charModel(spec);
  const GearLook* L = M.look ? &*M.look : nullptr;
  const VoxelLight light = o.light; const vec4 tint = o.tint; const bool fp = o.fp;
  std::string AR[4], dye[4];
  for (int i = 0; i < 4; i++) { AR[i] = wear.armor[i]; dye[i] = wear.dye[i]; }
  if (L && L->helmHide) AR[0].clear();
  bool hatItem = wear.hatItem && !AR[0].empty();
  const std::string& back = wear.back; const std::string& hook = wear.hook; const std::string& cape = wear.cape;
  PackOver ov;
  if (L && !back.empty()) ov = gearPackOver(L, AR[1], AR[0], !cape.empty());
  bool cloak = L && L->cloak && cape.empty();
  CharRig R;
  charRig(M, ps, base, R, cloak, !back.empty());
  // the 3D outer layer goes under armour: whatever a piece covers isn't drawn
  bool hid[CP_COUNT] = {};
  hid[CP_HEAD] = !AR[0].empty() && !hatItem;
  for (int i : {CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) hid[i] = !AR[1].empty();
  hid[CP_LEGR_U] = hid[CP_LEGL_U] = !AR[2].empty();
  hid[CP_LEGR_D] = hid[CP_LEGL_D] = !AR[2].empty() || !AR[3].empty();
  bool fpArmL = fp && R.fpTorso && ps.fpLeftArm && !ps.paddle;
  // which parts show: in first person only what you see looking down (the head and the right arm are the view's own)
  auto shows = [&](int i) {
    if (o.headOnly) return i == CP_HEAD;
    if (!fp) return true;
    if (i == CP_HEAD || i == CP_ARMR_U || i == CP_ARMR_D) return false;
    if (i == CP_ARML_U || i == CP_ARML_D) return fpArmL;
    if (i == CP_BODY) return R.fpTorso;
    return true;
  };
  static const std::map<std::string, int> PART = {{"head", CP_HEAD}, {"body", CP_BODY}, {"armR_u", CP_ARMR_U}, {"armR_d", CP_ARMR_D}, {"armL_u", CP_ARML_U},
                                                  {"armL_d", CP_ARML_D}, {"legR_u", CP_LEGR_U}, {"legR_d", CP_LEGR_D}, {"legL_u", CP_LEGL_U}, {"legL_d", CP_LEGL_D}};
  // ---- armour first: it is the outermost shell, so the skin under it fails the depth test early (cheap up close) ----
  for (int slot = 0; slot < 4; slot++) {
    if (AR[slot].empty() || (slot == 0 && (fp || hatItem))) continue;
    if (!reg().item(AR[slot])) continue;
    const ArmorSet& A = armorMeshes(AR[slot], dye[slot], slot, M.slim, L != nullptr, ts);
    for (auto& [pn, list] : A.byPart) {
      auto pit = PART.find(pn);
      if (pit == PART.end() || !shows(pit->second)) continue;
      int pi = pit->second;
      // in first person the chest's sleeves would sit right at the eye when you look down: only the torso's shell
      mat4 m = R.part[pi] * R.scale[pi];
      for (const CharMesh* cm : list) VR.draw(cm->gl, m * cm->local(), light, tint);
    }
  }
  // ---- the skin ----
  for (int i : {CP_HEAD, CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D, CP_LEGR_U, CP_LEGR_D, CP_LEGL_U, CP_LEGL_D}) {
    if (!shows(i)) continue;
    mat4 m = R.part[i] * R.scale[i];
    if (M.base[i].ready()) VR.draw(M.base[i].gl, m * M.base[i].local(), light, tint);
    if (!hid[i] && M.ovl[i].ready()) VR.draw(M.ovl[i].gl, m * M.ovl[i].local(), light, tint);
  }
  // ---- the look's shapes ----
  if (L && o.detail) {
    const auto& G = M.gear(!AR[0].empty() && !hatItem, !AR[1].empty(), &ts);
    for (auto& [part, mesh] : G) {
      if (o.headOnly && part != "head" && part != "neck") continue;
      if (fp && (part == "head" || part == "neck" || part == "cape" || part == "capeLo" || part == "tail")) continue;
      if (fp && !R.fpTorso && part != "legR" && part != "legL" && part != "shinR" && part != "shinL") continue;
      if (fp && !fpArmL && (part == "armL" || part == "foreL")) continue;
      if (fp && (part == "armR" || part == "foreR")) continue;
      auto fm = R.gear.find(part);
      if (fm == R.gear.end() || !mesh.ready()) continue;
      VR.draw(mesh.gl, fm->second * mesh.local(), light, tint);
    }
  }
  if (o.headOnly) return;
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
  // ---- what's in their hands (third person): Minecraft's thirdperson_righthand / lefthand ----
  if (!fp) {
    bool bowDraw = ps.bow > 0 && wear.held.rfind("bow", 0) == 0;
    if (!wear.held.empty()) heldInHand(bowDraw ? R.part[CP_ARML_D] : R.part[CP_ARMR_D], wear.held, bowDraw ? -1.f : 1.f, light, tint);
    if (!wear.off.empty() && !bowDraw) heldInHand(R.part[CP_ARML_D], wear.off, -1.f, light, tint);
  }
  // ---- a backpack: the pack itself, straps over the shoulders (in first person, just the straps down your chest) ----
  const mat4& B = R.body;
  if (!back.empty() && (!fp || R.fpTorso)) {
    mat4 m = B;
    if (!fp) m = glm::rotate(m, std::sin(ps.walk * 2) * 0.02f * ps.walkAmt, vec3(1, 0, 0));
    if (const CharMesh* pm = packWornMesh(back, fp, ov, ts)) VR.draw(pm->gl, m * pm->local(), light, tint);
  }
  // ---- a lantern (or torch) on the belt, or hung from the backpack's side ----
  if (!hook.empty() && (!fp || (R.fpTorso && back.empty())) && reg().item(hook)) {
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
  if (!fp && !ps.swim && !ps.sleep && ps.deathT <= 0 && !wear.carried.empty()) {
    const std::vector<std::string>& ids = wear.carried;
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

// ------------------------------------------------------------------ the first-person sleeve
void drawFirstPersonSleeve(const mat4& arm, VoxelLight light) {
  App& a = app();
  if (!a.session || !a.session->player) return;
  Player& p = *a.session->player;
  if (p.armor.size() < 2 || p.armor[1].empty() || hidden(p, 1) || !reg().item(p.armor[1].id)) return;
  auto spec = resolveCharacter(p.skin);
  const CharModel& M = charModel(*spec);
  std::string dye;
  if (p.armor[1].tag.is_object() && p.armor[1].tag.contains("col") && p.armor[1].tag["col"].is_string()) { std::string c = p.armor[1].tag["col"]; dye = c.size() == 7 ? c.substr(1) : ""; }
  const ArmorSet& A = armorMeshes(p.armor[1].id, dye, 1, M.slim, M.look.has_value(), a.tiles);
  // the first-person arm model (assets/char_model skinArmModel): 1/16 block a skin pixel, the hand's end at y -0.34375,
  // centred on the arm in x and z; the skin's right arm runs from y 12 (hand) to 24 (shoulder) about x = pivot.x
  using PP = std::pair<const char*, int>;
  for (const PP& pp : {PP{"armR_u", CP_ARMR_U}, PP{"armR_d", CP_ARMR_D}}) {
    const char* pn = pp.first; int pi = pp.second;
    auto it = A.byPart.find(pn);
    if (it == A.byPart.end()) continue;
    vec3 pv = M.pivot[pi];
    mat4 m = glm::translate(arm, vec3(0, (pv.y - 12) / 16.f - 0.34375f, 0));
    m = glm::scale(m, vec3(1 / 16.f));
    for (const CharMesh* cm : it->second) voxelRenderer().draw(cm->gl, m * cm->local(), light);
  }
}

} // namespace dq
