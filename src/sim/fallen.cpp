// sim/fallen.cpp — see fallen.h
#include "sim/fallen.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/creature.h"
#include "core/registry.h"
#include "core/tool_parts.h"
#include "sim/creature_rig.h"
#include "world/shapes.h"
#include "world/dims.h"
#include <cmath>
#include <map>
#include <mutex>
#include <unordered_map>

namespace dq {

namespace {
struct Loot { const char* id; int lo, hi; float chance; };
// what each kind of adventurer carried (the ids not in this build are skipped)
const std::vector<Loot>& lootFor(const std::string& kind) {
  static const std::vector<Loot> traveller = {{"bread", 1, 3, 0.7f}, {"apple", 1, 2, 0.45f}, {"torch", 2, 5, 0.55f}, {"string", 1, 3, 0.35f}, {"arrow", 2, 6, 0.35f},
                                              {"gold_coin", 1, 3, 0.35f}, {"leather", 1, 2, 0.25f}, {"stick", 1, 4, 0.3f}};
  static const std::vector<Loot> desert = {{"gold_nugget", 2, 6, 0.45f}, {"gold_coin", 1, 4, 0.4f}, {"bone", 2, 5, 0.6f}, {"string", 1, 3, 0.4f},
                                           {"melon_slice", 1, 2, 0.3f}, {"gunpowder", 1, 2, 0.22f}, {"bread", 1, 2, 0.3f}};
  static const std::vector<Loot> snowy = {{"cooked_beef", 1, 2, 0.5f}, {"leather", 1, 3, 0.45f}, {"torch", 2, 5, 0.55f}, {"snowball", 2, 6, 0.4f},
                                          {"coal", 1, 3, 0.3f}, {"packed_ice", 1, 3, 0.25f}, {"gold_coin", 1, 3, 0.3f}};
  static const std::vector<Loot> jungle = {{"cocoa_beans", 1, 3, 0.55f}, {"glow_berries", 1, 3, 0.4f},
                                           {"arrow", 2, 6, 0.4f}, {"gold_coin", 1, 3, 0.3f}, {"vine", 1, 3, 0.25f}, {"bread", 1, 2, 0.3f}};
  static const std::vector<Loot> swamp = {{"bone", 2, 5, 0.65f}, {"rotten_flesh", 1, 3, 0.45f}, {"string", 1, 3, 0.4f},
                                          {"gold_coin", 1, 3, 0.3f}, {"candle", 1, 2, 0.25f}, {"lily_pad", 1, 2, 0.2f}};
  static const std::vector<Loot> miner = {{"coal", 2, 6, 0.7f}, {"torch", 3, 8, 0.65f}, {"iron_ingot", 1, 2, 0.35f},
                                          {"bread", 1, 2, 0.4f}, {"string", 1, 3, 0.25f}, {"cobblestone", 2, 6, 0.35f}};
  if (kind == "desert") return desert;
  if (kind == "snowy") return snowy;
  if (kind == "jungle") return jungle;
  if (kind == "swamp") return swamp;
  if (kind == "miner") return miner;
  return traveller;
}
// their worn-out gear: a tool or weapon now and then, well used
const char* gearFor(const std::string& kind) {
  return kind == "miner" ? "copper_pickaxe" : kind == "desert" ? "copper_sword" : kind == "snowy" ? "copper_axe" : kind == "jungle" ? "bow" : kind == "swamp" ? "stone_pickaxe" : "copper_sword";
}
u32 mix(u32 h) { h ^= h >> 16; h *= 0x7feb352d; h ^= h >> 15; h *= 0x846ca68b; h ^= h >> 16; return h; }
}  // namespace

void fallenFill(World& w, int x, int y, int z, json& be) {
  const std::string kind = reg().block(w.getBlock(x, y, z)).str("remains");
  u32 h = mix((u32)x * 73856093u ^ (u32)y * 19349663u ^ (u32)z * 83492791u ^ 0xFA11E7u);
  auto rnd = [&] { h = mix(h + 0x9e3779b9u); return (h & 0xFFFFFF) / 16777216.f; };
  json items = json::array();
  for (const Loot& l : lootFor(kind)) {
    if ((int)items.size() >= 9 || !reg().item(l.id) || rnd() > l.chance) continue;
    const int n = l.lo + (int)(rnd() * (l.hi - l.lo + 1));
    items.push_back(json{{"id", l.id}, {"n", std::min(n, l.hi)}});
  }
  if (rnd() < 0.3f && (int)items.size() < 9) if (const ItemDef* gd = reg().item(gearFor(kind))) {   // their gear, worn
    ItemStack gs = proceduralOf(gd->key, 1);
    json g = gs.toJson();
    const int maxd = toolMaxDurability(gs);
    if (maxd > 0) g["d"] = (int)(maxd * (0.55f + 0.35f * rnd()));
    else if (gd->hasTool && gd->tool.durability > 0) g["d"] = (int)(gd->tool.durability * (0.55f + 0.35f * rnd()));
    items.push_back(g);
  }
  while ((int)items.size() < 9) items.push_back(nullptr);
  for (int i = 8; i > 0; i--) std::swap(items[i], items[(int)(rnd() * (i + 1))]);   // (scattered through the slots)
  be["items"] = items;
  if (rnd() < 0.125f) be["trap"] = kind == "desert" ? "husk" : kind == "snowy" ? "stray" : kind == "miner" ? "skeleton" : "zombie";
}

void fallenOpened(Level& lv, Player& p, World& w, int x, int y, int z) {
  if (lv.isRemote()) return;   // (the host's game raises them)
  json* e = w.blockEntity(x, y, z);
  if (!e || !e->is_object() || !e->contains("trap") || !(*e)["trap"].is_string()) return;
  std::string mob = (*e)["trap"];
  e->erase("trap"); w.blockEntityChanged(x, y, z);
  if (!reg().mobs.contains(mob)) mob = reg().mobs.contains("skeleton") ? "skeleton" : "zombie";
  for (int k = 0; k < 2; k++) {   // they rise beside the bones
    const float a = (k + 0.25f) * 3.14159f;
    vec3 at(x + 0.5f + std::cos(a) * 1.6f, (float)y, z + 0.5f + std::sin(a) * 1.6f);
    for (int dy = 2; dy >= -2; dy--) if (!reg().tSolid[w.getBlock((int)std::floor(at.x), y + dy, (int)std::floor(at.z))] && reg().tSolid[w.getBlock((int)std::floor(at.x), y + dy - 1, (int)std::floor(at.z))]) { at.y = (float)(y + dy); break; }
    spawnCreature(lv, p.realm, mob, at);
  }
  toast(lv, p, "It wasn't alone...", "warn");
  sfxAt(lv, "ghast_moan", vec3(x, y, z) + vec3(0.5f));
}

// ---------------------------------------------------------------- how they lie (render/fallen_layer.cpp draws them)
const FallenKind* fallenKind(const std::string& k) {
  static const std::map<std::string, FallenKind> K = {
    {"traveller", {"skeleton_ranger", FALLEN_SIT, "", ""}},
    {"desert", {"skeleton", FALLEN_LIE, "", ""}},
    {"snowy", {"skeleton", FALLEN_SIT, "", ""}},
    {"jungle", {"jungle_skeleton", FALLEN_LIE, "", ""}},
    {"swamp", {"skeleton_mossy", FALLEN_LIE, "", ""}},
    {"miner", {"skeleton_miner", FALLEN_KNEEL, "", "stone_pickaxe"}},
  };
  auto it = K.find(k);
  return it == K.end() ? nullptr : &it->second;
}
FallenVariant fallenVariant(ivec3 at, const FallenKind& K) {
  const u32 h = (u32)(posHashF(at.x, at.y, at.z, 0xFA11) * 4294967295.0);
  FallenVariant v; v.seed = h;
  const int r = (int)(h % 100);
  v.pose = r < 22 ? K.pose : (r - 22) * (FALLEN_POSES - 1) / 78; if (v.pose >= K.pose && r >= 22) v.pose++;   // (the others evenly)
  v.pose = std::min(v.pose, FALLEN_POSES - 1);
  v.jitter = (int)((h >> 8) % 4);
  const int a = (int)((h >> 12) % 100);
  v.arrows = a < 55 ? 0 : a < 75 ? 1 : a < 88 ? 2 : a < 96 ? 3 : 4;
  return v;
}
const FallenPose* fallenPose(const std::string& kind, int dim, int pose, int jitter) {
  static std::mutex mx;
  static std::unordered_map<std::string, std::unique_ptr<FallenPose>> cache;
  std::lock_guard<std::mutex> lk(mx);   // (the server's and the client's threads both ask: posed once, read after)
  const std::string ck = kind + "#" + std::to_string(dim) + "#" + std::to_string(pose) + "#" + std::to_string(jitter);
  auto it = cache.find(ck);
  if (it != cache.end()) return it->second.get();
  auto s = std::make_unique<FallenPose>();
  s->K = fallenKind(kind);
  if (s->K && reg().mobs.contains("skeleton")) {
    s->c = std::make_unique<Creature>("skeleton");
    // the realm's own dead: scorched bones in the Nether
    const char* model = dim == DIM_NETHER ? "scorched_skeleton_chained" : s->K->model;
    if (!rigModel(model)) model = s->K->model;
    if (rigModel(model)) s->c->variant = model;
    s->RM = rigModel(s->c->modelKey());
  }
  if (!s->RM) s->c.reset();
  else {
    // the pose: part turns (x forward/back, y, z sideways) on the biped's rest; +x swings a leg or arm forward (-z)
    const RigModel& M = *s->RM;
    std::vector<vec3> rot(M.parts.size(), vec3(0));
    s->hide.assign(M.parts.size(), 0);
    auto set = [&](const char* n, vec3 r) { if (int i = M.find(n); i >= 0) rot[i] = r; };
    auto hideP = [&](const char* n) { if (int i = M.find(n); i >= 0) s->hide[i] = 1; };
    mat4 root(1);
    int headLoose = -1; mat4 headAt(1);   // (the skull rolled off on its own: where it lies)
    auto onBack = [&](float dz) {          // lying on its back along +z (the feet that way)
      root = glm::translate(root, vec3(0, 0.07f, dz)); root = glm::rotate(root, PI / 2, vec3(1, 0, 0)); root = glm::translate(root, vec3(0, -0.75f, 0));
    };
    switch (pose) {
      case FALLEN_SIT:            // sat down for the last time: legs out in front, leaning back, head on its chest
        root = glm::translate(root, vec3(0, -0.69f, 0));
        set("legR", {1.45f, 0.12f, 0.1f}); set("legL", {1.3f, -0.15f, -0.08f}); set("shinL", {-0.35f, 0, 0});
        set("body", {0.32f, 0, 0.05f}); set("head", {-0.75f, 0.25f, 0.3f});
        set("armR", {0.25f, 0, 0.18f}); set("foreR", {-0.3f, 0, 0}); set("armL", {0.15f, 0, -0.12f}); set("foreL", {-0.2f, 0, 0});
        break;
      case FALLEN_LIE:            // on its back, arms flung out, the skull turned aside
        onBack(-0.35f);
        set("legR", {0, 0, 0.12f}); set("legL", {0.45f, 0, -0.1f}); set("shinL", {-0.9f, 0, 0});
        set("head", {0.1f, 0.7f, 0});
        set("armR", {0.2f, 0, 1.1f}); set("foreR", {-0.6f, 0, 0}); set("armL", {-0.1f, 0, -0.5f});
        break;
      case FALLEN_KNEEL:          // knelt over its pick, slumped forward
        root = glm::translate(root, vec3(0, -0.33f, 0.1f));
        set("legR", {0.08f, 0, 0.06f}); set("shinR", {-1.55f, 0, 0}); set("legL", {0.05f, 0, -0.06f}); set("shinL", {-1.5f, 0, 0});
        set("body", {-0.45f, 0, 0}); set("head", {-0.6f, 0.2f, 0.15f});
        set("armR", {0.9f, 0, 0.05f}); set("foreR", {0.25f, 0, 0}); set("armL", {0.6f, 0, -0.1f}); set("foreL", {0.3f, 0, 0});
        break;
      case FALLEN_FACEDOWN:       // fallen on its face crawling for the door: one arm reaching on ahead, a knee drawn up
        root = glm::translate(root, vec3(0, 0.1f, 0.35f)); root = glm::rotate(root, -PI / 2, vec3(1, 0, 0)); root = glm::translate(root, vec3(0, -0.75f, 0));
        set("armR", {-2.9f, 0, 0.1f}); set("foreR", {-0.2f, 0, 0}); set("armL", {0.2f, 0, -0.35f}); set("foreL", {-1.1f, 0, 0});
        set("legL", {0.9f, 0, -0.15f}); set("shinL", {-1.4f, 0, 0}); set("legR", {0, 0, 0.08f});
        set("head", {0.35f, -1.0f, 0});
        break;
      case FALLEN_CURLED:         // curled on its side, knees up to its chest
        root = glm::translate(root, vec3(-0.1f, 0.24f, 0)); root = glm::rotate(root, PI / 2, vec3(0, 0, 1)); root = glm::translate(root, vec3(0, -0.75f, 0));
        set("legR", {1.3f, 0, 0}); set("shinR", {-1.6f, 0, 0}); set("legL", {1.1f, 0, 0}); set("shinL", {-1.5f, 0, 0});
        set("body", {0.35f, 0, 0}); set("head", {0.5f, 0, 0.2f});
        set("armR", {1.2f, 0, 0.1f}); set("foreR", {1.1f, 0, 0}); set("armL", {0.9f, 0, -0.1f}); set("foreL", {1.3f, 0, 0});
        break;
      case FALLEN_FOLDED:         // sat and folded over its knees, the skull down between them
        root = glm::translate(root, vec3(0, -0.69f, 0.05f));
        set("legR", {1.5f, 0.05f, 0.08f}); set("legL", {1.5f, -0.05f, -0.08f});
        set("body", {-0.95f, 0, 0}); set("head", {-0.5f, 0, 0});
        set("armR", {1.4f, 0, 0.1f}); set("foreR", {0.2f, 0, 0}); set("armL", {1.5f, 0, -0.1f}); set("foreL", {0.1f, 0, 0});
        break;
      case FALLEN_SPRAWL:         // flat on its back, every limb thrown out
        onBack(-0.35f);
        set("legR", {0, 0, 0.55f}); set("legL", {0, 0, -0.5f});
        set("armR", {0, 0, 1.5f}); set("armL", {0, 0, -1.45f}); set("foreL", {-0.3f, 0, 0});
        set("head", {-0.2f, -0.5f, 0});
        break;
      case FALLEN_REACH:          // on its back, one hand still raised to the roof
        onBack(-0.35f);
        set("armR", {-1.55f, 0, 0.1f}); set("foreR", {-0.25f, 0, 0}); set("armL", {0.1f, 0, -0.3f});
        set("legL", {0.6f, 0, -0.05f}); set("shinL", {-1.2f, 0, 0});
        set("head", {0, 0.45f, 0});
        break;
      case FALLEN_RIBCAGE:        // all that's left: the ribcage on the floor and the skull rolled beside it
        onBack(-0.05f);
        for (const char* n : {"armR", "foreR", "armL", "foreL", "legR", "shinR", "legL", "shinL"}) hideP(n);
        headLoose = M.find("head");
        headAt = glm::rotate(glm::rotate(glm::translate(mat4(1), vec3(0.32f, 0, -0.42f)), 1.1f, vec3(0, 1, 0)), 0.35f, vec3(0, 0, 1));
        s->held = false;
        break;
      case FALLEN_SCATTERED:      // the ribcage, the skull a step off and one leg bone left
        onBack(-0.15f);
        for (const char* n : {"armR", "foreR", "armL", "foreL", "legL", "shinL"}) hideP(n);
        set("legR", {0.25f, 0, 0.7f}); set("shinR", {-0.6f, 0, 0});
        headLoose = M.find("head");
        headAt = glm::rotate(glm::rotate(glm::translate(mat4(1), vec3(-0.45f, 0, -0.55f)), -0.6f, vec3(0, 1, 0)), -0.25f, vec3(1, 0, 0));
        s->held = false;
        break;
      default: break;
    }
    // each one a little different: every turn off by up to a few degrees, the head more
    if (jitter) {
      Mulberry32 jr(hash32(pose, jitter, 0x7A11, 3));
      for (size_t i = 0; i < rot.size(); i++) {
        const float k = M.parts[i].name == "head" ? 0.35f : 0.14f;
        rot[i] += vec3(((float)jr() - 0.5f) * k, ((float)jr() - 0.5f) * k, ((float)jr() - 0.5f) * k);
      }
    }
    Creature& m = *s->c;
    m.mats.assign(M.parts.size(), mat4(0));
    for (size_t i = 0; i < M.parts.size(); i++) {   // (sim/creature_rig.cpp rigPose: parent, pivot, then y, x, z)
      const RigPart& p = M.parts[i];
      mat4 mm;
      if ((int)i == headLoose) {   // the loose skull: on the floor by itself, the same way up as on the neck, turned
        vec3 half = (p.max - p.min) * 0.5f;
        mm = glm::translate(headAt, vec3(0, half.y > 0 ? -p.min.y : 0, 0) - vec3(0, 0, 0));
        mm = glm::translate(mm, -vec3(0, 0, 0));
      } else {
        const mat4& parent = p.parent >= 0 ? m.mats[p.parent] : root;
        mm = glm::translate(parent, p.pivot);
      }
      vec3 r = (int)i == headLoose ? rot[i] * 0.3f : p.rest + rot[i];
      if (r.y) mm = glm::rotate(mm, r.y, vec3(0, 1, 0));
      if (r.x) mm = glm::rotate(mm, r.x, vec3(1, 0, 0));
      if (r.z) mm = glm::rotate(mm, r.z, vec3(0, 0, 1));
      m.mats[i] = mm;
    }
  }
  return (cache[ck] = std::move(s)).get();
}

namespace {
// the bones' boxes: each part's box where the pose puts it, turned as the block faces (meta & 3 quarter turns)
bool remainsBoxes(BlockId id, u8 meta, ivec3 at, int purpose, std::vector<ShapeBox>& out) {
  const std::string kind = reg().block(id).str("remains");
  const FallenKind* K = fallenKind(kind);
  if (!K) return false;
  const FallenVariant v = fallenVariant(at, *K);
  const FallenPose* P = fallenPose(kind, dimOfY(at.y), v.pose, v.jitter);
  if (!P || !P->c) return false;
  const RigModel& M = *P->RM;
  const Creature& m = *P->c;
  const mat4 T0 = glm::rotate(mat4(1), -(float)(meta & 3) * PI / 2, vec3(0, 1, 0));
  for (size_t i = 0; i < M.parts.size() && i < m.mats.size(); i++) {
    const RigPart& p = M.parts[i];
    if (p.alpha < 1 || p.glow || !rigPartShown(m, p) || (i < P->hide.size() && P->hide[i])) continue;
    const vec3 e = p.max - p.min;
    if (e.x <= 0 || e.y <= 0 || e.z <= 0) continue;
    vec3 lo(1e9f), hi(-1e9f);
    for (int c = 0; c < 8; c++) {
      const vec3 q((c & 1) ? p.max.x : p.min.x, (c & 2) ? p.max.y : p.min.y, (c & 4) ? p.max.z : p.min.z);
      const vec3 w = vec3(T0 * m.mats[i] * vec4(q, 1)) * 16.f + vec3(8, 0, 8);
      lo = glm::min(lo, w); hi = glm::max(hi, w);
    }
    // bumped within its own cell (the floor under it), picked where it lies even past the cell's edge
    const bool col = purpose == ShapePurpose::Collide;
    const float l0 = col ? 0.f : -8.f, l1 = col ? 16.f : 24.f;
    ShapeBox b(std::clamp(lo.x, l0, l1), std::clamp(lo.y, 0.f, 16.f), std::clamp(lo.z, l0, l1), std::clamp(hi.x, l0, l1), std::clamp(hi.y, 0.f, 16.f), std::clamp(hi.z, l0, l1));
    if (b.b[3] - b.b[0] < 0.1f || b.b[4] - b.b[1] < 0.1f || b.b[5] - b.b[2] < 0.1f) continue;
    out.push_back(b);
  }
  return !out.empty();
}
}  // namespace

void registerFallen() {
  for (const BlockDef& b : reg().blocks) if (b.has("remains")) setPlacedShapes(b.id, &remainsBoxes);
}

} // namespace dq
