// sim/fishing.h — fishing, the cozy way (game/fishing.js, 0.8.4).
// Use a rod: you wind back and cast, the float sails out on its line and bobs where it lands in water.
// Bubbles drift in as a fish comes nosing up; when the float ducks under, use the rod again, quick, and the
// catch flies up out of the water to you. Wait too long and it's gone. What bites depends on the water.
//
// Creatures: a fish being reeled toward the float has data["hooked"] = the float's entity id — its AI should
// leave its position alone while that is set (the float moves it); when let go, data["swimT"/"swimDir"/"swimY"]
// say "swim off that way, startled".
#pragma once
#include "sim/content.h"

namespace dq {

class Bobber : public Entity {
public:
  Bobber() { type = "fishing_bobber"; w = 0.08f; h = 0.12f; netLocal = true; noGravity = true; }
  u32 owner = 0;                 // the player's entity id
  enum State : u8 { Fly, Float, Ground } state = Fly;
  float waitT = 0, biteT = 0, comeT = 0;
  bool coming = false; vec3 comeFrom{0};
  u32 fish = 0;                  // the fish swimming up (entity id)
  bool fishLocal = false;        // that fish exists only for this float (a client's stand-in)
  std::string catchId, catchKind; int catchCm = 0;
  void tick(Level& lv, double dt) override;
  float surface(Level& lv) const;   // y of the water's surface here (NaN = none)
  void comeUp(Level& lv, float top);
  void letGo(Level& lv, bool scared);
};

struct FishCatch { std::string id, kind; int cm = 0; };   // kind: fish | junk | treasure
FishCatch fishingCatch(Level& lv, int realm, vec3 at);
Bobber* fishingOf(Level& lv, const Player& p);            // the player's float, if out
float castWindOf(const Player& p);                        // 0..1 while winding back to cast (pose.castWind)
void endFishing(Level& lv, Player& p, bool reel);
// the rod as the player holds it: "fishing_rod_cast" (no line hanging) while the float is out
std::string heldRodModel(Level& lv, const Player& p);
const int* fishSizes(const std::string& id);              // [min, max] cm

} // namespace dq
