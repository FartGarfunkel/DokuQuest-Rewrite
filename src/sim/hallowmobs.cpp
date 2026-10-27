// sim/hallowmobs.cpp — October's three night creatures (own design; they exist only while halloween() is on):
//  * pumpkin creature: sits in a pumpkin patch as a real pumpkin block with a plain pumpkin head on it. It is a dead
//    giveaway by day; at night it stirs when you come within ~4 blocks (2.6 if you sneak), or hit / mine it,
//    and it tears itself out of the block and rises on a tangle of vines; mining or hitting it first gives it a free
//    strike. By day it just is a pumpkin again.
//  * scarecrow: stands in fields and by village farms like a decoration; at night it only moves while no player is
//    looking at it (view-cone dot test + line of sight), shedding straw as it goes.
//  * ghost: a pale see-through sheet that drifts about at night, passes through leaves, moans, shies from light, chills
//    you (short slowness, a point of damage) on touch and fades away when you stare it down.
// Types and models are added to the registry here (the creature pipeline then treats them like any other); AI is the
// per-type brain hook (sim/creature.h); the rig animation of the pumpkin creature is registered below.
#include "sim/hallow.h"
#include "sim/creature_rig.h"
#include "sim/effects.h"
#include "sim/mob_combat.h"
#include "sim/player.h"
#include "common/holiday.h"
#include "core/registry.h"
#include <algorithm>

