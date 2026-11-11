// sim/controller.cpp — port of game/local.js (LocalPlayer.update/interact/use/mining/breaking/tree
// felling/breath/fire/falls/drop/pick block), fairplay.js (food over time), itemfix.js (eating for
// effect, the off hand), blocks2.js (bow and arrows) and blockfx.js (cracks that mend).
#include "sim/weather.h"
#include "sim/controller.h"
#include "sim/creature_rig.h"
#include "sim/vehicles.h"
#include "sim/effects.h"
#include "sim/rules.h"
#include "sim/blockfx.h"
#include "sim/worldsys.h"
#include "sim/gamecommands.h"
#include <random>
#include <unordered_set>
#include "sim/quiver.h"
#include "sim/piles.h"
#include "sim/immersive.h"
#include "sim/worktable.h"
#include "sim/knapping.h"
#include "sim/firestarter.h"
#include "sim/straw.h"
#include "sim/pit_kiln.h"
#include "core/metals.h"
#include "sim/metalwork.h"
#include "sim/forge.h"
#include "sim/heat.h"
#include "sim/anvil.h"
#include "sim/bloomery.h"
#include "sim/prospect.h"
#include "sim/quern.h"
#include "sim/barrel.h"
#include "sim/panning.h"
#include "sim/cavein.h"
#include "sim/spinning.h"
#include "world/rug.h"

namespace dq {
// (declared out here: MSVC gives a function declared inside a block the global namespace, so these never linked on Windows)
// the paths' talents (sim/villager_quests.cpp) and a poisoned arrow (sim/mob_combat.cpp)
float pathArrowFactor(const Player& p, float distance); bool pathArrowPoisons(const Player& p); bool pathQuiverSaves(const Player& p);
void poisonCreature(Entity& e, float secs);
}

namespace dq {

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }
constexpr float REACH = 5;
constexpr float CARRY_SLOW = 0.75f;
constexpr float FOOD_CD = 2.2f;

PlayerController::PlayerController(Level& l, Player& pl) : lv(l), p(pl) {}

bool useOnHost(PlayerController& pc, const std::string& kind, const RayHit* hit, u32 ent) {
  if (!pc.lv.isRemote() || !pc.lv.net) return false;
  json a = {{"k", kind}, {"sel", pc.p.selected}, {"sn", pc.p.sneaking ? 1 : 0}, {"yaw", pc.p.yaw}, {"pitch", pc.p.pitch}};
  if (hit && hit->hit) { a["b"] = {hit->block.x, hit->block.y, hit->block.z}; a["n"] = {hit->normal.x, hit->normal.y, hit->normal.z}; }
  if (ent) a["id"] = ent;
  pc.lv.net->action(pc.p, "use", a);
  pc.swingArm();
  return true;
}
void runGuestUse(Level& lv, Player& p, const json& a) {
  if (!a.is_object() || p.dead) return;
  PlayerController pc(lv, p);
  p.selected = clampv(a.value("sel", p.selected), 0, 8);
  const bool was = p.sneaking; p.sneaking = a.value("sn", 0) != 0;
  p.yaw = a.value("yaw", p.yaw); p.pitch = a.value("pitch", p.pitch);
  ItemStack& held = p.held();
  const ItemDef* def = held.empty() ? nullptr : reg().item(held.id);
  const std::string k = a.value("k", std::string());
  if (k == "ent") {
    Entity* e = lv.entity(a.value("id", 0u));
    if (e && !e->removed && e->realm == p.realm && glm::distance(e->pos, p.pos) < 8) {
      pc.targetEnt = e->id;
      for (auto& h : hooks().useEntity) if (h(pc, *e, held.empty() ? nullptr : &held, def)) break;
    }
  } else if (k == "block" && a.contains("b") && a["b"].is_array() && a["b"].size() == 3) {
    RayHit hit; hit.hit = true; hit.block = ivec3(a["b"][0].get<int>(), a["b"][1].get<int>(), a["b"][2].get<int>());
    if (a.contains("n") && a["n"].is_array() && a["n"].size() == 3) hit.normal = ivec3(a["n"][0].get<int>(), a["n"][1].get<int>(), a["n"][2].get<int>());
    hit.id = lv.world(p.realm).getBlock(hit.block.x, hit.block.y, hit.block.z); hit.point = vec3(hit.block) + vec3(0.5f);
    if (glm::distance(vec3(hit.block) + vec3(0.5f), p.pos) < 8) {
      pc.target = hit;
      for (auto& h : hooks().useBlock) if (h(pc, hit, held.empty() ? nullptr : &held, def)) break;
    }
  } else if (k == "item" && def) {
    for (auto& h : hooks().useItem) if (h(pc, held, *def, false)) break;
  }
  p.sneaking = was;
}

void PlayerController::swingArm() { if (p.swing == 0 || p.swing > 0.6f) p.swing = 0.0001f; }
void PlayerController::heldBecomes(ItemStack s) { p.inv[p.selected] = std::move(s); }
void PlayerController::consumeHeld(int n) {
  if (p.creative()) return;
  ItemStack& h = p.held();
  h.n -= n; if (h.n <= 0) h.clear();
}

static bool itemHasUse(const ItemDef& d) {
  for (const char* k : {"cape", "block", "heartCrystal", "cart", "boat", "food", "backpack", "bucket", "igniter", "pocketKey", "summon", "armor", "bow", "spyglass", "bonemeal", "plants", "disc", "bottle", "drink", "summonWand", "paintbrush", "pottery"})
    if (d.has(k)) return true;
  return false;
}

// ---- the per-frame update (LocalPlayer.update) ----------------------------------------------------
void PlayerController::update(float dt, const PlayerInput& in) {
  t += dt;
  World& w = world();
  if (p.dead) { mining.active = false; return; }
  p.yaw -= in.lookX; p.pitch = clampv(p.pitch - in.lookY, -1.55f, 1.55f);
  if (in.place) {
    placeMode ^= 1;
    toastMsg(placeMode ? "Placing: vertical slabs \xC2\xB7 upside-down stairs and trapdoors" : "Placing: normal");
    sfxUi(lv, p, "tick");
  }
  if (in.pick && target.hit && p.carrying.is_null()) pickBlock(target);
  if (p.sleeping) {
    if (in.jump || in.sneak || in.attackPressed) { p.sleeping = false; toastMsg("You got out of bed"); }
    else { p.vel.x = p.vel.z = 0; updateTarget(); return; }
  }
  // creative: double-tap jump to fly
  auto flyToggle = [&] {
    if (p.creative() && in.jump && !jumpWas) { if (t - lastJump < 0.3) { p.flying = !p.flying; lastJump = -10; } else lastJump = t; }
    jumpWas = in.jump;
    if (!p.creative()) p.flying = false;
  };
  auto hotbar = [&] {
    if (p.carrying.is_null()) {
      if (in.slot >= 0 && in.slot < 9) p.selected = in.slot;
      if (in.wheel) p.selected = floormod(p.selected + in.wheel, 9);
      if (in.swap) { std::swap(p.inv[p.selected], p.offhand[0]); sfxUi(lv, p, "swap"); p.equip = 0.4f; }
      if (in.hotRow && p.inv.size() >= 36) {   // hotbar <- first bag row <- second <- third <- hotbar
        std::vector<ItemStack> hot(p.inv.begin(), p.inv.begin() + 9);
        for (int r = 0; r < 3; r++) for (int i = 0; i < 9; i++) p.inv[r * 9 + i] = p.inv[(r + 1) * 9 + i];
        for (int i = 0; i < 9; i++) p.inv[27 + i] = hot[i];
        sfxUi(lv, p, "swap"); p.equip = 0.4f;
      }
    } else if ((in.wheel || in.slot >= 0) && !(t - carryHint < 4)) { carryHint = t; toastMsg("Your hands are full: put down what you're carrying to use the hotbar"); }
  };
  if (p.riding) {
    if (in.sneak && !sneakWas) { dismount(); sneakWas = true; return; }
    sneakWas = in.sneak;
    flyToggle(); hotbar();
    p.contacts = sampleContacts(w, p, p.eye);
    if (p.invuln > 0) p.invuln -= dt;
    if (p.hurtFlash > 0) p.hurtFlash = std::max(0.f, p.hurtFlash - dt * 2.5f);
    if (p.swinging || p.swing > 0) { p.swing += dt * 3.6f; if (p.swing >= 1) p.swing = 0; }
    p.swinging = false; p.bobAmt *= 0.9f;
    updateTarget(); interact(dt, in); updateHeldTools(dt, in);
    return;
  }
  sneakWas = in.sneak;
  flyToggle(); hotbar();
  p.contacts = sampleContacts(w, p, p.eye);
  bool wasHang = (bool)p.hang, wasClimb = (bool)p.climb;
  MoveOpts mo; mo.slow = (p.carrying.is_null() ? 1.f : CARRY_SLOW) * (p.scoping ? 0.25f : 1.f);   // (a spyglass to your eye: you edge along) mo.noLedge = !p.carrying.is_null(); mo.noSprint = !rule(lv, "allowSprinting");
  MoveInfo mv = p.move(w, in, dt, mo);
  if (in.crawl) toastMsg(p.crawlWant ? "Crawling" : "Standing up");
  if (p.hang && !wasHang) { sfxMaterial(lv, "stone", "step", p.pos); if (!ledgeTip) { ledgeTip = true; toastMsg("Hanging on \xE2\x80\x94 move left or right along the ledge, jump to climb up, sneak to drop"); } }
  if (p.climb && !wasClimb) sfxMaterial(lv, "stone", "step", p.pos);
  if (p.shimSound) { p.shimSound = false; sfxMaterial(lv, "stone", "step", p.pos); }
  p.contacts = sampleContacts(w, p, p.eye);
  tickSurvival(dt, mv);
  if (p.dead) return;
  if (p.equip < 1) p.equip = std::min(1.f, p.equip + dt * 5);
  if (p.eatAnim > 0) p.eatAnim -= dt;
  if (p.swinging || p.swing > 0) { p.swing += dt * 3.6f; if (p.swing >= 1) p.swing = 0; }
  p.swinging = false;
  updateTarget();
  interact(dt, in);
  updateHeldTools(dt, in);
  if (in.drop && p.carrying.is_null()) dropHeld(false);
  if (in.dropAll && p.carrying.is_null()) dropHeld(true);
  // arm timers for the third-person body (characters): swing 0..1, eating 0..1, bow draw
  p.swingT = p.swing; p.eatT = p.eatAnim > 0 ? std::min(1.f, p.eatAnim / 0.45f) : 0.f; p.bowPull = bowPull;
  // fairplay.js: food heals half at once and the rest over a few seconds
  if (p.foodCd > 0) p.foodCd -= dt;
  if (p.foodPool > 0) {
    if (p.dead || p.hp >= p.maxHp) p.foodPool = 0;
    else { float k = std::min(p.foodPool, dt * std::max(1.6f, p.foodPool / 2)); p.foodPool -= k; p.heal(k); }
  }
}

