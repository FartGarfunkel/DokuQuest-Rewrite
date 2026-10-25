// tools/test_magic.cpp — headless rules test for magic, enchanting, brewing, skills and the RPG side.
// The enchanting offers and rune strings are checked against values taken from the running original
// (tools/port/jsdump.py: enchOffers / enchRunes / skillLevel). Run: build/test_magic
#include "core/init.h"
#include "sim/magic.h"
#include "sim/magic_entities.h"
#include "sim/enchanting.h"
#include "sim/brewing.h"
#include "sim/effects.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "sim/commands.h"
#include "world/world.h"
#include <cstdio>
#include <thread>

using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  (ok ? passes : fails)++;
}
static bool near(double a, double b, double e = 1e-3) { return std::abs(a - b) <= e; }

struct RecFx : LevelFx {
  std::vector<std::string> msgs; std::vector<std::string> parts; int sounds = 0;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  void sound(const std::string&, vec3, float, float, float) override { sounds++; }
  void soundUi(u32, const std::string&) override { sounds++; }
  void particles(const std::string& k, vec3, int, const json& e) override { parts.push_back(k + ":" + (e.is_object() ? e.value("m", "") : "")); }
  bool heard(const std::string& s) const { for (auto& m : msgs) if (m.find(s) != std::string::npos) return true; return false; }
  bool saw(const std::string& s) const { for (auto& m : parts) if (m == s) return true; return false; }
};