namespace dq {

// ------------------------------------------------------------------ per-creature state (data["hw"])
float hwGet(const Creature& m, const char* key, float def) {
  auto it = m.data.find("hw");
  if (it == m.data.end() || !it->is_object()) return def;
  auto jt = it->find(key);
  return jt != it->end() && jt->is_number() ? jt->get<float>() : def;
}
static void hwSet(Creature& m, const char* key, float v) {
  json& o = m.data["hw"];
  if (!o.is_object()) o = json::object();
  auto it = o.find(key);
  if (it != o.end() && it->is_number() && it->get<float>() == v) return;
  o[key] = v;
}
static bool hwHas(const Creature& m, const char* key) { auto it = m.data.find("hw"); return it != m.data.end() && it->is_object() && it->contains(key); }

// ------------------------------------------------------------------ ghosts drift through leaves
MoveResult phaseMove(const World& w, vec3& pos, float hw, float h, vec3 d) {
  const Registry& R = reg();
  auto blocked = [&](const vec3& p) {
    for (int y = ifloor(p.y); y <= ifloor(p.y + h - 0.01f); y++)
      for (int z = ifloor(p.z - hw); z <= ifloor(p.z + hw); z++)
        for (int x = ifloor(p.x - hw); x <= ifloor(p.x + hw); x++) { BlockId id = w.getBlock(x, y, z); if (id && R.tSolid[id] && !R.tLeaves[id]) return true; }
    return false;
  };
  MoveResult r; vec3 q = pos;
  q.x += d.x; if (blocked(q)) { q.x = pos.x; r.hitX = true; }
  q.z += d.z; if (blocked(q)) { q.z = pos.z; r.hitZ = true; }
  q.y += d.y; if (blocked(q)) { q.y = pos.y; r.hitY = true; }
  pos = q;
  return r;
}

// ------------------------------------------------------------------ registry: types and models
static json part(const char* name, const char* parent, vec3 at, vec3 lo, vec3 hi, json tex, json extra = json::object()) {
  json j = {{"name", name}, {"at", {at.x, at.y, at.z}}, {"box", {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z}}, {"tex", tex},
            {"pivot", {at.x / 16, at.y / 16, at.z / 16}}, {"min", {lo.x / 16, lo.y / 16, lo.z / 16}}, {"max", {hi.x / 16, hi.y / 16, hi.z / 16}}};
  j["parent"] = parent ? json(parent) : json(nullptr);
  for (auto& [k, v] : extra.items()) j[k] = v;
  return j;
}

// The creeper (the fuseling): rebuilt as a voxel model from Creeper.jpg (three views: front, three-quarter,
// side — fine-grid pixel pass): a tall NARROW trunk body (as wide as deep, much taller than wide), a canopy head
// barely wider than the trunk with the moss face at its crown, and long vine curtains hanging down the flanks
// from brow to ground (the reference's vertical "stripes"; gaps between them read as eyes in the coarse pass).
// The dark maw patch sits at the BASE front of the trunk (jaw there), and leaf tufts stick out along the sides.
// My first attempt had the proportions inverted — squat body + oversized head slab — which the three-view sheet
// immediately rules out. Part names, parents, roles and pivots stay exactly as before, so animPod + the "fuse"
// post animate it identically. This replaces the stock fuseling model outright — same key "fuseling", so /summon
// fuseling, saves, dismemberment and the resource pack's entity/creeper reskin all keep working.
// Proportions follow the community-cited Minecraft creeper: Java height 1.7 blocks (0.6 wide; head 8x8 px,
// body 4x8x12 px, legs 4x8x4 px). Our voxels are 16 units/block so ×16: h≈27, w≈10 with four 3.2-wide corner
// legs reaching the ground (feet at y=0 — they detached last pass when I shortened leg boxes but kept their
// old pivots; keep leg box bottom = -pivot.y exactly).
static json creeperModel() {
  const json moss = "moss_block", vineT = "vine", leafT = "leaves", rootT = "fuse_root", mawT = "fuse_maw";
  json p = {{"rig", "pod"}, {"scale", 1}, {"height", 27}};
  p["parts"] = json::array({
    part("body", nullptr, {0, 8.5f, 0}, {-4, 0, -4}, {4, 17, 4}, moss, {{"role", "pbody"}}),
    part("head", "body", {0, 17, 0}, {-4.75f, 0, -4.75f}, {4.75f, 8, 4.75f}, {{"all", moss}, {"front", vineT}}, {{"role", "phead"}, {"limb", "head"}}),
    part("jaw", "body", {0, 3.5f, -4}, {-2.5f, -3.5f, -1.25f}, {2.5f, 0, 0}, {{"all", moss}, {"top", mawT}, {"front", mawT}}, {{"role", "jaw"}}),
    part("frond0", "head", {-4.75f, 8, 0}, {-0.9f, -16, -1.3f}, {0, 0, 1.3f}, vineT, {{"role", "frond"}, {"noCull", true}}),
    part("frond1", "head", {-3.2f, 8, -4.75f}, {-1.3f, -16, -0.9f}, {1.3f, 0, 0}, vineT, {{"role", "frond"}, {"noCull", true}}),
    part("frond2", "head", {4.75f, 8, 0}, {0, -16, -1.3f}, {0.9f, 0, 1.3f}, vineT, {{"role", "frond"}, {"noCull", true}}),
    part("frond3", "head", {3.2f, 8, -4.75f}, {-1.3f, -16, 0}, {1.3f, 0, 0.9f}, vineT, {{"role", "frond"}, {"noCull", true}}),
    part("frond4", "head", {-2.6f, 8, 4.75f}, {-1.3f, -16, 0}, {1.3f, 0, 0.9f}, vineT, {{"role", "frond"}, {"noCull", true}}),
    part("thorn10", "body", {4, 15, -1.5f}, {-0.5f, 0, -0.5f}, {0.5f, 2.5f, 0.5f}, leafT, {{"rest", {0, 0, -1.2f}}}),
    part("thorn11", "body", {-4, 11, 1.5f}, {-0.5f, 0, -0.5f}, {0.5f, 2.5f, 0.5f}, leafT, {{"rest", {0, 0, 1.2f}}}),
    part("thorn12", "body", {1.5f, 18, 4}, {-0.5f, 0, -0.5f}, {0.5f, 2.5f, 0.5f}, leafT, {{"rest", {1.1f, 0, 0}}}),
    part("thorn13", "body", {-1.5f, 9, -4}, {-0.5f, 0, -0.5f}, {0.5f, 2.5f, 0.5f}, leafT, {{"rest", {-1.1f, 0, 0}}}),
    part("thorn14", "body", {4, 6, 1.5f}, {-0.5f, 0, -0.5f}, {0.5f, 2.5f, 0.5f}, leafT, {{"rest", {0, 0, -1.2f}}}),
    part("thorn15", "body", {-4, 3, -1.5f}, {-0.5f, 0, -0.5f}, {0.5f, 2.5f, 0.5f}, leafT, {{"rest", {0, 0, 1.2f}}}),
    part("legFL", nullptr, {-3.4f, 8.5f, -3.4f}, {-1.6f, -8.5f, -1.6f}, {1.6f, 0, 1.6f}, rootT, {{"role", "qleg"}, {"front", true}, {"side", -1}, {"limb", "legFL"}}),
    part("legFR", nullptr, {3.4f, 8.5f, -3.4f}, {-1.6f, -8.5f, -1.6f}, {1.6f, 0, 1.6f}, rootT, {{"role", "qleg"}, {"front", true}, {"side", 1}, {"limb", "legFR"}}),
    part("legBL", nullptr, {-3.4f, 8.5f, 3.4f}, {-1.6f, -8.5f, -1.6f}, {1.6f, 0, 1.6f}, rootT, {{"role", "qleg"}, {"front", false}, {"side", -1}, {"limb", "legBL"}}),
    part("legBR", nullptr, {3.4f, 8.5f, 3.4f}, {-1.6f, -8.5f, -1.6f}, {1.6f, 0, 1.6f}, rootT, {{"role", "qleg"}, {"front", false}, {"side", 1}, {"limb", "legBR"}})});
  p["post"] = "fuse";
  return p;
}

static void injectTypes() {
  json& mobs = reg().mobs; json& models = reg().models;
  if (!mobs.is_object() || !models.is_object() || mobs.contains("scarecrow")) return;
  auto drops = [](std::initializer_list<json> l) { return json(l); };
  mobs["pumpkin_creature"] = {{"model", "pumpkin_creature"}, {"label", "a pumpkin creature"}, {"hp", 24}, {"w", 0.45}, {"h", 1.0}, {"speed", 2.7}, {"xp", 9}, {"hostile", true},
    {"damage", 4}, {"voice", "hw_gourd"}, {"noDismember", true}, {"knockResist", 0.3},
    {"drops", drops({json::array({"pumpkin_seeds", 1, 3, 1.0}), json::array({"vine", 1, 2, 0.8}), json::array({"carved_pumpkin", 1, 1, 0.3})})}};
  mobs["scarecrow"] = {{"model", "scarecrow"}, {"label", "a scarecrow"}, {"hp", 26}, {"w", 0.35}, {"h", 2.0}, {"speed", 3.2}, {"xp", 8}, {"hostile", true}, {"damage", 3},
    {"voice", "hw_straw"}, {"noDismember", true},
    {"drops", drops({json::array({"wheat", 1, 3, 1.0}), json::array({"stick", 1, 2, 0.5}), json::array({"carved_pumpkin", 1, 1, 0.2})})}};
  mobs["ghost"] = {{"model", "ghost"}, {"label", "a ghost"}, {"hp", 10}, {"w", 0.4}, {"h", 1.8}, {"speed", 1.3}, {"fly", 1.6}, {"xp", 6}, {"hostile", true}, {"flyer", true}, {"phase", true},
    {"damage", 1}, {"voice", "hw_ghost"}, {"noPush", true}, {"noDismember", true},
    {"drops", drops({json::array({"ectoplasm", 1, 1, 0.5})})}};

  // pumpkin creature: a 16 px pumpkin on the ground ("tell" = the faint glowing face, shown at night while it hides; the rest
  // shows once it is up). Its vine legs hang from the pumpkin and unfold as it rises (see animPumpkin).
  const json up = {{"only", "d:up"}};
  auto upx = [&](json x) { x["only"] = "d:up"; return x; };
  json pk = {{"rig", "hw_pumpkin"}, {"scale", 1}, {"hipY", 0}, {"height", 36}, {"bodyDepth", 16}};
  const json plainPumpkin = {{"side", "pumpkin_side"}, {"top", "pumpkin_top"}, {"bottom", "pumpkin_top"}, {"front", "jack_o_lantern"}};   // a plain jack-o'-lantern head
  pk["parts"] = json::array({
    part("head", nullptr, {0, 0, 0}, {-8, 0, -8}, {8, 16, 8}, plainPumpkin, up),
    part("stem", "head", {0, 16, 0}, {-1.5f, 0, -1.5f}, {1.5f, 4, 1.5f}, "moss_block", up),
    part("core", "head", {0, 0, 0}, {-4, -12, -4}, {4, 0, 4}, "moss_block", up),
    part("twistA", "core", {0, -4, 0}, {-5.5f, -1, -1}, {5.5f, 1, 1}, "moss_block", upx({{"rest", {0, 0.7, 0.5}}})),
    part("twistB", "core", {0, -8, 0}, {-5.5f, -1, -1}, {5.5f, 1, 1}, "moss_block", upx({{"rest", {0, -0.7, -0.5}}})),
    part("legFR", "head", {6, 1, -5}, {-1.5f, -10, -1.5f}, {1.5f, 0, 1.5f}, "moss_block", up),
    part("shinFR", "legFR", {0, -10, 0}, {-1, -9, -1}, {1, 0, 1}, "moss_block", up),
    part("legBR", "head", {6, 1, 5}, {-1.5f, -10, -1.5f}, {1.5f, 0, 1.5f}, "moss_block", up),
    part("shinBR", "legBR", {0, -10, 0}, {-1, -9, -1}, {1, 0, 1}, "moss_block", up),
    part("legFL", "head", {-6, 1, -5}, {-1.5f, -10, -1.5f}, {1.5f, 0, 1.5f}, "moss_block", up),
    part("shinFL", "legFL", {0, -10, 0}, {-1, -9, -1}, {1, 0, 1}, "moss_block", up),
    part("legBL", "head", {-6, 1, 5}, {-1.5f, -10, -1.5f}, {1.5f, 0, 1.5f}, "moss_block", up),
    part("shinBL", "legBL", {0, -10, 0}, {-1, -9, -1}, {1, 0, 1}, "moss_block", up),
    part("armR", "head", {8, 9, 0}, {-1.5f, -11, -1.5f}, {1.5f, 0, 1.5f}, "moss_block", up),
    part("armL", "head", {-8, 9, 0}, {-1.5f, -11, -1.5f}, {1.5f, 0, 1.5f}, "moss_block", up)});
  models["pumpkin_creature"] = pk;

  // scarecrow: a biped (the shared walk) in a brown shirt stuffed with straw, a pumpkin head under a straw hat, arms out sideways
  json sc = {{"rig", "biped"}, {"scale", 1}, {"hipY", 12}, {"bodyDepth", 4}, {"height", 34}};
  const json pumpkinHead = {{"side", "pumpkin_side"}, {"top", "pumpkin_top"}, {"bottom", "pumpkin_top"}, {"front", "carved_pumpkin"}};
  auto role = [](const char* r, json x = json::object()) { x["role"] = r; return x; };
  auto limb = [](const char* r, json x = json::object()) { x["role"] = r; x["limb"] = r; return x; };
  sc["parts"] = json::array({
    part("body", nullptr, {0, 12, 0}, {-4, 0, -2}, {4, 12, 2}, "brown_wool", role("body")),
    part("hem", "body", {0, 0, 0}, {-4.6f, -2.5f, -2.6f}, {4.6f, 0.5f, 2.6f}, "hay_side"),
    part("ruff", "body", {0, 12, 0}, {-5, -1, -3}, {5, 1.5f, 3}, "hay_side"),
    part("head", "body", {0, 12, 0}, {-4, 0, -4}, {4, 8, 4}, pumpkinHead, limb("head")),
    part("brim", "head", {0, 8, 0}, {-6.5f, 0, -6.5f}, {6.5f, 0.8f, 6.5f}, "hay_top"),
    part("crown", "head", {0, 8.8f, 0}, {-3.5f, 0, -3.5f}, {3.5f, 4.5f, 3.5f}, "hay_top"),
    part("band", "head", {0, 8.8f, 0}, {-3.8f, 0, -3.8f}, {3.8f, 1.3f, 3.8f}, "red_wool"),
    part("armR", "body", {6, 10, 0}, {-2, -4, -2}, {2, 2, 2}, "brown_wool", limb("armR", {{"rest", {0, 0, 1.35}}})),
    part("foreR", "armR", {0, -4, 0}, {-2, -6, -2}, {2, 0, 2}, "hay_side", limb("foreR", {{"hand", {0, -5, 0}}})),
    part("tuftR", "foreR", {0, -6, 0}, {-2.5f, -3.5f, -2.5f}, {2.5f, 0, 2.5f}, "hay_side"),
    part("armL", "body", {-6, 10, 0}, {-2, -4, -2}, {2, 2, 2}, "brown_wool", limb("armL", {{"rest", {0, 0, -1.35}}})),
    part("foreL", "armL", {0, -4, 0}, {-2, -6, -2}, {2, 0, 2}, "hay_side", limb("foreL", {{"hand", {0, -5, 0}}})),
    part("tuftL", "foreL", {0, -6, 0}, {-2.5f, -3.5f, -2.5f}, {2.5f, 0, 2.5f}, "hay_side"),
    part("legR", nullptr, {2, 12, 0}, {-1.5f, -6, -1.5f}, {1.5f, 0, 1.5f}, "spruce_planks", limb("legR")),
    part("shinR", "legR", {0, -6, 0}, {-1.5f, -6, -1.5f}, {1.5f, 0, 1.5f}, "spruce_planks", limb("shinR")),
    part("legL", nullptr, {-2, 12, 0}, {-1.5f, -6, -1.5f}, {1.5f, 0, 1.5f}, "spruce_planks", limb("legL")),
    part("shinL", "legL", {0, -6, 0}, {-1.5f, -6, -1.5f}, {1.5f, 0, 1.5f}, "spruce_planks", limb("shinL"))});
  models["scarecrow"] = sc;

  // ghost: a white sheet (see-through), a black hole of a face with two pale glowing eyes, arms hanging under the cloth. It
  // borrows the wraith's rig ("wraith" + the drift post: it bobs, the hem and sleeves sway)
  json gh = {{"rig", "wraith"}, {"scale", 1}, {"hipY", 10}, {"height", 30}, {"post", "drift"}};
  // a sheet you can barely see through (outer faces only, so the boxes underneath don't show as layers); the tatters thin out
  auto sheetx = [&](float a, json x = json::object()) { x["alpha"] = std::min(0.9f, a + 0.3f); x["glow"] = true; return x; };   // (self-lit: a pale glow in the dark)
  // the classic sheet: a rounded dome of a head flowing into a cloth that widens and thins out towards a ragged hem (fading
  // as it goes), two dark eye holes and a little round mouth, the arms only bumps under the cloth
  auto holes = [](json x = json::object()) { x["alpha"] = 0.92f; x["noCull"] = true; return x; };   // (not lit: dark holes)
  gh["parts"] = json::array({
    part("body", nullptr, {0, 16, 0}, {-5, -5, -5}, {5, 5, 5}, "white_concrete", sheetx(0.6f, {{"role", "wbody"}})),
    part("skirt", "body", {0, -5, 0}, {-5.7f, -5, -5.7f}, {5.7f, 0, 5.7f}, "white_concrete", sheetx(0.52f)),
    part("hem", "skirt", {0, -5, 0}, {-6.3f, -3, -6.3f}, {6.3f, 0, 6.3f}, "white_concrete", sheetx(0.42f, {{"role", "whem"}})),
    part("tailFL", "hem", {0, -3, 0}, {-6.3f, -3, -6.3f}, {-2.8f, 0, -2.8f}, "white_concrete", sheetx(0.32f)),
    part("tailFR", "hem", {0, -3, 0}, {2.8f, -2.2f, -6.3f}, {6.3f, 0, -2.8f}, "white_concrete", sheetx(0.32f)),
    part("tailBL", "hem", {0, -3, 0}, {-6.3f, -2.4f, 2.8f}, {-2.8f, 0, 6.3f}, "white_concrete", sheetx(0.32f)),
    part("tailBR", "hem", {0, -3, 0}, {2.8f, -3.2f, 2.8f}, {6.3f, 0, 6.3f}, "white_concrete", sheetx(0.32f)),
    part("tailM", "hem", {0, -3, 0}, {-1.6f, -1.6f, -6.3f}, {1.6f, 0, 6.3f}, "white_concrete", sheetx(0.3f)),
    part("head", "body", {0, 5, 0}, {-4.7f, 0, -4.7f}, {4.7f, 6, 4.7f}, "white_concrete", sheetx(0.66f, {{"role", "whead"}})),
    part("dome", "head", {0, 6, 0}, {-3.7f, 0, -3.7f}, {3.7f, 1.6f, 3.7f}, "white_concrete", sheetx(0.66f)),
    part("crown", "dome", {0, 1.6f, 0}, {-2.1f, 0, -2.1f}, {2.1f, 1, 2.1f}, "white_concrete", sheetx(0.66f)),
    part("eyeL", "head", {0, 0, 0}, {-3.1f, 2.4f, -4.95f}, {-1.2f, 4.9f, -4.65f}, "black_concrete", holes()),
    part("eyeR", "head", {0, 0, 0}, {1.2f, 2.4f, -4.95f}, {3.1f, 4.9f, -4.65f}, "black_concrete", holes()),
    part("mouth", "head", {0, 0, 0}, {-0.9f, 0.5f, -4.95f}, {0.9f, 1.9f, -4.65f}, "black_concrete", holes()),
    // the arms are only bumps under the cloth
    part("armR", "body", {-3.9f, 3, 0}, {-1.1f, -5, -1.8f}, {1.1f, 0, 1.8f}, "white_concrete", sheetx(0.5f, {{"role", "warm"}, {"rest", {0, 0, -0.12}}})),
    part("armL", "body", {3.9f, 3, 0}, {-1.1f, -5, -1.8f}, {1.1f, 0, 1.8f}, "white_concrete", sheetx(0.5f, {{"role", "warm"}, {"rest", {0, 0, 0.12}}}))});
  models["ghost"] = gh;

  // the stock fuseling model is replaced outright by the creeper build (same key, so everything keyed on "fuseling"
  // — /summon, the entity factory, saves, PMSpec pack reskins — keeps working; only its look changed).
  models["fuseling"] = creeperModel();
}

// ------------------------------------------------------------------ helpers
static float smooth01(float x) { x = clampv(x, 0.f, 1.f); return x * x * (3 - 2 * x); }

// is any player looking at it? cone = the dot of the view direction and the direction to the creature (1 = dead centre)
static bool watchedBy(Level& lv, Creature& m, float minDot, float maxDist) {
  vec3 c = m.pos + vec3(0, m.h * 0.6f, 0);
  for (Entity* e : pawnsOf(lv, m.realm)) {
    auto* pl = dynamic_cast<Player*>(e);
    if (!pl || pl->dead) continue;
    vec3 to = c - (pl->pos + vec3(0, 1.62f, 0));
    float d = glm::length(to);
    if (d > maxDist || d < 0.01f) continue;
    if (glm::dot(to / d, lookVector(pl->yaw, pl->pitch)) < minDot) continue;
    if (d > 2.5f && !m.lineOfSight(lv, *pl)) continue;
    return true;
  }
  return false;
}

// nobody (not even someone it can't target, a creative player) within r: nothing to hide from or haunt, so it goes
static bool nobodyNear(Level& lv, const Creature& m, float r) { return !nearestPawn(lv, m.realm, m.pos, r); }

// ------------------------------------------------------------------ the pumpkin creature
static BlockId pumpkinBlock() { static BlockId b = reg().blockId("pumpkin"); return b; }

static void wakePumpkin(Level& lv, Creature& m, Entity* cause) {
  hwSet(m, "mode", 1); hwSet(m, "up", 1); hwSet(m, "tell", 0);
  m.noGravity = false; m.hasFlyVy = false;
  if (cause) { m.foe = cause->id; hwSet(m, "free", (float)cause->id); }
  lv.fx->voice("hw_gourd", "angry", m.pos + vec3(0, 0.6f, 0));
}

// room to sit as a pumpkin: air or a small plant, on solid ground that is not another pumpkin (so they never stack up)
static bool roomForPumpkin(World& w, int x, int y, int z) {
  BlockId at = w.getBlock(x, y, z), under = w.getBlock(x, y - 1, z);
  if (at && (reg().tSolid[at] || reg().tLiquid[at])) return false;
  return reg().tSolid[under] && under != pumpkinBlock() && under != reg().blockId("jack_o_lantern") && under != reg().blockId("carved_pumpkin");
}

static OptMove pumpkinBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  World& w = lv.world(m.realm);
  const bool night = lv.isNight(), hw = halloween();
  int bx = ifloor(m.pos.x), by = ifloor(m.pos.y + 0.1f), bz = ifloor(m.pos.z);
  if (!w.isLoaded(bx, clampv(by, 0, WORLD_H - 1), bz)) return Move{0, m.yaw};
  if (!hwHas(m, "mode")) {   // first tick (a summoned one too): hide as a pumpkin where there is room, else it is just up
    if (p) m.yaw = m.prevYaw = std::round(toPlayer / (PI / 2)) * (PI / 2);   // (the face squarely on a block side, turned to whoever is near)
    if (w.getBlock(bx, by, bz) == pumpkinBlock()) hwSet(m, "mode", 0);
    else if (roomForPumpkin(w, bx, by, bz) && w.setBlock(bx, by, bz, pumpkinBlock(), 0)) { hwSet(m, "mode", 0); m.pos = vec3(bx + 0.5f, (float)by, bz + 0.5f); }
    else { m.removed = true; return Move{0, m.yaw}; }   // nowhere to hide: it never starts out up and hunting
  }
  int mode = (int)(hwGet(m, "mode") + 0.5f);
  if (mode != 0) m.data.erase("inBlock");
  float wk = hwGet(m, "w");
  if (mode == 0) {   // a pumpkin block; only the face gives it away
    m.noGravity = true; m.hasFlyVy = true; m.flyVy = 0; m.vel = vec3(0); m.h = 1.0f; m.w = 0.45f; m.data["inBlock"] = true;
    if (!hw || !night) { m.removed = true; return Move{0, m.yaw}; }   // by day it is only a pumpkin (the block stays)
    hwSet(m, "tell", 1); hwSet(m, "up", 0);
    if (nobodyNear(lv, m, 90)) { m.removed = true; return Move{0, m.yaw}; }
    if (w.getBlock(bx, by, bz) != pumpkinBlock()) { wakePumpkin(lv, m, p && dist < 8 ? p : nullptr); return Move{0, m.yaw}; }   // mined away (or blown up)
    if (p) {
      vec3 c(bx + 0.5f, by + 0.5f, bz + 0.5f);
      if (glm::distance(p->pos + vec3(0, 0.9f, 0), c) < (pawnSneaking(p) ? 2.6f : 4.f)) wakePumpkin(lv, m, nullptr);
    }
    return Move{0, m.yaw};
  }
  if (mode == 1) {   // tearing itself out of the block
    if (w.getBlock(bx, by, bz) == pumpkinBlock()) w.setBlock(bx, by, bz, AIR, 0);
    wk = std::min(1.f, wk + dt / 0.95f);
    hwSet(m, "w", wk); m.h = 1.0f + 1.2f * smooth01(wk);
    if (u32 id = (u32)hwGet(m, "free")) {   // mined or hit while it hid: a free strike
      hwSet(m, "free", 0);
      if (Entity* v = m.ent(lv, id)) if (glm::distance(v->pos, m.pos) < 6.f) { damageEntity(lv, *v, m.T->damage, &m, "mob"); m.swingT = 0.3f; m.attackCd = 1.2f; }
    }
    if (wk >= 1) hwSet(m, "mode", 2);
    return Move{0, p ? toPlayer : m.yaw};
  }
  if (mode == 3) {   // sinking back into a pumpkin
    wk = std::max(0.f, wk - dt / 1.1f);
    hwSet(m, "w", wk); m.h = 1.0f + 1.2f * smooth01(wk);
    if (wk <= 0) {
      hwSet(m, "w", 0);
      if (hw && night && roomForPumpkin(w, bx, by, bz) && w.setBlock(bx, by, bz, pumpkinBlock(), 0)) {
        m.pos = vec3(bx + 0.5f, (float)by, bz + 0.5f); m.vel = vec3(0); hwSet(m, "mode", 0); hwSet(m, "up", 0);
      } else { mobBurst(lv, m.pos + vec3(0, 0.8f, 0), "pumpkin_side", 10, 0.6f); m.removed = true; }
    }
    return Move{0, m.yaw};
  }
  // up and about
  m.h = 2.2f; m.w = 0.5f;
  float idle = hwGet(m, "idle");
  const bool near = p && dist < 24;
  idle = near ? 0.f : idle + dt;
  hwSet(m, "idle", idle);
  if (nobodyNear(lv, m, 90)) { m.removed = true; return Move{0, m.yaw}; }
  if (idle > 8 || ((!night || !hw) && !(p && dist < 6))) { hwSet(m, "mode", 3); return Move{0, m.yaw}; }
  if (!near) { m.wander(dt); Move mv = m.wanderMove(); return Move{mv.want * 0.6f, mv.dir}; }
  std::optional<float> nd; if (dist > 2.2f) nd = m.navDir(lv, p->pos, dt);
  float dir = nd ? *nd : toPlayer, want = m.T->speed;
  if (dist < 2.0f && std::fabs(dyp) < 2.4f && m.attackCd <= 0) { m.attackCd = 1.3f; m.swingT = 0.3f; damageEntity(lv, *p, m.T->damage, &m, "mob"); }
  if (dist < 1.4f) want = 0;
  m.soundT -= dt;
  if (m.soundT < 0) { m.soundT = 4 + frand() * 5; if (dist < 18) lv.fx->voice("hw_gourd", "say", m.pos + vec3(0, 1.2f, 0)); }
  return Move{want, dir};
}
static void pumpkinHurt(Level& lv, Creature& m, float dealt, const DamageSrc& src) {
  if (!m.alive() || dealt <= 0) return;
  int mode = (int)(hwGet(m, "mode") + 0.5f);
  if (mode == 0) wakePumpkin(lv, m, m.ent(lv, src.attacker));
  else if (mode == 3) hwSet(m, "mode", 1);
}

