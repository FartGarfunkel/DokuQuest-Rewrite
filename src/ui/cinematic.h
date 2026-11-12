// ui/cinematic.h — the stations as scenes (Cinematic Interactions' way, ui/cinematic_table.cpp): using a crafting table
// the camera glides from your eyes to lean over it, you work its 3 x 3 top in the world with the mouse or the pad, and
// leaving it glides back to wherever your eyes are by then, so you take over again without a cut.
#pragma once
#include "common/common.h"
#include <functional>

namespace dq {

class GameSession;
class Screen;
struct Camera;

// the scene's camera over the player's own (client/play.cpp clientPlayerCamera calls it last); nothing when no scene is on
void cinematicCamera(GameSession& s, Camera& cam, double dt);
bool cinematicActive();
// the crafting table at `at` as a scene (ui/ui_main.cpp opens it in place of the crafting screen while the setting is on)
std::unique_ptr<Screen> makeCinematicTableScreen(ivec3 at);
// for the hands (render/cinematic_hands.cpp): the table, the square aimed at (-1: none), seconds since the last tap
void cinematicSetAim(int cell);
void cinematicTap();
double cinematicTapTime();   // when the last thing was set down or taken (-1 none): the first-person arm swings
float cinematicMoving();   // 0..1 while the camera moves in or out (the letterbox bars)
bool cinematicHandsState(ivec3& at, int& aimCell, double& sinceTap, int& phase, float& viewYaw, vec3* rest = nullptr);
// any other station (kind 1 an anvil, 2 the enchanting table, 3 a brewing stand, 4 a furnace) as a scene: the lean in with
// its own pose, then its screen (make), the glide back when that closes
std::unique_ptr<Screen> makeCinematicStationScreen(ivec3 at, int kind, std::function<std::unique_ptr<Screen>()> make);
// the anvil worked in the world (sim/anvil_work.h): the work on its face, the hammer's three blows
std::unique_ptr<Screen> makeCinematicAnvilScreen(ivec3 at);
double cinematicForgeT();   // seconds into the hammering, -1 when not forging
// the enchanting table worked in the world (sim/ench_work.h): the item over it, the offers in a crown, the rite
std::unique_ptr<Screen> makeCinematicEnchantScreen(ivec3 at);
double cinematicRitualT();  // seconds into the rite, -1 when none
// the brewing stand worked in the world: bottles on its feet, the ingredient, the powder
std::unique_ptr<Screen> makeCinematicBrewScreen(ivec3 at);
// a furnace loaded by hand: its mouth, its grate, the smelted off its top
std::unique_ptr<Screen> makeCinematicFurnaceScreen(ivec3 at);
// a chest worked by hand (sim/chest_work.h): its lid up, its slots lying inside in rows, put into and taken out of (kind 5)
std::unique_ptr<Screen> makeCinematicChestScreen(ivec3 at);
// a point the working hand reaches for (a station without the table's squares: the chest's slots); false: none
void cinematicSetAimPoint(bool on, vec3 p = vec3(0));
bool cinematicAimPoint(vec3& p);
// what the enchanting and anvil scenes show in the world (render/station_cards.cpp draws it; the scenes pick by it)
vec3 cinematicOfferPos(ivec3 at, float viewYaw, int k, double clock);   // offer k's tablet (0 left, 1 the middle, 2 right)
double cinematicClock();
struct EnchantView { ivec3 at{0}; float viewYaw = 0; int hover = -1; double ritualT = -1; int pick = 0; };
bool cinematicEnchantView(EnchantView& v);
struct AnvilView { ivec3 at{0}; float viewYaw = 0; int hover = -1; double denyT = -1, forgeT = -1; };
bool cinematicAnvilView(AnvilView& v);
int cinematicKind();
bool cinematicLeaning();
float cinematicCrouch();
vec3 cinematicViewOffset();   // how far the eye has gone down and in, bent to the station (the first-person body goes with it)    // 0..1: bent down to a low station (seamless first person: the eye goes down with the body)
// the hands' places in the world (render/cinematic_hands.cpp; 0 right, 1 left): the body's arms reach for them in first person
bool cinematicHandsWorld(vec3 out[2]);    // the camera leans over the station (else seamless first person: the hands rest before your own eyes)        // the scene's station (0 the table, 1 an anvil...), -1 when none

} // namespace dq
