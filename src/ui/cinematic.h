// ui/cinematic.h — the stations as scenes (Cinematic Interactions' way, ui/cinematic_table.cpp): using a crafting table
// the camera glides from your eyes to lean over it, you work its 3 x 3 top in the world with the mouse or the pad, and
// leaving it glides back to wherever your eyes are by then, so you take over again without a cut.
#pragma once
#include "common/common.h"
#include "core/itemstack.h"
#include <functional>
#include <string>
#include <vector>

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
// the grindstone (kind 6), smithing table (8), loom (9), fletching table (10) and cartography table (11) worked in the
// world (sim/station_work.h pairWork*, ui/cinematic_work.cpp): what's worked and what goes into it lie on top; the
// work takes them up, makes the thing (at its contact time) and hands it to you
std::unique_ptr<Screen> makeCinematicPairScreen(ivec3 at, const std::string& station);
int pairSceneKind(const std::string& station);   // 6, 8.. 11; 0 none
// the stonecutter worked in the world (kind 7): the stone on its table, the shapes it cuts into on a dial over the saw
std::unique_ptr<Screen> makeCinematicCutScreen(ivec3 at);
// the jukebox (kind 12, JukeboxStation): your records on a dial over it; one put in sinks into the slot and plays, the one
// playing rises out and comes back to you. INSERT 1.3 s (in at INSERT_SEND 0.62), EJECT 1.15 s (out at EJECT_SEND 0.16)
std::unique_ptr<Screen> makeCinematicJukeboxScreen(ivec3 at);
// the campfire (kind 13, CampfireStation): sat down by it, your food set on its four spots by hand and taken back
std::unique_ptr<Screen> makeCinematicCampfireScreen(ivec3 at);
struct FireView { ivec3 at{0}; int hover = -1; bool placing = false; };
bool cinematicFireView(FireView& v);
struct JukeView { ivec3 at{0}; float viewYaw = 0; int hover = -1; double t = -1; bool eject = false; float scroll = 0; std::string disc; std::vector<std::string> dial; };
bool cinematicJukeView(JukeView& v);
constexpr double JUKE_INSERT = 1.3, JUKE_INSERT_SEND = 0.62, JUKE_EJECT = 1.15, JUKE_EJECT_SEND = 0.16;
vec3 jukeDialPoint(ivec3 at, float viewYaw, int k, int count, float scroll, double clock);   // a record's place on the dial
vec3 jukeDiscPos(const JukeView& v, vec3 from);   // the record moving in (from its place on the dial) or out (to you)
// what those scenes show (render/station_work_layer.cpp draws it): t the seconds into the work / the cut, -1 none
struct PairView { int kind = 0; ivec3 at{0}; float viewYaw = 0; int hover = -1; double t = -1; ItemStack before[2], after; };
bool cinematicPairView(PairView& v);
struct CutView { ivec3 at{0}; float viewYaw = 0; int hover = -1; double t = -1; bool all = false; float scroll = 0; ItemStack input, piece; };
bool cinematicCutView(CutView& v);
// each work's beats: length; contact, when the thing is made (and given); letGo, when it's lifted off toward you.
//  the grindstone (GrindstoneStation): lifted to the wheel by 0.6, stroked to and fro, let go at 1.9, of 2.4
//  the smithing table (SmithingStation): the material up by 0.5 and down on the gear by 0.76, pressed at 0.95 (glowing
//    to PEAK 1.35, cooled by 2.15), of 2.6
//  the loom (LoomStation): the dye taken up, six passes of the shuttle 0.2..1.85, of 2.4
//  the fletching table: flint and feather brought together by 0.6, the arrows fanned out, of 1.8
//  the cartography table (CartographyStation): the paper laid on the map by 0.6, of 2.1
struct PairBeats { double length, contact, letGo; };
PairBeats pairBeats(int kind);
constexpr double GRIND_CLEAN = 1.5, SMITH_PEAK = 1.35, SMITH_COOLED = 2.15, LOOM_FROM = 0.2, LOOM_TO = 1.85;
constexpr int LOOM_PASSES = 6;
constexpr double CUT_LENGTH = 1.05, CUT_ALL_LENGTH = 1.5, CUT_BITE = 0.26;
// where piece k is at t into the work (lying: where it lay, sim/station_work.h pairLiePos)
vec3 pairHeldPos(const PairView& v, int k, const vec3 lying[2]);
// StonecutterStation's dial (shapePoint: up to 9 shapes on a half ring over the saw, 0.72 wide, 0.5 high)
vec3 cutDialPoint(ivec3 at, float viewYaw, int k, int count, float scroll, double clock);
float cutDialShown(int k, int count, float scroll);   // 0..1: on the dial (fading at its ends)
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
