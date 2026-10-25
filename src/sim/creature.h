// sim/creature.h — creatures (game/entities.js Mob and the modules layered on it: nightmobs, monsters,
// quarrels, bestiary, husbandry, pets, aquatic, dismember, skymobs). One Creature class like the original's
// Mob; the type (reg().mobs = MOB_TYPES) decides the brain. Runs on the server (no GL).
//
// Brains live in: creature.cpp (core update, hurt, die), creature_monsters.cpp (night crowd, 0.8.3 monsters,
// the Cinder Golem, quarrels), creature_beasts.cpp (0.8 animals, farm animals, bees, pets, riding),
// creature_rig.cpp (rig animation + dismemberment), mobai.cpp (path finding).
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"
#include <set>
#include <optional>

namespace dq {

class Player;

// One MOB_TYPES entry, parsed for the hot paths (the json stays in `j`).
struct CreatureType {
  std::string key, model, label, voice, sound, ai, faction, family, element, ride;
  std::vector<std::string> variants, food, prey;
  float hp = 10, w = 0.3f, h = 0.9f, speed = 1, run = 0, fly = 0, swimSpeed = 0, damage = 0, knockResist = 0, skittish = 0, glows = 0;
  int xp = 0;
  bool hostile = false, special = false, boss = false, undead = false, fireImmune = false, ranged = false, climber = false, flyer = false,
       hopper = false, slimy = false, swims = false, walks = false, water = false, farm = false, woolly = false, layer = false, floaty = false,
       grazer = false, pecker = false, herd = false, pack = false, protective = false, angersWhenHurt = false, stalker = false, nocturnal = false,
       retaliates = false, rams = false, spit = false, curls = false, roosts = false, perches = false, stings = false, school = false, puffs = false,
       playful = false, needsAir = false, playsDead = false, frogTongue = false, ambient = false, displays = false, bear = false, snake = false,
       dayNeutral = false, noPush = false, fleesSun = false, npc = false, noDismember = false, carry = false, spirit = false;
  bool hasTint = false; vec4 tint{0};
  struct Drop { std::string id; int lo = 0, hi = 0; float chance = 1; };
  std::vector<Drop> drops;
  struct OnHit { float slow = 0, chill = 0, poison = 0, fire = 0; bool any = false; } onHit;
  json j;
  bool eats(const std::string& item) const { return std::find(food.begin(), food.end(), item) != food.end(); }
  bool hunts(const std::string& t) const { return std::find(prey.begin(), prey.end(), t) != prey.end(); }
};
const CreatureType* creatureType(const std::string& key);   // nullptr if not a MOB_TYPES key
bool isCreatureType(const std::string& key);
void reloadCreatureTypes();                                  // after data packs / mods changed reg().mobs

// what hurt something (the original's src object)
struct DamageSrc {
  std::string type;      // "mob", "projectile", "arrow", "explosion", "lava", "burn", "drown", "poison", "magic", "cactus", "" ...
  std::string element;   // "fire", "frost", ""
  std::string tool;      // tool kind of the weapon ("axe", "sword", "arrow"...) — decides severing
  u32 attacker = 0;      // entity id (player or creature; for arrows the archer)
  float knock = -1;      // < 0 = default (5)
  bool weapon = false, noEffect = false, bypassArmor = false;
  bool hasRay = false; vec3 rayO{0}, rayD{0};   // where the blow came from (dismemberment)
};

struct Move { float want = 0, dir = 0; };
using OptMove = std::optional<Move>;

// A rig's per-creature animation memory (render/rigs2.js rigLife, anim.js turn lean, aquatic.js squid)
struct RigLife {
  bool init = false; float lifeT = 0, yaw = 0, turn = 0, sp = 0, acc = 0, tail = 0, tailV = 0, idle = 0, rest = 0, ear = 2, earK = 0, sink = 0, shake = 0;
  int earSide = 1; bool wet = false; float lookT = 0, look = 0, cock = 0;
  float anT = -1, anYaw = 0, anLean = 0;
  float sqAge = -1, tilt = -1, sqYaw = 0, sqRoll = 0;
};

class Creature : public Entity {
public:
  explicit Creature(const std::string& type);
  const CreatureType* T;               // never null
  std::string variant;                 // model key when the type comes in coats/colours
  const std::string& modelKey() const { return variant.empty() ? T->model : variant; }
  bool alive() const { return deathTime == 0; }
  bool ghost = false;                  // a network replica: only smooths toward snapshots
  bool calm = false;                   // dev zoo: stands still, never attacks

