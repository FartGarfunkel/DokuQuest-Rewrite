// sim/vehicles.h — things you ride: minecarts (sim/minecart.h), rowboats, sailboats and the sailing ship
// (game/vehicles.js, boats2.js, ships.js, lilypads.js, trains.js pets/cargo). Simulated on the host; the
// rider's input reaches the vehicle through vehicleInput().
//
// API for the player controller (sim/ gameplay):
//   bool vehicleInput(Level&, Player&, vec2 move, bool jump, bool sneak)
//       call every frame with the movement axes (move.x = strafe right +, move.y = forward +). Returns true
//       while the player rides something: skip the normal walking/falling then (the vehicle carries them).
//       A fresh sneak press gets them out. Not calling it is fine: render/vehicle_layer calls it from
//       afterPlayerMove with the app's input when the controller didn't this frame.
//   void vehiclesAfterPlayerMove(Level&, Player&, double dt)
//       call after the player moved (the vehicle layer does it on the client): riders sit in their seats,
//       people standing on a boat or the ship's deck move and turn with it, boats and the ship are solid.
//   Vehicle* vehiclePick(Level&, Player&, float maxDist, float* dist)   the vehicle under the crosshair
//   bool useVehicle(Level&, Player&, Vehicle&, ItemStack* held)         right click on it (itemUse does this)
//   void hurtVehicle(Level&, Entity&, Player* attacker, bool blast)      a hit (combat): carts/boats break
//       after a few hits, the ship only when the attacker sneaks. Vehicles also notice a plain hp drop.
//   bool loadVehicle(Level&, Vehicle&, const json& carried) / json unloadVehicle(Level&, Vehicle&)
//       Carry On (trains.js/ships.js placeCarried): carried = {kind:"block", key, meta, ent} or
//       {kind:"mob", id}; unload returns the same shape (null if empty).
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

class Player;

// Wind: a slowly wandering direction and strength, with gusts (vehicles.js Wind). Sails and windmills use it.
struct Wind {
  float dir = 0, strength = 0.6f, t = 0, gust = 0;
  explicit Wind(int seed = 1234) : dir((seed % 628) / 100.f) {}
  void update(float dt);
};

struct VehicleInput { float mx = 0, mz = 0; bool jump = false, sprint = false; };   // mz < 0 = forward (the original's boatInput)

class Vehicle : public Entity {
public:
  std::vector<u32> riders;     // seats: player entity ids (0 = free); seat 0 steers
  u32 pet = 0;                 // a mob riding along (its data["mountedOn"] = uid)
  std::string uid;             // stable id across saves (train links, pets)
  VehicleInput inp; double inpT = -1;
  float health = 3, hitT = 0, shakeT = 0, lastHp = -1;
  virtual bool mount(Level& lv, Player& p) = 0;
  virtual void dismount(Level& lv, Player& p) = 0;
  virtual bool use(Level& lv, Player& p, ItemStack* held) = 0;
  virtual void hurt(Level& lv, Player* by, bool blast) = 0;
  virtual void breakApart(Level& lv, bool creative) = 0;
  virtual vec3 riderPos(int seat) const = 0;           // where a rider in that seat stands
  virtual bool pick(vec3 eye, vec3 dir, float& t) const = 0;   // ray against its body
  virtual bool loadCargo(Level& lv, const json& carried) { return false; }
  virtual json unloadCargo(Level& lv) { return nullptr; }
  int seatOf(u32 id) const { for (size_t i = 0; i < riders.size(); i++) if (riders[i] == id) return (int)i; return -1; }
  bool hasRider() const { for (u32 r : riders) if (r) return true; return false; }
  void ensureUid();
  vec2 toLocal(float x, float z) const { float dx = x - pos.x, dz = z - pos.z, c = std::cos(yaw), n = std::sin(yaw); return {dx * c - dz * n, dx * n + dz * c}; }
  vec2 toWorld(float lx, float lz) const { float c = std::cos(yaw), n = std::sin(yaw); return {pos.x + lx * c + lz * n, pos.z - lx * n + lz * c}; }
protected:
  void tickCommon(Level& lv, double dt);   // hit timers, riders still there, hp drops from combat, pet in place
  void seatPet(Level& lv, vec3 at);
  json saveCommon() const;
  void loadCommon(const json& d);
};

