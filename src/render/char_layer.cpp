#include <chrono>
#include <algorithm>
// render/char_layer.cpp — players drawn as voxel characters (render/skins.js avatarDraws, game/anim.js,
// game/capes.js, game/carried.js, game/charparts.js, render/gear3d.js gearDraws).
#include "ui/cinematic.h"
#include "render/book_model.h"
#include "render/journal_model.h"
#include "ui/book_ui.h"
#include "ui/held_book.h"
#include "render/gl.h"
#include "common/fs.h"
#include <stb_image.h>
#include "core/tool_parts.h"
#include "sim/weather.h"
#include "render/char_layer.h"
#include "core/item_glint.h"
#include "render/rod_tip.h"
#include "assets/cape_tex.h"
#include "render/item_layers.h"
#include "render/fp_motion.h"
#include "client/app.h"
#include "render/creature_models.h"
#include "ui/screen.h"
#include "client/photo_mode.h"
#include "core/registry.h"
#include "assets/pack_armor.h"
#include "render/map_render.h"
#include "render/instruments.h"
#include "sim/maps.h"
#include <cstdlib>
#include <cstdio>
#include "sim/quiver.h"

namespace dq {
std::unique_ptr<Screen> makeKnappingScreen(const std::string& material, const std::string& item);   // ui/knapping_screen.cpp
std::unique_ptr<Screen> makeAnvilScreen(ivec3 at, int tier);   // ui/anvil_screen.cpp

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
// a bow carried as archers carry one walking: by the grip at the side, the arm hanging, so the bow lies level, its limbs
// fore and aft: held as it's shot and the arm lowered, the belly (the stave's curve, the sprite's upper left) down and the
// string on top. The fist is round the middle of the stave (bowPoint(0.5)); the chord (bottom left to top right) runs forward.
static mat4 bowHeldMatrix(float s, vec2 grip) {
  const float r2 = std::sqrt(0.5f);
  const vec3 X(0, r2 * s, -r2 * s), Y(0, -r2 * s, -r2 * s), Z(-s, 0, 0), t = -(grip.x * X + grip.y * Y);
  return mat4(vec4(X, 0), vec4(Y, 0), vec4(Z, 0), vec4(t, 1));
}
static bool isBowKey(const std::string& id) { return id == "bow" || id.rfind("bow_pulling", 0) == 0 || id.rfind("bow_pq", 0) == 0; }
const mat4& heldMat(const std::string& id) {
  static const mat4 BOW = bowHeldMatrix(BOW_SCALE, {-0.235f, 0.235f});   // (the grip was 1.5 px off the stave, out of the fist)
  if (isBowKey(id)) return BOW;
  static const mat4 TOOL = heldMatrix(0.25f, 0.85f, {-0.34f, -0.24f}, false), ITEM = heldMatrix(0.2f, 0.55f, {0, -0.25f}, true), ROD = heldMatrix(0.75f, 0.85f, {-0.34f, -0.24f}, false);
  return id == "fishing_rod" || id == "fishing_rod_cast" || id == "fishing_rod_tp" ? ROD : isHandheld(id) ? TOOL : ITEM;
}
float st0yaw(const CharPose& ps) { return ps.hasBodyYaw && !ps.fp ? ps.bodyYaw : ps.yaw; }
// held compasses and clocks in the bodies being drawn: their live dial goes on after the voxel pass (null = nobody is collecting)
struct Dial { std::string id; mat4 m; vec2 light; };
// the open books held in the bodies being drawn, the picture of their pages (Player::bookPic) to go on after the voxel pass
// (leaf 0..1: a page turning over, as they turned it — Player::bookFlip — dir its way, flipAt when it lifted)
struct BookPage { mat4 M; std::string pic, lay; u64 key; vec2 light; float w, h, bend; float leaf = -1; int dir = 1; double flipAt = -10; };
std::vector<BookPage> g_bookPages;
// a player's book picture, and the two before it (newest first, each with when it came): the page turning over shows
// the one they had when it lifted. (A menu switch can send two pictures in a turn: the first caught mid-change.)
struct BookPicTex { struct Pic { GLuint tex = 0; std::string lay; double at = -10; }; Pic h[3]; size_t hash = 0; };
BookPicTex& bookPicTex(u64 key, const std::string& pic, const std::string& lay, double now) {
  static auto& cache = *new std::unordered_map<u64, BookPicTex>();
  BookPicTex& t = cache[key];
  const size_t h = std::hash<std::string>()(pic);
  if (t.h[0].tex && t.hash == h) { t.h[0].lay = lay; return t; }
  const std::vector<u8> jpg = base64Decode(pic);
  int w = 0, hh = 0, n = 0;
  u8* px = jpg.empty() ? nullptr : stbi_load_from_memory(jpg.data(), (int)jpg.size(), &w, &hh, &n, 4);
  if (!px) return t;
  // (the key colour round the book, cut away: the ribbons' swallowtails and the air about them)
  // (the picture is squeezed: the key bleeds a pixel or two into what's next to it — those go too, and nothing pink is left)
  std::vector<u8> cut((size_t)w * hh, 0);
  for (int i = 0; i < w * hh; i++) { const u8* q = px + i * 4; cut[i] = q[0] - q[1] > 55 && q[2] - q[1] > 55 && q[0] > 120 && q[2] > 120; }
  for (int y = 0; y < hh; y++) for (int x = 0; x < w; x++) {
    u8* q = px + ((size_t)y * w + x) * 4;
    bool near = false;
    for (int dy = -2; dy <= 2 && !near; dy++) for (int dx = -2; dx <= 2; dx++) {
      const int xx = x + dx, yy = y + dy;
      if (xx >= 0 && yy >= 0 && xx < w && yy < hh && cut[(size_t)yy * w + xx]) { near = true; break; }
    }
    if (near) q[3] = 0;
  }
  if (t.h[2].tex) glDeleteTextures(1, &t.h[2].tex);
  t.h[2] = std::move(t.h[1]); t.h[1] = std::move(t.h[0]);
  t.h[0] = {makeTexture2D(w, hh, px, false, false, false), lay, now}; t.hash = h;
  stbi_image_free(px);
  return t;
}
std::vector<Dial>* g_dials = nullptr;
// skins.js held(): an item in a fist (fore = the forearm's frame, model pixels about the elbow; side -1 = the left hand)
void heldInHand(const mat4& fore, std::string id, float side, VoxelLight light, vec4 tint, bool glint = false, u32 rodOwner = 0, double now = 0, glm::dvec3 camPos = glm::dvec3(0), const std::string& modelKey = "") {
  if (id == "fishing_rod") id = "fishing_rod_tp";
  const VoxelModel& im = itemModel(modelKey.empty() ? id : modelKey);
  if (!im.ready()) return;
  // tuning aid: DQ_DEV_TPHELD="dy,dz,scale" (model pixels, scale added) for screenshots
  static float T[3] = {0, 0, 0}; static bool tl = false;
  if (!tl) { tl = true; if (const char* v = std::getenv("DQ_DEV_TPHELD")) std::sscanf(v, "%f,%f,%f", &T[0], &T[1], &T[2]); }
  mat4 hm = glm::translate(fore, vec3(0, -6 + T[0], -1 + T[1]));
  if (im.kind == VoxelModel::Block) {
    hm = glm::translate(hm, vec3(0, 0, -1.5f));
    hm = glm::scale(hm, vec3(1 / CHAR_PX));
    hm = glm::rotate(hm, -0.3f, vec3(1, 0, 0)); hm = glm::scale(hm, vec3(0.375f));
  } else if (im.stand) {
    // a vessel (a potion, a bucket) is held upright whatever the arm does, turned with the body: a potion by its belly,
    // a bucket hanging from its handle (it lay on its side in a raised arm)
    const float sc = glm::length(vec3((glm::scale(hm, vec3((1 + T[2]) / CHAR_PX)) * heldMat(id))[0]));
    const vec3 fist = vec3(hm[3]);
    vec3 fz = vec3(fore[2]); fz.y = 0;
    const float yaw = glm::length(fz) > 1e-5f ? std::atan2(fz.x, fz.z) : 0.f;
    const bool bucket = id.find("bucket") != std::string::npos;
    hm = glm::translate(mat4(1), fist);
    hm = glm::rotate(hm, yaw, vec3(0, 1, 0));
    hm = glm::scale(hm, vec3(sc * (bucket ? 1.f : 1.15f)));
    hm = glm::translate(hm, vec3(0, bucket ? -0.42f : 0.2f, 0));
    // its liquid sloshes with the hand's own motion (each hand found again by where it was: no two hands that close)
    struct Slosh { vec3 at, vel; vec2 sl, slv; double t; };
    static std::vector<Slosh> S;
    const double tnow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    Slosh* me = nullptr;
    for (auto& q : S) if (glm::length(q.at - fist) < 0.6f && tnow - q.t < 0.5) { me = &q; break; }
    if (!me) { S.erase(std::remove_if(S.begin(), S.end(), [&](const Slosh& q) { return tnow - q.t > 1.0; }), S.end()); S.push_back({fist, vec3(0), vec2(0), vec2(0), tnow}); me = &S.back(); }
    const float dt = (float)std::clamp(tnow - me->t, 0.0, 0.05);
    if (dt > 0) {
      const vec3 vel = (fist - me->at) / dt, acc = (vel - me->vel) / dt;
      const vec3 fwd(std::sin(yaw), 0, std::cos(yaw)), right(std::cos(yaw), 0, -std::sin(yaw));
      const vec2 want = glm::clamp(vec2(-glm::dot(acc, right), glm::dot(acc, fwd)) * 0.012f, vec2(-0.35f), vec2(0.35f));
      me->slv += ((want - me->sl) * 70.f - me->slv * 6.f) * dt; me->sl = glm::clamp(me->sl + me->slv * dt, vec2(-0.35f), vec2(0.35f));
      me->vel = vel;
    }
    me->at = fist; me->t = tnow;
    voxelRenderer().slosh = me->sl;
    voxelRenderer().draw(im.gl, hm * im.matrix(), light, tint, glint ? 1.f : 0.f);
    voxelRenderer().slosh = vec2(0);
    return;
  } else {
    hm = glm::scale(hm, vec3((1 + T[2]) / CHAR_PX)) * heldMat(id);
  }
  (void)side;
  if (rodOwner && (id == "fishing_rod_tp" || id == "fishing_rod_cast")) setRodTip(rodOwner, camPos + glm::dvec3(hm * vec4(0.47f, 0.47f, 0, 1)), now);
  if (g_dials && isInstrument(id) && im.kind != VoxelModel::Block) g_dials->push_back({id, hm, vec2(light.sky, light.block)});
  voxelRenderer().draw(im.gl, hm * im.matrix(), light, tint, glint ? 1.f : 0.f);
}
// a spyglass up to the eye (third person, Player::scoping): its eyepiece at the right eye and the glass along the look,
// following the head; the right hand (render/char_anim.cpp pose.scope) holds it there. DQ_DEV_SPY="x,y,z,scale" tunes it
void spyglassAtEye(const mat4& head, VoxelLight light, vec4 tint) {
  const VoxelModel& im = itemModel("spyglass");
  if (!im.ready()) return;
  static float T[4] = {2.f, 3.6f, -7.5f, 0.62f}; static bool tl = false;
  if (!tl) { tl = true; if (const char* v = std::getenv("DQ_DEV_SPY")) std::sscanf(v, "%f,%f,%f,%f", &T[0], &T[1], &T[2], &T[3]); }
  mat4 hm = glm::translate(head, vec3(T[0], T[1], T[2]));
  hm = glm::scale(hm, vec3(T[3] / CHAR_PX));
  hm = glm::rotate(hm, PI / 2, vec3(0, 1, 0)); hm = glm::rotate(hm, -PI / 4, vec3(0, 0, 1));   // the picture's diagonal along -z, the way the head looks
  voxelRenderer().draw(im.gl, hm * im.matrix(), light, tint, 0.f);
}
// the bow drawn (third person): the grip in the left fist, upright across the aim, its belly toward the target and the
// string pulled back to the right hand (render/char_anim.cpp bowDrawArms reached the hands there), bent as far as the
// hands are apart (item models bow_pq0..8, the arrow on the string)
void drawnBow(const CharRig& R, VoxelLight light, vec4 tint, bool glint) {
  const vec3 F(R.part[CP_ARML_D] * vec4(0, -BOW_FIST, 0, 1)), D(R.part[CP_ARMR_D] * vec4(0, -BOW_FIST, 0, 1));
  const float S = BOW_SCALE / CHAR_PX * glm::length(vec3(R.part[CP_ARML_D][1]));   // one item unit, in render space
  vec3 a = F - D; const float dist = glm::length(a); a = dist > 1e-6f ? a / dist : vec3(0, 0, -1);
  const vec3 up = glm::normalize(vec3(R.part[CP_BODY][1]));
  vec3 U = up - a * glm::dot(up, a); U = glm::length(U) > 1e-4f ? glm::normalize(U) : vec3(0, 1, 0);
  U = glm::normalize(U * std::cos(0.26f) + glm::cross(a, U) * std::sin(0.26f));   // canted, the top limb toward the drawing hand (it shows past the head from behind)
  int q = (int)std::lround(clampv((dist / (1.41421f * S) - 0.235f) / 0.3405f, 0.f, 1.f) * 8);
  const VoxelModel* im = nullptr;
  for (int d = 0; d <= 8 && !im; d++) for (int sg : {1, -1}) {   // the nearest stave that has been built
    const int qq = q + sg * d; if (qq < 0 || qq > 8) continue;
    const VoxelModel& c = itemModel("bow_pq" + std::to_string(qq));
    if (c.ready()) { im = &c; q = qq; break; }
  }
  if (!im) return;
  const vec3 X = (U - a) * 0.70711f, Y = (U + a) * 0.70711f, Z = glm::cross(X, Y);
  const vec2 g = vec2(-0.235f, 0.235f) * (1 + 0.3f * (q / 8.f));   // the middle of the stave, where the fist is
  const mat4 M(vec4(X * S, 0), vec4(Y * S, 0), vec4(Z * S, 0), vec4(F - (g.x * X + g.y * Y) * S, 1));
  voxelRenderer().draw(im->gl, M * im->matrix(), light, tint, glint ? 1.f : 0.f);
}
std::string toolType(const std::string& id) { const ItemDef* d = reg().item(id); return d && d->hasTool ? d->tool.type : std::string(); }
bool rx(const std::string& s, std::initializer_list<const char*> parts) { for (auto p : parts) if (s.find(p) != std::string::npos) return true; return false; }
}

