// tools/test_metal.cpp — headless checks for metallurgy (core/metals.h, core/tool_parts.h, sim/metalwork.h, sim/forge.h,
// sim/anvil.h, sim/bloomery.h; after TerraFirmaCraft): metals and alloys, makeup-blended tools, ores, vessels and casting,
// the forge, the anvil, the bloomery, quenching and the prospector's pick. Run: build/test_metal (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "core/metals.h"
#include "core/tool_parts.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "sim/rules.h"
#include "sim/events.h"
#include "sim/crafting.h"
#include "sim/heat.h"
#include "sim/stations.h"
#include "sim/worldsys.h"
#include "sim/inventory.h"
#include "sim/metalwork.h"
#include "sim/firepit.h"
#include "sim/forge.h"
#include "sim/anvil.h"
#include "sim/bloomery.h"
#include "sim/prospect.h"
#include "common/threadpool.h"
#include "world/world.h"
#include "world/dims.h"
#include <thread>
#include <cstdio>
#include <cmath>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

struct Rig {
  ThreadPool pool{2}; EventBus bus; std::unique_ptr<Level> lv; Player* P = nullptr; int Y = 0;
  Rig(const std::string& name, bool immersiveOn, bool setRule) {
    LevelInfo li; li.name = name; li.seedText = "immersive";
    if (setRule) li.rules = json{{"immersive", immersiveOn}, {"_v", 1}};
    lv = Level::create(userSub("saves"), li, &pool, &bus);
    World& w = lv->world();
    Y = dimBase(DIM_OVERWORLD) + 200;
    auto p = std::make_unique<Player>(); p->name = "Tester"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(lv->spawn(std::move(p)));
    double t0 = nowSeconds();
    while (nowSeconds() - t0 < 60) {
      w.stream({{P->pos, 2, false}}, 20);
      bool all = true;
      for (int cx = -1; cx <= 1; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const BlockId stone = reg().blockId("stone");
    for (int x = -4; x < 20; x++) for (int z = -4; z < 20; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 12; y++) w.setBlock(x, y, z, AIR); }
    P->onGround = true;
  }
  void tick(double secs) {   // (keeps the test's chunks streamed in, or a long wait could see them unloaded and made afresh)
    double since = 1;
    for (double t = 0; t < secs; t += TICK_DT) { lv->tick(TICK_DT); if ((since += TICK_DT) >= 1) { since = 0; lv->world().stream({{P->pos, 2, false}}, 20); } }
  }
  void fall(double secs) { lv->tick(TICK_DT); for (double t = 0; t < secs; t += 1 / 60.0) P->move(lv->world(), PlayerInput{}, 1 / 60.f, MoveOpts{}); }
};

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-metal").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }

  printf("[metals]\n");
  {
    check(metalDef("copper") && metalDef("zinc") && metalDef("bismuth") && metalDef("bronze"), "copper, zinc, bismuth, bronze");
    check(namedAlloy({{"copper", 0.9f}, {"tin", 0.1f}}) == "bronze", "nine parts copper to one tin is bronze");
    check(namedAlloy({{"copper", 0.84f}, {"tin", 0.16f}}).empty(), "too much tin: just a mix");
    check(namedAlloy({{"copper", 1.f}}) == "copper", "all copper is copper");
    check(namedAlloy({{"copper", 0.6f}, {"zinc", 0.25f}, {"bismuth", 0.15f}}) == "bismuth_bronze", "bismuth bronze");
    check(namedAlloy({{"gold", 0.8f}, {"copper", 0.2f}}) == "rose_gold", "rose gold");
    const Comp mix = {{"copper", 0.6f}, {"zinc", 0.3f}, {"tin", 0.1f}};
    check(parseCompKey(compKey(mix)) == normComp(mix), "a makeup's key reads back the same");
    check(compKey({{"tin", 0.1f}, {"copper", 0.9f}}) == "@copper=0.90,tin=0.10", "its key: sorted, two places");
    check(compName({{"copper", 0.9f}, {"tin", 0.1f}}) == "Bronze" && compName(mix) == "Copper-Zinc-Tin Alloy", "names: " + compName(mix));
    const Metal b = blendMetal({{"copper", 0.9f}, {"tin", 0.1f}}), cu = blendMetal({{"copper", 1.f}}), sn = blendMetal({{"tin", 1.f}});
    check(b.durability > 0.9f * cu.durability + 0.1f * sn.durability, "bronze is better than its parts");
    const Metal m = blendMetal(mix);
    check(m.tier == std::min(metalDef("zinc")->tier, metalDef("tin")->tier), "a mix works only as well as its weakest metal");
    check(m.rgb.x < cu.rgb.x + 1e-3f && m.rgb.x > std::min(metalDef("zinc")->rgb.x, metalDef("tin")->rgb.x) - 1e-3f, "its colour lies between its metals'");
    check(std::fabs(blendMetal({{"copper", 0.5f}, {"zinc", 0.5f}}).melt - 750) < 1, "the melt point of a half-and-half mix is between");
    ItemStack ing("bronze_ingot", 1);
    check(namedAlloy(compOfStack(ing)) == "bronze", "a bronze ingot is bronze");
    ItemStack tagged("alloy_ingot", 1); tagged.tag = json{{"comp", {{"copper", 0.7}, {"silver", 0.3}}}};
    check(std::fabs(compOfStack(tagged)["silver"] - 0.3f) < 1e-3f, "an alloy ingot carries its makeup");
    check(unitsOf("copper_ore_piece") == 25 && unitsOf("copper_ore_sample") == 10 && unitsOf("copper_ingot") == 100, "units: piece 25, sample 10, ingot 100");
    check(unitsOf("metal_sheet") == 200 && unitsOf("metal_double_sheet") == 400 && unitsOf("metal_double_ingot") == 200, "units: sheet 200, double sheet 400, double ingot 200");
  }