void PlayerController::tickSurvival(float dt, const MoveInfo& mv) {
  World& w = world();
  const Registry& R = reg();
  const Contacts& c = p.contacts;
  // footsteps and a little dust: one as each foot comes down, on the stride the legs swing (render/char_layer.cpp) and the
  // view dips (client/play.cpp) to: phase p.bob * 1.4, a foot down at each pi/2 + k*pi (every 1.4 blocks)
  if (p.onGround && mv.horizSpeed > 1) {
    const float foot = std::floor((p.bob * 1.4f - (float)PI / 2) / (float)PI);
    if (foot != p.stepAcc) {
      p.stepAcc = foot; BlockId id = w.getBlock(ifloor(p.pos.x), ifloor(p.pos.y - 0.2f), ifloor(p.pos.z));
      if (id) {
        const BlockDef& sb = R.block(id);
        sfxMaterial(lv, sb.sound, "step", p.pos);
        // snow, sand and gravel give under each step
        if (sb.sound == "snow" || sb.sound == "sand" || sb.sound == "gravel") lv.fx->particles("crumbs", p.pos + vec3(0, 0.05f, 0), 3, {{"block", sb.key}, {"speed", 0.35f}, {"grav", 12}});
      }
    }
    if (!c.water && (p.dustAcc += dt) > (p.sprinting ? 0.06f : 0.22f)) {   // bits of what you're running on kicked up behind you
      p.dustAcc = 0;
      BlockId under = w.getBlock(ifloor(p.pos.x), ifloor(p.pos.y - 0.2f), ifloor(p.pos.z));
      if (p.sprinting && under && R.tSolid[under]) lv.fx->particles("crumbs", p.pos + vec3(0, 0.1f, 0), 1, {{"block", R.block(under).key}, {"speed", 0.5f}, {"grav", 12}});
      else lv.fx->particles("dust", p.pos, 2, {{"speed", mv.horizSpeed}});
    }
  }
  // your breath fogs in the cold (where snow falls)
  if (!c.water && !p.dead && (breathAcc += dt) > 2.6f) {
    breathAcc = (float)rnd() * 0.6f;
    WeatherSystem* ws = weatherOf(lv);
    if (ws && ws->precipAt(w, p.pos.x, p.pos.y + p.eye, p.pos.z) == 2) {
      vec3 L = lookVector(p.yaw, 0);
      lv.fx->particles("breath", p.pos + vec3(L.x * 0.32f, p.eye - 0.12f, L.z * 0.32f), 3, {{"dx", L.x}, {"dz", L.z}});
    }
  }
  // falling (another system zeroing Entity::fallDist — a seamless crossing, an updraft — cancels the fall)
  if (p.hasFallStart && p.fallDist < fallDistSeen - 1e-4f) p.fallStart = p.pos.y;
  if (!p.onGround && !c.water && !c.lava) { if (!p.hasFallStart || p.pos.y > p.fallStart) { p.fallStart = p.hasFallStart ? std::max(p.fallStart, p.pos.y) : p.pos.y; p.hasFallStart = true; } }
  if (c.water || c.lava) p.hasFallStart = false;
  p.fallDist = fallDistSeen = p.hasFallStart ? std::max(0.f, p.fallStart - p.pos.y) : 0.f;
  if (landT > 0) landT -= dt;
  if (p.onGround && p.hasFallStart) {
    float fall = p.fallStart - p.pos.y;
    p.hasFallStart = false;
    if (fall > 1.2f) landT = 0.2f;
    BlockId landId = w.getBlock(ifloor(p.pos.x), ifloor(p.pos.y - 0.2f), ifloor(p.pos.z));
    if (fall > 2.5f && landId && R.tSolid[landId] && !c.water)   // a hard landing throws up a ring of what you land on
      lv.fx->particles("crumbs", p.pos + vec3(0, 0.1f, 0), std::min(28, (int)(fall * 3)), {{"block", R.block(landId).key}, {"speed", std::min(3.f, 0.8f + fall * 0.12f)}, {"grav", 14}});
    // beds, hay, slime and honey break a fall
    float soft = (bhas(landId, BF::Bed) || bhas(landId, BF::Bouncy) || bhas(landId, BF::Sticky) || R.block(landId).has("cloud") ? 0.f : startsWith(R.block(landId).key, "hay") ? 0.2f : 1.f) * (1 - charmBonus(p, "fall"));
    if (R.block(landId).has("sunCloud")) giveEffect(lv, p, "slowfall", 10);   // a golden cloud: float down from it
    int dmg = (int)std::floor((fall - 3) * soft);
    if (fall > 3.4f && !p.creative() && dmg > 0) { DamageSource s; s.type = "fall"; s.bypassArmor = true; hurt((float)dmg, s); sfxUi(lv, p, "thud"); }
  }
  // hitting the water
  if (c.water && !p.wasWater && p.vel.y < -2.5f) { sfxUi(lv, p, "splash"); lv.fx->particles("splash", vec3(p.pos.x, std::floor(p.pos.y + 0.5f) + 0.9f, p.pos.z), 12, {{"big", p.vel.y < -9}}); }
  p.wasWater = c.water;
  breathe(dt);
  bool lavaBoat = false;   // aboard a lava boat the lava never reaches you
  if (p.riding) if (auto* b = dynamic_cast<Boat*>(lv.entity(p.riding))) lavaBoat = b->kind == "lava_rowboat";
  if (lavaBoat) p.burn = 0;
  else if (c.lava) { DamageSource s; s.type = "lava"; hurt(4, s); p.burn = 7; }
  if (p.burn > 0) {
    p.burn -= dt;
    if (c.water) { p.burn = 0; sfxUi(lv, p, "fizz"); }
    if ((p.burnT += dt) > 1) { p.burnT = 0; DamageSource s; s.type = "burn"; hurt(1, s); }
  }
  if (c.cactus) { DamageSource s; s.type = "cactus"; hurt(1, s); }
  if (spikeOverlap(w, p)) {
    float st = p.data.value("spikeT", 0.f) + dt;
    float need = p.sneaking ? 1.f : 0.5f;
    if (st >= need) {
      st = 0;
      DamageSource s; s.type = "cactus"; s.bypass = true;
      hurt(p.sneaking ? 1.f : 2.f, s);
    }
    p.data["spikeT"] = st;
  } else if (p.data.contains("spikeT")) p.data["spikeT"] = 0;
  if (c.fire && !c.water) p.burn = std::max(p.burn, 3.f);
  if (c.hot && !p.sneaking) { if ((p.hotT += dt) > 1) { p.hotT = 0; DamageSource s; s.type = "burn"; hurt(1, s); } }
  // BTA 8.0's Nether: an ember (buried under the Old World's ash) goes off when it is stepped on: a blast with fire that
  // flings you up; the rubyglass crust over the crystal seas cracks under you, a stage every quarter second, and shatters
  {
    static const BlockId ember = reg().blockId("ember"), ruby = reg().blockId("rubyglass_crystal");
    const int fx = ifloor(p.pos.x), fy = ifloor(p.pos.y - 0.05f), fz = ifloor(p.pos.z);
    const BlockId below = (ember || ruby) && p.onGround ? w.getBlock(fx, fy, fz) : AIR;
    if (ember && below == ember && p.mode != "spectator") {
      w.setBlock(fx, fy, fz, AIR, 0);
      explode(lv, p.realm, vec3(fx + 0.5f, fy + 0.5f, fz + 0.5f), 3.0f);
      p.burn = std::max(p.burn, 5.f);
      p.vel.y = std::max(p.vel.y, 13.f);
    }
    if (ruby && below == ruby && p.mode != "spectator") {
      float t = p.data.value("rubyT", 0.f) + dt;
      if (t >= 0.25f) {
        t = 0;
        const u8 m = w.getMeta(fx, fy, fz);
        const vec3 at(fx + 0.5f, fy + 0.9f, fz + 0.5f);
        if (m >= 3) {
          w.setBlock(fx, fy, fz, AIR, 0);
          lv.fx->sound("glass_break", at, 20);
          lv.fx->particles("crumbs", at, 16, {{"block", "rubyglass_crystal"}, {"speed", 1.4f}, {"grav", 12}});
        } else {
          w.setBlock(fx, fy, fz, ruby, (u8)(m + 1));
          lv.fx->particles("crumbs", at, 3, {{"block", "rubyglass_crystal"}, {"speed", 0.5f}, {"grav", 12}});
        }
      }
      p.data["rubyT"] = t;
    } else if (p.data.contains("rubyT")) p.data.erase("rubyT");
  }
  if (c.acid && !p.creative()) {   // the Sulfur Pools' acid: it eats at bare skin (one hurt a second per piece of armour not worn), and at the armour
    if ((p.acidT += dt) > 1) {
      p.acidT = 0;
      int bare = 0; for (auto& a : p.armor) if (a.empty()) bare++;
      if (bare) { DamageSource s; s.type = "acid"; s.bypassArmor = true; hurt((float)bare, s); }
      wearArmor(p);
      if (bare && !p.data.value("toldAcid", false)) { p.data["toldAcid"] = true; toast(lv, p, "The acid burns! Armour keeps it off your skin.", "bad"); }
    }
  } else p.acidT = 0;
  float voidY = p.realm == REALM_MAIN ? -32.f : dimBase(DIM_OVERWORLD) - 32.f;
  if (p.pos.y < voidY) { DamageSource s; s.type = "void"; s.bypassArmor = s.bypass = true; hurt(4, s); }
  if (p.invuln > 0) p.invuln -= dt;
  if (p.hurtFlash > 0) p.hurtFlash = std::max(0.f, p.hurtFlash - dt * 2.5f);
  if (p.slow > 0) p.slow -= dt;
  if (!p.brokeSomething.empty()) { toastMsg("Your " + p.brokeSomething + " broke", "warn"); sfxUi(lv, p, "hit"); p.brokeSomething.clear(); }
}

