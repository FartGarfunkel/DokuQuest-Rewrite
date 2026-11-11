// sim/anvil.h — smithing on an anvil, TFC-lite (after TerraFirmaCraft's anvil; data/anvil.json): the piece (hot metal in
// your other hand) is worked toward a target on a 0–150 scale by eight moves — light, medium and heavy hits (−3, −6, −9),
// draw (−15), punch (+2), bend (+7), upset (+13), shrink (+16) — and each piece's plan wants its last three steps to be so
// (a pickaxe: punch last, bend and draw not last). Forgiving: within 3 of the target and on plan it's done; exact is the
// best work (a sturdier tool), a few off a little less good; the metal is never ruined (the pointer stops at 150). The
// target is fixed per piece per world. Metal is worked between 60% and 100% of its melting point (welding: 80%, with flux);
// an anvil works metals up to its tier (stone 0, copper 1, bronze 2, iron 3, steel 4, mithril 5, adamantite 6).
#pragma once
#include "core/metals.h"

namespace dq {

enum class AnvilMove : u8 { LightHit, MediumHit, HeavyHit, Draw, Punch, Bend, Upset, Shrink, Count };
enum class AnvilStep : u8 { Hit, Draw, Punch, Bend, Upset, Shrink };
int anvilMoveValue(AnvilMove m);
AnvilStep anvilStepOf(AnvilMove m);
const char* anvilMoveName(AnvilMove m);

struct AnvilRule { AnvilStep step; enum Where : u8 { Last, Second, Third, NotLast, Any } where; };
struct AnvilRecipe { std::string key, name, in, out; int n = 1; bool weld = false; std::vector<AnvilRule> plan; };
const std::vector<AnvilRecipe>& anvilRecipes();
const AnvilRecipe* anvilRecipe(const std::string& key);
std::string anvilPieceOf(const ItemStack& s);              // ingot | double_ingot | sheet | double_sheet | bloom | pig_iron | ""
int anvilTarget(u64 worldSeed, const std::string& recipe);  // 70..119

struct AnvilWork { std::string recipe; int target = 100, value = 0; std::vector<AnvilStep> steps; };   // steps: newest last
void anvilApply(AnvilWork& w, AnvilMove m);
bool anvilPlanMet(const AnvilRecipe& r, const std::vector<AnvilStep>& steps);
bool anvilDone(const AnvilWork& w);                        // within 3 of the target and on plan
float anvilQuality(const AnvilWork& w);                    // 1.15 exact; 0.95/0.9/0.85 for 1/2/3 off
bool anvilCanWork(const ItemStack& piece, const AnvilRecipe& r, int anvilTier, std::string* why = nullptr);
ItemStack anvilOutput(const AnvilRecipe& r, const ItemStack& piece, float quality);
int anvilTierOfBlock(const std::string& blockKey);         // -1 if not an anvil
std::string anvilBlockFor(const Comp& c);                  // which anvil four double ingots of this make

} // namespace dq