  printf("[tools of any makeup]\n");
  {
    auto head = [](const char* kind, Comp c) { ItemStack h(std::string("metal_") + kind + "_head", 1); json jc = json::object(); for (auto& [k, v] : c) jc[k] = v; h.tag = json{{"comp", jc}}; return h; };
    std::vector<ItemStack> g(4);
    g[0] = head("pickaxe", {{"copper", 0.9f}, {"tin", 0.1f}}); g[2] = ItemStack("stick", 1);
    recipeGate().immersive = true;
    CraftMatch m = matchGrid(g, 2);
    ItemStack t = m ? m.result() : ItemStack();
    check(m && t.id == "mixed_pickaxe", "a bronze pickaxe head and a stick make a pickaxe");
    check(stackName(t) == "Bronze Pickaxe", "  ...named \"" + stackName(t) + "\"");
    const ItemDef* cp = reg().item("copper_pickaxe");
    check(cp && toolOf(t).tier >= cp->tool.tier && toolOf(t).durability > cp->tool.durability, "  ...as hard as copper and longer-lasting");
    g[0] = head("knife", {{"copper", 0.6f}, {"zinc", 0.3f}, {"tin", 0.1f}});
    m = matchGrid(g, 2); t = m ? m.result() : ItemStack();
    check(m && t.id == "mixed_knife" && stackName(t) == "Copper-Zinc-Tin Alloy Knife", "any mix makes a knife: " + stackName(t));
    check(partToolModelKey(t).find("@copper=0.60") != std::string::npos, "its model knows its makeup");
    std::vector<ItemStack> g3(9);
    ItemStack al("alloy_ingot", 1); al.tag = json{{"comp", {{"copper", 0.7}, {"silver", 0.3}}}};
    g3[0] = al; g3[1] = ItemStack("copper_ingot", 1); g3[2] = al; g3[4] = ItemStack("stick", 1); g3[7] = ItemStack("stick", 1);
    recipeGate().immersive = false;
    m = matchGrid(g3, 3); t = m ? m.result() : ItemStack();
    check(m && t.id == "mixed_pickaxe" && stackName(t).find("Copper") != std::string::npos && stackName(t).find("Copper-Silver Alloy") != std::string::npos, "alloy ingots in the pickaxe pattern: " + stackName(t));
    {   // drawn: the model key keeps each section's whole makeup (an unnamed mix has commas in it), and an unnamed mix is
        // painted in its blend flecked with each metal by its share; a named alloy is its own colour
      const std::string key = partToolModelKey(t);
      const size_t c1 = key.find(':', 3), c2 = key.find(':', c1 + 1);
      const std::vector<std::string> mats = modelKeyMaterials(key.substr(c1 + 1, c2 - c1 - 1));
      check(mats.size() == 3 && mats[0].rfind("@copper=0.70", 0) == 0 && mats[0].find("silver=0.30") != std::string::npos && mats[1] == "copper", "the model key splits back into 3 sections: " + key);
      const MaterialPaint mp = materialPaint(mats[0]);
      const vec3 cu = metalDef("copper")->rgb, ag = metalDef("silver")->rgb;
      int nCu = 0, nAg = 0;
      for (u32 h = 0; h < 4000; h++) { const vec3 c = mp.at(h * 2654435761u); if (glm::distance(c, mp.base) < 1e-4f) continue; if (glm::distance(c, cu) < glm::distance(c, ag)) nCu++; else nAg++; }
      check(mp.flecks.size() == 2 && nAg > 0 && nCu > nAg, "a 70/30 copper-silver mix: copper flecks outnumber silver (" + std::to_string(nCu) + "/" + std::to_string(nAg) + ")");
      check(materialPaint("@copper=0.90,tin=0.10").flecks.empty() && materialPaint("copper").flecks.empty(), "bronze, and plain copper, are their own even colour");
    }
    {   // every metal tool, weapon and armour piece is procedural: from the grid (classic too), a Tool Forge head, trades and loot
      const ItemStack ip = proceduralOf("iron_pickaxe");
      const ItemDef* ipd = reg().item("iron_pickaxe");
      check(ip.id == "mixed_pickaxe" && stackName(ip) == "Iron Pickaxe" && ipd && toolOf(ip).tier == ipd->tool.tier, "a fixed iron pickaxe becomes the procedural one: " + stackName(ip));
      const ItemStack ih = proceduralOf("iron_helmet");
      check(ih.id == "mixed_helmet" && std::fabs(armorPointsOf(ih) - 2.f) < 0.01f, "an iron helmet: procedural, the same protection");
      check(proceduralOf("wood_sword").id == "wood_sword" && proceduralOf("diamond_sword").id == "diamond_sword" && proceduralOf("bread").id == "bread", "wood, gems and the rest stay as they are");
      std::vector<ItemStack> g3b(9);
      for (int i : {0, 1, 2}) g3b[i] = ItemStack("iron_ingot", 1);
      g3b[4] = ItemStack("stick", 1); g3b[7] = ItemStack("stick", 1);
      CraftMatch ipm = matchGrid(g3b, 3); const ItemStack ipr = ipm ? ipm.result() : ItemStack();
      check(ipm && ipr.id == "mixed_pickaxe" && stackName(ipr) == "Iron Pickaxe", "classic: three iron ingots make a procedural iron pickaxe");
      std::vector<ItemStack> g2(4); g2[0] = ItemStack("iron_pickaxe_head", 1); g2[2] = ItemStack("stick", 1);
      CraftMatch fh = matchGrid(g2, 2); const ItemStack fhr = fh ? fh.result() : ItemStack();
      check(fh && fhr.id == "mixed_pickaxe" && stackName(fhr) == "Iron Pickaxe", "a Tool Forge head and a stick: the procedural pickaxe");
      // a named alloy still varies with its makeup: nearer the ideal mix, the better the tool, and a touch different in colour
      ItemStack b1 = head("pickaxe", {{"copper", 0.90f}, {"tin", 0.10f}}), b2 = head("pickaxe", {{"copper", 0.86f}, {"tin", 0.14f}});
      g[0] = b1; g[2] = ItemStack("stick", 1); recipeGate().immersive = true;
      CraftMatch m1 = matchGrid(g, 2); g[0] = b2; CraftMatch m2 = matchGrid(g, 2); recipeGate().immersive = false;
      const ItemStack t1 = m1 ? m1.result() : ItemStack(), t2 = m2 ? m2.result() : ItemStack();
      check(stackName(t1) == "Bronze Pickaxe" && stackName(t2) == "Bronze Pickaxe" && toolOf(t1).durability > toolOf(t2).durability, "bronze near its ideal outlasts an off-ratio bronze (" + std::to_string(toolOf(t1).durability) + " vs " + std::to_string(toolOf(t2).durability) + ")");
      // armour follows the blend: more iron in a copper-iron mix, more protection
      ItemStack a1, a2;
      makePartArmor("helmet", std::vector<std::string>(5, "@copper=0.70,iron=0.30"), a1); makePartArmor("helmet", std::vector<std::string>(5, "@copper=0.30,iron=0.70"), a2);
      check(armorPointsOf(a2) > armorPointsOf(a1), "armour protection follows the mix (" + std::to_string(armorPointsOf(a1)) + " < " + std::to_string(armorPointsOf(a2)) + ")");
      {   // worn: the pieces' points summed before rounding; a mixed piece's fire resistance counts
        Player pl; pl.armor.assign(4, ItemStack());
        ItemStack h1("mixed_helmet", 1), h2("mixed_boots", 1);
        h1.tag = json{{"pa", {{"p", "helmet"}, {"m", json::array()}, {"pts", 1.4}, {"dur", 100}, {"fr", 0.2}}}};
        h2.tag = json{{"pa", {{"p", "boots"}, {"m", json::array()}, {"pts", 1.4}, {"dur", 100}, {"fr", 0.0}}}};
        pl.armor[0] = h1; pl.armor[3] = h2;
        check(armorPoints(pl) == 3, "two pieces of 1.4 points: 3 (" + std::to_string(armorPoints(pl)) + ")");
        check(fireResist(pl) > 0.19f, "a mixed piece's fire resistance counts");
      }
    }
    ItemStack hd = head("axe", {{"copper", 0.9f}, {"zinc", 0.1f}});
    check(stackName(hd) == "Brass Axe Head", "a head named by its makeup: " + stackName(hd));
  }