// Breath runs out with your head under water; then you drown, a heart a second. Rest heals.
void PlayerController::breathe(float dt) {
  p.recalcMax();
  if (p.stats.contains("refundHearts")) {
    int n = p.stats.value("refundHearts", 0); p.stats.erase("refundHearts");
    if (n > 0) { giveItem(lv, p, ItemStack("heart_crystal", n)); toastMsg("Hearts no longer come from levels \xE2\x80\x94 your " + std::to_string(n) + " extra became Heart Crystals. Eat one for a heart that stays.", "rare"); }
  }
  const Contacts& c = p.contacts;
  if (c.eyeWater && !p.creative() && !p.dead) {
    p.air -= dt / (1 + charmBonus(p, "breath"));
    if (rnd() < dt * (p.air <= 0 ? 6 : 1.2)) { vec3 L = lookVector(p.yaw, p.pitch); lv.fx->particles("bubbles", p.pos + vec3(L.x * 0.3f, p.eye - 0.1f, L.z * 0.3f), 1); }
    if (p.air <= 0) {
      p.air = 0;
      if ((p.drownT += dt) >= 1) { p.drownT = 0; DamageSource s; s.type = "drown"; s.bypassArmor = true; hurt(2, s); }
    }
  } else { p.air = std::min(AIR_MAX, p.air + dt * 6); p.drownT = 0; }
  // slow healing, half a heart every four seconds once you've been safe for a while
  if (rule(lv, "naturalRegen") && !p.dead && p.hp < p.maxHp && gameClock(lv) - p.lastHurtT > 8) {
    if ((p.regenTimer += dt) > 4) { p.regenTimer = 0; p.heal(1); }
  }
}

// ---- the crosshair ----------------------------------------------------------------------------
void PlayerController::updateTarget() {
  World& w = world();
  vec3 d = lookVector(p.yaw, p.pitch);
  glm::dvec3 o(p.pos.x, p.pos.y + p.eye, p.pos.z);
  if (aimOverride) {   // over the shoulder: aim at whatever is under the crosshair, as seen from the camera
    RayHit ch = raycastBlocks(w, aimOrigin, aimDir, 10);
    double tt = ch.hit ? ch.dist : 10;
    glm::dvec3 P = aimOrigin + glm::dvec3(aimDir) * tt - o;
    double l = glm::length(P);
    if (l > 0.3) d = vec3(P / l);
  }
  RayHit hit = raycastBlocks(w, o, d, REACH);
  double ed = hit.hit ? hit.dist : REACH;
  u32 ent = 0;
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != p.realm || e.get() == &p || e->id == p.riding) continue;
    bool vehicle = startsWith(e->type, "boat") || startsWith(e->type, "minecart") || e->data.value("vehicle", false);
    if (!vehicle && !(isMob(*e) && e->hp > 0)) continue;
    double tt = -1;
    bool rigged = false;
    if (!vehicle) if (auto* c = dynamic_cast<Creature*>(e.get())) {
      const RigModel* M = rigModel(c->modelKey());
      if (M && !M->rig.empty() && !M->parts.empty()) {
        rigged = true;
        float span = std::max(c->w, c->h) * 4.f + 4.f;
        if (glm::distance(vec3(o), c->pos + vec3(0, c->h * 0.5f, 0)) < (double)REACH + span) {
          rigPose(*c, *M, creatureBase(*c, false));
          float partT = -1.f;
          if (rigHitPart(*c, *M, vec3(o), d, &partT) >= 0) tt = partT;
        }
      }
    }
    // a rigged mob is only the parts you can see. vehicles and unrigged mobs keep one box
    if (tt < 0 && !rigged) {
      double pad = startsWith(e->type, "boat") ? 0.45 : 0.1, hh = startsWith(e->type, "boat") ? 0.9 : e->h;
      tt = rayAABB(o, glm::dvec3(d), glm::dvec3(e->pos.x - e->w - pad, e->pos.y, e->pos.z - e->w - pad), glm::dvec3(e->pos.x + e->w + pad, e->pos.y + hh, e->pos.z + e->w + pad));
    }
    if (tt >= 0 && tt < ed && tt < (vehicle ? 4.5 : 3.8)) { ed = tt; ent = e->id; }
  }
  target = hit; targetEnt = ent;
  targetPoint = hit.hit ? vec3(o + glm::dvec3(d) * (double)hit.dist) : vec3(0);
}
int PlayerController::targetQuarter() const {
  if (!target.hit) return -1;
  float fx = targetPoint.x - target.block.x, fz = targetPoint.z - target.block.z;
  return (fx >= 0.5f ? 1 : 0) + (fz >= 0.5f ? 2 : 0);
}
vec3 PlayerController::hitFrac(const RayHit& hit) const {
  vec3 d = lookVector(p.yaw, p.pitch);
  vec3 h = vec3(p.pos.x, p.pos.y + p.eye, p.pos.z) + d * hit.dist;
  return h - vec3(hit.block);
}
ivec3 PlayerController::placeTarget(const RayHit& hit) const {
  const Registry& R = reg();
  if (R.tReplaceable[hit.id] && !R.tLiquid[hit.id]) return hit.block;
  return hit.block + hit.normal;
}
bool PlayerController::blockedByBody(int x, int y, int z) {
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != p.realm) continue;
    float ew, eh;
    if (e->isPlayer()) { if (static_cast<Player*>(e.get())->dead) continue; ew = PLAYER_W; eh = PLAYER_H; }
    else if (isMob(*e) && e->hp > 0) { ew = e->w; eh = e->h; }
    else continue;
    if (x + 1 > e->pos.x - ew && x < e->pos.x + ew && y + 1 > e->pos.y && y < e->pos.y + eh && z + 1 > e->pos.z - ew && z < e->pos.z + ew) return true;
  }
  return false;
}

bool deflectFireball(Level& lv, Entity& by, vec3 eye, vec3 look, float reach);   // sim/creature_entities.cpp

// ---- mining / attacking / using --------------------------------------------------------------
void PlayerController::interact(float dt, const PlayerInput& in) {
  if (breakCd > 0) breakCd -= dt;
  if (p.attackCd > 0) p.attackCd -= dt;
  if (p.mode == "spectator") { mining.active = false; return; }   // watching only: no breaking, placing, using or fighting
  if (!p.carrying.is_null()) { mining.active = false; if (in.usePressed) placeCarried(); return; }
  const ItemStack& h = p.held();
  if (!h.empty() && h.id == "we_wand") { if (in.attackPressed) weClick(1); if (in.usePressed) weClick(2); mining.active = false; return; }
  Entity* te = targetEnt ? lv.entity(targetEnt) : nullptr;
  // a ghast's fireball can be punched back (Minecraft): it flies for you then, and finishes off a ghast it hits
  if (in.attackPressed && deflectFireball(lv, p, vec3(p.pos.x, p.pos.y + p.eye, p.pos.z), lookVector(p.yaw, p.pitch), 4.5f)) { swingArm(); mining.active = false; fightHold = true; return; }
  if (in.attackPressed && te) { swingArm(); attackEntity(lv, p, *te); mining.active = false; fightHold = true; return; }
  if (!in.attack) fightHold = false;
  if (in.attackPressed && !target.hit) swingArm();
  // a held swing that hit a creature never digs into the ground behind it; in creative a sword never breaks blocks (Minecraft)
  const ToolInfo* ti = heldTool(p);
  bool noDig = fightHold || (p.creative() && ti && ti->type == "sword") || p.mode == "adventure";   // adventure: the world stays as it is
  // creative: every fresh click breaks the block it's on at once, however short the click and however soon after the
  // last one (Minecraft's startDestroyBlock); holding repeats every quarter second
  if (p.creative() && in.attackPressed && target.hit && !te && !noDig) {
    const RayHit& tg = target;
    if (mining.active) rememberDamage(lv, mining);
    p.swinging = true; if (p.swing == 0) p.swing = 0.0001f;
    breakBlock(tg.block.x, tg.block.y, tg.block.z); forgetDamage(lv, tg.block.x, tg.block.y, tg.block.z);
    mining.active = false; breakCd = 0.25f;
  } else if (in.attack && target.hit && !te && !noDig) {
    const RayHit& tg = target;
    if (!mining.active || mining.x != tg.block.x || mining.y != tg.block.y || mining.z != tg.block.z || mining.id != tg.id) {
      if (mining.active) rememberDamage(lv, mining);   // a block you gave up on keeps its cracks for a while
      mining = Mining{true, tg.block.x, tg.block.y, tg.block.z, tg.id, damageFor(lv, tg.block.x, tg.block.y, tg.block.z, tg.id), breakTime(tg.id, &tg), 0};
    }
    p.swinging = true; if (p.swing == 0) p.swing = 0.0001f;
    if (breakCd <= 0 && std::isfinite(mining.time)) {
      mining.progress += mining.time <= 0 ? 1 : dt / mining.time;
      mining.pt += dt;
      const BlockDef& b = reg().block(tg.id);
      if (mining.pt > 0.22f) { mining.pt = 0; sfxMaterial(lv, b.sound, "dig", vec3(tg.block) + vec3(0.5f)); burst(lv, vec3(tg.block) + vec3(0.5f) + vec3(tg.normal) * 0.52f, b.key, 2, 0.6f); }
      if (mining.progress >= 1) { breakBlock(tg.block.x, tg.block.y, tg.block.z); forgetDamage(lv, tg.block.x, tg.block.y, tg.block.z); mining.active = false; breakCd = 0.18f; }
    }
  } else { if (mining.active) rememberDamage(lv, mining); mining.active = false; }
  // holding use repeats only after a fresh press in the game
  if (!in.use || t - interactAt > 0.15) useArmed = false;
  interactAt = t;
  if (in.usePressed) { useArmed = true; useRepeat = 0.28f; use(false, in); }
  else if (in.use && useArmed) { useRepeat -= dt; if (useRepeat <= 0) { useRepeat = 0.22f; use(true, in); } }
}

