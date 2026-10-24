// sim/entity.h — anything that moves: players, creatures, dropped items, falling blocks, arrows,
// boats, carts... Per-type behaviour lives in subclasses or in systems keyed by `type`.
// Sizes follow the original (game/entities.js MOB_TYPES): w = half width, h = height.
#pragma once
#include "common/fs.h"

namespace dq {

class Level;

class Entity {
public:
  virtual ~Entity() = default;
  u32 id = 0;
  std::string type;          // "player", "item", "falling_block", or a MOB_TYPES key ("zombie", "cow", ...)
  int realm = 0;
  vec3 pos{0}, vel{0}, prevPos{0};
  float yaw = 0, pitch = 0, bodyYaw = 0, prevYaw = 0;
  float w = 0.3f, h = 1.8f;  // half-width, height
  float hp = 20, maxHp = 20;
  bool onGround = false, inWater = false, inLava = false, removed = false, noGravity = false, persistent = false;
  float age = 0, hurtTime = 0, fallDist = 0;
  u32 riding = 0;            // id of the entity this one rides
  json data = json::object();   // per-type state (AI, colour, contents, owner...) saved with the entity

  virtual void tick(Level& lv, double dt) {}
  virtual json save() const;
  virtual void load(const json& j);
  vec3 eye() const { return pos + vec3(0, h * 0.9f, 0); }
  bool isPlayer() const { return type == "player"; }
};

using EntityFactory = std::function<std::unique_ptr<Entity>(const std::string& type)>;
void registerEntityFactory(const std::string& typeOrPrefix, EntityFactory f);   // "" = fallback for mobs
std::unique_ptr<Entity> createEntity(const std::string& type);

} // namespace dq