  printf("[ores]\n");
  {
    check(reg().blockId("zinc_ore") && reg().blockId("bismuth_ore"), "zinc and bismuth ores");
    bool items = true;
    for (const char* m : {"copper", "tin", "zinc", "bismuth", "iron", "gold", "silver"})
      for (const char* k : {"_ore_piece", "_ore_sample"}) if (!reg().item(std::string(m) + k)) { items = false; printf("    %s%s\n", m, k); }
    check(items, "ore pieces and samples for every metal");
    check(metalOfOreBlock("copper_ore") == "copper" && metalOfOreBlock("zinc_ore") == "zinc" && metalOfOreBlock("stone").empty(), "which metal an ore block holds");
    recipeGate().immersive = true;
    check(smeltResult("copper_ore", "furnace").empty() && smeltResult("copper_ore_piece", "furnace").empty(), "immersive: the furnace won't smelt ore");
    check(smeltResult("steel_blend", "blast") == "pig_iron", "  ...the blast furnace makes pig iron");
    recipeGate().immersive = false;
    check(smeltResult("copper_ore", "furnace") == "copper_ingot", "classic: it smelts as ever");
  }
  Rig rg("Metal", true, true);
  World& w = rg.lv->world();
  {
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->inv[0] = ItemStack("crude_pickaxe", 1);
    const BlockId CU = reg().blockId("copper_ore");
    int pieces = 0, ores = 0;
    for (int i = 0; i < 20; i++) { w.setBlock(5, rg.Y + 1, 5, CU); pc.breakBlock(5, rg.Y + 1, 5); rg.P->inv[0].d = 0; }
    for (auto& e : rg.lv->entities()) if (!e->removed && e->type == "item") { const json st = e->data.value("stack", json::object()); if (st.value("id", "") == "copper_ore_piece") pieces += st.value("n", 1); if (st.value("id", "") == "copper_ore") ores++; e->removed = true; }
    check(pieces >= 20 && pieces <= 40 && ores == 0, "immersive: copper ore breaks into ore pieces (" + std::to_string(pieces) + " from 20)");
    int zinc = 0, bis = 0, samples = 0;
    for (int ox : {700, 1700}) {
      const vec3 at(ox + 8, dimBase(DIM_OVERWORLD) + 100, ox + 8);
      for (int k = 0; k < 600; k++) { w.stream({{at, 3, false}, {rg.P->pos, 2, false}}, 40); bool all = true; for (int cx = -2; cx <= 1; cx++) for (int cz = -2; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, (ox >> 4) + cx, (ox >> 4) + cz)) all = false; if (all) break; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
      for (int x = ox - 32; x < ox + 32; x++) for (int z = ox - 32; z < ox + 32; z++) for (int y = dimBase(DIM_OVERWORLD) + 1; y < dimBase(DIM_OVERWORLD) + 250; y++) {
        const BlockId b = w.getBlock(x, y, z);
        if (b == reg().blockId("zinc_ore")) zinc++; else if (b == reg().blockId("bismuth_ore")) bis++;
        else if (b && reg().block(b).key.find("_sample") != std::string::npos) samples++;
      }
    }
    check(zinc > 0 && bis > 0, "zinc and bismuth in the ground (" + std::to_string(zinc) + ", " + std::to_string(bis) + ")");
    check(samples > 0, "ore samples lying on the surface over ore (" + std::to_string(samples) + ")");
  }