float PlayerController::breakTime(BlockId id, const RayHit* hit) {
  if (hit) if (const BlockId rug = rugBlockOf(world().getCoat(hit->block.x, hit->block.y, hit->block.z))) id = rug;   // (a rug on it: that comes up first, as quick as carpet)
  const BlockDef& b = reg().block(id);
  if (p.creative()) return 0.05f;   // creative breaks anything, bedrock and command blocks too (Minecraft)
  if (b.hardness < 0) return INFINITY;
  if (b.hardness == 0) return 0;
  const ToolInfo* tool = heldTool(p);
  if (needsAxe(lv, id) && !(tool && tool->type == "axe")) {   // immersive: no punching trees (TFC)
    static double hintAt = -10;
    if (nowSeconds() - hintAt > 4) { hintAt = nowSeconds(); toastMsg("You need an axe"); }
    return INFINITY;
  }
  bool right = tool && !b.tool.empty() && tool->type == b.tool;
  bool harvest = !b.tier || (right && tool->tier >= b.tier);
  if (b.has("needTier") && (!harvest || (p.realm == REALM_MAIN && dimOfY(ifloor(p.pos.y)) == DIM_END))) return INFINITY;
  float t2 = b.hardness * (harvest ? 1.5f : 5.f) / (right ? tool->speed : 1.f);
  if (tool && tool->type == "sword" && bhas(id, BF::IsLeaves)) t2 /= 3;
  if (bhas(id, BF::IsLog) && !p.sneaking && hit) {
    std::vector<std::array<int, 4>> logs, leaves; bool ok;
    scanTree(hit->block.x, hit->block.y, hit->block.z, logs, leaves, ok);
    if (ok) t2 *= std::min(3.f, 1 + 0.1f * (logs.size() - 1));
  }
  if (p.contacts.eyeWater) t2 *= 3;
  for (auto& h : hooks().breakTime) t2 = h(*this, id, t2);
  return std::max(0.05f, t2);
}
bool PlayerController::canHarvest(BlockId id) {
  const BlockDef& b = reg().block(id);
  if (!b.tier) return true;
  const ToolInfo* tool = heldTool(p);
  return tool && tool->type == b.tool && tool->tier >= b.tier;
}

void PlayerController::breakBlock(int x, int y, int z) {
  World& w = world();
  BlockId id = w.getBlock(x, y, z);
  if (!id) return;
  const BlockDef& b = reg().block(id);
  if (lv.events && !lv.events->emit("blockBreak", json::array({p.name, x, y, z, b.key}))) return;   // a mod said no
  if (const BlockId rug = rugBlockOf(w.getCoat(x, y, z))) {   // a rug on it comes up first (world/rug.h); the floor stays
    w.setCoat(x, y, z, 0, SetFlags::Urgent);
    const BlockDef& rd = reg().block(rug);
    const vec3 at = vec3(x, y, z) + vec3(0.5f, 1.1f, 0.5f);
    if (!p.creative()) dropItem(lv, p.realm, ItemStack(rd.drop.empty() ? rd.key : rd.drop, 1), at, true, (const vec3*)nullptr);
    sfxMaterial(lv, rd.sound, "break", at); burst(lv, at, rd.key, 8, 0.8f);
    return;
  }
  if (bhas(id, BF::IsLog) && !p.sneaking && rule(lv, "treeFelling")) {
    std::vector<std::array<int, 4>> logs, leaves; bool ok;
    scanTree(x, y, z, logs, leaves, ok);
    if (ok) { fellTree(x, y, z, logs, leaves); return; }
  }
  bool harvest = canHarvest(id);
  u8 meta = w.getMeta(x, y, z);
  if (json* ent = w.blockEntity(x, y, z)) { json copy = *ent; spillEntity(lv, w, copy, x, y, z); }
  w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
  afterBreak(x, y, z, id, meta);
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  sfxMaterial(lv, b.sound, "break", c);
  burst(lv, c, b.key, 14, 1.2f);
  const std::string oreMetal = immersive(lv) ? metalOfOreBlock(b.key) : std::string();
  if (harvest && !p.creative() && !oreMetal.empty()) dropItem(lv, p.realm, ItemStack(oreMetal + "_ore_piece", 1 + (rnd() < 0.5 ? 1 : 0)), c, true, (const vec3*)nullptr);   // immersive: ore pieces (core/metals.h)
  else if (harvest && !p.creative()) dropBlockItems(lv, w, x, y, z, id, false, meta, heldTool(p));
  if (!p.creative()) if (const std::string fr = b.str("fruit"); !fr.empty() && rnd() < 0.15) dropItem(lv, p.realm, ItemStack(fr, 1), c, true, (const vec3*)nullptr);   // fruit from leaves (cherries)
  strawOnBreak(lv, *this, p, x, y, z, id);   // sim/straw.h: straw from grass cut with a knife or scythe
  if (!p.creative() && immersive(lv) && isRock(id)) caveIn(lv, w, x, y, z, (float)rnd());   // sim/cavein.h: now and then a little rubble from an unsupported roof
  if (!p.creative() && bhas(id, BF::IsLeaves) && immersive(lv) && rnd() < LEAF_STICK_CHANCE) dropItem(lv, p.realm, ItemStack("stick", 1), c, true, (const vec3*)nullptr);   // immersive: sticks from leaves
  if (harvest && b.num("xp") > 0) { vec3 at(x + 0.5f, y + 0.8f, z + 0.5f); grantXP(lv, p, (int)b.num("xp"), &at); }
  wearHeld(lv, p, b.hardness > 0 ? 1 : 0);
  p.stats["blocksMined"] = p.stats.value("blocksMined", 0) + 1;
}

// ---- tree felling (Tree Capitator) ----
void PlayerController::scanTree(int x, int y, int z, std::vector<std::array<int, 4>>& logs, std::vector<std::array<int, 4>>& leaves, bool& ok) {
  World& w = world();
  ok = false; logs.clear(); leaves.clear();
  auto key = [](int a, int b, int c) { return ((u64)(u32)a * 73856093ull) ^ ((u64)(u32)b << 40) ^ ((u64)(u32)c * 19349663ull) ^ ((u64)(u32)c << 20); };
  std::unordered_set<u64> seen;
  std::vector<std::array<int, 3>> q = {{x, y, z}};
  while (!q.empty() && logs.size() < 140) {
    auto [a, b, c] = q.back(); q.pop_back();
    if (!seen.insert(key(a, b, c)).second) continue;
    BlockId id = w.getBlock(a, b, c);
    if (!bhas(id, BF::IsLog)) continue;
    logs.push_back({a, b, c, id});
    for (int dy = 0; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy && !dz) continue;
      int nx = a + dx, ny = b + dy, nz = c + dz;
      if (ny < y || std::abs(nx - x) > 7 || std::abs(nz - z) > 7) continue;
      q.push_back({nx, ny, nz});
    }
  }
  if (logs.size() < 2) return;
  std::unordered_set<u64> ls;
  for (auto& l : logs) for (int dy = -1; dy <= 3; dy++) for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
    int nx = l[0] + dx, ny = l[1] + dy, nz = l[2] + dz;
    u64 k = key(nx, ny, nz);
    if (ls.count(k)) continue;
    BlockId id = w.getBlock(nx, ny, nz);
    if (bhas(id, BF::IsLeaves)) { ls.insert(k); leaves.push_back({nx, ny, nz, id}); }
  }
  if (leaves.size() < 4) {
    // leafless "watcher" trees still fall as trees: a single straight trunk on soil
    bool bare = false;
    if (logs.size() >= 3 && leaves.empty() && p.realm == REALM_MAIN && dimOfY(y) == DIM_OVERWORLD) {
      bool one = true; int minY = logs[0][1];
      for (auto& l : logs) { if (l[0] != logs[0][0] || l[2] != logs[0][2]) one = false; minY = std::min(minY, l[1]); }
      BlockId g = w.getBlock(logs[0][0], minY - 1, logs[0][2]);
      const KnownBlocks& B = KB();
      bare = one && g && (g == B.grass || g == B.dirt || g == B.snowy_grass);
    }
    if (!bare) return;
  }
  ok = true;
}
void PlayerController::fellTree(int x, int y, int z, const std::vector<std::array<int, 4>>& logs, const std::vector<std::array<int, 4>>& leaves) {
  World& w = world();
  const Registry& R = reg();
  std::map<std::string, int> counts, extras;
  std::vector<std::array<int, 4>> blocks;
  for (auto& l : logs) { counts[R.block((BlockId)l[3]).key]++; blocks.push_back(l); }
  for (auto& l : leaves) {
    if (blocks.size() < 360) blocks.push_back(l);
    // the sapling of the tree it came from, so every kind can be grown back
    if (rnd() < 0.05) extras[R.block((BlockId)l[3]).str("sapling", "sapling")]++;
    if (l[3] == KB().leaves && rnd() < 0.02) extras["apple"]++;
    if (const std::string fr = R.block((BlockId)l[3]).str("fruit"); !fr.empty() && rnd() < 0.12) extras[fr]++;   // BTA's flowering cherry leaves: cherries
    { static const BlockId palmLeaves = R.blockId("palm_leaves"); if (palmLeaves && l[3] == palmLeaves && rnd() < 0.06) extras["coconut"]++; }   // a felled palm drops its coconuts
    if (rnd() < 0.015) extras["stick"]++;
  }
  for (auto& l : logs) w.setBlock(l[0], l[1], l[2], AIR, 0, SetFlags::Urgent);
  for (auto& l : leaves) w.setBlock(l[0], l[1], l[2], AIR, 0, SetFlags::Urgent);
  std::vector<ItemStack> drops;
  for (auto& [k, n] : counts) { int left = n; while (left > 0) { int s = std::min(64, left); drops.push_back(ItemStack(k, s)); left -= s; } }
  for (auto& [k, n] : extras) if (R.item(k)) drops.push_back(ItemStack(k, n));
  float dirYaw = std::atan2(-(x + 0.5f - p.pos.x), -(z + 0.5f - p.pos.z));   // fall away from the chopper
  spawnFallingTree(lv, p.realm, blocks, ivec3(x, y, z), dirYaw, drops);
  sfxMaterial(lv, "wood", "break", vec3(x, y, z));
  wearHeld(lv, p, (int)std::ceil(logs.size() / 2.0));
  p.stats["blocksMined"] = p.stats.value("blocksMined", 0) + (int)logs.size();
  toastMsg("Timber! " + std::to_string(logs.size()) + " logs", "good");
}

// ---- using --------------------------------------------------------------------------------------
void PlayerController::useOffhand(bool repeat, const PlayerInput& in) {
  std::swap(p.inv[p.selected], p.offhand[0]);
  use(repeat, in, true);
  std::swap(p.inv[p.selected], p.offhand[0]);
}