  // ---- motion / animation (rigs read these) ----
  float walk = 0, walkAmt = 0, speedNow = 0, lastWant = 0, landT = 0, fallV = 0, swingT = 0;
  bool hitWall = false, hitCeiling = false, wantJump = false, lockMove = false;
  float flyVy = 0, swimVy = 0, swimPitch = 0; bool hasFlyVy = false;
  float headYawRel = 0, headPitch = 0;
  u32 lookAt = 0;
  RigLife life;
  std::vector<mat4> mats;              // last pose (part matrices, model space; empty = none)

  // ---- life ----
  float deathTime = 0, burn = 0, burnTick = 0, slowT = 0, attackCd = 0, invulnT = 0, poisonT = 0, poisonTick = 0;
  std::string state = "idle"; float stateT = 0;
  struct Attack { bool on = false; std::string kind; float tele = 0, progress = 0, yaw = 0, start = 0, t = 0; int left = 0; std::set<u32> hit; } attack;
  bool enragedOnce = false;
  float wanderT = 0, wanderDir = 0; bool wanderOn = false;
  float panicT = 0, soundT = 0, hopT = 0; u32 fleeFrom = 0; int fleeTurn = 0;
  bool stag = false;
  // farm animals
  float loveT = 0, breedCd = 0, growT = 0, eggT = 0; bool baby = false, kept = false, sheared = false, fed = false;
  std::string color;
  std::string heldItem, offItem;
  // the night crowd
  bool aiming = false, sees = false, yelled = false; float shootT = -1, strafeT = 0, losT = 0, huntT = 0, alertT = 0, quarrelT = 0; int strafe = 1;
  u32 quarrel = 0, leader = 0, wary = 0; float waryT = 0;
  bool hasShade = false; vec3 shade{0}; float shadeT = 0;
  float fuse = 0; int size = 0;
  int surf = 0; float wallYaw = 0, pounceCd = 0, tp = 0, tr = 0; bool climbing = false, hasTilt = false; vec2 surfaceTilt{0};   // surf 0 ground 1 wall 2 ceiling
  float drinkT = 0, drinkCd = 0, throwT = 1.5f, retreatT = 0, volleyT = 2, dawnT = 0, leapCd = 0, slamT = 0; int shots = 0;
  float angryT = 0, lungeT = 0, restT = 0, dryT = 0, flopT = 0, seekT = 0; u32 foe = 0; int flopSide = 0; bool hasWaterTo = false; vec2 waterTo{0};
  float striking = 0, strikeCd = 0, rattleT = 0; int stings = 0; bool rattling = false, coiled = false, tongueOut = false;
  // the 0.8 animals
  float hunger = -1, scanT = 0, curlT = 0, rearT = 0, displayT = 0, croakT = 0, tongueT = 0, ramT = 0, spitT = 1, grazeT = 0, grazeMax = 1, peckT = 0, flutterT = 0;
  u32 nearPred = 0, nearPrey = 0, herdMate = 0, parent = 0; int herdCount = 0; float parentT = 0;
  bool curled = false, sitting = false, sleeping = false, ramming = false, ramOnce = false, roosting = false, perched = false, wantPerch = false, dancing = false;
  float roostT = -1, flitT = 0, flitDir = 0, flitUp = 0, perchT = 0, flyAlt = 0; bool hasHome = false, hasFlyAlt = false; vec3 home{0};
  float swimT = 0, swimDir = 0, swimY = 0, airT = 90, puffT = 0, playDeadT = 0, swimUp = 0; bool hasSwimDir = false, hasSwimY = false, puffed = false, inked = false, playDead = false;
  bool hasSting = false;
  // dismemberment
  std::set<std::string> lost;
  bool crawling = false; int limp = 0; bool noFloaty = false;
  struct Regrab { bool on = false; std::string id; u32 item = 0; float t = 0; } regrab;
  // pets and riding
  std::string owner, petName; bool tamed = false, saddled = false, stay = false;
  u32 petFoe = 0, riddenBy = 0, jockeyOf = 0, jockeyRider = 0;
  struct RideInput { float fwd = 0, strafe = 0; bool jump = false, sprint = false; } rideIn;
  // bees
  bool hasHive = false, hasFlower = false, pollen = false; ivec3 hive{0}, flower{0}; int beeState = 0, visits = 0, wantVisits = -1; float beeSeekT = 0, workT = 0, beeWanderT = 0;   // 0 seek 1 work 2 home
  // path finding (mobai.cpp)
  float navT = 0, stuckT = 0; bool hasNavGoal = false; vec3 navGoal{0}; std::vector<ivec3> path; size_t pathI = 0; bool hasPath = false; vec2 navLast{0}; bool hasNavLast = false;
  // network replica target
  vec3 tpos{0}; float tyaw = 0; bool hasTarget = false;