  printf("[vessels and casting]\n");
  {
    ItemStack v("ceramic_vessel", 1), ore("copper_ore_piece", 3), sn("tin_ore_sample", 1);
    check(vesselAdd(v, ore) && ore.empty() && vesselAdd(v, sn), "copper and tin ore into a vessel");
    check(vesselUnits(v) == 85, "85 units of metal in it");
    const BlockId FIRE = reg().blockId("campfire");
    const int X = 6, Z = 6, Y1 = rg.Y + 1;
    w.setBlock(X, Y1, Z, FIRE, 0);
    *w.blockEntity(X, Y1, Z, true) = json{{"type", "campfire"}, {"items", {v.toJson(), nullptr, nullptr, nullptr}}, {"t", {0, 0, 0, 0}}, {"temp", 20}, {"burn", 0}, {"fuel", {"charcoal", "charcoal", "charcoal", "charcoal"}}};
    rg.tick(120);
    json* fe = w.blockEntity(X, Y1, Z);
    ItemStack hot = fe && (*fe)["items"].is_array() ? ItemStack::fromJson((*fe)["items"][0]) : ItemStack();
    check(vesselMolten(hot), "on a charcoal fire it melts (" + std::to_string((int)itemHeat(hot)) + " degrees)");
    check(namedAlloy(vesselComp(hot)) == "bronze" && vesselUnits(hot) == 85, "  ...into 85 units of bronze");
    ItemStack mold("ceramic_ingot_mold", 1); std::string why;
    check(!pourInto(hot, mold, &why) && why == "Not enough metal for that mold", "not enough for an ingot: it won't pour");
    {   // short of a mold's worth isn't stuck: top it up with more ore, it melts in with the rest
      ItemStack more("copper_ore_piece", 1);
      check(vesselAdd(hot, more) && vesselUnits(hot) == 110, "more ore goes into a vessel already holding metal (110 units)");
      ItemStack tipped = hot; check(vesselEmpty(tipped).size() == 1 && vesselUnits(tipped) == 85, "  ...tipping it out gives back the ore, the metal stays");
      vesselMeltStep(hot);
      check(vesselMolten(hot) && vesselUnits(hot) == 110 && hot.tag.contains("metal") && !hot.tag.contains("c"), "  ...and melts in with it: 110 molten units");
      ItemStack m1("ceramic_ingot_mold", 1), m2("ceramic_ingot_mold", 1);
      ItemStack v4("ceramic_vessel", 1), c8("copper_ore_piece", 8); vesselAdd(v4, c8); setItemHeat(v4, 1200); vesselMeltStep(v4);
      check(pourInto(v4, m1, &why) && pourInto(v4, m2, &why) && !(m1.tag == m2.tag && m1.id == m2.id), "two molds poured alike still don't stack (each pour its own)");
    }
    ItemStack v2("ceramic_vessel", 1), cu("copper_ore_piece", 4);
    vesselAdd(v2, cu); setItemHeat(v2, 1200); vesselMeltStep(v2);
    ItemStack pm("ceramic_pickaxe_mold", 1);
    check(vesselMolten(v2) && pourInto(v2, pm, &why) && vesselUnits(v2) == 0, "a hundred units of copper poured into a pickaxe mold");
    check(!breakMold(pm).first.empty() == false, "  ...still molten, it can't come out");
    setItemHeat(pm, 300);
    auto [piece, keep] = breakMold(pm);
    check(piece.id == "metal_pickaxe_head" && namedAlloy(compOfStack(piece)) == "copper", "cooled, the mold gives a copper pickaxe head");
    ItemStack iron("iron_ingot", 1), v3("ceramic_vessel", 1); vesselAdd(v3, iron); setItemHeat(v3, 1600); vesselMeltStep(v3);   // (iron ore is the bloomery's; an ingot melts)
    ItemStack am("ceramic_axe_mold", 1);
    check(vesselMolten(v3) && !pourInto(v3, am, &why), "iron won't cast in a tool mold (" + why + ")");
  }

  printf("[vessel controls]\n");
  {
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; pc.target = RayHit{};
    rg.P->inv[0] = ItemStack("ceramic_vessel", 1); rg.P->offhand[0] = ItemStack("copper_ore_piece", 6);
    pc.use(false, PlayerInput{});
    check(vesselUnits(rg.P->inv[0]) == 150 && rg.P->offhand[0].empty(), "use the vessel with ore in the other hand: it goes in");
    setItemHeat(rg.P->inv[0], 1200); vesselMeltStep(rg.P->inv[0]);
    rg.P->offhand[0] = ItemStack("ceramic_shovel_mold", 3);
    pc.use(false, PlayerInput{});
    int filled = 0; for (auto& st : rg.P->inv) if (st.id == "ceramic_shovel_mold" && moldFilled(st)) filled++;
    check(vesselUnits(rg.P->inv[0]) == 50 && rg.P->offhand[0].n == 2 && filled == 1, "a molten vessel pours into a mold from the other hand");
    for (auto& st : rg.P->inv) if (moldFilled(st)) setItemHeat(st, 200);
    for (int i = 0; i < (int)rg.P->inv.size(); i++) if (moldFilled(rg.P->inv[i])) { std::swap(rg.P->inv[0], rg.P->inv[i]); break; }
    rg.P->offhand[0] = ItemStack();
    pc.use(false, PlayerInput{});
    int heads = 0; for (auto& st : rg.P->inv) if (st.id == "metal_shovel_head") heads++;
    check(heads == 1, "use the cooled mold: a copper shovel head");
    // the mold needn't be in the other hand: the first empty fired mold in the bag
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->inv[0] = ItemStack("ceramic_vessel", 1); rg.P->offhand[0] = ItemStack("copper_ore_piece", 8);
    pc.use(false, PlayerInput{}); setItemHeat(rg.P->inv[0], 1200); vesselMeltStep(rg.P->inv[0]);
    rg.P->inv[5] = ItemStack("ceramic_ingot_mold", 1);
    pc.use(false, PlayerInput{});
    check(moldFilled(rg.P->inv[5]) || [&] { for (auto& st : rg.P->inv) if (st.id == "ceramic_ingot_mold" && moldFilled(st)) return true; return false; }(), "a molten vessel pours into a mold carried in the bag");
    // and into a mold set down on the ground (an item pile)
    const BlockId PILE = reg().blockId("item_pile");
    const int PX = 6, PY = rg.Y + 1, PZ = 6;
    w.setBlock(PX, PY - 1, PZ, reg().blockId("stone")); w.setBlock(PX, PY, PZ, PILE);
    w.setBlockEntity(PX, PY, PZ, {{"type", "pile"}, {"items", {ItemStack("ceramic_axe_mold", 2).toJson(), nullptr, nullptr, nullptr}}, {"rot", {0, 0, 0, 0}}});
    for (auto& st : rg.P->inv) if (st.id == "ceramic_ingot_mold") st = ItemStack();
    const int before = vesselUnits(rg.P->inv[0]);
    RayHit h; h.hit = true; h.block = ivec3(PX, PY, PZ); h.normal = ivec3(0, 1, 0); h.id = PILE; h.point = vec3(PX + 0.3f, PY + 0.1f, PZ + 0.3f);
    pc.target = h;
    pc.use(false, PlayerInput{});
    pc.target = RayHit{};
    const json* pe = w.blockEntity(PX, PY, PZ);
    int filledOnGround = 0, emptyOnGround = 0;
    if (pe) for (auto& it : (*pe)["items"]) if (!it.is_null()) { ItemStack s = ItemStack::fromJson(it); if (moldFilled(s)) filledOnGround += s.n; else emptyOnGround += s.n; }
    check(filledOnGround == 1 && emptyOnGround == 1 && vesselUnits(rg.P->inv[0]) == before - 100, "a molten vessel used on molds on the ground fills one of them (" + std::to_string(filledOnGround) + " filled, " + std::to_string(emptyOnGround) + " empty)");
  }