static bool interactable(BlockId id) {
  const BlockDef& b = reg().block(id);
  static const char* keys[] = {"anvil", "enchant", "brewing", "grindstone", "loom", "smithing", "cartography", "fletching", "composter", "cauldron", "lectern", "barrel", "chest", "furnace", "smoker", "crafting", "table", "bench", "bed", "door", "gate", "trapdoor", "bell", "note", "jukebox", "shelf", "bookcase", "campfire", "cooking", "altar", "forge", "press", "mill", "rack", "stand"};
  for (const char* k : {"container", "table", "furnace", "lectern", "station", "enderChest", "partyChest", "machine", "stonecutter", "jukebox", "bed", "door", "trapdoor", "noteBlock", "bellBlock", "cauldron", "pile", "placedBackpack", "frame", "sign", "pot"}) if (b.has(k)) return true;
  if (reg().tShape[id] == ShapeKind::Gate) return true;
  for (const char* k : keys) if (b.key.find(k) != std::string::npos) return true;
  return false;
}

void PlayerController::use(bool repeat, const PlayerInput& in, bool fromOff) {
  // a held sneak key/button always counts when using: flying (creative) and gliding clear p.sneaking (there sneak
  // means "down"), so a block in hand would open the chest instead of going against it (furnish.js)
  struct SneakScope { bool& r; bool old; ~SneakScope() { r = old; } } sneakScope{p.sneaking, p.sneaking};
  if (in.sneak && !p.riding) p.sneaking = true;
  World& w = world();
  const Registry& R = reg();
  const RayHit* hit = target.hit ? &target : nullptr;
  ItemStack& held = p.held();
  const ItemDef* def = held.empty() ? nullptr : R.item(held.id);
  Entity* te = targetEnt ? lv.entity(targetEnt) : nullptr;
  // EventBus "itemUse" [player, item ("" = empty hand), x, y, z, face] for every use, before any default block or
  // item interaction (portals, redstone levers and buttons, minecarts, tables hook it); false = used up
  if (lv.events && (!repeat || (def && !def->block.empty()))) {
    int face = -1;
    if (hit) { ivec3 n = hit->normal; face = n.x > 0 ? 0 : n.x < 0 ? 1 : n.y > 0 ? 2 : n.y < 0 ? 3 : n.z > 0 ? 4 : n.z < 0 ? 5 : -1; }
    json a = json::array({p.name, held.empty() ? std::string() : held.id, hit ? hit->block.x : 0, hit ? hit->block.y : 0, hit ? hit->block.z : 0, face});
    if (!lv.events->emit("itemUse", a)) { swingArm(); return; }
    def = held.empty() ? nullptr : R.item(held.id);   // a handler may have changed the stack
  }
  // quenching (sim/heat.h): something hot used on water (a source, a cauldron) or held in it is cooled at once
  if (!fromOff && !repeat && !held.empty() && itemHeat(held) > 100) {
    bool water = p.inWater || p.contacts.water;
    if (hit) { const ivec3 n = hit->block + hit->normal; water = water || reg().tLiquid[w.getBlock(n.x, n.y, n.z)] == 1 || reg().tLiquid[hit->id] == 1 || (reg().block(hit->id).has("cauldron") && (w.getMeta(hit->block.x, hit->block.y, hit->block.z) & 3)); }
    if (water && quench(held)) {
      const vec3 at = hit ? vec3(hit->block) + vec3(0.5f, 1, 0.5f) : p.pos + vec3(0, 1, 0);
      lv.fx->sound("fizz", at, 16); lv.fx->particles("campfire_smoke", at, 10, {{"spread", 0.3}, {"vel", {0, 1.4, 0}}, {"life", 1.2}, {"size", 0.14}, {"rise", true}});
      swingArm(); return;
    }
  }
  // scraping a soaked hide (in the other hand) with a knife (sim/barrel.h)
  if (!fromOff && !repeat && def && def->has("knife") && p.offhand[0].id == "soaked_hide" && scrapeHide(p.offhand[0], held)) { swingArm(); return; }
  // the prospector's pick (sim/prospect.h): tapped on rock, what ore is near
  if (!fromOff && !repeat && hit && def && def->has("propick")) { toastMsg(prospect(w, hit->block.x, hit->block.y, hit->block.z)); wearHeld(lv, p, 1); swingArm(); return; }
  // smithing (sim/anvil.h): use an anvil with a hammer in hand (the hot piece in the other) opens the anvil's work; a hammer
  // used on the top of raw stone makes a stone anvil there
  if (!fromOff && !repeat && hit && def && def->has("hammer") && immersive(lv)) {   // (immersive worlds: a classic anvil repairs as ever)
    const ivec3 b = hit->block;
    const int tier = anvilTierOfBlock(reg().block(hit->id).key);
    if (tier >= 0) { openScreen(lv, p, "anvil_smith", {{"pos", {b.x, b.y, b.z}}, {"tier", tier}}); swingArm(); return; }
    static const std::unordered_set<std::string> RAW = {"stone", "granite", "diorite", "andesite", "basalt", "deepslate", "limestone", "marble", "tuff", "blackstone"};
    if (hit->normal.y == 1 && RAW.count(reg().block(hit->id).key) && reg().blockId("stone_anvil")) {
      w.setBlock(b.x, b.y, b.z, reg().blockId("stone_anvil")); wearHeld(lv, p, 1); swingArm(); sfxMaterial(lv, "stone", "hit", vec3(b) + vec3(0.5f));
      toastMsg("A stone anvil: use it with your hammer, hot metal in your other hand");
      return;
    }
  }
  // the forge (sim/forge.h): sneak + use charcoal into a stone-ringed pit makes one; on it charcoal feeds it, metal and
  // vessels go in to heat, an empty hand takes the last one out
  // what a station hands back goes in the bag, or at your feet when the bag is full (never lost)
  auto takeOut = [&](ItemStack s) { if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, (const vec3*)nullptr); } };
  if (!fromOff && !repeat && hit) {
    const ivec3 b = hit->block;
    if (reg().block(hit->id).key == "sluice" && held.empty()) { ItemStack f = sluiceTake(w, b.x, b.y, b.z); if (!f.empty()) { takeOut(f); swingArm(); } return; }   // sim/panning.h
    if (reg().block(hit->id).key == "sealed_barrel") {   // sim/barrel.h: water by the bucket, things in, sneak seals
      if (p.sneaking) { if (!barrelSeal(lv, w, b.x, b.y, b.z, true)) barrelSeal(lv, w, b.x, b.y, b.z, false); swingArm(); toastMsg(barrelLiquid(w, b.x, b.y, b.z).empty() ? "Empty" : "Holding " + barrelLiquid(w, b.x, b.y, b.z)); return; }
      if (!held.empty() && (held.id == "water_bucket" || held.id == "bucket")) { if (barrelPour(w, b.x, b.y, b.z, held)) swingArm(); return; }
      if (!held.empty()) { if (barrelPut(w, b.x, b.y, b.z, held)) swingArm(); else toastMsg("It's sealed, or full"); return; }
      ItemStack s = barrelTake(w, b.x, b.y, b.z); if (!s.empty()) { takeOut(s); swingArm(); }
      return;
    }
    if (reg().block(hit->id).key == "bloomery") {   // sim/bloomery.h: iron ore and charcoal in; an empty hand takes a bloom
      std::string why;
      if (!held.empty()) { if (bloomeryLoad(w, b.x, b.y, b.z, held, &why)) swingArm(); else if (!why.empty()) toastMsg(why); return; }
      ItemStack bl = bloomeryTake(w, b.x, b.y, b.z);
      if (!bl.empty()) { takeOut(bl); swingArm(); } else toastMsg("Iron ore and as much charcoal; light it with a firestarter");
      return;
    }
    if (reg().block(hit->id).key == "bellows" && held.empty() && bellowsPump(w, b.x, b.y, b.z)) { swingArm(); sfxMaterial(lv, "wool", "step", vec3(b) + vec3(0.5f)); return; }   // pumped by hand
    if (reg().block(hit->id).key == "forge") {
      if (!held.empty() && held.id == "charcoal") { if (!forgeAddFuel(w, b.x, b.y, b.z, held)) toastMsg("The forge is full of charcoal"); else swingArm(); return; }
      if (!held.empty() && heatable(held.id)) { if (!forgePut(w, b.x, b.y, b.z, held)) toastMsg("There's no more room in the forge"); else swingArm(); return; }
      if (held.empty()) { ItemStack s = forgeTake(w, b.x, b.y, b.z); if (!s.empty()) { takeOut(s); swingArm(); } return; }
    }
    if (immersive(lv) && p.sneaking && hit->normal.y == 1 && !held.empty() && held.id == "charcoal" && forgeBuild(w, b.x, b.y + 1, b.z, held)) { swingArm(); sfxMaterial(lv, "stone", "place", vec3(b) + vec3(0.5f, 1.1f, 0.5f)); return; }
  }
  // the pit kiln (sim/pit_kiln.h): sneak + use with unfired pottery on the ground sets it down; on a kiln straw, then logs,
  // go on; an empty hand takes a piece back
  if (!fromOff && !repeat && hit) {
    const ivec3 b = hit->block;
    if (hit->id && reg().block(hit->id).has("kiln")) {
      std::string why;
      if (!held.empty() && isUnfired(held.id) && p.sneaking) { if (kilnPlace(w, b.x, b.y, b.z, std::max(0, targetQuarter()), held)) swingArm(); return; }
      if (!held.empty() && (held.id == "straw" || reg().itemMatches(held.id, "#log"))) {
        if (kilnAdd(w, b.x, b.y, b.z, held, &why)) { swingArm(); sfxMaterial(lv, held.id == "straw" ? "grass" : "wood", "place", vec3(b) + vec3(0.5f)); }
        else if (!why.empty()) toastMsg(why);
        return;
      }
      if (held.empty()) { ItemStack s = kilnTake(w, b.x, b.y, b.z, targetQuarter()); if (!s.empty()) { takeOut(s); swingArm(); } return; }
    }
    if (p.sneaking && hit->normal.y == 1 && !held.empty() && isUnfired(held.id)) {
      if (kilnPlace(w, b.x, b.y + 1, b.z, std::max(0, targetQuarter()), held)) { swingArm(); sfxMaterial(lv, "gravel", "place", vec3(b) + vec3(0.5f, 1.1f, 0.5f)); }
      return;
    }
  }
  // ingot and log piles (sim/piles.h): sneak + use with an ingot or a log on the ground sets one down, on a pile adds one;
  // use on a pile takes one back
  if (!fromOff && !repeat && hit) {
    const PileKind pk = pileKindOfBlock(hit->id);
    const ivec3 b = hit->block;
    std::string why;
    if (pk != PileKind::None) {
      const char* mat = pk == PileKind::Ingots ? "metal" : "wood";
      if (p.sneaking && !held.empty() && pileKindOfItem(held.id) == pk) {
        if (pileAdd(w, b.x, b.y, b.z, held, &why)) { swingArm(); sfxMaterial(lv, mat, "place", vec3(b) + vec3(0.5f)); }
        else if (!why.empty()) toastMsg(why);
        return;
      }
      ItemStack one = pileTake(w, b.x, b.y, b.z);
      if (!one.empty() && giveItem(lv, p, one) > 0) {   // no room: back on the pile
        if (w.getBlock(b.x, b.y, b.z) == AIR) pilePlace(w, b.x, b.y, b.z, one); else pileAdd(w, b.x, b.y, b.z, one);
        toastMsg("No room to pick that up", "warn");
      }
      swingArm();
      return;
    }
    if (immersive(lv) && p.sneaking && hit->normal.y == 1 && !held.empty() && pileKindOfItem(held.id) != PileKind::None) {   // (a classic world places a log as ever)
      const bool ingots = pileKindOfItem(held.id) == PileKind::Ingots;
      if (pilePlace(w, b.x, b.y + 1, b.z, held, &why)) { swingArm(); sfxMaterial(lv, ingots ? "metal" : "wood", "place", vec3(b) + vec3(0.5f, 1.1f, 0.5f)); return; }
      if (ingots) { if (!why.empty()) toastMsg(why); return; }   // (a log that can't make a pile is placed as a block below)
    }
  }
  // furnish.js: crouched, a block in hand goes against a chest, door, table... instead of opening it
  if (!fromOff && !repeat && p.sneaking && hit && def && !def->block.empty() && R.blockId(def->block) && !def->has("backpack") && !te) {
    if (interactable(hit->id) && !R.block(hit->id).has("pile")) { placeBlock(*hit, *def, held); return; }
  }
  // crouched, any item that isn't a block is set down on top of what you look at: the ground, a furnace, a table, a chest
  // (Vintage Story's way) -- before the item's own use or the block's. Items whose crouched use is aimed at the block keep it.
  if (!fromOff && !repeat && p.sneaking && hit && hit->normal.y == 1 && def && def->block.empty() && !te && !R.block(hit->id).has("pile")) {
    bool aimed = false;
    for (const char* k : {"bucket", "igniter", "plants", "bonemeal", "summon", "paintbrush", "cart", "boat", "disc", "backpack"}) if (def->has(k)) aimed = true;
    if (!aimed && held.id != "firestarter" && putDown(*hit, held)) return;
  }
  if (!fromOff && !repeat && te) for (auto& h : hooks().useEntity) if (h(*this, *te, held.empty() ? nullptr : &held, def)) return;
  // Carry On: sneak + use with an empty hand picks up containers, stations and animals
  if (!fromOff && !repeat && p.sneaking && held.empty()) {
    if (te && isMob(*te) && te->hp > 0 && R.mobs[te->type].value("carry", false)) {
      p.carrying = {{"kind", "mob"}, {"type", te->type}, {"hp", te->hp}, {"data", te->data}};
      te->removed = true; sfxUi(lv, p, te->type == "boar" ? "boar" : "pop"); swingArm();
      toastMsg("Carrying a " + te->type + " \xE2\x80\x94 use to set it down", "good");
      return;
    }
    if (hit && bhas(hit->id, BF::Carry)) { pickUpBlock(hit->block.x, hit->block.y, hit->block.z); return; }
  }
  if (hit && repeat && !fromOff && R.block(hit->id).has("sieve") && useBlockAt(*hit, held.empty() ? nullptr : &held, def)) return;   // hold use to keep sifting
  if (hit && !repeat && !fromOff) {
    const BlockDef& b = R.block(hit->id);
    if (b.has("pile") && !fromOff && !repeat && metalworkPourOnGround(lv, p, w, hit->block.x, hit->block.y, hit->block.z)) { swingArm(); return; }   // a molten vessel into molds set down there (sim/metalwork.h)
    if (b.has("pile")) { usePile(*hit, held.empty() ? nullptr : &held); return; }
    if (b.has("placedBackpack")) {
      if (p.sneaking && held.empty()) {
        json* e = w.blockEntity(hit->block.x, hit->block.y, hit->block.z);
        ItemStack st = e && e->contains("stack") ? ItemStack::fromJson((*e)["stack"]) : ItemStack("leather_backpack", 1);
        if (p.held().empty()) p.held() = st; else if (p.give(st) > 0) { toastMsg("No room for the backpack", "warn"); return; }
        w.setBlock(hit->block.x, hit->block.y, hit->block.z, AIR);
        sfxMaterial(lv, "wool", "break", vec3(hit->block)); swingArm();
        return;
      }
      openScreen(lv, p, "container", {{"pos", {hit->block.x, hit->block.y, hit->block.z}}}); return;
    }
    bool sneakPlace = p.sneaking && def && (!def->block.empty() || def->has("backpack"));
    if (!sneakPlace) {
      bool uba = useBlockAt(*hit, held.empty() ? nullptr : &held, def);
      if (uba) return;
      json pos = {hit->block.x, hit->block.y, hit->block.z};
      if (bhas(hit->id, BF::Container)) { swingArm(); sfxAt(lv, "chest", vec3(hit->block) + vec3(0.5f)); containerEntity(w, hit->block.x, hit->block.y, hit->block.z); openScreen(lv, p, "container", {{"pos", pos}}); return; }
      if (b.has("table") && immersive(lv) && worktableUse(lv, p, w, *hit, held)) { swingArm(); return; }   // sim/worktable.h: the top worked in the world
      if (b.has("table")) { swingArm(); openScreen(lv, p, "table"); return; }
      if (bhas(hit->id, BF::Furnace)) { swingArm(); containerEntity(w, hit->block.x, hit->block.y, hit->block.z); openScreen(lv, p, "furnace", {{"pos", pos}, {"kind", binfo(hit->id).furnace}}); return; }
      if (b.has("harvest")) {
        swingArm(); sfxMaterial(lv, "grass", "dig", vec3(hit->block));
        dropItem(lv, p.realm, ItemStack(b.str("harvest"), 2 + (rnd() < 0.5 ? 1 : 0)), vec3(hit->block) + vec3(0.5f, 0.6f, 0.5f), true, 0.f);
        w.setBlock(hit->block.x, hit->block.y, hit->block.z, KB().berry_bush_empty);
        return;
      }
    }
  }
  // a block in hand with nothing to put it on: drink or eat what's in the other hand (itemfix.js)
  const ItemDef* offDef = p.offhand[0].empty() ? nullptr : R.item(p.offhand[0].id);
  auto consumable = [](const ItemDef* d) { return d && (d->has("food") || d->has("drink") || d->has("potion")) && d->block.empty(); };
  if (!repeat && !fromOff && def && !def->block.empty() && !hit && !te && consumable(offDef)) { useOffhand(repeat, in); return; }
  // nothing useful in the main hand: the off hand acts (a torch while holding a pickaxe)
  if ((held.empty() || (def && !itemHasUse(*def))) && !p.offhand[0].empty() && !fromOff) { useOffhand(repeat, in); return; }
  if (held.empty() || !def) return;
  if (!fromOff && held.id == "firestarter" && firestarterUse(lv, p, hit, repeat)) { if (!repeat) swingArm(); return; }   // hold use: sim/firestarter.h
  if (!fromOff && hit && quernUse(lv, p, *hit, repeat)) { swingArm(); return; }   // hold use: sim/quern.h
  if (!fromOff && hit && panUse(lv, p, *hit, repeat)) { if (!repeat) swingArm(); return; }   // hold use: sim/panning.h
  if (!fromOff && spindleUse(lv, p, repeat)) return;   // hold use: sim/spinning.h
  if (!fromOff && !repeat && metalworkUse(lv, p)) { swingArm(); return; }   // sim/metalwork.h: vessels and molds (on a fire the camping code sets them on it first)
  if (repeat && def->block.empty()) return;
  // knapping (sim/knapping.h), immersive worlds: use in the air with stones, flint (2 or more) or clay (5) in hand; sneak + use
  // keeps the old ways (a pebble thrown)
  if (!repeat && !fromOff && !hit && !te && !p.sneaking && immersive(lv)) if (const std::string km = knapMaterialOf(held.id); !km.empty()) {   // (classic: a pebble is thrown as ever)
    int have = 0; for (auto& s : p.inv) if (s.id == held.id) have += s.n;
    if (have >= knapNeeds(km)) { openScreen(lv, p, "knapping", {{"material", km}, {"item", held.id}}); return; }
  }
  for (auto& h : hooks().useItem) if (h(*this, held, *def, repeat)) return;
  // sneak + use with an item that isn't a block sets it down on the ground
  if (!repeat && p.sneaking && hit && hit->normal.y == 1 && def->block.empty()) {
    if (def->has("backpack")) {
      int x = hit->block.x, y = hit->block.y + 1, z = hit->block.z;
      if (!R.tReplaceable[w.getBlock(x, y, z)] || !KB().backpack_block) return;
      w.setBlock(x, y, z, KB().backpack_block);
      w.setBlockEntity(x, y, z, {{"type", "backpack"}, {"stack", held.toJson()}, {"yaw", p.yaw}});
      held.clear(); swingArm(); sfxMaterial(lv, "wool", "place", vec3(x, y, z));
      return;
    }
    if (putDown(*hit, held)) return;
  }
  // "drink: milk" was the original's hook for many usable items (fireworks, pearls, potions, snowballs...): their own
  // systems take them through "itemUse" first; only real milk refreshes you and leaves the bucket
  if (def->str("drink") == "milk" && held.id.find("milk") != std::string::npos) { p.burn = 0; p.slow = 0; heldBecomes(ItemStack("bucket", 1)); p.eatAnim = 0.45f; sfxUi(lv, p, "eat"); toastMsg("Refreshing", "good"); return; }
  if (def->has("heartCrystal")) {
    if (repeat) return;
    if (p.heartCrystals >= 10) { toastMsg("You can't hold any more hearts"); return; }
    p.heartCrystals++; p.recalcMax(); p.heal(2);
    if (--held.n <= 0) held.clear();
    sfxUi(lv, p, "levelup"); toastMsg("One more heart \xE2\x80\x94 " + std::to_string((int)(p.maxHp / 2)) + " in all", "epic"); swingArm();
    return;
  }
  if (def->has("plants") && hit && plantItem(*def, *hit, held)) return;
  if (def->has("food")) { if (eatFood(held, *def, repeat)) return; if (def->block.empty()) return; }
  if (def->has("backpack")) { openScreen(lv, p, "container", {{"held", true}}); return; }
  if (def->has("bucket")) { if (!bucketWaterlog(*def, held)) useBucket(*def, held); return; }
  if (def->str("igniter") == "flint") { useFlint(hit, held); return; }
  if (def->has("bonemeal") && hit) { useBonemeal(*hit, held); return; }
  if (def->has("igniter")) {
    if (hit && hooks().ignitePortal && hooks().ignitePortal(w, hit->block.x, hit->block.y, hit->block.z, "ember")) { sfxUi(lv, p, "portal"); toastMsg("The portal roars open", "boss"); swingArm(); }
    else if (!repeat) toastMsg("Use the igniter on the inside of an obsidian frame (at least 2 wide, 3 tall)");
    return;
  }
  if (def->has("cape")) { std::swap(p.cape[0], held); sfxUi(lv, p, "equip"); toastMsg("Wearing " + def->name, "good"); return; }
  if (def->props.contains("armor") && def->props["armor"].is_object()) {
    int slot = clampv(def->props["armor"].value("slot", 0), 0, 3);
    std::swap(p.armor[slot], held); sfxUi(lv, p, "equip"); toastMsg("Equipped " + def->name, "good");
    return;
  }
  if (!def->block.empty() && hit) placeBlock(*hit, *def, held);
}