// ------------------------------------------------------------------ test switches
void CharacterLayer::devSwitches(GameSession& s) {
  devFrames++;
  Player* p = s.player;
  if (devFrames == 1) {
    if (const char* v = std::getenv("DQ_VIEW")) s.viewMode = clampv(std::atoi(v), 0, 3);   // 3: from the front
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
    if (const char* v = std::getenv("DQ_DEV_POSE")) devPose = v;   // walk run sneak swim fly swing bow sit sleep hang shimmy climb ladder paddle eat glide
    if (const char* v = std::getenv("DQ_DEV_CARRY")) {   // "mob:cow" / "block:chest": picked up (Carry On)
      std::string w = v;
      if (w.rfind("mob:", 0) == 0) p->carrying = {{"kind", "mob"}, {"type", w.substr(4)}, {"hp", 10}};
      else if (w.rfind("block:", 0) == 0) p->carrying = {{"kind", "block"}, {"key", w.substr(6)}, {"meta", 0}};
    }
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
    else if (w == "anvil" && s.player) { s.player->held() = ItemStack("stone_hammer", 1); s.player->offhand[0] = ItemStack("copper_ingot", 1); s.player->offhand[0].tag = json{{"heat", 800}}; s.app.pushScreen(makeAnvilScreen(ivec3(0), 1)); }
    else if (w == "knapping" && s.player) { s.player->held() = ItemStack("pebble", 6); s.app.pushScreen(makeKnappingScreen("stone", "pebble")); }
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
  if (Player* lp = s.player) if (HeldItemLayer* h = s.layer<HeldItemLayer>()) { lp->swingT = h->swing; }
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
      if (devPose == "crawl") { p->crawling = true; v = vec3(0, 0, -1.2f); }
      if (devPose == "fly") { p->flying = true; v = vec3(0, 0, -8); }
      if (devPose == "scope") { p->scoping = true; if (p->held().empty()) p->held() = ItemStack("spyglass", 1); }
      if (devPose == "swing") { if (p->swingT <= 0) p->swingT = 0.0001f; p->swingT += fdt * 1.8f; if (p->swingT >= 1) p->swingT = 0.0001f; }
      if (devPose == "bow") { static const float devPull = [] { const char* v = std::getenv("DQ_DEV_PULL"); return v ? (float)std::atof(v) : 0.f; }(); p->bowPull = devPull > 0 ? devPull : std::min(1.f, p->bowPull + fdt); if (p->held().empty()) p->held() = ItemStack("bow", 1); }
      float c = std::cos(p->yaw), sn = std::sin(p->yaw);
      v = vec3(v.x * c + v.z * sn, v.y, -v.x * sn + v.z * c);
      st.vel = v;
    }
    float hs = std::hypot(v.x, v.z);
    bool ground = e->onGround || (!devPose.empty() && p == s.player && devPose != "fly");
    st.bobAmt += ((ground ? std::min(1.f, hs / 4.3f) : 0.f) - st.bobAmt) * std::min(1.f, fdt * 8);
    {   // backing off (the body still facing ahead): the step runs the other way
      const float fw = -(v.x * std::sin(st.bodyYaw) + v.z * std::cos(st.bodyYaw));
      st.backward = hs > 0.5f && fw < -0.3f * hs;
    }
    st.bob += hs * fdt * 1.6f * (st.backward ? -1.f : 1.f);
    st.swimPh = std::fmod(st.swimPh + fdt * (e->inWater ? 2.4f + hs * 0.5f : 1.2f), 628.3f);
    st.crawlPh = std::fmod(st.crawlPh + hs * fdt * 2.2f, 628.3f);   // a pull of the elbows to every ~1.4 blocks
    st.ladderPh = std::fmod(st.ladderPh + std::fabs(v.y) * fdt * 4.2f, 628.3f);   // hand over hand up a ladder
    // a little crouch on landing after a fall
    if (st.landT > 0) st.landT -= fdt;
    if (!e->onGround && !e->inWater && !p->flying) { if (!st.falling || e->pos.y > st.fallStart) st.fallStart = st.falling ? std::max(st.fallStart, e->pos.y) : e->pos.y; st.falling = true; }
    if (e->inWater || p->flying) st.falling = false;
    if (e->onGround && st.falling) {   // Story Mode weight: the bigger the drop, the deeper and longer the landing
      const float drop = st.fallStart - e->pos.y;
      if (drop > 0.8f) { st.landAmt = clampf(0.3f + drop / 5, 0.3f, 1.6f); st.landT = 0.2f * std::max(1.f, st.landAmt); }
      st.falling = false;
    }
    st.deathT = p->dead ? st.deathT + fdt : 0;
    // AnimatedPlayer: the look eased (a remote player's comes in steps), a hit's flinch timed from the hit, a lead leg per jump
    st.pitch += wrapAngle(p->pitch - st.pitch) * std::min(1.f, fdt * (p == s.player ? 30.f : 14.f));
    if (p->hurtTime > st.lastHurt + 0.01f) st.hurtT = 0.0001f; else if (st.hurtT > 0) st.hurtT += fdt;
    if (st.hurtT > 1) st.hurtT = 0;
    st.lastHurt = p->hurtTime;
    const bool inAir = !e->onGround && !e->inWater && !p->flying;
    if (inAir && !st.wasAir) st.airLeft = !st.airLeft;
    st.wasAir = inAir;
    // standing about (for the idles), and the ground under each foot a few times a second (a step or a slab under one), eased
    {
      const bool calm = ground && hs < 0.25f && p->swingT <= 0 && p->eatT <= 0 && p->bowPull <= 0 && !p->riding && !p->sleeping && !p->swimming && !p->dead;
      st.idleT = calm ? st.idleT + fdt : 0;
      float wl = 0, wr = 0;
      if (e->onGround && hs < 1.5f && !p->swimming && !p->riding && !p->sleeping && !p->dead) {
        if ((st.footT -= fdt) <= 0) {
          st.footT = 0.12f;
          const float c = std::cos(st.bodyYaw), sn = std::sin(st.bodyYaw);
          const vec3 right(c, 0, -sn);
          auto groundAt = [&](vec3 at) -> float {
            thread_local std::vector<DBox> bx; bx.clear();
            gatherBoxes(s.world(), at.x - 0.07, e->pos.y - 0.65, at.z - 0.07, at.x + 0.07, e->pos.y + 0.6, at.z + 0.07, bx);
            float top = -0.6f;
            for (auto& b : bx) if (b.y1 <= e->pos.y + 0.6 && (float)(b.y1 - e->pos.y) > top) top = (float)(b.y1 - e->pos.y);
            return std::clamp(top, -0.6f, 0.6f);
          };
          st.footTL = groundAt(e->pos - right * 0.13f); st.footTR = groundAt(e->pos + right * 0.13f);
          static const bool dbg = std::getenv("DQ_FOOT_LOG") != nullptr;
          if (dbg) DQ_LOG("feet %s: L %.2f R %.2f", p->name.c_str(), st.footTL, st.footTR);
        }
        wl = st.footTL; wr = st.footTR;
      } else st.footT = 0;
      const float k = std::min(1.f, fdt * 10);
      st.footL += (wl - st.footL) * k; st.footR += (wr - st.footR) * k;
    }
    {   // the elytra's wings opening and folding; in a steep dive they fold back in (ElytraModel: 1 - (-dir.y)^1.5)
      float want = (p->gliding || devPose == "glide") ? 1.f : 0.f;
      if (p->gliding && p->vel.y < 0 && glm::length(p->vel) > 1e-3f) want = 1.f - std::pow(std::min(1.f, -glm::normalize(p->vel).y), 1.5f);
      st.wing += (want - st.wing) * std::min(1.f, fdt * 9);
    }
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
    // the air the cape feels: your own motion, and the wind (sim/weather.h) blowing through it, gusting
    vec3 air = -v;
    if (const WeatherSystem* W = weatherOf(*s.level)) {
      const float ws = (0.6f + W->windStrength * 2.2f + W->gust * 3.f) * (1 + 0.35f * std::sin((float)time * 1.7f + std::sin((float)time * 0.6f) * 2));
      const bool sheltered = (s.world().getLight(ifloor(p->pos.x), ifloor(p->pos.y + 1.5f), ifloor(p->pos.z)) >> 4) < 10;   // indoors, in caves: still air
      if (!sheltered) air += vec3(-std::sin(W->windDir), 0, -std::cos(W->windDir)) * ws;
    }
    float cf = air.x * std::sin(p->yaw) + air.z * std::cos(p->yaw);   // air streaming back past you lifts it out behind
    float want = 0.06f + clampf(cf, 0, 8) * 0.13f + clampf(-v.y, 0, 20) * 0.04f + (p->sneaking ? 0.25f : 0);
    if (p->swimming || e->riding) want = 0.3f;
    want = std::min(want, 1.35f) + std::sin(st.bob * 2.2f) * 0.04f * st.bobAmt;
    st.capeA += (want - st.capeA) * std::min(1.f, fdt * 7);
    float side = -(air.x * std::cos(p->yaw) - air.z * std::sin(p->yaw));
    st.capeSway = clampf(-side * 0.05f, -0.25f, 0.25f) + std::sin((float)time * 3.1f) * (0.02f + 0.01f * clampf(glm::length(vec2(air.x, air.z)), 0, 6));
  }
  for (auto it = anims.begin(); it != anims.end();) it = it->second.seen + 120 < frameNo ? anims.erase(it) : std::next(it);
}