  printf("[forge]\n");
  {
    const BlockId STONE = reg().blockId("stone"), FORGE = reg().blockId("forge"), BELLOWS = reg().blockId("bellows");
    check(FORGE && BELLOWS, "the forge and the bellows");
    auto pit = [&](int X, int Z) { for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) if (dx || dz) w.setBlock(X + dx, rg.Y + 1, Z + dz, STONE); };
    const int X = 14, Z = 4, Y1 = rg.Y + 1;
    pit(X, Z);
    ItemStack coal("charcoal", 12);
    check(!forgeBuild(w, X + 4, Y1, Z, coal), "charcoal on open ground isn't a forge");
    check(forgeBuild(w, X, Y1, Z, coal) && w.getBlock(X, Y1, Z) == FORGE, "charcoal in a stone-ringed pit makes a forge");
    for (int i = 0; i < 7; i++) forgeAddFuel(w, X, Y1, Z, coal);
    check(coal.n == 4, "eight charcoal in it");
    check(forgeLight(*rg.lv, w, X, Y1, Z), "lit");
    ItemStack ingot("iron_ingot", 1);
    check(forgePut(w, X, Y1, Z, ingot) && ingot.empty(), "an iron ingot set in it");
    rg.tick(150);
    json* e = w.blockEntity(X, Y1, Z);
    const float temp = e ? e->value("temp", 0.f) : 0;
    check(temp > 1200, "it burns hot (" + std::to_string((int)temp) + ")");
    const ItemStack hotIron = e ? ItemStack::fromJson((*e)["items"][0]) : ItemStack();
    check(itemHeat(hotIron) >= metalDef("iron")->melt * 0.6f, "the iron at working heat (" + std::to_string((int)itemHeat(hotIron)) + ")");
    // the crucible: mithril melts only with the bellows blowing
    ItemStack cru("crucible", 1), mi("mithril_ingot", 2);
    check(vesselAdd(cru, mi), "mithril into a crucible");
    forgePut(w, X, Y1, Z, cru);
    rg.tick(60);
    e = w.blockEntity(X, Y1, Z);
    check(e && !vesselMolten(ItemStack::fromJson((*e)["items"][1])), "without bellows the forge can't melt mithril");
    w.setBlock(X + 1, Y1, Z, BELLOWS); w.setBlock(X - 1, Y1, Z, BELLOWS);
    rg.tick(5);
    check(w.blockEntity(X, Y1, Z)->value("temp", 0.f) <= FORGE_PEAK + 1, "bellows standing idle add nothing");
    auto pumpBoth = [&] { bellowsPump(w, X + 1, Y1, Z); bellowsPump(w, X - 1, Y1, Z); };
    for (int i = 0; i < 4; i++) { ItemStack c2("charcoal", 1); forgeAddFuel(w, X, Y1, Z, c2); }
    for (int i = 0; i < 15; i++) { pumpBoth(); rg.tick(8); }   // someone working the bellows
    e = w.blockEntity(X, Y1, Z);
    const ItemStack cr2 = e ? ItemStack::fromJson((*e)["items"][1]) : ItemStack();
    check(e && e->value("temp", 0.f) > 1700 && vesselMolten(cr2), "with bellows it does (" + std::to_string((int)(e ? e->value("temp", 0.f) : 0)) + ")");
  }

  printf("[anvil]\n");
  {
    const AnvilRecipe* pick = anvilRecipe("pickaxe");
    check(pick && pick->in == "ingot" && pick->plan.size() == 3, "the pickaxe head's plan");
    const int t1 = anvilTarget(12345, "pickaxe"), t2 = anvilTarget(12345, "pickaxe"), t3 = anvilTarget(999, "pickaxe");
    check(t1 == t2 && t1 >= 70 && t1 <= 119 && t3 >= 70 && t3 <= 119, "the target is fixed for a world (" + std::to_string(t1) + ")");
    AnvilWork wk; wk.recipe = "pickaxe"; wk.target = 80;
    anvilApply(wk, AnvilMove::Draw); check(wk.value == 0, "the work can't go below nothing");
    for (int i = 0; i < 20; i++) anvilApply(wk, AnvilMove::Shrink);
    check(wk.value == 150, "...or past 150 (the metal isn't ruined)");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 80;
    for (AnvilMove m : {AnvilMove::Upset, AnvilMove::Upset, AnvilMove::Upset, AnvilMove::Upset, AnvilMove::Upset, AnvilMove::Bend, AnvilMove::Draw, AnvilMove::Punch}) anvilApply(wk, m);
    // 13*5 + 7 - 15 + 2 = 59: short of 80
    check(wk.value == 59 && !anvilDone(wk), "not there yet (59 of 80)");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 80;
    for (AnvilMove m : {AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Bend, AnvilMove::Draw, AnvilMove::Punch}) anvilApply(wk, m);
    // 80 + 7 - 15 + 2 = 74: within 3? no (6 off)
    check(wk.value == 74 && !anvilDone(wk), "six off the mark: not done");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 80;
    for (AnvilMove m : {AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Upset, AnvilMove::Bend, AnvilMove::Draw, AnvilMove::Punch}) anvilApply(wk, m);
    // 80 + 13 + 7 - 15 + 2 = 87: 7 off
    check(!anvilDone(wk), "seven off: not done");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 82;
    for (AnvilMove m : {AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Bend, AnvilMove::Draw, AnvilMove::Punch}) anvilApply(wk, m);
    check(wk.value == 74 ? !anvilDone(wk) : true, "");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 76;
    for (AnvilMove m : {AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Bend, AnvilMove::Draw, AnvilMove::Punch}) anvilApply(wk, m);
    check(anvilDone(wk) && anvilQuality(wk) < 1.f, "two off and on plan: done, a little less good (" + std::to_string(anvilQuality(wk)) + ")");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 74;
    for (AnvilMove m : {AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Bend, AnvilMove::Draw, AnvilMove::Punch}) anvilApply(wk, m);
    check(anvilDone(wk) && anvilQuality(wk) > 1.1f, "exact and on plan: the best work");
    wk = AnvilWork{}; wk.recipe = "pickaxe"; wk.target = 74;
    for (AnvilMove m : {AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Shrink, AnvilMove::Bend, AnvilMove::Punch, AnvilMove::Draw}) anvilApply(wk, m);
    check(!anvilDone(wk), "the right number but the last step not a punch: not done");
    ItemStack cold("copper_ingot", 1), hot("copper_ingot", 1); setItemHeat(hot, 900);
    std::string why;
    check(!anvilCanWork(cold, *pick, 1, &why) && anvilCanWork(hot, *pick, 1, &why), "a cold ingot can't be worked; a hot one can");
    ItemStack hotIron("iron_ingot", 1); setItemHeat(hotIron, 1200);
    // an anvil works its own tier and the one above (TFC): stone works copper (to make the first anvil), bronze works iron
    check(anvilCanWork(hot, *pick, 0, &why), "the stone anvil works copper (the first anvil comes from it)");
    check(!anvilCanWork(hotIron, *pick, 1, &why) && anvilCanWork(hotIron, *pick, 2, &why), "iron needs a bronze anvil, not a copper one");
    ItemStack outp = anvilOutput(*pick, hot, 1.15f);
    check(outp.id == "metal_pickaxe_head" && namedAlloy(compOfStack(outp)) == "copper" && outp.tag.value("ql", 0.0) > 1.1, "a copper pickaxe head, the quality in it");
  }

  printf("[anvil controls]\n");
  {
    PlayerController pc(*rg.lv, *rg.P);
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->inv[0] = ItemStack("stone_hammer", 1);
    pc.target = RayHit{}; pc.target.hit = true; pc.target.block = ivec3(2, rg.Y, 14); pc.target.normal = ivec3(0, 1, 0); pc.target.id = w.getBlock(2, rg.Y, 14);
    pc.use(false, PlayerInput{});
    check(w.getBlock(2, rg.Y, 14) == reg().blockId("stone_anvil"), "a hammer on raw stone makes a stone anvil");
    int tier = -1;
    auto h = rg.bus.on("screenOpen", [&](const json& a) { if (a.is_array() && a.size() > 2 && a[1] == "anvil_smith") tier = a[2].value("tier", -1); return true; });
    pc.target.id = w.getBlock(2, rg.Y, 14);
    pc.use(false, PlayerInput{});
    check(tier == 0, "a hammer on the stone anvil starts the work (tier 0)");
    w.setBlock(4, rg.Y + 1, 14, reg().blockId("bronze_anvil"));
    pc.target.block = ivec3(4, rg.Y + 1, 14); pc.target.id = w.getBlock(4, rg.Y + 1, 14); tier = -1;
    pc.use(false, PlayerInput{});
    check(tier == 2, "a bronze anvil is tier 2");
    rg.bus.off(h);
  }

  printf("[bloomery]\n");
  {
    const BlockId BLOOM = reg().blockId("bloomery"), BRICK = reg().blockId("stone_bricks");
    check(BLOOM != 0, "the bloomery");
    const int X = 10, Z = 16, Y1 = rg.Y + 1;
    w.setBlock(X, Y1, Z, BLOOM);
    ItemStack ore("iron_ore_piece", 8), coal("charcoal", 8);
    std::string why;
    check(!bloomeryLoad(w, X, Y1, Z, ore, &why) && ore.n == 8, "no chimney: it takes nothing (" + why + ")");
    w.setBlock(X, Y1 + 1, Z, BRICK);
    check(bloomeryCapacity(w, X, Y1, Z) == 8, "a chimney one high: room for eight");
    while (!ore.empty() && bloomeryLoad(w, X, Y1, Z, ore)) {}
    while (!coal.empty() && bloomeryLoad(w, X, Y1, Z, coal)) {}
    check(ore.empty() && coal.empty(), "eight iron ore and eight charcoal in");
    check(bloomeryLight(*rg.lv, w, X, Y1, Z), "lit");
    rg.tick(gameHourSecs(*rg.lv) * BLOOMERY_HOURS + 5);
    ItemStack b1 = bloomeryTake(w, X, Y1, Z), b2 = bloomeryTake(w, X, Y1, Z), b3 = bloomeryTake(w, X, Y1, Z);
    check(b1.id == "raw_bloom" && b2.id == "raw_bloom" && b3.empty(), "five hours on: two blooms (200 units of iron)");
    {   // carried while lit: the fire goes out, the load stays, and it lights again where it's set down
      ItemStack o2("iron_ore_piece", 2), c2("charcoal", 2);
      while (!o2.empty() && bloomeryLoad(w, X, Y1, Z, o2)) {}
      while (!c2.empty() && bloomeryLoad(w, X, Y1, Z, c2)) {}
      check(bloomeryLight(*rg.lv, w, X, Y1, Z), "lit again");
      PlayerController pc(*rg.lv, *rg.P);
      pc.pickUpBlock(X, Y1, Z);
      check(!rg.P->carrying.is_null() && !rg.P->carrying["ent"].value("lit", true), "picked up lit: carried out (not lit)");
      const json ent = rg.P->carrying["ent"]; rg.P->carrying = json();
      w.setBlock(X, Y1, Z, BLOOM); *w.blockEntity(X, Y1, Z, true) = ent;
      check(bloomeryLight(*rg.lv, w, X, Y1, Z), "set down, it lights again with its load");
    }
  }

  printf("[procedural gear: review fixes]\n");
  {
    // a named alloy wears its own pieces: an anvil-made bronze chestplate is as good as a bronze one (better near the ideal)
    ItemStack bc; makePartArmor("chestplate", std::vector<std::string>(8, "@copper=0.90,tin=0.10"), bc);
    const ItemDef* bcd = reg().item("bronze_chestplate"); const float bronzePts = bcd ? bcd->props["armor"].value("points", 0.f) : 5.f;
    check(armorPointsOf(bc) >= bronzePts, "an ideal bronze chestplate: at least a bronze chestplate's protection (" + std::to_string(armorPointsOf(bc)) + ")");
    ItemStack cc; makePartArmor("chestplate", std::vector<std::string>(8, "copper"), cc);
    check(armorPointsOf(bc) > armorPointsOf(cc), "  ...and better than copper");
    // smithing a used fixed copper tool into bronze keeps it procedural (and its wear marks, its enchantments)
    ItemStack used("copper_pickaxe", 1); used.d = 10; used.tag = json{{"gx", 3}, {"en", {{"efficiency", 1}}}};
    StationResult sr = stationResult("smithing", used, ItemStack("tin_ingot", 1));
    check(sr.ok && isPartTool(sr.out) && stackName(sr.out) == "Bronze Pickaxe" && sr.out.tag.contains("gx") && sr.out.tag.contains("en"), "smithing a used copper pickaxe: a procedural bronze one, its marks and enchantments kept");
    // two filled molds that ended up stacked: breaking one leaves the other full
    ItemStack two("ceramic_ingot_mold", 2); two.tag = json{{"metal", {{"comp", {{"copper", 1.0}}}, {"u", 100}}}};
    ItemStack opened;
    auto [pc, kept] = breakMold(two, &opened);
    check(!pc.empty() && two.n == 1 && moldFilled(two) && (kept ? opened.id == "ceramic_ingot_mold" && !moldFilled(opened) : opened.empty()), "a stack of two filled molds: one opened (handed back empty), the other still full");
    // silver is holy, procedural or not
    ItemStack ss = proceduralOf("silver_sword");
    check(gearIsHoly(ss) && gearIsHoly(ItemStack("silver_sword", 1)) && !gearIsHoly(proceduralOf("iron_sword")), "a procedural silver sword is still holy");
    // a raw bloom (or pig iron) can't be melted into an ingot: it's worked on the anvil
    ItemStack vb("crucible", 1), bloom("raw_bloom", 1), pig("pig_iron", 1), ingot("iron_ingot", 1);
    check(!vesselAdd(vb, bloom) && !vesselAdd(vb, pig) && vesselAdd(vb, ingot), "a raw bloom or pig iron won't go in the crucible; a wrought iron ingot will");
    // procedural armour mends at the anvil with its own metal
    ItemStack ih = proceduralOf("iron_helmet"); ih.d = 40;
    StationResult mend = stationResult("anvil", ih, ItemStack("iron_ingot", 2));
    check(mend.ok && mend.out.d < 40, "a worn procedural iron helmet mends at the anvil with iron");
  }

  printf("[TFC progression: no shortcuts]\n");
  {
    auto recipeFor = [](const std::string& out, const std::string& anyIn) -> const Recipe* {
      for (auto& r : reg().recipes) if (r.out == out) { const std::string j = json(r.items).dump() + json(r.key).dump(); if (j.find(anyIn) != std::string::npos) return &r; }
      return nullptr; };
    recipeGate().immersive = true;
    check(smeltResult("steel_blend", "furnace").empty() && smeltResult("steel_blend", "blast") == "pig_iron", "immersive: steel blend makes pig iron, and only in the blast furnace");
    check(smeltResult("mithril_ore", "furnace").empty() && smeltResult("adamantite_ore", "blast").empty() && unitsOf("mithril_ore") > 0, "immersive: mithril and adamantite ore melt in the crucible, not the furnace");
    ItemStack v("ceramic_vessel", 1), ironP("iron_ore_piece", 2), cuP("copper_ore_piece", 2);
    check(!vesselAdd(v, ironP) && ironP.n == 2 && vesselAdd(v, cuP), "iron ore won't go in a vessel: iron is a bloomery's work");
    const Recipe* anv = recipeFor("anvil", "iron_block"); const Recipe* pelt = recipeFor("leather", "pelt"); const Recipe* hide = recipeFor("raw_hide", "pelt");
    const Recipe* wb = recipeFor("bucket", "lumber");
    check(anv && !recipeOpen(*anv), "immersive: no anvil from iron blocks (smith it)");
    check(pelt && !recipeOpen(*pelt) && hide && recipeOpen(*hide), "immersive: a pelt is a raw hide to tan, not leather");
    check(wb && recipeOpen(*wb), "immersive: a wooden bucket from lumber (water for the barrel before iron)");
    recipeGate().immersive = false;
    check(wb && !recipeOpen(*wb) && pelt && recipeOpen(*pelt), "classic: as it was");
    auto anvilR = [](const std::string& in) -> const AnvilRecipe* { for (auto& r : anvilRecipes()) if (r.in == in) return &r; return nullptr; };
    check(anvilR("bloom") && anvilR("bloom")->out == "refined_bloom" && anvilR("refined_bloom") && anvilR("refined_bloom")->out == "iron_ingot", "a raw bloom is refined, then worked into wrought iron");
    check(anvilR("pig_iron") && anvilR("pig_iron")->out == "high_carbon_steel" && anvilR("high_carbon_steel") && anvilR("high_carbon_steel")->out == "steel_ingot", "pig iron becomes high-carbon steel, then steel");
    check(anvilPieceOf(ItemStack("refined_bloom", 1)) == "refined_bloom" && heatable("high_carbon_steel"), "  ...both go on the anvil and in the forge");
    // armour: the anvil makes an unfinished piece; padding (leather, wool cloth or burlap) finishes it
    const AnvilRecipe* helm = nullptr; for (auto& r : anvilRecipes()) if (r.out == "armor:helmet") helm = &r;
    ItemStack ds("metal_double_sheet", 1); ds.tag = json{{"comp", {{"copper", 0.9}, {"tin", 0.1}}}}; setItemHeat(ds, 700);
    const ItemStack un = helm ? anvilOutput(*helm, ds, 1.f) : ItemStack();
    check(un.id == "unfinished_helmet" && isPartArmor(un) && stackName(un).rfind("Unfinished", 0) == 0, "the anvil makes an unfinished helmet: " + stackName(un));
    std::vector<ItemStack> pg(4); pg[0] = un; setItemHeat(pg[0], 20); pg[1] = ItemStack("wool_cloth", 1);
    CraftMatch fin = matchGrid(pg, 2); const ItemStack done = fin ? fin.result() : ItemStack();
    check(fin && done.id == "mixed_helmet" && stackName(done) == "Bronze Helmet" && armorPointsOf(done) > 0, "padded with wool cloth: a bronze helmet");
    pg[1] = ItemStack("stick", 1);
    check(!matchGrid(pg, 2) || matchGrid(pg, 2).out != "mixed_helmet", "  ...not with a stick");
  }

  printf("[cooling, tongs, prospecting, recipes]\n");
  {
    ItemStack h("metal_axe_head", 1); h.tag = json{{"comp", {{"copper", 1.0}}}}; setItemHeat(h, 700);
    check(quench(h) && itemHeat(h) <= 25 && h.tag.value("q", 0) == 1, "a hot head quenched: cold, and a little tougher");
    ItemStack c("copper_ingot", 1);
    check(!quench(c), "a cold ingot: nothing to quench");
    {   // hot things cool wherever they are: dropped, in a chest, on a pile; at once in water, a water cauldron or a barrel of water
      World& w = rg.lv->world();
      auto drop = [&](vec3 at) { auto e = std::make_unique<Entity>(); e->type = "item"; e->realm = REALM_MAIN; e->noGravity = true; e->pos = at;
                                 ItemStack s("copper_ingot", 1); setItemHeat(s, 800); e->data["stack"] = s.toJson(); return rg.lv->spawn(std::move(e)); };
      auto heatOf = [](Entity* e) { return itemHeat(ItemStack::fromJson(e->data["stack"])); };
      const int Y1 = rg.Y + 1;
      Entity* air = drop(vec3(22.5f, Y1 + 0.2f, 2.5f));
      w.setBlock(24, Y1, 2, reg().blockId("water")); Entity* wet = drop(vec3(24.5f, Y1 + 0.2f, 2.5f));
      const BlockId CAUL = reg().blockId("water_cauldron");
      if (CAUL) w.setBlock(26, Y1, 2, CAUL, 3);
      Entity* caul = drop(vec3(26.5f, Y1 + 0.3f, 2.5f));
      ItemStack hot("copper_ingot", 1); setItemHeat(hot, 800);
      w.setBlock(22, Y1, 6, reg().blockId("chest")); *w.blockEntity(22, Y1, 6, true) = json{{"type", "chest"}, {"items", json::array({hot.toJson(), nullptr})}};
      ItemStack pile = hot; pile.n = 4;
      w.setBlock(24, Y1, 6, reg().blockId("ingot_pile"), 1); *w.blockEntity(24, Y1, 6, true) = json{{"type", "ingots"}, {"item", pile.toJson()}};
      w.setBlock(26, Y1, 6, reg().blockId("sealed_barrel")); *w.blockEntity(26, Y1, 6, true) = json{{"type", "sbarrel"}, {"liquid", "water"}, {"amount", 4}, {"items", json::array({hot.toJson(), nullptr, nullptr, nullptr})}, {"sealed", false}, {"t", 0.0}};
      rg.tick(10);
      check(heatOf(wet) <= 25 && (!CAUL || heatOf(caul) <= 25), "dropped in water (or a water cauldron): cold at once");
      check(heatOf(air) < 800 - 50, "dropped on the ground: it cools (" + std::to_string((int)heatOf(air)) + ")");
      check(itemHeat(ItemStack::fromJson((*w.blockEntity(22, Y1, 6))["items"][0])) < 800 - 50, "in a chest: it cools");
      check(itemHeat(ItemStack::fromJson((*w.blockEntity(24, Y1, 6))["item"])) < 800 - 50, "on an ingot pile: it cools");
      check(itemHeat(ItemStack::fromJson((*w.blockEntity(26, Y1, 6))["items"][0])) <= 25, "in a barrel of water: cold at once");
      air->removed = wet->removed = caul->removed = true;
    }
    for (auto& st : rg.P->inv) st = ItemStack();
    rg.P->selected = 0; rg.P->offhand[0] = ItemStack();
    rg.P->inv[0] = ItemStack("copper_ingot", 1); setItemHeat(rg.P->inv[0], 600);
    rg.P->hp = rg.P->maxHp; const float hp0 = rg.P->hp;
    rg.tick(4);
    check(rg.P->hp < hp0, "holding hot metal bare-handed burns");
    rg.P->hp = rg.P->maxHp; setItemHeat(rg.P->inv[0], 600); rg.P->offhand[0] = ItemStack("tongs", 1);
    rg.tick(4);
    check(rg.P->hp >= rg.P->maxHp - 0.01f, "with tongs in the other hand it doesn't");
    const int PX = 2, PZ = 2, PY = rg.Y - 6;
    for (int i = 0; i < 6; i++) w.setBlock(PX + i, PY, PZ, reg().blockId("tin_ore"));
    std::string msg = prospect(w, PX, rg.Y, PZ);
    check(msg.find("Tin") != std::string::npos, "the prospector's pick finds the tin: \"" + msg + "\"");
    recipeGate().immersive = true;
    std::vector<ItemStack> g(9);
    for (int i : {0, 1, 2}) g[i] = ItemStack("iron_ingot", 1);
    g[4] = ItemStack("stick", 1); g[7] = ItemStack("stick", 1);
    CraftMatch m = matchGrid(g, 3);
    check(!m || m.out != "iron_pickaxe", "immersive: no iron pickaxe from the crafting table");
    recipeGate().immersive = false;
    m = matchGrid(g, 3);
    check(m && m.out == "mixed_pickaxe" && stackName(m.result()) == "Iron Pickaxe", "classic: an iron pickaxe as ever (the procedural one)");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  rg.lv->close();
  return fails;
}