// Eating: half the healing at once and the rest over a few seconds, a moment between bites
// (fairplay.js); food with an effect can be eaten at full health (itemfix.js). Returns true if handled.
bool PlayerController::eatFood(ItemStack& held, const ItemDef& def, bool repeat) {
  const json& food = def.props["food"];
  bool effectFood = def.block.empty() && (food.contains("effect") || food.contains("cure"));
  if (p.hp >= p.maxHp) {
    if (!def.block.empty()) return false;   // a block you could eat: place it instead
    if (!repeat && effectFood && p.foodCd <= 0) {
      if (--held.n <= 0) held.clear();
      p.eatAnim = 0.45f; sfxUi(lv, p, held.id.find("bottle") != std::string::npos || held.id.find("stew") != std::string::npos || held.id.find("soup") != std::string::npos ? "drink" : "eat");
      p.foodCd = FOOD_CD;
      if (lv.events) lv.events->emit("foodEffect", json::array({p.name, def.key, food.value("effect", json()), food.value("cure", json())}));
      if (def.has("bowl") || def.has("returns")) { ItemStack bw(def.str("returns", "bowl"), 1); if (held.empty()) held = bw; else giveItem(lv, p, bw); }
      return true;
    }
    const ItemStack& off = p.offhand[0];
    const ItemDef* od = off.empty() ? nullptr : reg().item(off.id);
    if (!repeat && od && itemHasUse(*od)) { PlayerInput none; useOffhand(false, none); return true; }
    if (!repeat) toastMsg("You are already at full health");
    return true;
  }
  if (p.foodCd > 0) { if (!repeat && !chewTip) { chewTip = true; toastMsg("Still chewing \xE2\x80\x94 food heals over a few seconds"); } return true; }
  float heal = food.value("heal", 0.0f), now = heal > 1 ? heal / 2 : heal;
  if (heal > 1) p.foodPool += heal - now;
  float healed = p.heal(now);
  std::string icon = def.icon;
  if (--held.n <= 0) held.clear();
  p.eatAnim = 0.45f; sfxUi(lv, p, "eat"); sfxUi(lv, p, "heal");
  p.foodCd = FOOD_CD;
  vec3 L = lookVector(p.yaw, p.pitch);
  burst(lv, p.pos + vec3(L.x * 0.4f, p.eye - 0.25f, L.z * 0.4f), icon, 8, 0.6f);
  char buf[32]; snprintf(buf, sizeof buf, "+%g \xE2\x99\xA5", healed / 2);
  floatText(lv, p, p.pos + vec3(L.x * 1.2f, p.eye + 0.1f, L.z * 1.2f), buf, "#ff8a8a");
  if (food.contains("effect") || food.contains("cure")) if (lv.events) lv.events->emit("foodEffect", json::array({p.name, def.key, food.value("effect", json()), food.value("cure", json())}));
  if (def.has("bowl") || def.has("returns")) { ItemStack bw(def.str("returns", "bowl"), 1); if (p.held().empty()) p.held() = bw; else giveItem(lv, p, bw); }
  return true;
}