  // ---- behaviour ----
  void tick(Level& lv, double dt) override;
  float hurt(Level& lv, float amount, const DamageSrc& src);   // damage actually dealt
  void die(Level& lv, const DamageSrc& src);
  void makeBaby();
  void grow(Level& lv);
  void setSize(int n);
  float weaponBonus() const;
  json save() const override;
  void load(const json& j) override;
  json snap() const;                    // compact state for guests (entities.js Mob.snap)
  void applySnap(const json& o);

  // shared know-how (used across the brain files)
  void wander(float dt);
  Move wanderMove() const;
  vec2 light(Level& lv, float dy) const;   // sky, block 0..15
  Entity* ent(Level& lv, u32 id) const;
  std::optional<float> navDir(Level& lv, vec3 to, float dt);
  bool stepAhead(Level& lv) const;
  bool lineOfSight(Level& lv, Entity& p) const;
  struct Senses { bool near = false, sees = false, hunting = false; };
  Senses senses(Level& lv, Entity* p, float dist, float dyp, float dt, float range = 20);
  Move chase(Level& lv, Entity& p, float dist, float toPlayer, float dt, float k = 1);
  void voiceTick(Level& lv, float dt, float dist, float lo = 5, float span = 7);
  void meleeAt(Level& lv, Entity& p, float dist, float dyp);
  bool bite(Level& lv, Entity* target, float dmg);
  void provoke(Level& lv, Entity* who);
  float goToward(Level& lv, vec3 t, float dt);
  float fleeDir(Entity* from);
  int groundY(Level& lv) const;
  void say(Level& lv, const std::string& kind, float dy = -1);   // voice through LevelFx (kind say/hurt/death/angry...)