// ------------------------------------------------------------------ drawing
void CharacterLayer::drawOpaque(GameSession& s, const FrameCtx& f) {
  if (!s.player) return;
  bool begun = false;
  std::vector<Dial> dials; g_dials = &dials;
  for (auto& up : s.level->entities()) {
    Entity* e = up.get();
    if (!e || e->removed || e->type != "player" || e->realm != s.player->realm) continue;
    Player* p = dynamic_cast<Player*>(e);
    if (!p) continue;
    bool local = p == s.player, fp = false;
    if (local && photoMode().active && !s.split()) { if (!photoMode().showPlayer) continue; }   // photo mode: the camera is elsewhere
    else if (local && s.viewMode == 0) {
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
  g_dials = nullptr;
  for (const Dial& d : dials) drawInstrumentFace(f, f.cam.viewProj, true, d.id, itemModel(d.id), d.m, d.light);   // the needle / dial turning in their hands
  if (!g_bookPages.empty()) {   // the pages of their open books (what they're looking at: the others see it, as a map)
    ContentDraw& cd = contentDraw();
    if (cd.begin(f, f.cam.viewProj)) {
      for (auto& b : g_bookPages) {
        BookPicTex& bt = bookPicTex(b.key, b.pic, b.lay, f.time);
        JournalPic jp; jp.tex = bt.h[0].tex;
        journalLayoutParse(bt.h[0].lay, jp);
        // a page turning over (as in your own hands, render/item_layers.cpp): its face the pages as they were when it
        // lifted (the newest picture that came before the turn: one that came with it is already the new page), its back
        // the new ones (until the new picture comes, the same page both sides)
        const bool turning = b.leaf >= 0 && b.leaf <= 1;
        JournalPic old;
        if (turning) {
          const BookPicTex::Pic* was = nullptr;
          for (const auto& q : bt.h) if (q.tex) { was = &q; if (q.at < b.flipAt - 0.05) break; }   // (else the oldest kept)
          old.tex = was->tex;
          journalLayoutParse(was->lay, old);
        }
        drawJournalPages(cd, b.M, b.w, b.h, b.bend, jp, f.time, b.light, true, turning ? &old : nullptr, b.dir);
        if (turning) drawJournalLeaf(cd, b.M, b.w, b.h, b.bend, old, jp, b.leaf, b.dir, b.light, true);
      }
      cd.end();
    }
    g_bookPages.clear();
  }
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
  const std::vector<ItemStack>* v = k == "cape" ? &p.cape : k == "back" ? &p.back : k == "hook" ? &p.hook : k == "quiver" ? &p.quiver : nullptr;
  return v && !v->empty() && !(*v)[0].empty() ? (*v)[0].id : std::string();
}
bool gearHidden(const Player& p, int k) { return hidden(p, k); }

CharWear charWearOf(const Player& p, const CharacterSpec& spec, bool withCarried) {
  CharWear w;
  for (int i = 0; i < 4 && i < (int)p.armor.size(); i++) {
    if (p.armor[i].empty() || hidden(p, i)) continue;
    w.armor[i] = partArmorWornLook(p.armor[i]);   // a mixed piece: "pa:..." (each cell's material on its part of the body)
    if (p.armor[i].tag.is_object() && p.armor[i].tag.contains("col") && p.armor[i].tag["col"].is_string()) { std::string c = p.armor[i].tag["col"]; w.dye[i] = c.size() == 7 ? c.substr(1) : ""; }
  }
  if (w.armor[1] == "elytra") w.armor[1].clear();   // worn in the chest slot (until the cape slot has them): wings, not a breastplate
  const ItemDef* hd = w.armor[0].empty() ? nullptr : reg().item(w.armor[0]);
  w.hatItem = hd && (hd->has("hat") || hd->has("mobHead"));   // a mob head is worn over the head, not as armour
  w.back = hidden(p, 4) ? "" : wornItem(p, "back");
  w.hook = wornItem(p, "hook");
  if (!hidden(p, 6)) {   // the quiver (and are there arrows in it? a remote player's shows them: we can't see in)
    w.quiver = wornItem(p, "quiver");
    if (!w.quiver.empty()) w.quiverFill = !p.quiver.empty() && !p.quiver[0].empty() ? quiverFill(p.quiver[0]) : w.quiver == "golden_quiver" ? 4 : clampv(p.data.value("quiverFill", 0), 0, 4);
  }
  if (!hidden(p, 5)) {
    std::string c = wornItem(p, "cape");
    if (!c.empty()) { const ItemDef* cd = reg().item(c); w.cape = cd ? cd->str("cape") : ""; if (w.cape.empty() && cd) w.cape = c; }
    if (w.cape.empty() && p.armor.size() > 1 && !p.armor[1].empty() && p.armor[1].id == "elytra") w.cape = "elytra";
    if (w.cape.empty()) {   // no cape item worn: the cape painted in the cape editor, else the creator's cosmetic cape
      auto cc = p.data.is_object() ? p.data.find("customCape") : p.data.end();
      if (p.data.is_object() && cc != p.data.end() && cc->is_string() && !cc->get<std::string>().empty()) w.cape = customCapeKey(cc->get<std::string>());
      else w.cape = spec.cape;
    }
  }
  if (!p.inv.empty() && p.selected >= 0 && p.selected < (int)p.inv.size() && !p.inv[p.selected].empty()) { w.held = p.inv[p.selected].id; w.heldModel = partToolModelKey(p.inv[p.selected]); w.heldGlint = itemGlints(p.inv[p.selected]); if (w.held == "fishing_rod") { w.rodOwner = p.id; if (p.data.value("rodOut", false)) w.held = "fishing_rod_cast"; } }
  if (p.eatT > 0 && !p.eatId.empty() && !isDrinkItem(p.eatId) && reg().item(p.eatId)) { w.held = p.eatId; w.heldModel = eatModelKey(p.eatId, 1 - p.eatT); w.heldGlint = false; }   // the food they're eating, bitten down
  if (!p.offhand.empty() && !p.offhand[0].empty()) { w.off = p.offhand[0].id; w.offGlint = itemGlints(p.offhand[0]); }
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
  static const float devTurn = [] { const char* v = std::getenv("DQ_DEV_TURN"); return v ? (float)std::atof(v) : 0.f; }();   // test aid: turn the model (radians) to see it from the side
  ps.yaw = p.yaw + devTurn; ps.headPitch = st.pitch * 0.8f; ps.walk = st.bob * 1.4f; ps.walkAmt = st.bobAmt; ps.swing = p.swingT; ps.time = (float)time;
  ps.sneak = p.sneaking && !p.swimming; ps.sit = p.riding != 0; ps.sleep = sleeping; ps.swim = p.swimming;
  ps.crawl = p.crawling && !p.swimming;   // on the belly (it was the swim on dry land)
  ps.swimPitch = p.swimming ? p.pitch : 0;
  // ledge grab (player.cpp LedgeHang / LedgeClimb): hands up on the edge, hauling yourself over, the shimmy
  ps.hang = (bool)p.hang || p.data.value("hang", false);
  ps.climb = p.climb ? std::min(1.f, p.climb->t / 0.55f) : p.data.value("climb", 0.f);
  if (p.hang) { ps.shimPh = p.hang->shimPh; ps.shimMv = p.hang->shimT > 0 ? 1.f : 0.f; }
  // on a ladder or vines: hand over hand as you go up or down
  ps.ladder = p.contacts.ladder && !p.onGround && !p.flying && !ps.hang && ps.climb <= 0 && !ps.swim && !ps.crawl;
  if (ps.ladder) ps.ladderPh = st.ladderPh, ps.ladderMv = std::min(1.f, std::fabs(st.vel.y) / 2.f);
  ps.held = !wear.held.empty(); ps.off = !wear.off.empty(); ps.deathT = p.dead ? std::max(st.deathT, 0.01f) : 0;
  ps.bowArm = wear.held == "bow";
  ps.air = !p.onGround && !p.flying && !p.inWater && !p.riding && !ps.ladder && !ps.hang && ps.climb <= 0; ps.vy = st.vel.y; ps.sprint = p.sprinting; ps.land = std::max(0.f, st.landT); ps.landAmt = st.landAmt;
  ps.bow = p.bowPull; ps.eat = p.eatT; ps.scope = p.scoping;
  ps.gui = (p.activity & (Player::ACT_SCREEN | Player::ACT_TYPING)) && !p.riding && !p.sleeping; ps.doze = (p.activity & Player::ACT_DOZE) && !ps.gui && !p.sleeping;   // render/activity_layer.cpp
  ps.hurt = st.hurtT; ps.airLeft = st.airLeft; ps.backward = st.backward && !p.sneaking;
  if (ps.eat > 0) {   // a potion, a bottle, a mug, milk: drunk, not eaten
    ps.drink = isDrinkItem(p.eatT > 0 && !p.eatId.empty() ? p.eatId : wear.held);
  }
  {   // local.js paddling(): treading water or swimming along upright in water deeper than about a block
    auto liquidAt = [&](float dy) { return reg().tLiquid[w.getBlock(ifloor(p.pos.x), ifloor(p.pos.y + dy), ifloor(p.pos.z))] == 1; };
    bool deep = p.contacts.eyeWater || liquidAt(1.1f) || (!p.onGround && liquidAt(0.5f));
    ps.paddle = (p.contacts.water || p.inWater) && deep && !p.swimming && !p.flying && !p.riding && !p.dead && !ps.hang && ps.climb <= 0;
  }
  ps.swimPh = st.swimPh; ps.swimMv = std::min(1.f, std::hypot(st.vel.x, st.vel.z) / 2.2f);
  ps.crawlPh = st.crawlPh; ps.crawlMv = std::min(1.f, std::hypot(st.vel.x, st.vel.z) / 0.8f);
  ps.hasBodyYaw = true; ps.bodyYaw = st.bodyYaw + devTurn; ps.headTurn = wrapAngle(p.yaw - st.bodyYaw); ps.legTurn = st.legTurn;
  ps.accLean = p.onGround ? st.accLean : 0; ps.turnLean = st.turnLean;
  ps.idleT = st.idleT; ps.seed = p.id * 7919u + (u32)std::hash<std::string>()(p.name); ps.footL = st.footL; ps.footR = st.footR;
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
    if (devPose == "drink") { ps.eat = 0.3f; ps.drink = true; ps.held = true; }
    if (devPose == "hurt") ps.hurt = std::fmod((float)time, 1.2f) + 0.0001f;
    if (devPose == "die") ps.deathT = std::fmod((float)time, 2.f) + 0.0001f;
    if (devPose == "block") { ps.block = 1; ps.off = true; }
    if (devPose == "xbow") { ps.xbowCharge = std::fmod((float)time * 0.8f, 1.f) + 0.0001f; ps.held = true; }
    if (devPose == "xbowaim") { ps.xbowAim = true; ps.held = true; }
    if (devPose == "trident") { ps.throwCharge = std::min(1.f, std::fmod((float)time, 2.f)) + 0.0001f; ps.held = true; }
    if (devPose == "spear") { ps.spearCharge = 1; ps.held = true; }
    if (devPose == "back") ps.backward = true;
  }
  ps.capeA = st.capeA; ps.capeSway = st.capeSway;
  ps.fp = fp;
  ps.fpLeftArm = fp && wear.off.empty() && !ps.hang && !(ps.climb > 0) && !(p.bowPull > 0);
  // a map held up in both hands (or anything else the hand view takes the left hand for) has its own two arms: yours
  // stays out of it, so there are never three (render/item_layers.h handsTaken; a map in the main hand with the other
  // hand free is held in both)
  if (!fp && p.stationKind >= 0 && !p.dead && !p.riding) {   // working a station (theirs, or yours seen in F5): the arm reaching to the work
    if (p.workTap != st.workTapSeen) { st.workTapSeen = p.workTap; st.workTapAt = f.time; }
    const float tap = f.time - st.workTapAt < 0.22 ? std::sin((float)((f.time - st.workTapAt) / 0.22) * 3.14159f) : 0.f;
    const float yaw = p.yaw; const vec3 right(std::cos(yaw), 0, -std::sin(yaw));
    const vec3 hand = p.workAt + vec3(0, -0.06f * tap, 0);
    ps.reach = true;
    ps.reachR = f.cam.rel(glm::dvec3(hand));
    ps.reachL = f.cam.rel(glm::dvec3(hand - right * 0.42f + vec3(0, -0.04f, 0)));   // (the other hand on the station beside it)
    if (p.stationKind == 3 || p.stationKind == 4) ps.sneak = true;   // (down to the brewing stand, the furnace)
    if (p.stationKind == 1 && f.time - st.workTapAt < 1.4) { wear.held = "smith_hammer"; wear.heldModel.clear(); ps.held = true; }   // (the hammer while the blows land)
  }
  if (fp && cinematicActive()) {
    if (cinematicLeaning()) ps.fpLeftArm = false;   // (leaning over a station: both hands are the scene's; seamless: your own arms stay)
    // seamless first person: your own body works it, the arms reaching to where the hands go, crouched at a low station
    vec3 hw[2];
    if (!cinematicLeaning() && cinematicHandsWorld(hw)) {
      // (the arms are the hand view's, render/cinematic_hands.cpp: the body's own, by the eye, swelled and broke at the elbow)
      if (cinematicCrouch() > 0.5f) ps.sneak = true;
    }
  }
  if (fp && (p.activity & Player::ACT_SCREEN)) ps.fpLeftArm = false;
  if (fp && &p == s.player && (bookUiStationOn(s.app) || heldBookOpen())) ps.fpLeftArm = false;   // (a book read in both hands: the hand view's)   // (your journal in your off hand at a station: that arm is the one holding it, no second)   // (your journal held in both your hands: the hand view's two arms, no third)
  if (fp && ((HeldItemLayer::handsTaken & 2) || (isMapStack(p.held()) && (p.offhand.empty() || p.offhand[0].empty())))) ps.fpLeftArm = false;
  if (wear.held == "bow" && p.bowPull > 0) wear.held = p.bowPull < 0.45f ? "bow_pulling_0" : p.bowPull < 0.85f ? "bow_pulling_1" : "bow_pulling_2";
  // game/elytra.js: gliding on the wings (they open, you lie flat, arms swept back)
  ps.wingOpen = st.wing;
  if ((p.gliding || (devPose == "glide" && &p == s.player)) && wear.cape == "elytra") { ps.swim = true; ps.glide = true; ps.swimPitch = p.pitch; ps.air = false; ps.capeA = 0.05f; wear.back.clear(); }
  // Carry On: both arms out around it, nothing else in the hands
  const bool carrying = p.carrying.is_object();
  if (carrying) { ps.carry = true; ps.held = ps.off = false; wear.held.clear(); wear.off.clear(); wear.carried.clear(); ps.fpLeftArm = false; }
  if (p.data.value("invisible", false)) return;
  vec3 rel = f.cam.rel(glm::dvec3(p.pos));
  // maps.js: a map in the hand is held up in front of the chest (both hands when the other is free)
  vec2 lt((l >> 4) / 15.f, (l & 15) / 15.f);
  if (!fp && !ps.sleep && !ps.swim && !ps.crawl && !ps.hang && ps.climb <= 0 && ps.deathT <= 0 && !ps.ladder && !carrying) {
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
  mat4 base = glm::translate(mat4(1), rel + (fp && cinematicActive() ? cinematicViewOffset() : vec3(0)));   // (bent to a station: the body goes down and in with the eye)
  if (ps.sit) base = glm::translate(base, vec3(0, -0.45f, 0));
  CharDrawOpts o;
  o.light = VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f};
  o.tint = p.hurtTime > 0 ? vec4(1, 0.15f, 0.1f, 0.4f) : vec4(0);
  o.fp = fp; o.time = f.time; o.camPos = f.cam.origin; o.blend = &st.blend; o.cape = &st.cape;
  o.detail = fp || glm::dot(rel, rel) < 56.f * 56.f;   // far off, the detail is lost anyway: past 56 blocks only the skin
  {   // the journal: off the belt into both hands at a menu, its pages turned as they turn them (synced: Player::bookFlip)
    const bool want = (p.activity & Player::ACT_SCREEN) && !p.riding && !p.sleeping;
    const float dtb = st.bookT < 0 ? 0.f : (float)std::clamp(f.time - st.bookT, 0.0, 0.1); st.bookT = f.time;
    st.book = std::clamp(st.book + (want ? dtb * 3.f : -dtb * 4.f), 0.f, 1.f);
    if (!st.bookSeen) { st.bookFlip = p.bookFlip; st.bookSeen = true; }
    if (p.bookFlip != st.bookFlip) { st.bookFlip = p.bookFlip; st.flipAt = f.time; st.flipDir = p.bookDir; }
    o.book = st.book > 0.02f ? st.book : -1.f;
    o.bookPic = &p.bookPic; o.bookLay = &p.bookLay; o.bookKey = p.id;
    o.bookLeaf = f.time - st.flipAt < 0.45 ? (float)((f.time - st.flipAt) / 0.45) : -1.f; o.bookDir = st.flipDir;
    if (want) { ps.gui = true; }
  }
  drawCharacter(*spec, ps, wear, base, o);
  if (carrying && !fp && !ps.sleep && ps.deathT <= 0) {   // skins.js: it sits against the chest, in front of the arms
    mat4 m = glm::rotate(base, ps.hasBodyYaw && !ps.swim && !ps.crawl ? ps.bodyYaw : ps.yaw, vec3(0, 1, 0));
    m = glm::translate(m, vec3(0, 15 * CHAR_PX - (ps.sneak ? 0.12f : 0.f), -9 * CHAR_PX));
    drawCarriedThing(p.carrying, m, false, o.light, o.tint);
  }
}

void drawCharFace(const CharModel& M, const CharPose& ps, const mat4& head, VoxelLight light, vec4 tint) {
  const CharFace& F = M.face;
  if (!F.has || ps.deathT > 0) return;
  VoxelRenderer& VR = voxelRenderer();
  float t = ps.time, ph = std::fmod(t + F.seed, F.period);
  if (ps.sleep || ph < 0.13f || (ph > 1.6f && ph < 1.7f && F.period > 4.5f)) VR.draw(F.lid.gl, head * F.lid.local(), light, tint);   // a blink (now and then a double one)
  // the mouth: talking; a gasp when hit; chewing; open to drink; puffing on a long run (AnimatedPlayer's face)
  bool open = ps.talk && std::sin(t * 13 + F.seed * 3) + std::sin(t * 7.7f) * 0.7f > 0.25f;
  if (ps.hurt > 0 && ps.hurt < 0.35f) open = true;
  if (ps.eat > 0) open = ps.drink || std::sin(t * 22) > 0.2f;
  if (ps.sprint && ps.walkAmt > 0.5f && std::sin(t * 5 + F.seed) > 0.55f) open = true;
  if (open && F.mouth.ready() && !ps.sleep) VR.draw(F.mouth.gl, head * F.mouth.local(), light, tint);
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
  if (o.blend) charBlendStep(*o.blend, R, base, charStateKey(ps), o.time);
  // the 3D outer layer goes under armour: whatever a piece covers isn't drawn
  bool hid[CP_COUNT] = {};
  hid[CP_HEAD] = !AR[0].empty() && !hatItem;
  for (int i : {CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) hid[i] = !AR[1].empty();
  hid[CP_LEGR_U] = hid[CP_LEGL_U] = !AR[2].empty();
  hid[CP_LEGR_D] = hid[CP_LEGL_D] = !AR[2].empty() || !AR[3].empty();
  bool fpArmL = fp && R.fpTorso && ps.fpLeftArm && !ps.paddle;
  if (fp && ps.reach) for (int i : {CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D}) hid[i] = true;   // (no sleeve layer over them so close to the eye)
  // which parts show: in first person only what you see looking down (the head and the right arm are the view's own)
  auto shows = [&](int i) {
    if (o.headOnly) return i == CP_HEAD;
    if (!fp) return true;
    if (i == CP_HEAD) return false;
    // reaching to the work at a station: your forearms only (the upper arms, right by the eye, filled the view: an arm in an arm)
    if (ps.reach && (i == CP_ARMR_U || i == CP_ARML_U)) return false;
    if (ps.reach && (i == CP_ARMR_D || i == CP_ARML_D)) return true;
    if (i == CP_ARMR_U || i == CP_ARMR_D) return false;
    if (i == CP_ARML_U || i == CP_ARML_D) return fpArmL;
    if (i == CP_BODY) return R.fpTorso;
    return true;
  };
  static const std::map<std::string, int> PART = {{"head", CP_HEAD}, {"body", CP_BODY}, {"armR_u", CP_ARMR_U}, {"armR_d", CP_ARMR_D}, {"armL_u", CP_ARML_U},
                                                  {"armL_d", CP_ARML_D}, {"legR_u", CP_LEGR_U}, {"legR_d", CP_LEGR_D}, {"legL_u", CP_LEGL_U}, {"legL_d", CP_LEGL_D}};
  // ---- armour first: it is the outermost shell, so the skin under it fails the depth test early (cheap up close) ----
  for (int slot = 0; slot < 4; slot++) {
    if (AR[slot].empty() || (slot == 0 && (fp || hatItem))) continue;
    if (!reg().item(AR[slot]) && AR[slot].rfind("pa:", 0) != 0) continue;
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
  // ---- a mob head worn in the helmet slot: the creature's own head, a little bigger than yours, over it ----
  if (!AR[0].empty() && !fp && shows(CP_HEAD)) if (const ItemDef* hdef = reg().item(AR[0]); hdef && hdef->has("mobHead")) {
    const CreatureModelGL* CM = nullptr; std::vector<std::pair<int, mat4>> hp;
    if (creatureHeadParts(hdef->str("mobHead"), CM, hp)) {
      mat4 m = R.part[CP_HEAD] * R.scale[CP_HEAD] * glm::scale(glm::translate(mat4(1), vec3(-4.25f, -0.25f, -4.25f)), vec3(8.5f));   // the head box is x,z -4..4, y 0..8 px
      for (auto& [i, pm] : hp) VR.draw(CM->parts[i]->gl, m * pm * CM->parts[i]->local, light, tint);
    }
  }
  // ---- the skin ----
  for (int i : {CP_HEAD, CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D, CP_LEGR_U, CP_LEGR_D, CP_LEGL_U, CP_LEGL_D}) {
    if (!shows(i)) continue;
    mat4 m = R.part[i] * R.scale[i];
    if (M.base[i].ready()) VR.draw(M.base[i].gl, m * M.base[i].local(), light, tint);
    if (!hid[i] && M.ovl[i].ready()) VR.draw(M.ovl[i].gl, m * M.ovl[i].local(), light, tint);
    if (i == CP_HEAD && !fp) drawCharFace(M, ps, m, light, tint);
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
  // ---- game/elytra.js: two folded wings on the back, spreading open as you glide (they are worn in the cape's place) ----
  // Hung like the classic elytra (its model and layer): each wing a 10 x 20 x 2 panel grown a pixel all round (its
  // picture stretched over it), hinged at its outer shoulder blade 5 px out at the top of the back, 2 px behind the
  // body's middle. Folded it leans 15 deg back and 15 deg in, the two crossing in a V down the back; gliding it swings out
  // level (90 deg) with a 20 deg sweep; sneaking, 40 / 45 deg and dropped 3 px. (The model's space has x and y the other
  // way round to the rig's: the left wing hinges at -5 here, and its turns about y and x change sign.)
  if (cape == "elytra" && !fp && !ps.sleep) {
    const float open = clampf(ps.wingOpen, 0, 1);
    float ax = 0.2617994f, az = 0.2617994f, ay = 0.f, drop = 0.f;
    if (ps.sneak && !ps.glide) { ax = 0.6981317f; az = 0.7853982f; ay = 0.08726646f; drop = 3.f; }
    ax = lerpf(ax, 0.34906584f, open); az = lerpf(az, 1.5707964f, open);
    const float zBack = !back.empty() && ov.capeZ > 0 ? ov.capeZ : 2.f;
    for (int side = 0; side < 2; side++) {
      const CharMesh* wm = elytraWingMesh(side, ts);
      if (!wm) continue;
      const float s = side == 0 ? 1.f : -1.f;   // 0: the left wing
      mat4 m = glm::translate(R.body, vec3(-5.f * s, 12.f - drop, zBack));
      m = glm::rotate(m, -az * s, vec3(0, 0, 1));
      m = glm::rotate(m, -ay * s, vec3(0, 1, 0));
      m = glm::rotate(m, -ax, vec3(1, 0, 0));
      // (grown along the panel only: through it a wing stays a thin membrane, a pixel or so, as in the reference)
      m = glm::translate(m, vec3(5.f * s, -10.f, 1.f)); m = glm::scale(m, vec3(1.2f, 1.1f, 0.55f)); m = glm::translate(m, vec3(-5.f * s, 10.f, -1.f));
      VR.draw(wm->gl, m * wm->local(), light, tint);
    }
  }
  if (!cape.empty() && cape != "elytra" && !fp && !ps.sleep) {
    const mat4& B = R.body;
    // cloth: the cape hangs as a chain of pivots (like the banners' sway codes), so the hem
    // whips while the mantle stays calm. charRig already turned pose.yaw to bodyYaw, so
    // the wind vector below can be rotated into body-local space with just ps.yaw.
    float t = ps.time;
    float str = app().settings.wavingPlants ? 1.f : 0.f;   // same switch the banners honour
    App& A = app();
    static FrameCtx calm;                        // fallback for previews with no open session
    const FrameCtx* Fs = A.session ? &A.session->frame : nullptr;
    if (!Fs) { calm.windDir = vec2(1, 0); calm.wind = 0.3f; Fs = &calm; }
    const FrameCtx& F = *Fs;                     // this frame's wind (world_renderer / sky fill it)
    str *= std::min(1.5f, clampf(F.wind, 0.f, 1.5f) * 2.f + 0.3f);   // banners' uWind ~ 0.45 + strength*0.9
    float c = std::cos(ps.yaw), s2 = std::sin(ps.yaw);
    vec2 wloc(F.windDir.x * c - F.windDir.y * s2, F.windDir.x * s2 + F.windDir.y * c);   // wind in body-local space
    float gust = 1.f + std::sin(t * 2.3f) * 0.45f + std::sin(t * 4.7f + 1.9f) * 0.25f;   // banner-style gusts
    float ph = (floorf(o.camPos.x) + floorf(o.camPos.z)) * 1.7f;   // shared phase per block, so nearby capes ripple with the banners
    auto swayZ = [&](int k) { return std::cos(t * 1.9f + ph + k * 0.8f) * 0.04f * gust * str; };
    // one solid piece hung from the shoulders (it was two, the hem hinged on its own: a split showed across it). Walking
    // lifts it; the wind from in front blows it out behind, a side wind swings it over, and the gusts make it flutter.
    // Under a backpack the pack pins its top: it only stirs.
    const float flut = (std::sin(t * 2.3f + ph) + 0.5f * std::sin(t * 4.1f + ph * 1.3f)) * 0.035f * gust * str;
    float lift = ps.capeA + str * (std::max(0.f, wloc.y) * 0.32f * gust + 0.05f * gust) + flut + std::sin(t * 3.1f) * 0.03f * ps.walkAmt;
    float side = ps.capeSway + str * (wloc.x * 0.12f * gust) + swayZ(1);
    if (!back.empty()) { lift = std::min(0.22f, lift * 0.35f); side *= 0.4f; }
    lift = clampf(lift, 0.f, 1.3f); side = clampf(side, -0.45f, 0.45f);
    // it flies like a banner: eight strips, each hung from the foot of the one above. The shoulders take half the lift,
    // the rest curves on down the cloth, and a ripple runs down it to the hem (more with the wind and the pace). Never
    // bent in against the legs. (The strips' seams don't show: each turns only a little from the last.)
    if (o.cape) {   // the cloth (render/cape_sim.h): hung from the shoulders, simulated in the world, held to the body
      const vec3 anchor(0, 12, !back.empty() && ov.capeZ > 0 ? ov.capeZ : 2.1f);
      const float windSpeed = str * clampf(F.wind, 0.f, 1.5f) * 2.4f * gust;   // (the same wind and gusts the banners fly in)
      const mat4 worldBody = glm::translate(mat4(1), vec3(o.camPos)) * B;
      capeStep(*o.cape, worldBody, anchor, vec3(F.windDir.x, 0, F.windDir.y) * windSpeed, o.time, back.empty() ? 1.3f : 0.22f);
      mat4 m = glm::translate(B, anchor);
      for (int i = 0; i < CapeSim::N; i++) {
        const CharMesh* cm = capeMesh(cape, 10 + i, ts);
        if (!cm) break;
        m = glm::rotate(m, -o.cape->bend[i], vec3(1, 0, 0));
        m = glm::rotate(m, o.cape->side[i], vec3(0, 0, 1));
        VR.draw(cm->gl, m * cm->local(), light, tint);
        m = glm::translate(m, vec3(0, -CapeSim::SEG, 0));
      }
    } else {
    const int NS = 8;
    const float flap = clampf(lift * 1.1f + str * 0.35f + ps.walkAmt * 0.3f, 0.f, 1.4f) * (back.empty() ? 1.f : 0.3f);
    mat4 m = glm::translate(B, vec3(0, 12, !back.empty() && ov.capeZ > 0 ? ov.capeZ : 2.1f));
    float acc = 0;
    for (int i = 0; i < NS; i++) {
      const CharMesh* cm = capeMesh(cape, 10 + i, ts);
      if (!cm) break;
      const float f = (float)i / NS;
      float bend = i == 0 ? lift * 0.5f : lift * 0.5f / (NS - 1) * (0.6f + 0.8f * f);
      bend += i == 0 ? 0.f : std::sin(t * 4.3f + ph - i * 0.85f) * (0.02f + 0.07f * flap) * f;
      if (acc + bend < 0) bend = -acc;
      acc += bend;
      const float sd = i == 0 ? side * 0.6f : side * 0.4f / (NS - 1) + std::sin(t * 3.4f + ph * 1.3f - i * 0.7f) * 0.025f * flap * f;
      m = glm::rotate(m, -bend, vec3(1, 0, 0));
      m = glm::rotate(m, sd, vec3(0, 0, 1));
      VR.draw(cm->gl, m * cm->local(), light, tint);
      m = glm::translate(m, vec3(0, -2, 0));
    }
    }
  }
  // ---- what's in their hands (third person): Minecraft's thirdperson_righthand / lefthand ----
  if (!fp) {
    bool bowDraw = ps.bow > 0 && wear.held.rfind("bow", 0) == 0;
    if (bowDraw) drawnBow(R, light, tint, wear.heldGlint);
    else if (ps.scope && wear.held == "spyglass") spyglassAtEye(R.part[CP_HEAD], light, tint);
    else if (!wear.held.empty()) heldInHand(R.part[CP_ARMR_D], wear.held, 1.f, light, tint, wear.heldGlint, wear.rodOwner, o.time, o.camPos, wear.heldModel);
    if (!wear.off.empty() && !bowDraw) heldInHand(R.part[CP_ARML_D], wear.off, -1.f, light, tint, wear.offGlint);
  }
  // ---- the journal (render/book_model.h): on the belt at the left hip, or held open before them in both hands ----
  if (!o.headOnly && o.detail) {
    const vec3 sR = vec3(R.part[CP_ARMR_U][3]), sL = vec3(R.part[CP_ARML_U][3]);
    const vec3 up = glm::normalize(vec3(R.part[CP_BODY][1]));
    vec3 left = sL - sR; left -= up * glm::dot(left, up); left = glm::length(left) > 1e-5f ? glm::normalize(left) : vec3(1, 0, 0);
    const vec3 fwd = glm::cross(left, up);   // (the way they face)
    const vec3 hip = vec3(R.part[CP_BODY][3]);
    if (o.book < 0.35f) {   // on the belt: upright on the front of the left hip (clear of the arm), its cover outward
      const vec3 at = hip + left * 0.26f + up * 0.06f + fwd * 0.15f;
      mat4 F(1); F[0] = vec4(fwd, 0); F[1] = vec4(-left, 0); F[2] = vec4(up, 0); F[3] = vec4(at, 1);
      drawBook(VR, F, light, 0.f, -1.f, 1, 0.42f, 0x5A3418);
    } else if (!fp) {   // in both hands before them, open, the pages toward their face
      const vec3 fR = vec3(R.part[CP_ARMR_D] * vec4(0, -6, -1, 1)), fL = vec3(R.part[CP_ARML_D] * vec4(0, -6, -1, 1));
      const vec3 head = vec3(R.part[CP_HEAD] * vec4(0, 4, 0, 1));
      const vec3 c = (fR + fL) * 0.5f + up * 0.03f;
      vec3 y = glm::normalize(head - c), z = up - y * glm::dot(up, y);
      z = glm::length(z) > 1e-4f ? glm::normalize(z) : fwd;
      const vec3 x = glm::cross(y, z);
      mat4 F(1); F[0] = vec4(x, 0); F[1] = vec4(y, 0); F[2] = vec4(z, 0); F[3] = vec4(c, 1);
      const float sc = std::clamp(glm::length(fR - fL) / 0.75f, 0.4f, 0.75f);
      const bool pic = o.bookPic && !o.bookPic->empty() && o.book > 0.95f;
      if (!pic) drawBook(VR, F, light, std::clamp((o.book - 0.35f) / 0.65f, 0.f, 1.f), o.bookLeaf, o.bookDir, sc, 0x5A3418);
      else {   // the journal as they hold it: the drawn book of their menus as a real one (render/journal_model.h), its pages
        // toward their face — x across it as they read it, y up its pages, z out of them toward their eyes
        const vec3 zb = y, yb = z, xb = glm::cross(yb, zb);
        const float jw = 0.75f * sc * 1.25f, jh = jw * (180.f / 292.f), bend = 1.f;
        mat4 J(1); J[0] = vec4(xb, 0); J[1] = vec4(yb, 0); J[2] = vec4(zb, 0); J[3] = vec4(c - zb * (jh * 0.03f), 1);
        drawJournalBody(VR, J, jw, jh, bend, light, false);
        g_bookPages.push_back({J, *o.bookPic, o.bookLay ? *o.bookLay : std::string(), o.bookKey, vec2(light.sky, light.block), jw, jh, bend,
                               o.bookLeaf, o.bookDir, o.time - std::max(0.f, o.bookLeaf) * 0.45});   // (drawn after the voxels)
      }
    }
  }
  // ---- a backpack: the pack itself, straps over the shoulders (in first person, just the straps down your chest) ----
  const mat4& B = R.body;
  if (!back.empty() && (!fp || R.fpTorso)) {
    mat4 m = B;
    if (!fp) m = glm::rotate(m, std::sin(ps.walk * 2) * 0.02f * ps.walkAmt, vec3(1, 0, 0));
    if (const CharMesh* pm = packWornMesh(back, fp, ov, ts)) VR.draw(pm->gl, m * pm->local(), light, tint);
  }
  // ---- a quiver (assets/quiver3d.h): slung across the back from hip to shoulder, or strapped upright to the backpack's
  // side (the lantern hangs on the other). Empty it's just the case; the fuller it is, the more arrows stand in its mouth ----
  if (!wear.quiver.empty() && !fp) {
    if (const CharMesh* qm = quiverWornMesh(wear.quiver, wear.quiverFill, ts)) {
      const float walk = std::sin(ps.walk * 2) * ps.walkAmt;
      mat4 m;
      if (!back.empty()) {   // its strap against the pack's side pocket
        m = glm::translate(B, vec3(-6.4f, 0.6f, 3.7f + ov.po));
        m = glm::rotate(m, 0.06f, vec3(0, 0, 1));
        m = glm::rotate(m, walk * 0.05f, vec3(1, 0, 0));
        m = glm::rotate(m, PI / 2, vec3(0, 1, 0));
        m = glm::scale(m, vec3(0.8f));
      } else {               // its strap against the back, the base at the hip and the mouth over the shoulder
        m = glm::translate(B, vec3(-2.2f, 0.8f, 4.0f + (AR[1].empty() ? 0.f : 1.f)));
        m = glm::rotate(m, -0.4f, vec3(0, 0, 1));
        m = glm::rotate(m, walk * 0.06f, vec3(1, 0, 0));
        m = glm::rotate(m, PI, vec3(0, 1, 0));
        m = glm::scale(m, vec3(0.85f));
      }
      VR.draw(qm->gl, m * qm->local(), light, tint);
    }
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
  if (!fp && !ps.swim && !ps.crawl && !ps.sleep && ps.deathT <= 0 && !wear.carried.empty()) {
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
  const ArmorSet& A = armorMeshes(partArmorWornLook(p.armor[1]), dye, 1, M.slim, M.look.has_value(), a.tiles);
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
