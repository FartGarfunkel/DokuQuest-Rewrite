// sim/minecart.h — rails and minecarts (game/minecart.js) and trains (game/trains.js).
//
// Rails shape themselves as they're laid (straight, slopes, corners). Carts follow the track like Minecraft's:
// each rail is a line between the middles of two of its edges (a curve is the diagonal), slopes speed them,
// powered rails push them up to 8 blocks a second (and brake them when off), detector rails see them,
// activator rails tip riders out, light TNT carts and lock hopper carts. Kinds: minecart (a seat), chest,
// hopper, furnace (the double cart: furnace in front, a seat behind; feed it coal and drive) and tnt.
// A chain joins carts into a train (two joins a cart); joined carts keep their distance.
#pragma once
#include "sim/vehicles.h"

namespace dq {

struct CartType { const char* key; const char* item; const char* block; int slots; bool seat; };
const CartType& cartType(const std::string& kind);
constexpr float CART_MAX = 8;        // top speed on rails, blocks a second
constexpr float RAIL_Y = 1.f / 16;   // carts ride a sixteenth above the rail block's floor

class Minecart : public Vehicle {
public:
  Minecart() { type = "minecart"; w = 0.45f; h = 0.7f; riders.assign(1, 0); }
  std::string kind = "minecart";
  std::string cargoBlock;      // a chest-like block set in a plain cart (barrel...): drawn and dropped instead
  json cargo;                  // any other carried block riding along {id, key, meta, ent}
  float fuel = 0, pushX = 0, pushZ = 0, fuse = -1, roll = 0, rollT = 0, hopT = 0;
  bool locked = false, onRail = false;
  u64 rk = 0; bool rkSet = false;
  std::vector<std::string> links;   // uids of joined carts
  const CartType& T() const { return cartType(kind); }
  std::string blockShown() const { return !cargoBlock.empty() ? cargoBlock : T().block ? T().block : ""; }
  float half() const { return kind == "furnace" ? 1.36f : 0.68f; }   // half a cart's length (trains.js cartHalf)
  json* items() { return data["items"].is_array() ? &data["items"] : nullptr; }
  void become(const std::string& kind, const std::string& blockKey = "");   // trains.js cartBecome
  void prime(Level& lv);

  void tick(Level& lv, double dt) override;
  json save() const override;
  void load(const json& j) override;
  bool mount(Level& lv, Player& p) override;
  void dismount(Level& lv, Player& p) override;
  bool use(Level& lv, Player& p, ItemStack* held) override;
  void hurt(Level& lv, Player* by, bool blast) override;
  void breakApart(Level& lv, bool creative) override;
  vec3 riderPos(int seat) const override;
  bool pick(vec3 eye, vec3 dir, float& t) const override;
  bool loadCargo(Level& lv, const json& carried) override;
  json unloadCargo(Level& lv) override;
private:
  void baseTick(Level& lv, double dt, bool drivenByFurnace);
  void step(Level& lv, World& w, float dt);
  void turnTo(float target);
  void activate(Level& lv, bool on);
  void hopperPull(Level& lv);
};

Minecart* cartAt(Level& lv, int realm, int x, int y, int z, bool withItems = false);
Minecart* placeCart(Level& lv, int realm, const std::string& kind, int x, int y, int z, int dir);
bool placeCartItem(Level& lv, Player& p, ItemStack& held, ivec3 rail);   // rsplace.js placeCartItem
std::vector<Minecart*> trainOf(Level& lv, Minecart* c);
void trainStep(Level& lv, double dt);   // joined carts hold their distance and share their speed
// laying track
int railShapeFor(const World& w, int x, int y, int z, BlockId id, int prefer);
void railRefreshAround(World& w, int x, int y, int z);

} // namespace dq