  // brains (one per original module)
  Move specialAI(Level& lv, Entity* p, float dist, float dyp, float toPlayer, float dt);
  Move archetypeAI(Level& lv, Entity* p, float dist, float dyp, float toPlayer, float dt);
  Move bossAI(Level& lv, Entity* p, float dist, float toPlayer, float dt);
  OptMove farmAI(Level& lv, Entity* p, float dist, float toPlayer, float dt);
  void farmTick(Level& lv, float dt);
  OptMove controlAI(Level& lv, float dt);
  OptMove fetchWeapon(Level& lv, float dt);
  Entity* aiTarget(Level& lv, Entity* p);
  void takeQuarrel(Creature& a);
  void tryDismember(Level& lv, const DamageSrc& src, float dealt);
  void dropWeapon(Level& lv, bool main);
  bool teleportNear(Level& lv, Entity& o);
  void breedWith(Level& lv, Creature& mate);
  Player* ownerPawn(Level& lv) const;
  bool isPetOf(const Entity* who) const;

private:
  void update(Level& lv, float dt);
  void idleHabits(Level& lv, float dt, float want);
  void ghostStep(Level& lv, float dt);
};

// ---- helpers shared by the creature files ----
float frand();                                                   // 0..1
inline float wrapAngle(float a) { return std::fmod(std::fmod(a + PI * 3, TAU) + TAU, TAU) - PI; }
inline float heading(float fx, float fz, float tx, float tz) { return std::atan2(-(tx - fx), -(tz - fz)); }
float envDay(const Level& lv, int dim);                          // the original's env.day (0.2 night .. 1 noon)
int dimAt(const Entity& e);
bool isPawn(const Entity* e);                                    // a player that's in the game (not dead / spectating)
bool pawnSneaking(const Entity* e);
bool pawnCreative(const Entity* e);
Entity* nearestPawn(Level& lv, int realm, vec3 p, float maxD);
std::vector<Entity*> pawnsOf(Level& lv, int realm);
Entity* dropItem(Level& lv, int realm, const ItemStack& st, vec3 p, bool scatter, const vec3* vel = nullptr, float delay = 0);
Creature* spawnCreature(Level& lv, int realm, const std::string& type, vec3 p);
void burst(Level& lv, vec3 p, const std::string& kind, int n, float spread = 0.6f);   // fx->particles
void flame(Level& lv, vec3 p);
void hearts(Level& lv, vec3 p, int n = 5);
void floatText(Level& lv, u32 playerId, vec3 p, const std::string& text, const std::string& color);

// Using a creature (game/blocks2.js useOnAnimal + game/pets.js petUse): feeding, breeding, taming, milking,
// shearing, dyeing, saddling, riding, stay/follow. Returns true when something happened (the held stack is
// changed in place). Gameplay calls this from the player's "use" on an entity.
bool useOnCreature(Level& lv, Player& p, Creature& e, bool sneaking);
// Other areas' say in a creature's life (the villagers: their brain, who may hurt them, using them). All optional;
// a second user wraps the first (keep the old function and call it).
struct CreatureHooks {
  std::function<OptMove(Level&, Creature&, Entity* p, float dist, float dt)> brain;   // a move = it replaces the AI
  std::function<bool(Level&, Creature&, float& amount, const DamageSrc&)> beforeHurt;  // false = no harm (amount may change)
  std::function<void(Level&, Creature&, float dealt, const DamageSrc&)> afterHurt;    // after a blow that did harm (dead or not)
  std::function<void(Level&, Creature&, const DamageSrc&)> died;                      // as it dies, before drops
  std::function<bool(Level&, Player&, Creature&, bool sneaking)> use;                // true = handled (checked first by useOnCreature)
  std::function<bool(Level&, int realm, const std::string& type, vec3 at)> naturalSpawn;  // false = the night spawner leaves it out
};
CreatureHooks& creatureHooks();
// Riding: the client feeds the rider's input; sneak gets off. Returns the mount the player is on (or null).
Creature* rideControl(Level& lv, Player& p, float fwd, float strafe, bool jump, bool sprint, bool sneak);
void dismount(Level& lv, Creature& mount, Player& p);
// The owner's fights become the pets' fights (pets.js petsDefend): call when a player is hurt by / attacks e.
void petsDefend(Level& lv, Player& owner, Entity& foe);
// Stone mites from mined stone (monsters.js wakeMites; husbandry.js removed them: kept as a no-op hook)
void wakeMites(Level& lv, ivec3 at, int n, bool near);

// Habitat of a place (bestiary.js habitatAt): plains, forest, birch_forest, taiga, snowy_taiga, snowy_plains, snowy_peaks,
// mountains, savanna, desert, jungle, swamp, beach, snowy_beach, ocean. The terrain workstream can plug in the real
// biome (return "" to fall back to the built-in estimate from climate, ground and height).
using HabitatFn = std::function<std::string(World& w, int x, int z, BlockId ground)>;
void setHabitatProvider(HabitatFn f);
std::string habitatAt(World& w, int x, int z, BlockId ground);
void summonGolem(Level& lv, int realm, vec3 at, Entity* near);   // game.js summonGolem (boss arenas / commands)

void registerCreatures();     // entity factories + the creature LevelSystem (called from initCore)

} // namespace dq