// the unfolding: the legs start folded up under the pumpkin, swing out, then push the body up (lift = how far the feet reach down)
static void animPumpkin(Creature& m, const RigModel& M, RigAnimOut& R) {
  auto set = [&](const char* n, float x, float y, float z) { int i = M.find(n); if (i >= 0) { R.has[i] = 1; R.rot[i] = vec3(x, y, z); } };
  float wk = hwGet(m, "w"), k = smooth01(wk), t = m.age + m.id * 0.31f;
  float phi = 1.9f + (0.35f - 1.9f) * k;
  float ext = -1 + 10 * std::cos(phi) + 9 * std::cos(std::max(0.f, phi - 0.7f));
  float amp = wk >= 0.99f ? m.walkAmt : 0.f, ph = m.walk;
  R.lift = std::max(0.f, ext) / 16.f + std::fabs(std::sin(ph * 2)) * 0.035f * amp;
  struct Leg { const char* leg; const char* shin; float sx, sz, phase; };
  static const Leg LEGS[4] = {{"legFR", "shinFR", 1, -1, 0}, {"legBR", "shinBR", 1, 1, PI}, {"legFL", "shinFL", -1, -1, PI}, {"legBL", "shinBL", -1, 1, 0}};
  for (const Leg& l : LEGS) {
    float swing = std::sin(ph + l.phase) * 0.55f * amp;
    set(l.leg, -l.sz * 0.25f + swing, 0, l.sx * phi);
    set(l.shin, 0, 0, -l.sx * 0.7f - std::max(0.f, std::cos(ph + l.phase)) * 0.3f * amp * l.sx);
  }
  float sw = m.swingT > 0 ? std::sin(m.swingT / 0.3f * PI) : 0;
  float sway = std::sin(t * 1.7f) * 0.08f, arms = 2.6f + (0.45f - 2.6f) * k;
  set("armR", 1.9f * sw, 0, arms + sway); set("armL", 1.9f * sw, 0, -arms - sway);
  set("stem", std::sin(t * 2.1f) * 0.12f, 0, std::sin(t * 1.3f) * 0.12f);
  set("twistA", 0, std::sin(t * 1.2f) * 0.15f, 0);
  R.pitch = -0.06f * amp;
}