// Boat kinds (BOAT_TYPES): seats, item, hp, scale (the sailboat is built half again bigger), ship
struct BoatType { const char* key; const char* item; int hp; float scale; bool ship; vec3 seats[2]; };
const BoatType& boatType(const std::string& kind);
struct ShipDims { float deck = 1.3f, halfW = 1.42f, bow = -4.2f, stern = 4.2f; vec2 helm{0, 3.2f}, cargo{0, -2.4f}; };
constexpr ShipDims SHIP{};
float shipHalfAt(float lz);
bool onShipDeck(float lx, float lz, float inset = 0);
struct SmallBoat { float top = 0.6f, bow = -1.42f, stern = 1.16f; float half(float lz) const { return lz > -0.52f ? 0.7f : 0.7f * clampv((lz + 1.42f) / 0.9f, 0.08f, 1.f); } };
constexpr SmallBoat SMALL_BOAT{};

class Boat : public Vehicle {
public:
  Boat() { type = "boat"; w = 0.65f; h = 0.6f; riders.assign(2, 0); }
  std::string kind = "rowboat";
  float speed = 0, turn = 0, sail = 0.6f, boom = 0, heel = 0, bob = 0, eff = 0;
  bool hitWall = false, scaled = false;
  json items;                  // a chest aboard: 27 slots (null = none)
  std::string cargoKey;        // chest / barrel / trapped_chest
  json cargo;                  // any other block riding along {id, key, meta, ent}
  const BoatType& T() const { return boatType(kind); }
  bool ship() const { return T().ship; }
  float scale() const { return T().scale; }
  void tick(Level& lv, double dt) override;
  json save() const override;
  void load(const json& j) override;
  bool mount(Level& lv, Player& p) override;
  void dismount(Level& lv, Player& p) override;
  bool use(Level& lv, Player& p, ItemStack* held) override;
  void hurt(Level& lv, Player* by, bool blast) override;
  void breakApart(Level& lv, bool creative) override;
  vec3 riderPos(int seat) const override { return seatPos(seat); }
  bool pick(vec3 eye, vec3 dir, float& t) const override;
  bool loadCargo(Level& lv, const json& carried) override;
  json unloadCargo(Level& lv) override;
  vec3 seatPos(int i) const;
  float waterSurface(const World& w) const;   // NAN when not on water
};

class VehicleSystem : public LevelSystem {
public:
  explicit VehicleSystem(Level& lv);
  ~VehicleSystem() override;
  int tickHandler = 0;
  const char* name() const override { return "vehicles"; }
  void tick(Level& lv, double dt) override;    // wind, trains (joined carts), pets
  Level& lv;
  Wind wind;
  double time = 0;
  struct PlayerState { u32 onBoat = 0, aboard = 0; vec4 boatPose{0}, shipPose{0}; u32 linkFrom = 0; bool sneakWas = false; double inputT = -1; float noFuelT = -100; float yawRef = 0; bool yawSet = false; };
  std::unordered_map<u32, PlayerState> players;
  PlayerState& ps(u32 id) { return players[id]; }
};
VehicleSystem* vehicleSystem(Level& lv);
Wind& windOf(Level& lv);

bool isVehicle(const Entity& e);
Vehicle* asVehicle(Entity* e);
Vehicle* ridingOf(Level& lv, Player& p);
Entity* spawnBoat(Level& lv, int realm, const std::string& kind, vec3 pos, float yaw);
bool vehicleInput(Level& lv, Player& p, vec2 move, bool jump, bool sneak);
void vehiclesAfterPlayerMove(Level& lv, Player& p, double dt);
Vehicle* vehiclePick(Level& lv, Player& p, float maxDist, float* dist);
bool useVehicle(Level& lv, Player& p, Vehicle& v, ItemStack* held);
void hurtVehicle(Level& lv, Entity& v, Player* attacker, bool blast = false);
bool loadVehicle(Level& lv, Vehicle& v, const json& carried);
json unloadVehicle(Level& lv, Vehicle& v);
bool placeBoatItem(Level& lv, Player& p, ItemStack& held);   // vehicles.js placeBoat: on water or the shore
void toast(Level& lv, const Player& p, const std::string& text, const std::string& kind = "");
bool waterAt(const World& w, int x, int y, int z);
void moveVehicle(Level& lv, Entity& e, double dt, bool* hitWall = nullptr);   // moveEntity: collide with blocks

} // namespace dq