// Middle click: find this block in the bag and put it in hand (creative: take a stack).
void PlayerController::pickBlock(const RayHit& hit) {
  const Registry& R = reg();
  const BlockDef& b = R.block(hit.id);
  std::string key = R.item(b.key) ? b.key : (!b.drop.empty() && R.item(b.drop) ? b.drop : "");
  if (b.has("unlit")) key = b.str("unlit");
  if (b.key == "lit_furnace") key = "furnace";
  if (key.empty()) return;
  for (int i = 0; i < 9; i++) if (!p.inv[i].empty() && p.inv[i].id == key) { p.selected = i; return; }
  for (int i = 0; i < INV_MAIN; i++) if (!p.inv[i].empty() && p.inv[i].id == key) { std::swap(p.inv[p.selected], p.inv[i]); sfxUi(lv, p, "click"); return; }
  if (p.creative()) {
    for (int i = 0; i < 9; i++) if (p.inv[i].empty()) { p.selected = i; break; }
    p.held() = ItemStack(key, maxStackOf(key));
  }
}

void PlayerController::dropHeld(bool all) {
  ItemStack& s = p.held();
  if (s.empty()) return;
  int n = all ? s.n : 1;
  ItemStack out = s; out.n = n;
  s.n -= n; if (s.n <= 0) s.clear();
  throwStack(out);
}
void PlayerController::throwStack(ItemStack s) {
  vec3 L = lookVector(p.yaw, p.pitch);
  vec3 vel = L * 6.f + vec3(0, 1.5f, 0);
  dropItem(lv, p.realm, s, p.pos + vec3(L.x * 0.4f, p.eye - 0.3f + L.y * 0.4f, L.z * 0.4f), false, 0, &vel, 1.5f);
  swingArm();
}

void PlayerController::respawn() { respawnPlayer(lv, p); mining.active = false; }

// (enchanting.h and mob_combat.h redeclare things gameplay.h has; these are all the bow needs)
float enchArrowDamageFactor(const ItemStack& bow);
int enchArrowPunch(const ItemStack& bow);
bool enchArrowFlame(const ItemStack& bow);
bool enchInfinity(const ItemStack& bow);
void igniteEntity(Entity& e, float t);
void primeTNT(Level& lv, World& w, int x, int y, int z, float fuse);   // sim/blockfx.cpp

// ---- bow and arrows (blocks2.js) ----------------------------------------------------------------
class ArrowEntity : public Entity {
public:
  ArrowEntity() { type = "arrow"; w = 0.05f; h = 0.05f; noGravity = true; }
  void aim() { float s = std::hypot(vel.x, vel.z); if (s > 0.01f || std::fabs(vel.y) > 0.01f) { yaw = std::atan2(-vel.x, -vel.z); pitch = std::atan2(vel.y, s); } }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& w = lv.world(realm);
    bool stuck = data.value("stuck", false);
    if (stuck) {
      if (age > 60) { removed = true; return; }
      if (!reg().tSolid[w.getBlock(data.value("bx", 0), data.value("by", 0), data.value("bz", 0))]) { data["stuck"] = false; vel = vec3(0); return; }
      if (data.value("pickup", false)) for (Player* pl : playersIn(lv, realm)) {
        if (!pl->dead && std::fabs(pl->pos.x - pos.x) < 1.6f && std::fabs(pl->pos.z - pos.z) < 1.6f && pos.y > pl->pos.y - 1.2f && pos.y < pl->pos.y + 2.8f) {   // Minecraft: the player box grown by 1
          const std::string kind = data.value("item", std::string("arrow"));   // the arrow you shot comes back (golden, flaming...)
          if (pl->give(ItemStack(kind, 1)) <= 0) { removed = true; sfxUi(lv, *pl, "pop"); if (lv.events) lv.events->emit("pickup", json::array({pl->name, kind, 1})); return; }
        }
      }
      return;
    }
    if (age > 30) { removed = true; return; }
    u32 owner = data.value("owner", 0u);
    const float grav = data.value("grav", 1.0f);   // BTA's golden arrow barely drops
    if (data.value("fire", false) && (int)(age * 20) % 2 == 0) lv.fx->particles("flame", pos, 1);   // the flaming arrow
    for (int s = 0; s < 4; s++) {
      float h2 = dt / 4;
      vel.y -= 20 * grav * h2;
      float drag = std::max(0.f, 1 - h2 * 0.25f);
      vel *= drag;
      vec3 n = pos + vel * h2;
      const bool glanced = data.value("glanced", false);
      for (auto& e : lv.entities()) {
        if (glanced) break;   // turned aside by a Wither's hardened bones: it only falls now
        if (e->removed || e->realm != realm || e->id == owner || e->hp <= 0) continue;
        bool mob = isMob(*e), player = e->isPlayer();
        if (!mob && !(player && !(owner && lv.entity(owner) && lv.entity(owner)->isPlayer()))) continue;
        if (player && static_cast<Player*>(e.get())->dead) continue;
        if (n.x > e->pos.x - e->w - 0.1f && n.x < e->pos.x + e->w + 0.1f && n.z > e->pos.z - e->w - 0.1f && n.z < e->pos.z + e->w + 0.1f && n.y > e->pos.y && n.y < e->pos.y + e->h) {
          float speed = glm::length(vel);
          DamageSource src; src.attacker = owner; src.knock = 3 * (1 + 1.2f * data.value("punch", 0)); src.tool = "arrow"; src.type = "arrow";   // Punch (enchanting.js)
          src.ench = data.value("ench", 0);
          Entity* o = owner ? lv.entity(owner) : nullptr;
          // the Ranger's path (Eagle Eye, Deadeye at range, Poisoned Tips: sim/villager_quests.cpp)
          float pk = o && o->isPlayer() ? pathArrowFactor(*static_cast<Player*>(o), glm::distance(o->pos, e->pos)) : 1.f;
          float dealt = damageEntity(lv, *e, std::max(1.f, std::round(data.value("damage", 3.0f) * pk * speed / 30)), src);
          if (dealt <= 0 && (e->type == "wither" || e->type == "wither_storm") && e->hp > 0) {
            // below half its strength the Wither's bones harden: the arrow glances off (it doesn't vanish into it)
            vel = vec3(-vel.x * 0.25f, 2.5f + std::fabs(vel.y) * 0.2f, -vel.z * 0.25f);
            data["glanced"] = true;
            lv.fx->particles("crit", n, 8);
            lv.fx->material("metal", "place", n);
            if (o && o->isPlayer() && !e->data.value("toldArmor", false)) {
              e->data["toldArmor"] = true;
              toast(lv, *static_cast<Player*>(o), "Its bones have hardened: arrows glance off. Strike it, or hit its skulls back!", "info");
            }
            return;
          }
          if (dealt > 0 && o && o->isPlayer() && !e->isPlayer() && pathArrowPoisons(*static_cast<Player*>(o))) poisonCreature(*e, 4);
          if ((data.value("flame", false) || data.value("fire", false)) && !e->isPlayer()) igniteEntity(*e, data.value("fire", false) ? 8 : 5);
          if (dealt > 0 && o && o->isPlayer()) { char buf[16]; snprintf(buf, sizeof buf, "%g", std::round(dealt * 10) / 10); floatText(lv, *static_cast<Player*>(o), e->pos + vec3(0, e->h + 0.2f, 0), buf, "#ffffff"); }
          sfxAt(lv, "hit", e->pos);
          removed = true; return;
        }
      }
      int bx = ifloor(n.x), by = ifloor(n.y), bz = ifloor(n.z);
      BlockId id = w.getBlock(bx, by, bz);
      if (reg().tSolid[id]) {
        bool hitIt = true;
        static thread_local std::vector<PixBox> bs;
        if (hasShapeBoxes(id) && worldBoxes(w, bx, by, bz, id, SHAPE_COLLIDE, bs)) {
          hitIt = false;
          for (auto& b : bs) if (n.x >= bx + b.x0 / 16 && n.x <= bx + b.x1 / 16 && n.y >= by + b.y0 / 16 && n.y <= by + b.y1 / 16 && n.z >= bz + b.z0 / 16 && n.z <= bz + b.z1 / 16) hitIt = true;
        }
        if (hitIt) {
          if (data.value("golden", false)) { removed = true; lv.fx->particles("burst", pos, 6, {{"tile", "gold_block"}, {"speed", 1.2f}}); return; }   // a golden quiver's arrow breaks where it lands
          if (data.value("breaks", false)) { removed = true; lv.fx->particles("burst", pos, 4, {{"tile", "amethyst_block"}, {"speed", 1.f}}); sfxAt(lv, "glass_break", n); return; }   // BTA's purple arrow shatters
          if (data.value("fire", false)) {   // BTA's flaming arrow lights what it strikes: TNT, a brazier, a candle, a campfire
            const BlockDef& hb = reg().block(id);
            if (hb.key == "tnt") { w.setBlock(bx, by, bz, 0, 0); primeTNT(lv, w, bx, by, bz, 4.f); }
            else if (hb.has("lightMeta") || hb.key == "brazier" || hb.has("campfire")) { w.setBlock(bx, by, bz, id, (u8)(w.getMeta(bx, by, bz) | 4)); }
            else if (w.getBlock(bx, by + 1, bz) == 0 && reg().blockId("fire")) w.setBlock(bx, by + 1, bz, reg().blockId("fire"), 0);
          }
          aim(); data["stuck"] = true; data["bx"] = bx; data["by"] = by; data["bz"] = bz; age = 0; vel = vec3(0);
          sfxAt(lv, "arrow_hit", n);
          return;
        }
      }
      pos = n;
    }
    aim();
  }
};