// ------------------------------------------------------------------ the scarecrow
static OptMove scarecrowBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  if (!halloween()) { m.removed = true; return Move{0, m.yaw}; }
  if (!m.kept && nobodyNear(lv, m, 90)) { m.removed = true; return Move{0, m.yaw}; }
  if (!lv.isNight() || !p || dist > 40) return Move{0, m.yaw};   // by day: a decoration
  if (watchedBy(lv, m, 0.5f, 70)) { m.vel.x = m.vel.z = 0; m.walkAmt = 0; return Move{0, m.yaw}; }   // seen: frozen mid-stride
  std::optional<float> nd; if (dist > 2.2f) nd = m.navDir(lv, p->pos, dt);
  float dir = nd ? *nd : toPlayer, want = m.T->speed;
  if (dist < 1.4f + m.w && std::fabs(dyp) < 1.8f && m.attackCd <= 0) { m.attackCd = 1.2f; m.swingT = 0.3f; damageEntity(lv, *p, m.T->damage, &m, "mob"); }
  if (dist < 1.0f) want = 0;
  if (want > 0 || m.speedNow > 0.6f) {   // straw on the wind
    float acc = hwGet(m, "straw") + dt;
    if (acc > 0.14f) {
      acc = 0;
      mobBurst(lv, m.pos + vec3((frand() - 0.5f) * 0.7f, 0.5f + frand() * 1.1f, (frand() - 0.5f) * 0.7f), "hay_side", 1, 0.25f);
    }
    hwSet(m, "straw", acc);
    m.soundT -= dt;
    if (m.soundT < 0) { m.soundT = 0.9f + frand() * 1.4f; if (dist < 22) lv.fx->voice("hw_straw", frand() < 0.25f ? "angry" : "say", m.pos + vec3(0, 1, 0)); }
  }
  return Move{want, dir};
}

