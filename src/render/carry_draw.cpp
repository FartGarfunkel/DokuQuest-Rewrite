// render/carry_draw.cpp — Carry On (game/local.js pickUpBlock / pickUpMob, render/skins.js, local.js handState): what a player holds in
// both arms. A block shows as its item model; an animal as its creature model in the rest pose (the original's modelStillDraws).
// Player::carrying is {kind:'block', key, meta, ent} or {kind:'mob', type, hp, data}.
#include "render/char_layer.h"
#include "render/creature_models.h"
#include "assets/item_models.h"
#include "core/registry.h"
#include "sim/creature_rig.h"
#include <unordered_map>

namespace dq {

namespace {
// one creature per type, posed once at rest (age 0, not walking): its part matrices are the carried animal
Creature* restCreature(const std::string& type) {
  static std::unordered_map<std::string, std::unique_ptr<Creature>> cache;
  auto it = cache.find(type);
  if (it != cache.end()) return it->second.get();
  std::unique_ptr<Creature> c;
  if (reg().mobs.contains(type)) {
    c = std::make_unique<Creature>(type);
    if (const RigModel* RM = rigModel(c->modelKey())) rigPose(*c, *RM, creatureBase(*c, false));
    else c.reset();
  }
  return (cache[type] = std::move(c)).get();
}
}

void drawCarriedThing(const json& c, const mat4& at, bool fp, VoxelLight light, vec4 tint) {
  if (!c.is_object()) return;
  VoxelRenderer& VR = voxelRenderer();
  if (c.value("kind", std::string()) == "block") {
    std::string key = c.value("key", std::string());
    if (key.empty() || !reg().item(key)) return;
    const VoxelModel& im = itemModel(key);
    if (!im.ready()) return;
    mat4 m = at;
    if (fp) m = glm::rotate(m, 0.1f, vec3(0, 1, 0));
    m = glm::scale(m, vec3(fp ? 0.62f : 0.6f));
    VR.draw(im.gl, m * im.matrix(), light, tint);
    return;
  }
  std::string type = c.value("type", std::string());
  Creature* cr = restCreature(type);
  if (!cr || cr->mats.empty()) return;
  const RigModel* RM = rigModel(cr->modelKey());
  const CreatureModelGL* CM = creatureModel(cr->modelKey());
  if (!RM || !CM) return;
  float sc = reg().mobs[type].value("carryScale", type == "deer" ? 0.45f : type == "rabbit" ? 1.f : fp ? 0.7f : 0.6f);
  mat4 pm = glm::translate(at, vec3(0, fp ? -0.25f : -0.3f, 0));
  pm = glm::rotate(glm::scale(pm, vec3(sc)), PI / 2, vec3(0, 1, 0));
  for (size_t i = 0; i < RM->parts.size() && i < CM->parts.size(); i++) {
    const CreaturePartMesh& part = *CM->parts[i];
    if (part.hidden || !rigPartPosed(*cr, (int)i) || !rigPartShown(*cr, RM->parts[i]) || RM->parts[i].alpha < 1) continue;
    VR.draw(part.gl, pm * cr->mats[i] * part.local, RM->parts[i].glow ? VoxelLight{1, 1} : light, tint);
  }
}

} // namespace dq
