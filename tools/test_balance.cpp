// tools/test_balance.cpp — the RPG balance check (headless). Every stat and talent attribute does something and stays
// inside its cap (sim/balance.h); stat points never go to waste; tougher creatures and elites drop more; and a fight
// table: a player of gear tier x skill level against a creature of level x elite, with the game's own blow, armour and
// damage code, time to kill (TTK) and time to die (TTD) checked against target bands. Run: build/test_balance
#include "core/init.h"
#include "core/tool_parts.h"
#include "sim/balance.h"
#include "sim/combat.h"
#include "sim/combat_rpg.h"
#include "sim/controller.h"
#include "sim/creature.h"
#include "sim/magic.h"
#include "sim/skills.h"
#include "sim/skilltree.h"
#include "world/world.h"
#include <cstdio>
#include <set>
#include <thread>

using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  (ok ? passes : fails)++;
}
struct QuietFx : LevelFx {
  std::vector<std::string> msgs;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  bool heard(const std::string& s) const { for (auto& m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
};

static const char* TIER[] = {"none", "copper", "iron", "diamond", "adamantite"};
static const int SKILL[] = {1, 25, 50, 99};

// a player: gear of a tier, every skill at S, the stat points that brings spread evenly (each capped)
static void dress(Player& p, int tier, int S) {
  for (auto& s : p.armor) s.clear();
  for (auto& s : p.inv) s.clear();
  p.selected = 0;
  if (tier > 0) {
    const std::string t = TIER[tier];
    p.armor[0] = ItemStack(t + "_helmet", 1); p.armor[1] = ItemStack(t + "_chestplate", 1);
    p.armor[2] = ItemStack(t + "_leggings", 1); p.armor[3] = ItemStack(t + "_boots", 1);
    p.inv[0] = ItemStack(t + "_sword", 1);
  }
  json sk = json::object(); for (auto& d : magicData().skills) sk[d.key] = skillXpFor(S); p.stats["skills"] = sk;
  int pts = statPointsEarned(p);
  int* st[] = {&p.statCon, &p.statStr, &p.statAgi, &p.statInt, &p.statLck, &p.statFoc};
  for (int* s : st) *s = 0;
  for (int i = 0; pts > 0 && i < 6 * bal::STAT_CAP; i++) if (*st[i % 6] < bal::STAT_CAP) { (*st[i % 6])++; pts--; }
  p.recalcMax(); p.hp = p.maxHp; p.secondChanceCd = 1e9f; p.invuln = 0; p.dead = false;
}
static Creature* foe(Level& lv, Player& p, int lvl, bool elite) {
  Creature* m = spawnCreature(lv, 0, "ghoul", p.pos + vec3(0, 0, -1.5f));
  if (!m) return nullptr;
  m->heldItem.clear(); m->offItem.clear(); m->data.erase("armor"); m->variant.clear();
  m->maxHp = m->hp = m->T->hp;
  mobApplyLevel(*m, lvl, elite, m->maxHp);
  return m;
}
struct Fight { double ttk = 0, ttd = 0; };
static Fight fight(Level& lv, Player& p, int tier, int S, int lvl, bool elite, int trials) {
  Fight f;
  for (int t = 0; t < trials; t++) {
    dress(p, tier, S);
    Creature* m = foe(lv, p, lvl, elite);
    if (!m) return f;
    // time to kill: swing whenever the swing is ready
    double tk = 0;
    while (m->hp > 0 && !m->removed && tk < 600) {
      p.attackCd = 0; m->invulnT = 0; m->pos = p.pos + vec3(0, 0, -1.5f); m->vel = vec3(0);
      attackEntity(lv, p, *m, 1.f, 1.f, false, true);
      const float step = std::max(0.05f, p.attackCd);
      tk += step; m->staggerT = std::max(0.f, m->staggerT - step);
    }
    // time to die: its blows (one each 1.1 s), from full health; the health is put back after each so nobody dies here
    Creature* n = foe(lv, p, lvl, elite);
    dress(p, tier, S);
    double taken = 0; int hits = 0;
    while (taken < p.maxHp && hits < 2000) {
      p.hp = p.maxHp; p.invuln = 0; n->attackCd = 0; magicState(lv, p).ward = 0;
      n->meleeAt(lv, p, 1.f, 0.f);
      taken += p.maxHp - p.hp; hits++;
    }
    f.ttk += tk / trials; f.ttd += hits * 1.1 / trials;
    m->removed = true; n->removed = true; lv.tick(TICK_DT);
  }
  return f;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::string ud = "/tmp/dq-test-balance";
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(1);
  EventBus bus;
  LevelInfo info; info.name = "BalanceTest"; info.seedText = "balance"; info.mode = "survival";
  auto lv = Level::create(userSub("saves"), info, &pool, &bus);
  QuietFx fx; lv->fx = &fx;
  auto pp = std::make_unique<Player>(); pp->name = "Tester"; pp->mode = "survival"; pp->pos = vec3(0.5f, 700, 0.5f); pp->noGravity = true;
  Player& p = *static_cast<Player*>(lv->spawn(std::move(pp)));
  lv->tick(TICK_DT);

  printf("[stat points: none wasted]\n");
  {
    dress(p, 0, 99);
    const int earned = statPointsEarned(p);
    check(earned == 14 * 99 / bal::STAT_EVERY, "all skills 99: " + std::to_string(earned) + " stat points");
    check(6 * bal::STAT_CAP >= earned, "six stats x " + std::to_string(bal::STAT_CAP) + " = " + std::to_string(6 * bal::STAT_CAP) + ": room for every point to the end");
    check(p.statCon + p.statStr + p.statAgi + p.statInt + p.statLck + p.statFoc == earned, "...all of them spent");
  }

  printf("[every stat does what it says]\n");
  {
    dress(p, 0, 1);
    Player& q = p;
    auto with = [&](int* stat, int v, auto f) { const int keep = *stat; *stat = v; q.recalcMax(); combatRpgTick(*lv, q, TICK_DT); auto r = f(); *stat = keep; q.recalcMax(); return r; };
    const int C = bal::STAT_CAP;
    // Constitution: stamina, health, less harm
    float st0 = with(&q.statCon, 0, [&] { return q.maxStamina; }), st1 = with(&q.statCon, C, [&] { return q.maxStamina; });
    float hp0 = with(&q.statCon, 0, [&] { return q.maxHp; }), hp1 = with(&q.statCon, C, [&] { return q.maxHp; });
    float rd = with(&q.statCon, C, [&] { return conReduction(q); });
    check(st1 > st0 && hp1 > hp0 && rd > 0 && rd <= bal::CON_REDUCE_CAP, "Constitution: stamina " + std::to_string((int)st0) + " -> " + std::to_string((int)st1) + ", health " + std::to_string((int)hp0) + " -> " + std::to_string((int)hp1) + ", " + std::to_string((int)std::lround(rd * 100)) + "% less harm");
    {   // ...and a real blow lands softer
      Creature* m = foe(*lv, q, 1, false);
      auto blow = [&](int con) { q.statCon = con; q.recalcMax(); double sum = 0; for (int i = 0; i < 40; i++) { q.hp = q.maxHp; q.invuln = 0; m->attackCd = 0; m->meleeAt(*lv, q, 1.f, 0.f); sum += q.maxHp - q.hp; } return sum; };
      const double b0 = blow(0), b1 = blow(C);
      q.statCon = 0; q.recalcMax(); m->removed = true;
      check(b1 < b0, "...a ghoul's blows: " + std::to_string(b0 / 40).substr(0, 4) + " -> " + std::to_string(b1 / 40).substr(0, 4) + " a hit");
    }
    check(with(&q.statStr, C, [&] { return strDamageFactor(q); }) > 1.25f, "Strength: melee damage x" + std::to_string(strDamageFactor(q)).substr(0, 4) + " -> x" + std::to_string(with(&q.statStr, C, [&] { return strDamageFactor(q); })).substr(0, 4));
    float mv0 = with(&q.statAgi, 0, [&] { return moveFactor(q); }), mv1 = with(&q.statAgi, C, [&] { return moveFactor(q); });
    float dc0 = with(&q.statAgi, 0, [&] { return dodgeCost(q); }), dc1 = with(&q.statAgi, C, [&] { return dodgeCost(q); });
    float dt0 = with(&q.statAgi, 0, [&] { return dodgeTime(q); }), dt1 = with(&q.statAgi, C, [&] { return dodgeTime(q); });
    float as1 = with(&q.statAgi, C, [&] { return attackSpeedFactor(q); }), ds1 = with(&q.statAgi, C, [&] { return drawSpeedFactor(q); });
    check(mv1 > mv0 && mv1 <= bal::MOVE_FACTOR_MAX && dc1 < dc0 && dt1 > dt0 && as1 > 1 && ds1 > 1,
          "Agility: speed x" + std::to_string(mv1).substr(0, 4) + ", roll " + std::to_string((int)dc0) + " -> " + std::to_string((int)dc1) + " stamina, " + std::to_string(dt1).substr(0, 4) + " s safe, swings/draws x" + std::to_string(as1).substr(0, 4));
    {   // ...a real swing recovers sooner
      q.inv[0] = ItemStack("iron_sword", 1);
      Creature* m = foe(*lv, q, 1, false);
      auto cd = [&](int agi) { q.statAgi = agi; q.attackCd = 0; attackEntity(*lv, q, *m, 1, 1, false, true); m->hp = m->maxHp; return q.attackCd; };
      const float c0 = cd(0), c1 = cd(C);
      q.statAgi = 0; m->removed = true; q.inv[0].clear();
      check(c1 < c0, "...a sword's recovery " + std::to_string(c0).substr(0, 4) + " -> " + std::to_string(c1).substr(0, 4) + " s");
    }
    float mana0 = with(&q.statInt, 0, [&] { return manaMax(q); }), mana1 = with(&q.statInt, C, [&] { return manaMax(q); });
    float rg0 = with(&q.statInt, 0, [&] { return manaRegen(q); }), rg1 = with(&q.statInt, C, [&] { return manaRegen(q); });
    check(mana1 > mana0 && rg1 > rg0 && with(&q.statInt, C, [&] { return spellPowerFactor(q); }) > 1.2f, "Intelligence: mana " + std::to_string((int)mana0) + " -> " + std::to_string((int)mana1) + ", regeneration and spell power up");
    check(with(&q.statLck, C, [&] { return critChance(q); }) > 0.1f && with(&q.statLck, C, [&] { return lootLuck(q); }) > 0, "Luck: crit chance and loot luck");
    check(with(&q.statFoc, C, [&] { return postureFactor(q); }) > 1.4f, "Focus: posture damage x" + std::to_string(with(&q.statFoc, C, [&] { return postureFactor(q); })).substr(0, 4));
  }

  printf("[every talent attribute is read, and capped]\n");
  {
    // the attributes the systems read: the capped ones in TALENTS and these, read where they are used
    std::set<std::string> known = {"health", "mana", "mana_regen", "armor", "spell_resist", "spell_power", "damage", "stamina", "crit_chance", "crit_damage"};
    for (auto& r : bal::TALENTS) known.insert(r.attr);
    std::set<std::string> unread; int nodes = 0, bonuses = 0;
    for (const SkillTree& t : skillTrees()) for (const TreeNode& n : t.nodes) { nodes++; for (auto& b : n.bonus) { bonuses++; if (!known.count(b.attr) && b.attr.rfind("spell_power.", 0) != 0) unread.insert(b.attr); } }
    if (skillTrees().empty()) printf("     (no skill-tree data here: data/local_models/skilltree.json is local only; checking the rules alone)\n");
    std::string list; for (auto& u : unread) list += " " + u;
    check(unread.empty(), "every attribute in the trees (" + std::to_string(nodes) + " nodes, " + std::to_string(bonuses) + " bonuses) has a reader" + (list.empty() ? "" : ":" + list));
    // everything, a hundred times over: still within the caps
    json big = json::object();
    for (auto& r : bal::TALENTS) big[r.attr] = json::array({100.f, 100.f});
    p.stats["attrBonus"] = big;
    bool inside = true; std::string bad;
    for (auto& r : bal::TALENTS) { const float v = talent(p, r.attr); if (v > r.hi + 1e-5f || v < r.lo - 1e-5f) { inside = false; bad += std::string(" ") + r.attr; } }
    check(inside, "every talent at a hundred times the most a tree gives stays within its cap" + bad);
    check(critChance(p) <= bal::CRIT_CHANCE_CAP && moveFactor(p) <= bal::MOVE_FACTOR_MAX && attackSpeedFactor(p) <= 1 + bal::ATTACK_SPEED_CAP + 1e-5f && dodgeChance(p) <= 0.2f + 1e-5f && knockResist(p) < 1 && cooldownFactor(p) >= 0.6f - 1e-5f,
          "crit " + std::to_string(critChance(p)).substr(0, 4) + ", speed x" + std::to_string(moveFactor(p)).substr(0, 4) + ", evade " + std::to_string(dodgeChance(p)).substr(0, 4) + ", knock resist " + std::to_string(knockResist(p)).substr(0, 4) + ", cooldowns x" + std::to_string(cooldownFactor(p)).substr(0, 4));
    // the systems feel them
    {
      p.stats["attrBonus"] = json::object();
      p.vel = vec3(0); knockback(p, p.pos + vec3(0, 0, -1), 5, 0); const float k0 = std::fabs(p.vel.z);
      p.stats["attrBonus"] = json{{"knock_resist", {0.5f, 0.f}}};
      p.vel = vec3(0); knockback(p, p.pos + vec3(0, 0, -1), 5, 0); const float k1 = std::fabs(p.vel.z); p.vel = vec3(0);
      check(k1 < k0 * 0.6f, "knock_resist: shoved " + std::to_string(k0).substr(0, 4) + " -> " + std::to_string(k1).substr(0, 4));
      p.stats["attrBonus"] = json{{"overheal", {0.2f, 0.f}}};
      MagicState& ms = magicState(*lv, p); ms.ward = 0; p.recalcMax(); p.hp = p.maxHp - 1;
      combatHeal(*lv, p, 5);
      check(ms.ward > 3.9f && ms.ward <= p.maxHp * 0.2f + 1e-3f, "overheal: 4 healed past full kept as a ward (" + std::to_string(ms.ward).substr(0, 4) + ")");
      ms.ward = 0;
      p.stats["attrBonus"] = json{{"armor_pierce", {0.f, 0.5f}}};
      Creature* m = foe(*lv, p, 1, false);
      m->data["armor"] = json::array({"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"});
      p.inv[0] = ItemStack("iron_sword", 1);
      auto hit = [&]() { m->hp = m->maxHp = 1000; m->invulnT = 0; p.attackCd = 0; m->staggerT = 0; m->posture = 0; DamageSource s; s.attacker = p.id; s.pierce = armorPierce(p); return damageEntity(*lv, *m, 10, s); };
      const float with = hit(); p.stats["attrBonus"] = json::object(); const float without = hit();
      m->removed = true; p.inv[0].clear();
      check(with > without, "armor_pierce: through iron armour " + std::to_string(without).substr(0, 4) + " -> " + std::to_string(with).substr(0, 4));
    }
    p.stats.erase("attrBonus");
  }

  printf("[tougher creatures pay]\n");
  {
    dress(p, 0, 1);
    MobLoot a = mobLoot(1, false, &p), b = mobLoot(30, false, &p), e = mobLoot(30, true, &p), none = mobLoot(30, true, nullptr);
    check(a.rolls == 1 && std::fabs(a.chanceMul - 1) < 1e-4f && a.extra == 0 && a.gearChance == 0, "level 1: its plain drops");
    check(b.chanceMul > 1.5f && b.extra > 0.3f && b.gearChance > 0 && b.gearChance <= bal::GEAR_CHANCE_MAX, "level 30: drops x" + std::to_string(b.chanceMul).substr(0, 4) + ", +1 " + std::to_string((int)(b.extra * 100)) + "% of the time, gear " + std::to_string(b.gearChance).substr(0, 5));
    check(e.rolls == 2 && e.gearChance >= bal::ELITE_GEAR && e.gearTier > b.gearTier, "an elite: two rolls, gear " + std::to_string((int)(e.gearChance * 100)) + "% (tier " + std::string(bal::GEAR_TIERS[e.gearTier]) + ")");
    check(none.rolls == 1 && none.gearChance == 0, "...but only for a player's kill");
    int real = 0; std::set<std::string> kinds;
    for (int i = 0; i < 60; i++) { ItemStack g = mobGearRoll(e, i / 60.0, (i * 7 % 60) / 60.0, 0.5); if (!g.empty() && toolMaxDurability(g) > 0 && g.d > 0) real++, kinds.insert(g.id); }
    check(real == 60, "gear it drops is real, worn gear (" + std::to_string(kinds.size()) + " kinds)");
    p.statLck = bal::STAT_CAP;
    check(mobLoot(10, false, &p).chanceMul > mobLoot(10, false, nullptr).chanceMul + 0.15f, "Luck: better drops");
    p.statLck = 0;
    // a real kill: an elite at level 40 against a level-1 one, counted over many deaths
    auto haul = [&](int lvl, bool elite) {
      int items = 0;
      for (int i = 0; i < 40; i++) {
        Creature* m = foe(*lv, p, lvl, elite);
        DamageSource s; s.attacker = p.id;
        damageEntity(*lv, *m, 1e6f, s);
        for (auto& en : lv->entities()) if (!en->removed && en->type == "item") { items++; en->removed = true; }
        m->removed = true; lv->tick(TICK_DT);
      }
      return items;
    };
    const int h1 = haul(1, false), h40 = haul(40, true);
    check(h40 > h1 * 1.5, "forty ghouls' drops: level 1 " + std::to_string(h1) + " stacks, elite level 40 " + std::to_string(h40));
  }

  printf("[skills grow from play]\n");
  {
    dress(p, 0, 1);
    json sk = json::object(); for (auto& d : magicData().skills) sk[d.key] = 0; p.stats["skills"] = sk;
    PlayerController pc(*lv, p);
    World& w = lv->world(0);
    const BlockId logB = reg().blockId("birch_log"), stone = reg().blockId("stone");
    for (double t0 = nowSeconds(); !w.chunk(DIM_OVERWORLD, 0, 0) && nowSeconds() - t0 < 60;) { w.stream({{p.pos, 1, false}}, 20); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }   // (the ground about you loaded)
    for (int x = 4; x < 10; x++) for (int z = 4; z < 10; z++) for (int y = 710; y < 714; y++) w.setBlock(x, y, z, AIR);
    w.setBlock(5, 710, 5, logB);
    p.inv[0] = ItemStack("iron_axe", 1); p.selected = 0;
    pc.breakBlock(5, 710, 5);
    check(skillXp(p, "woodcutting") > 0, "breaking a log: Woodcutting +" + std::to_string((int)skillXp(p, "woodcutting")));
    w.setBlock(6, 710, 6, stone);
    p.inv[0] = ItemStack("iron_pickaxe", 1);
    const float slow = pc.breakTime(stone, nullptr);
    pc.breakBlock(6, 710, 6);
    const double mined = skillXp(p, "mining");
    check(mined > 0, "breaking stone: Mining +" + std::to_string((int)mined));
    bus.emit("blockPlace", json::array({p.name, 7, 710, 7, "stone"})); w.setBlock(7, 710, 7, stone);
    pc.breakBlock(7, 710, 7);
    check(skillXp(p, "mining") == mined, "...but not a block you put there yourself");
    p.stats["skills"]["mining"] = skillXpFor(99);
    check(pc.breakTime(stone, nullptr) < slow * 0.9f, "Mining 99 digs faster (" + std::to_string(slow).substr(0, 4) + " -> " + std::to_string(pc.breakTime(stone, nullptr)).substr(0, 4) + " s)");
    bus.emit("skillGain", json::array({p.name, "farming", 30}));
    check(skillXp(p, "farming") == 30, "the \"skillGain\" event (watering, machines) is heard");
    skillQueueSmelt(p, "cooked_beef", 2); skillQueueSmelt(p, "iron_ingot", 3);
    skillsTick(*lv, p);
    check(skillXp(p, "cooking") > 0 && skillXp(p, "smithing") > 0 && !p.data.contains("smeltTaken"), "taking a furnace's food and ingots: Cooking and Smithing (once)");
    p.stats["skills"]["cooking"] = skillXpFor(99);
    check(cookingHealFactor(p) > 1.2f, "Cooking 99: food heals x" + std::to_string(cookingHealFactor(p)).substr(0, 4));
    p.inv[0] = ItemStack("iron_sword", 1);
    Creature* m = foe(*lv, p, 1, false);
    auto swing = [&] { m->hp = m->maxHp = 1000; p.attackCd = 0; m->staggerT = 0; m->posture = 0; m->invulnT = 0; return attackEntity(*lv, p, *m, 1, 1, false, true); };
    swing();   // (the first blow lands on an unaware one: a sneak attack)
    p.stats["skills"]["melee"] = 0.0; double loSum = 0; for (int i = 0; i < 20; i++) loSum += swing();
    check(skillXp(p, "melee") > 0, "a blow that lands: Melee +" + std::to_string((int)skillXp(p, "melee")));
    p.stats["skills"]["melee"] = skillXpFor(99);
    double hiSum = 0; for (int i = 0; i < 20; i++) hiSum += swing();
    m->removed = true;
    check(hiSum > loSum * 1.1, "Melee 99 hits harder (" + std::to_string(loSum / 20).substr(0, 4) + " -> " + std::to_string(hiSum / 20).substr(0, 4) + " a blow)");
    Creature* c1 = spawnCreature(*lv, 0, "cow", p.pos + vec3(3, 0, 0)); Creature* c2 = spawnCreature(*lv, 0, "cow", p.pos + vec3(3.5f, 0, 0));
    if (c1 && c2) {
      c1->data["fedBy"] = p.name; c1->loveT = c2->loveT = 30;
      c1->breedWith(*lv, *c2);
      check(skillXp(p, "taming") > 0 && !c1->data.contains("fedBy"), "two animals you fed breed: Taming +" + std::to_string((int)skillXp(p, "taming")));
      c1->removed = c2->removed = true;
      for (auto& en : lv->entities()) if (en->type == "cow") en->removed = true;
    } else check(false, "cows to breed");
    p.inv[0].clear();
    lv->tick(TICK_DT);
  }

  printf("[two kinds of level, told apart]\n");
  {
    fx.msgs.clear();
    grantXP(*lv, p, 100000);
    check(fx.heard("Experience level"), "the experience level: \"Experience level N!\"");
    json sk = json::object(); for (auto& d : magicData().skills) sk[d.key] = 0; p.stats["skills"] = sk;
    skillGain(*lv, p, "mining", skillXpFor(5));
    check(fx.heard("Mining skill level 5!"), "a skill's level: \"Mining skill level 5!\"");
    skillGain(*lv, p, "melee", skillXpFor(5));
    check(fx.heard("a stat point to spend"), "...and the stat point it brings is named");
  }

  printf("[the fight table: gear tier x skill level vs a ghoul of level x elite]\n");
  printf("     %-11s %5s %4s | %-21s | %-21s\n", "gear", "skill", "lvl", "plain: TTK / TTD s", "elite: TTK / TTD s");
  bool bandsOk = true; std::string why;
  for (int i = 0; i <= 4; i++) {
    const int tier = i, S = SKILL[std::max(0, i - 1)];   // gear and skill grow together: none/1, copper/1, iron/25, diamond/50, adamantite/99
    dress(p, tier, S);
    const int L = playerCombatLevel(p);
    for (int dl : {0, 3}) {
      const int lvl = L + dl;
      Fight a = fight(*lv, p, tier, S, lvl, false, 6), b = fight(*lv, p, tier, S, lvl, true, 6);
      printf("     %-11s %5d %4d | %8.1f / %-10.1f | %8.1f / %-10.1f\n", TIER[tier], S, lvl, a.ttk, a.ttd, b.ttk, b.ttd);
      // target bands (an even fight, dl 0, geared for it, tier >= 1): plain ones fall in 1-15 s and lose the trade by 1.5x;
      // elites take 2-40 s and are a real fight (you outlast it, but not by much more than 6x); at +3 levels still winnable
      if (tier >= 1) {
        const bool ok = dl == 0 ? (a.ttk >= 1 && a.ttk <= 15 && a.ttd >= a.ttk * 1.5 && b.ttk >= 2 && b.ttk <= 40 && b.ttd >= b.ttk * 0.8 && b.ttd <= b.ttk * 6 + 10)
                                : (a.ttd >= a.ttk && a.ttk <= 25);
        if (!ok) { bandsOk = false; why += std::string(" ") + TIER[tier] + "/" + std::to_string(lvl); }
      }
    }
  }
  check(bandsOk, "every geared fight inside its band" + why);
  {   // never immune, either way: the best gear and every stat against a level-1 ghoul still hurts; an ungeared novice vs a high level falls fast
    dress(p, 4, 99);
    Fight f = fight(*lv, p, 4, 99, 1, false, 3);
    check(f.ttd < 600, "adamantite and every skill at 99 vs a level-1 ghoul: it still hurts you (TTD " + std::to_string((int)f.ttd) + " s)");
    Fight g = fight(*lv, p, 0, 1, 60, true, 2);
    check(g.ttd < g.ttk, "bare hands, level 1 vs a level-60 elite: run (TTK " + std::to_string((int)g.ttk) + " s, TTD " + std::to_string((int)g.ttd) + " s)");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  lv->close();
  return fails ? 1 : 0;
}