// ------------------------------------------------------------------ the ghost
static OptMove ghostBrain(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  World& w = lv.world(m.realm);
  m.noGravity = true; m.hasFlyVy = true;
  const bool night = lv.isNight();
  if (!halloween() || nobodyNear(lv, m, 90)) { m.removed = true; return Move{0, m.yaw}; }
  float fa = hwGet(m, "fa", 1), fadeT = hwGet(m, "fadeT"), lookT = hwGet(m, "lookT");
  // looked at straight for a moment, it fades back for a while; at dawn it thins away for good
  if (fadeT > 0) fadeT -= dt;
  if (fadeT <= 0 && night && watchedBy(lv, m, 0.94f, 22)) { lookT += dt; if (lookT > 1.0f) { lookT = 0; fadeT = 7; lv.fx->voice("hw_ghost", "hurt", m.pos + vec3(0, 1, 0)); } }
  else lookT = std::max(0.f, lookT - dt * 2);
  float faT = !night ? 0.f : fadeT > 0 ? 0.12f : 1.f;
  fa += (faT - fa) * std::min(1.f, dt * 1.5f);
  hwSet(m, "fa", std::round(fa * 100) / 100); hwSet(m, "fadeT", fadeT); hwSet(m, "lookT", lookT);
  if (!night && fa < 0.06f) { mobBurst(lv, m.pos + vec3(0, 1, 0), "white", 10, 0.5f); m.removed = true; return Move{0, m.yaw}; }
  // altitude: a person's height near you, otherwise a couple of blocks over the ground
  int gx = ifloor(m.pos.x), gz = ifloor(m.pos.z), gy = ifloor(m.pos.y);
  float ground = m.pos.y - 3;
  for (int i = 0; i < 14 && gy > 0; i++, gy--) if (reg().tSolid[w.getBlock(gx, gy, gz)] && !reg().tLeaves[w.getBlock(gx, gy, gz)]) { ground = gy + 1.f; break; }
  const bool close = p && dist < 10;
  float alt = close ? p->pos.y + 0.6f + std::sin(m.age * 0.8f) * 0.4f : ground + 1.8f + std::sin(m.age * 0.7f + m.id) * 0.6f;
  m.flyVy = clampv((alt - m.pos.y) * 1.5f, -1.6f, 1.6f);
  // where to: drift toward you when you are near and it is not faded, else wander; faded, it backs away
  float dir = m.yaw, want = 0;
  if (fadeT > 0 && p) { dir = toPlayer + PI; want = 1.6f; }
  else if (p && dist < 40 && night) { dir = toPlayer + std::sin(m.age * 0.6f + m.id) * 0.5f; want = dist < 2.0f ? 0.3f : m.T->speed; }
  else { m.wander(dt); Move mv = m.wanderMove(); dir = mv.dir; want = mv.want * 0.5f; }
  // light keeps it away: it turns to the darkest way and won't go on toward torches
  auto blockLight = [&](float x, float y, float z) { return (int)(w.getLight(ifloor(x), ifloor(y), ifloor(z)) & 15); };
  float fx = -std::sin(dir), fz = -std::cos(dir);
  int here = blockLight(m.pos.x, m.pos.y + 1, m.pos.z), ahead = blockLight(m.pos.x + fx * 3, m.pos.y + 1, m.pos.z + fz * 3);
  if (here >= 7 || ahead >= 7) {
    float best = dir; int bl = 99;
    for (int k = 0; k < 8; k++) {
      float a = dir + k * (TAU / 8), l = (float)blockLight(m.pos.x - std::sin(a) * 3, m.pos.y + 1, m.pos.z - std::cos(a) * 3);
      if (l < bl) { bl = (int)l; best = a; }
    }
    dir = best; want = std::max(want, 0.9f) * (here >= 10 ? 1.5f : 1.f);
  }
  // touch: a chill (short slowness) and a point of damage
  if (p && fadeT <= 0 && fa > 0.6f && m.attackCd <= 0 && night) {
    vec3 a = m.pos + vec3(0, 0.9f, 0), b = p->pos + vec3(0, 0.9f, 0);
    if (glm::distance(a, b) < 1.25f) {
      m.attackCd = 3.0f; m.swingT = 0.3f;
      if (auto* pl = dynamic_cast<Player*>(p)) giveEffect(lv, *pl, "slow", 4.f, 0, &m);
      damageEntity(lv, *p, m.T->damage, &m, "mob");
      mobBurst(lv, b, "white", 8, 0.5f);
      lv.fx->voice("hw_ghost", "hurt", m.pos + vec3(0, 1, 0));
    }
  }
  m.soundT -= dt;
  if (m.soundT < 0) { m.soundT = 6 + frand() * 8; if (dist < 36) lv.fx->voice("hw_ghost", "say", m.pos + vec3(0, 1.2f, 0)); }
  return Move{want, dir};
}