void PlayerController::updateHeldTools(float dt, const PlayerInput& in) {
  const ItemStack& held = p.held();
  const ItemDef* def = held.empty() ? nullptr : reg().item(held.id);
  bool playing = !p.dead && p.carrying.is_null();
  // the spyglass: up to your eye while use is held (client/play.cpp narrows the view to a tenth, ui/hud.cpp the round glass)
  const bool scope = playing && def && def->has("spyglass") && in.use;
  if (scope && !p.scoping) sfxUi(lv, p, "click");
  p.scoping = scope;
  if (playing && def && def->has("bow")) {
    const bool golden = goldenQuiver(p);
    // the arrows: the off hand's first, then the quiver's (the worn one, then one in the bag: whatever kind it holds),
    // then the first arrows in the bag (BTA's golden, flaming and purple arrows too)
    auto isArrow = [](const ItemStack& s) { return isArrowItem(s); };
    auto bagArrow = [&]() -> ItemStack* { for (auto& s : p.inv) if (isArrow(s)) return &s; return nullptr; };
    bool hasArrow = isArrow(p.offhand[0]) || bagArrow() || quiverArrows(p) > 0 || golden;
    if (in.use) {
      if (!hasArrow && !p.creative()) { if (in.usePressed) toastMsg("You need arrows"); bowPull = 0; }
      else { if (bowPull <= 0) sfxUi(lv, p, "bow_draw"); bowPull = std::min(1.25f, bowPull + dt); }   // a creak as you draw (bows.js)
    } else if (bowPull > 0) {
      if (bowPull > 0.12f) {
        float power = std::min(1.f, bowPull);
        bool goldShot = false;
        std::string kind = "arrow";
        if (isArrow(p.offhand[0])) { kind = p.offhand[0].id; if (!p.creative() && --p.offhand[0].n <= 0) p.offhand[0].clear(); }
        else if (std::string q = p.creative() ? std::string() : quiverTake(p); !q.empty()) kind = q;
        else if (ItemStack* b = bagArrow()) { kind = b->id; if (!p.creative() && --b->n <= 0) b->clear(); }
        else goldShot = golden && !p.creative();
        const ItemDef* ad = reg().item(kind);
        const json arrowProps = ad && ad->props.contains("arrow") && ad->props["arrow"].is_object() ? ad->props["arrow"] : json::object();
        vec3 L = lookVector(p.yaw, p.pitch); float sp = (8 + power * 30) * arrowProps.value("speed", 1.0f);
        auto a = std::make_unique<ArrowEntity>();
        a->realm = p.realm; a->pos = p.pos + vec3(L.x * 0.4f, p.eye - 0.1f + L.y * 0.4f, L.z * 0.4f); a->vel = L * sp;
        a->data = {{"owner", p.id}, {"damage", (3 + power * 6) * enchArrowDamageFactor(held) * (goldShot ? 0.8f : 1.f) * arrowProps.value("damage", 4.0f) / 4.f},
                   {"pickup", !p.creative() && !enchInfinity(held) && !goldShot && !arrowProps.value("breaks", false)}, {"item", kind}};
        if (arrowProps.value("gravity", 1.0f) != 1.0f) a->data["grav"] = arrowProps.value("gravity", 1.0f);
        if (arrowProps.value("fire", false)) a->data["fire"] = true;
        if (arrowProps.value("breaks", false)) a->data["breaks"] = true;
        if (goldShot) a->data["golden"] = true;
        if (int pu = enchArrowPunch(held)) a->data["punch"] = pu;
        if (enchArrowFlame(held)) a->data["flame"] = true;
        {   // the bow's enchantment levels, all told (a Wither Storm feels only an enchanted bow's arrows)
          int lv_ = 0;
          for (const char* k : {"en", "ench"}) if (auto it = held.tag.find(k); it != held.tag.end() && it->is_object()) for (auto& [n, v] : it->items()) lv_ += v.is_number() ? v.get<int>() : 1;
          if (lv_) a->data["ench"] = lv_;
        }
        a->aim();
        lv.spawn(std::move(a));
        if (enchInfinity(held) && !p.creative()) p.give(ItemStack(kind, 1));   // Infinity: the arrow isn't used up
        else if (!p.creative() && !goldShot && pathQuiverSaves(p)) p.give(ItemStack(kind, 1));   // Quiver Master
        sfxUi(lv, p, "bow");
        wearHeld(lv, p, 1);
      }
      bowPull = 0;
    }
  } else bowPull = 0;
}

// ---- cracks that mend --------------------------------------------------------------------------
struct DamageMark { int x, y, z; BlockId id; float p; double t; };
struct DamageMap { std::map<std::tuple<int, int, int>, DamageMark> m; double prevT = -1; };
static DamageMap& dmgOf(Level& lv) { return levelSlot<DamageMap>(lv, "cracks"); }
void rememberDamage(Level& lv, const Mining& m) {
  if (!(m.progress > 0) || m.progress >= 1 || !std::isfinite(m.time)) return;
  dmgOf(lv).m[{m.x, m.y, m.z}] = {m.x, m.y, m.z, m.id, m.progress, gameClock(lv)};
}
float damageFor(Level& lv, int x, int y, int z, BlockId id) {
  auto& mp = dmgOf(lv).m;
  auto it = mp.find({x, y, z});
  if (it == mp.end() || it->second.id != id) return 0;
  float pr = it->second.p; mp.erase(it); return pr;
}
void forgetDamage(Level& lv, int x, int y, int z) { dmgOf(lv).m.erase({x, y, z}); }
std::vector<Crack> damageCracks(Level& lv, World& w, vec3 cam, const Mining* cur) {
  DamageMap& D = dmgOf(lv);
  auto& mp = D.m;
  double now = gameClock(lv), dt = D.prevT < 0 ? 0 : now - D.prevT; D.prevT = now;
  std::vector<Crack> out;
  for (auto it = mp.begin(); it != mp.end();) {
    DamageMark& d = it->second;
    if (w.getBlock(d.x, d.y, d.z) != d.id) { it = mp.erase(it); continue; }
    if (now - d.t > 2.5) d.p -= (float)(dt * 0.12);
    if (d.p <= 0) { it = mp.erase(it); continue; }
    if (std::fabs(d.x - cam.x) <= 48 && std::fabs(d.z - cam.z) <= 48) out.push_back({d.x, d.y, d.z, std::min(9, (int)std::floor(d.p * 10))});
    ++it;
  }
  if (cur && cur->active && cur->progress > 0) out.push_back({cur->x, cur->y, cur->z, std::min(9, (int)std::floor(cur->progress * 10))});
  return out;
}

void PlayerController::weClick(int which) {
  if (!target.hit) return;
  weSetCorner(lv, p, which, target.block);
  toast(lv, p, "Corner " + std::to_string(which) + ": " + std::to_string(target.block.x) + " " + std::to_string(target.block.y) + " " + std::to_string(target.block.z) + weSizeText(lv, p), "ok");
  swingArm(); sfxUi(lv, p, "click");
}

void registerArrowEntity() { registerEntityFactory("arrow", [](const std::string&) { return std::unique_ptr<Entity>(new ArrowEntity()); }); }

} // namespace dq