// the original's enchOffers for these stacks / shelves / seeds (and enchRunes(777, 12))
static const char* REF = R"([{"id":"diamond_pickaxe","n":15,"s":12345,"o":[{"level":8,"cost":1,"en":{"eff":2,"unb":1}},{"level":17,"cost":2,"en":{"unb":2,"fortune":2}},{"level":30,"cost":3,"en":{"eff":4}}]},
{"id":"book","n":0,"s":99,"o":[{"level":1,"cost":1,"en":{"power":1}},{"level":2,"cost":2,"en":{"prot":1}},{"level":2,"cost":3,"en":{"feather":1}}]},
{"id":"iron_sword","n":8,"s":4242,"o":[{"level":3,"cost":1,"en":{"unb":1}},{"level":7,"cost":2,"en":{"frostasp":1}},{"level":16,"cost":3,"en":{"smite":4,"unb":2}}]},
{"id":"bow","n":15,"s":7,"o":[{"level":5,"cost":1,"en":{"unb":1}},{"level":11,"cost":2,"en":{"power":2,"unb":1}},{"level":30,"cost":3,"en":{"punch":2}}]},
{"id":"leather_boots","n":3,"s":1,"o":[{"level":2,"cost":1,"en":{"bprot":1}},{"level":5,"cost":2,"en":{"prot":1}},{"level":7,"cost":3,"en":{"feather":2}}]},
{"id":"apprentice_staff","n":12,"s":555,"o":[{"level":4,"cost":1,"en":{"unb":1}},{"level":10,"cost":2,"en":{"flow":1}},{"level":24,"cost":3,"en":{"unb":2,"arcane":2}}]},
{"id":"golden_helmet","n":15,"s":31337,"o":null}])";

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::string ud = "/tmp/dq-test-magic";
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  const MagicData& md = magicData();
  check(md.loaded && md.spells.size() == 29 && md.ench.size() == 35 && md.potions.size() == 22 && md.skills.size() == 14, "data/magic.json: 29 spells, 35 enchantments, 22 potions, 14 skills");

  // ---- skills ----
  check(skillLevel(82) == 1 && skillLevel(83) == 2 && skillLevel(101333) == 50 && skillLevel(13034431) == 99 && near(skillXpFor(99), 13034431), "skill curve: 83 -> 2, 101,333 -> 50, 13,034,431 -> 99");
  check(gearLevelOf(0) == 1 && gearLevelOf(30) == 2 && gearLevelOf(120) == 3 && gearLevelOf(1e9) == GEAR_MAX && near(gearXpFor(3), 120), "gear levels: sqrt(gx/30), max 50");

  // ---- enchanting (against the original) ----
  check(enchName("sharp", 3) == "Sharpness III" && enchName("silk", 1) == "Silk Touch" && enchName("mend", 1) == "Mending", "enchantment names with Roman levels");
  check(enchKind("diamond_sword") == "sword" && enchKind("iron_helmet") == "helmet" && enchKind("iron_boots") == "boots" && enchKind("book") == "book" && enchKind("apprentice_staff") == "staff" && enchKind("stone") == "", "what kind of thing an item is");
  check(enchLevelFor("sharp", 1) == 1 && enchLevelFor("sharp", 40) == 5 && enchLevelFor("silk", 14) == 0 && enchLevelFor("silk", 15) == 1, "enchLevelFor: power -> level, minimum power");
  check(!enchCompatible(json{{"sharp", 1}}, "smite") && !enchCompatible(json{{"inf", 1}}, "mend") && !enchCompatible(json{{"fire", 1}}, "frostasp") && enchCompatible(json{{"sharp", 1}}, "unb"), "groups and exclusions (damage, infinity/mending, fire/frost aspect)");
  json ref = json::parse(REF);
  for (auto& r : ref) {
    std::array<EnchOffer, 3> o;
    bool got = enchOffers(ItemStack(r["id"], 1), r["n"], r["s"].get<u32>(), o);
    bool same = got == !r["o"].is_null();
    if (got && same) for (int i = 0; i < 3; i++) same = same && o[i].level == r["o"][i]["level"] && o[i].cost == r["o"][i]["cost"] && o[i].en == r["o"][i]["en"];
    std::string desc = r["id"].get<std::string>() + " / " + std::to_string(r["n"].get<int>()) + " shelves";
    if (!same && got) for (int i = 0; i < 3; i++) printf("     got L%d %s\n", o[i].level, o[i].en.dump().c_str());
    check(same, "enchOffers matches the original: " + desc);
  }
  {
    std::string s;
    for (int i : enchRunes(777, 12)) s += i < 0 ? " " : md.runes[i];
    check(s == "\xE1\x9B\x8F \xE1\x9A\xB1 \xE1\x9B\x8A\xE1\x9A\xB7\xE1\x9B\x89\xE1\x9B\x8F\xE1\x9A\xA8\xE1\x9B\x88\xE1\x9B\x87 \xE1\x9B\x9E\xE1\x9B\x9F \xE1\x9A\xBA", "enchRunes matches the original");
  }
  {
    ItemStack sword("diamond_sword", 1); sword.tag = json{{"en", {{"sharp", 3}}}};
    ItemStack book("enchanted_book", 1); book.tag = json{{"en", {{"sharp", 3}, {"unb", 1}}}};
    StationResult r = anvilEnchant(sword, book);
    check(r.ok && r.out.tag["en"]["sharp"] == 4 && r.out.tag["en"]["unb"] == 1 && r.lv == 5, "anvil: Sharpness III + III -> IV, adds Unbreaking, costs 5 levels");
    ItemStack bow("bow", 1); bow.tag = json{{"en", {{"inf", 1}}}};
    ItemStack mb("enchanted_book", 1); mb.tag = json{{"en", {{"mend", 1}}}};
    check(!anvilEnchant(bow, mb).ok, "anvil: Mending won't go onto Infinity");
    StationResult g = grindstoneDisenchant(&book, nullptr);
    check(g.ok && g.out.id == "book" && g.xp == 12, "grindstone: a book back and 3 xp per level");
  }

  // ---- a level to play in ----
  ThreadPool pool(1);
  EventBus bus;
  LevelInfo info; info.name = "MagicTest"; info.seedText = "magic"; info.mode = "survival";
  auto lv = Level::create(userSub("saves"), info, &pool, &bus);
  RecFx fx; lv->fx = &fx;
  auto pp = std::make_unique<Player>(); pp->name = "Mage"; pp->mode = "survival"; pp->pos = vec3(0.5f, 700, 0.5f); pp->noGravity = true;
  Player& p = *static_cast<Player*>(lv->spawn(std::move(pp)));
  lv->tick(TICK_DT);
  check(lv->system<LevelSystem>() != nullptr && near(manaMax(p), 20) && near(magicState(*lv, p).mana, 20, 0.1), "the magic system runs; 20 mana to start");

  // armour and protection
  p.armor[0] = ItemStack("iron_helmet", 1); p.armor[1] = ItemStack("iron_chestplate", 1); p.armor[2] = ItemStack("iron_leggings", 1); p.armor[3] = ItemStack("iron_boots", 1);
  for (auto& s : p.armor) s.tag = json{{"en", {{"prot", 4}}}};
  check(near(protectionFactor(p, "mob"), 0.6f) && near(protectionFactor(p, "lava"), 0.6f), "Protection IV x4: 60% cap (0.9.0.24)");
  p.armor[3].tag = json{{"en", {{"feather", 4}}}};
  check(near(protectionFactor(p, "fall"), 1 - 12 * 0.025f), "Feather Falling IV: three points a level on falls");
  for (auto& s : p.armor) s.clear();

  // tomes
  ItemStack tome("tome_fireball", 1);
  learnTome(*lv, p, tome);
  check(fx.heard("You need Magic 28 to learn Fireball") && tome.n == 1, "a tome too deep for you: the words swim");
  ItemStack t1("tome_firebolt", 1);
  learnTome(*lv, p, t1);
  check(fx.heard("You have learned Firebolt! Hold a staff and use it to cast") && t1.empty() && knownSpells(p).size() == 1 && skillXp(p, "magic") == 40, "learning Firebolt (+40 Magic xp)");

  // casting a firebolt at a skeleton
  p.inv[0] = ItemStack("apprentice_staff", 1); p.selected = 0;
  p.yaw = 0; p.pitch = 0;   // looking down -z
  Entity* sk = spawnCreature(*lv, "skeleton", 0, p.pos + vec3(0, 0, -6));
  float hp0 = sk ? sk->hp : 0;
  bool used = !bus.emit("itemUse", json::array({"Mage", "apprentice_staff", 0, 0, 0, -1}));
  MagicState& m = magicState(*lv, p);
  bool bolt = false; for (auto& e : lv->entities()) bolt |= e->type == "spell_bolt" && !e->removed;
  check(used && bolt && near(m.mana, 16, 0.05) && fx.saw("mana_fire:cast"), "itemUse with a staff casts Firebolt: 4 mana, a bolt, the cast for every machine");
  for (int i = 0; i < 15; i++) lv->tick(TICK_DT);
  // power = 1 * (1 + (lv-1)/98*0.6): Magic is level 1 here -> 6 damage
  check(sk && near(sk->hp, hp0 - 6) && mobStatus(*sk, "burnT") > 0, "the bolt hits: 6 damage and burning");
  check(skillXp(p, "magic") == 40 + 24 && gearLevel(p.inv[0]) == 1 && p.inv[0].tag.value("gx", 0) == 2 && p.inv[0].d == 1, "Magic +24 xp, the staff wears and grows");
  check(p.stats["schools"].value("fire", 0) == 24, "the Fire school grows with the cast");

  // not enough mana, the cooldown
  m.castCd = 0; m.mana = 2;
  castSpell(*lv, p);
  check(fx.heard("Not enough mana") && near(m.mana, 2), "not enough mana: a fizzle");
  // Mend at full health
  p.stats["spells"].push_back("heal"); p.stats["spells"].push_back("ward"); p.stats["spells"].push_back("homeport"); p.stats["spells"].push_back("flames");
  pickSpell(*lv, p, "heal");
  m.castCd = 0; m.mana = 20;
  castSpell(*lv, p);
  check(fx.heard("You are not hurt") && near(m.mana, 20), "Mend when you're not hurt: no cost");
  // Ward soaks harm
  pickSpell(*lv, p, "ward"); m.castCd = 0;
  castSpell(*lv, p);
  check(near(m.ward, 6) && fx.heard("Warded (6 harm)"), "Ward: 6 harm at power 1");
  json src{{"type", "mob"}};
  float left = magicIncomingDamage(*lv, p, 4, src);
  check(near(left, 0) && near(m.ward, 2), "the ward soaks a blow of 4");
  left = magicIncomingDamage(*lv, p, 5, src);
  check(near(left, 3) && m.ward == 0 && fx.heard("Your ward breaks"), "...and breaks on the next");
  m.stoneT = 10;
  json s2{{"type", "mob"}, {"knock", 4}};
  left = magicIncomingDamage(*lv, p, 10, s2);
  check(near(left, 6.6f) && near(s2["knock"].get<float>(), 1), "Stoneskin: a third less harm, a quarter of the knockback");
  m.stoneT = 0;
  // a spellcraft spell bare-handed: Flames at a skeleton close by
  p.inv[0].clear();
  sk->pos = p.pos + vec3(0, 0, -2.5f); sk->hp = sk->maxHp; sk->data.erase("burnT");
  pickSpell(*lv, p, "flames"); m.castCd = 0; m.mana = 20;
  castQuick(*lv, p);
  // bare: power 0.7 -> max(1, round(3.5 * 0.7)) = 2
  check(near(sk->hp, sk->maxHp - 2) && near(m.mana, 15), "Flames without a staff (Z): weaker (2), 5 mana");
  // Home Teleport has its own ten-minute cooldown
  pickSpell(*lv, p, "homeport"); m.castCd = 0;
  lv->info.spawn = vec3(10.5f, 600, 10.5f);
  castQuick(*lv, p);
  check(fx.heard("Home again") && near(p.pos.x, 10.5f), "Home Teleport to the world spawn");
  m.castCd = 0; castQuick(*lv, p);
  check(fx.heard("Home Teleport is ready in"), "...once every ten minutes");

  // ---- effects and potions ----
  giveEffect(*lv, p, "speed", 180, 0);
  check(near(effectSpeedFactor(p), 1.2f) && near(effectTime(p, "speed"), 180), "Swiftness: 20% faster");
  giveEffect(*lv, p, "speed", 90, 1);
  check(near(effectSpeedFactor(p), 1.4f) && effectLevel(p, "speed") == 1, "Swiftness II replaces I");
  lv->tick(1.0);
  check(near(effectTime(p, "speed"), 179, 0.01), "effects count down (II kept I's longer time, as the original)");
  p.hp = 10;
  ItemStack heal("potion_heal", 1);
  drinkPotion(*lv, p, heal);
  check(near(p.hp, 18) && heal.id == "glass_bottle", "Potion of Healing: 4 hearts, the bottle comes back");
  check(potionTooltip("potion_swift_long") == "Speed (8:00)" && potionTooltip("splash_potion_heal_strong") == "Heals 8 hearts (less at the edge)", "potion tooltips");
  ItemStack milk("milk_bucket", 1); p.inv[1] = milk; p.selected = 1;
  bus.emit("itemUse", json::array({"Mage", "milk_bucket", 0, 0, 0, -1}));
  check(effectsOn(p).empty(), "milk washes every effect away");
  p.selected = 0;
  // a splash potion of harming on a skeleton heals it (undead)
  sk->hp = 5;
  splashPotionAt(*lv, 0, sk->pos + vec3(0, 1, 0), "splash_potion_harm", &p);
  check(sk->hp > 5, "Splash Harming heals the undead");

  // ---- brewing ----
  check(brewResult("potion_water", "nether_wart_item") == "potion_awkward" && brewResult("potion_awkward", "sugar") == "potion_swift" &&
        brewResult("potion_swift", "redstone") == "potion_swift_long" && brewResult("potion_swift", "fermented_spider_eye") == "potion_slow" &&
        brewResult("potion_swift", "gunpowder") == "splash_potion_swift" && brewResult("potion_awkward", "ghast_tear") == "potion_regen", "brewing recipes (+ the ghast tear)");
  World& w = lv->world(0);
  w.stream({{p.pos, 1, false}}, 50);
  for (int i = 0; i < 400 && !w.isLoaded(0, 700, 0); i++) { w.stream({{p.pos, 1, false}}, 50); w.pumpMain(50); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
  check(w.isLoaded(0, 700, 0), "a chunk to brew in");
  w.setBlock(2, 700, 2, reg().blockId("brewing_stand"));
  json& be = brewEntity(w, 2, 700, 2);
  for (int i = 0; i < 3; i++) be["items"][i] = ItemStack("potion_water", 1).toJson();
  be["items"][3] = ItemStack("nether_wart_item", 2).toJson();
  be["items"][4] = ItemStack("cinder_powder", 1).toJson();
  check(brewStatus(be) == "", "a stand ready to brew");
  for (int i = 0; i < 6; i++) lv->tick(1.0);
  json* e2 = w.blockEntity(2, 700, 2);
  check(e2 && e2->value("t", 0.0) > 0 && (*e2)["items"][4].is_null() && e2->value("fuel", 0) == 20 && brewStatus(*e2) == "Brewing\xE2\x80\xA6", "cinder powder lit it: 20 brews");
  for (int i = 0; i < 7; i++) lv->tick(1.0);
  e2 = w.blockEntity(2, 700, 2);
  check(e2 && (*e2)["items"][0]["id"] == "potion_awkward" && (*e2)["items"][2]["id"] == "potion_awkward" && (*e2)["items"][3]["n"] == 1 && e2->value("fuel", 0) == 19, "twelve seconds later: three awkward potions, fuel 19");
  check(skillXp(p, "alchemy") == 105, "Alchemy +35 a potion for the brewer nearby");

  // ---- enchanting at the table ----
  p.level = 30; p.xp = 0;
  ItemStack pick("diamond_pickaxe", 1), lapis("lapis_lazuli", 5);
  p.stats["enchSeed"] = 12345u;
  std::string why = enchApplyOffer(*lv, p, pick, lapis, 2, 15, vec3(0, 700, 0), nullptr);
  check(why.empty() && enchLv(pick, "eff") == 4 && p.level == 27 && lapis.n == 2 && skillXp(p, "alchemy") == 105 + 40 + 90, "the third offer: Efficiency IV for 3 levels and 3 lapis (+ Alchemy)");
  check(near(enchMiningBonus(pick), 17), "Efficiency IV: +17 speed");
  ItemStack sw("iron_sword", 1), lp2("lapis_lazuli", 1);
  p.level = 0;
  why = enchApplyOffer(*lv, p, sw, lp2, 0, 0, vec3(0), nullptr);
  check(!why.empty() && why.rfind("You need level", 0) == 0, "not enough levels: the original's warning");

  // ---- skills from breaking ----
  double mx0 = skillXp(p, "mining");
  onBlockBroken(*lv, p, 5, 700, 5, reg().block(reg().blockId("iron_ore")), 0, true, false);
  check(skillXp(p, "mining") - mx0 == 32, "iron ore: +32 Mining");
  onBlockBroken(*lv, p, 5, 700, 5, reg().block(reg().blockId("iron_ore")), 0, true, true);
  check(skillXp(p, "mining") - mx0 == 32, "a block you put there yourself gives nothing (0.9.0.24)");

  // ---- the journal ----
  Story s = storyOf(*lv, p);
  check(s.ch.size() == 12 && s.cur == 0 && s.ch[0].t == "A Village at the Edge", "the journal starts at the village");
  p.stats["vseen"] = json{{"v1", 1}};
  s = storyOf(*lv, p);
  check(s.cur == 1 && s.ch[0].done, "...and moves on once you find one");

  // ---- commands ----
  CommandSource cs; cs.level = lv.get(); cs.player = &p; cs.name = p.name;
  std::string r = runCommand(*lv, cs, "/learn all");
  check(knownSpells(p).size() == 29, "/learn all (" + r + ")");

  printf("\n%d passed, %d failed\n", passes, fails);
  lv->close();
  return fails ? 1 : 0;
}