// ------------------------------------------------------------------ October nights: where they turn up
namespace {
class HallowSystem : public LevelSystem {
public:
  const char* name() const override { return "hallowmobs"; }
  double acc = 0; int evBreak = 0; EventBus* bus = nullptr;
  explicit HallowSystem(Level& lv) {
    bus = lv.events;
    Level* L = &lv;
    // mining the pumpkin it hides in wakes it (with a free strike at whoever swung)
    if (bus) evBreak = bus->on("blockBreak", [L](const json& a) {
      if (!halloween() || !a.is_array() || a.size() < 5 || !a[4].is_string() || a[4].get<std::string>() != "pumpkin") return true;
      int x = a[1].get<int>(), y = a[2].get<int>(), z = a[3].get<int>();
      Entity* who = nullptr;
      const std::string pname = a[0].is_string() ? a[0].get<std::string>() : std::string();
      for (auto& e : L->entities()) if (auto* pl = dynamic_cast<Player*>(e.get())) if (!pl->removed && pl->name == pname) who = pl;
      for (auto& e : L->entities()) {
        auto* c = dynamic_cast<Creature*>(e.get());
        if (!c || c->removed || c->type != "pumpkin_creature" || (int)(hwGet(*c, "mode") + 0.5f) != 0) continue;
        if (std::fabs(c->pos.x - (x + 0.5f)) < 0.6f && std::fabs(c->pos.z - (z + 0.5f)) < 0.6f && std::fabs(c->pos.y - y) < 1.2f) wakePumpkin(*L, *c, who);
      }
      return true;
    }, -90);
  }
  ~HallowSystem() override { if (bus) bus->off(evBreak); }

  struct Spot { int x, y, z; };
  void tick(Level& lv, double dt) override {
    if (!halloween() || lv.isRemote()) return;
    acc += dt;
    if (acc < 1.5) return;
    acc = 0;
    if (!lv.isNight() || !lv.info.rules.value("mobSpawning", true) || lv.info.difficulty == "peaceful" || lv.info.difficulty == "peaceful_all") return;
    std::vector<int> realms;
    for (auto& e : lv.entities()) if (isPawn(e.get()) && std::find(realms.begin(), realms.end(), e->realm) == realms.end()) realms.push_back(e->realm);
    for (int realm : realms) spawnFor(lv, realm);
  }

  void spawnFor(Level& lv, int realm) {
    std::vector<Entity*> ps = pawnsOf(lv, realm);
    if (ps.empty()) return;
    Entity* p = ps[(size_t)(frand() * ps.size()) % ps.size()];
    int dim = dimAt(*p);
    if (dim != DIM_OVERWORLD) return;
    World& w = lv.world(realm);
    int n = (int)ps.size(), pumpkins = 0, crows = 0, ghosts = 0;
    std::vector<vec3> taken;
    for (auto& e : lv.entities()) {
      auto* c = dynamic_cast<Creature*>(e.get());
      if (!c || c->removed || c->realm != realm) continue;
      if (std::hypot(c->pos.x - p->pos.x, c->pos.z - p->pos.z) > 80) continue;
      if (c->type == "pumpkin_creature") { pumpkins++; taken.push_back(c->pos); }
      else if (c->type == "scarecrow") { crows++; taken.push_back(c->pos); }
      else if (c->type == "ghost") ghosts++;
    }
    // fewer than before: a couple of pumpkins about, a scarecrow now and then, and one ghost a night
    static i64 ghostNight = INT64_MIN;
    const i64 nightNo = (i64)std::floor(lv.info.time / DAY_LEN + (double)lv.info.day + 0.5);
    const bool wantP = pumpkins < 1 + n / 2 && frand() < 0.15f, wantS = crows < 1 && frand() < 0.015f, wantG = ghosts < 1 && ghostNight != nightNo && frand() < 0.03f;   // scarecrows and ghosts are rare: one at a time round you
    if (wantP || wantS) {
      // one pass over the loaded columns around the player: pumpkins on the surface, farmland / crops / hay bales
      static BlockId farmland = reg().blockId("farmland"), hay = reg().blockId("hay_bale");
      std::vector<Spot> pk, fm; int seenP = 0, seenF = 0;
      int base = dimBase(dim), pcx = ifloor(p->pos.x) >> 4, pcz = ifloor(p->pos.z) >> 4;
      for (int cz = -3; cz <= 3; cz++) for (int cx = -3; cx <= 3; cx++) {
        Chunk* c = w.chunk(dim, pcx + cx, pcz + cz);
        if (!c || !c->lightReady) continue;
        for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
          int x = ((pcx + cx) << 4) + lx, z = ((pcz + cz) << 4) + lz, y = base + c->heightMap[lz * 16 + lx];
          float d = std::hypot(x + 0.5f - p->pos.x, z + 0.5f - p->pos.z);
          if (d < 8 || d > 46) continue;
          BlockId b = w.getBlock(x, y, z);
          if (!b) continue;
          if (b == pumpkinBlock()) { if (wantP && (int)pk.size() < 6) pk.push_back({x, y, z}); else if (wantP && frand() * (++seenP) < 6) pk[(size_t)(frand() * pk.size())] = {x, y, z}; }
          else if (wantS && d > 14) {
            int sy = -1;
            if (b == farmland || b == hay) sy = y + 1;
            else if (w.getBlock(x, y - 1, z) == farmland) sy = y;
            if (sy > 0) { if ((int)fm.size() < 6) fm.push_back({x, sy, z}); else if (frand() * (++seenF) < 6) fm[(size_t)(frand() * fm.size())] = {x, sy, z}; }
          }
        }
      }
      auto spaced = [&](const Spot& s, float apart) { for (auto& t : taken) if (std::hypot(t.x - (s.x + 0.5f), t.z - (s.z + 0.5f)) < apart) return false; return true; };
      if (wantP && !pk.empty()) {
        Spot s = pk[(size_t)(frand() * pk.size()) % pk.size()];
        if (spaced(s, 1.2f)) if (Creature* c = spawnCreature(lv, realm, "pumpkin_creature", vec3(s.x + 0.5f, (float)s.y, s.z + 0.5f))) {
          c->kept = false; c->yaw = c->prevYaw = (float)((int)(frand() * 4)) * (PI / 2);
          hwSet(*c, "mode", 0); hwSet(*c, "up", 0); c->noGravity = true; c->hasFlyVy = true;
        }
      }
      if (wantS && !fm.empty()) {
        Spot s = fm[(size_t)(frand() * fm.size()) % fm.size()];
        const Registry& R = reg();
        if (spaced(s, 8.f) && !R.tSolid[w.getBlock(s.x, s.y, s.z)] && !R.tSolid[w.getBlock(s.x, s.y + 1, s.z)])
          if (Creature* c = spawnCreature(lv, realm, "scarecrow", vec3(s.x + 0.5f, (float)s.y, s.z + 0.5f))) { c->kept = false; c->yaw = c->prevYaw = frand() * TAU; }
      }
    }
    if (wantG) {
      float ang = frand() * TAU, d = 14 + frand() * 20;
      int x = ifloor(p->pos.x + std::cos(ang) * d), z = ifloor(p->pos.z + std::sin(ang) * d);
      int top = w.surfaceY(x, z, dim);
      if (top > 0 && w.isLoaded(x, top, z) && (w.getLight(x, top + 1, z) & 15) < 5)
        if (Creature* c = spawnCreature(lv, realm, "ghost", vec3(x + 0.5f, top + 1.6f + frand() * 1.5f, z + 0.5f))) { c->kept = false; hwSet(*c, "fa", 0.05f); ghostNight = nightNo; }
    }
  }
};
} // namespace

void registerHalloweenMobs() {
  static bool done = false;
  if (done) return;
  done = true;
  injectTypes();
  reloadCreatureTypes();
  registerRigAnim("hw_pumpkin", animPumpkin);
  { CreatureHooks h; h.brain = pumpkinBrain; h.afterHurt = pumpkinHurt; setCreatureHooks("pumpkin_creature", h); }
  { CreatureHooks h; h.brain = scarecrowBrain; setCreatureHooks("scarecrow", h); }
  { CreatureHooks h; h.brain = ghostBrain; setCreatureHooks("ghost", h); }
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return std::make_shared<HallowSystem>(lv); });
}

} // namespace dq
