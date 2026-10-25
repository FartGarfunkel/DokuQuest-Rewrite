// tools/sweep.cpp — dq-sweep: uses every item and places, uses and breaks every block in a headless world, the way a
// player would through PlayerController, and reports anything odd (the original's 0.9.0.54 item-use sweep, grown):
//   crash/exception  · an item that turns into an unrelated item (e.g. a firework becoming a bucket)
//   no effect        · an item the original says has a use (local.js itemHasUse) that does nothing at all
//   bad place        · a block item that places something else (or nothing)
//   no screen        · a block asking for a screen kind the game's UI doesn't open
// Run: ./build/dq-sweep [--items] [--blocks] [--only key] > sweep.txt   (the last line is the summary)
#include "core/init.h"
#include "core/registry.h"
#include "sim/controller.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/rules.h"
#include "sim/inv_ops.h"
#include <cstdio>
#include <csignal>
#include <set>

using namespace dq;

namespace {
struct Rec : LevelFx {
  std::vector<std::string> ev;
  void sound(const std::string& n, vec3, float, float, float) override { ev.push_back("sound:" + n); }
  void soundUi(u32, const std::string& n) override { ev.push_back("sound:" + n); }
  void particles(const std::string& k, vec3, int, const json&) override { ev.push_back("fx:" + k); }
  void message(u32, const std::string& t, const std::string&) override { ev.push_back("msg:" + t); }
};
struct Rig {
  std::unique_ptr<Level> lv; Player* p = nullptr; std::unique_ptr<PlayerController> c; double acc = 0; int Y = 0;
  World& w() { return lv->world(REALM_MAIN); }
  void step(double secs, PlayerInput in) {
    const double dt = 1.0 / 60;
    for (double t = 0; t < secs - 1e-9; t += dt) {
      c->update((float)dt, in);
      in.attackPressed = in.usePressed = in.place = in.pick = in.drop = in.dropAll = in.swap = in.crawl = false;
      acc += dt;
      while (acc >= TICK_DT) { lv->tick(TICK_DT); acc -= TICK_DT; }
    }
  }
  void arena() {
    BlockId stone = reg().blockId("stone"), grass = reg().blockId("grass");
    for (int x = -6; x <= 6; x++) for (int z = -6; z <= 6; z++) {
      for (int y = Y; y < Y + 8; y++) w().setBlock(x, y, z, AIR, 0, SetFlags::NoNotify);
      w().setBlock(x, Y - 1, z, (x == 0 && z == -2) ? grass : stone, 0, SetFlags::NoNotify);
      w().setBlock(x, Y - 2, z, stone, 0, SetFlags::NoNotify);
    }
  }
  void reset() {
    for (auto& e : lv->entities()) if (e.get() != p && !e->isPlayer()) e->removed = true;
    lv->tick(TICK_DT);
    arena();
    for (auto& s : p->inv) s.clear();
    for (auto& s : p->armor) s.clear();
    for (auto& s : p->offhand) s.clear();
    p->cursor.clear(); p->hp = p->maxHp; p->dead = false; p->burn = 0; p->air = AIR_MAX; p->riding = 0; p->carrying = nullptr;
    p->realm = REALM_MAIN; p->pos = vec3(0.5f, Y, 0.5f); p->vel = vec3(0); p->gliding = false; p->sleeping = false;
    p->selected = 0; c->useArmed = false; c->useRepeat = 0;
  }
  void look(vec3 at) { vec3 e = p->pos + vec3(0, p->eye, 0), d = glm::normalize(at - e); p->pitch = std::asin(d.y); p->yaw = std::atan2(-d.x, -d.z); }
  std::multiset<std::string> invSet() { std::multiset<std::string> s; for (auto& x : p->inv) if (!x.empty()) s.insert(x.id); for (auto& x : p->armor) if (!x.empty()) s.insert("worn:" + x.id); for (auto& x : p->offhand) if (!x.empty()) s.insert("off:" + x.id); return s; }
  std::map<std::string, int> ents() { std::map<std::string, int> m; for (auto& e : lv->entities()) if (!e->removed && e.get() != p) m[e->type]++; return m; }
  int changedBlocks(std::vector<u32>& snap, bool take) {
    int n = 0, i = 0;
    for (int x = -6; x <= 6; x++) for (int z = -6; z <= 6; z++) for (int y = Y - 1; y < Y + 8; y++, i++) {
      u32 v = (u32)w().getBlock(x, y, z) << 8 | w().getMeta(x, y, z);
      if (take) snap.push_back(v); else if (snap[i] != v) n++;
    }
    return n;
  }
};
const char* g_cur = "";
void onCrash(int sig) { fprintf(stdout, "\nCRASH (signal %d) while sweeping: %s\n", sig, g_cur); fflush(stdout); std::signal(sig, SIG_DFL); std::raise(sig); }
bool related(const std::string& a, const std::string& b) {
  static const std::set<std::string> CONTAINERS = {"bucket", "glass_bottle", "bowl", "flower_pot"};
  if (CONTAINERS.count(b)) return a.find("bucket") != std::string::npos || a.find("bottle") != std::string::npos || a.find("potion") != std::string::npos ||
                                    a.find("stew") != std::string::npos || a.find("soup") != std::string::npos || a.find("milk") != std::string::npos || a.find("honey") != std::string::npos;
  return true;
}
} // namespace

int main(int argc, char** argv) {
  bool doItems = true, doBlocks = true, doInv = true; std::string only;
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--inv") doItems = doBlocks = false; else if (a == "--items") doBlocks = doInv = false; else if (a == "--blocks") doItems = doInv = false; else if (a == "--only" && i + 1 < argc) only = argv[++i]; }
  for (int s : {SIGSEGV, SIGABRT, SIGFPE}) std::signal(s, onCrash);
  std::string ud = "/tmp/dq-sweep"; std::error_code ec; fs::remove_all(ud, ec); fs::create_directories(ud);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("initCore failed\n"); return 99; }
  EventBus bus;
  LevelInfo info; info.name = "Sweep"; info.seedText = "sweep"; info.mode = "survival";
  Rig r; r.Y = dimBase(DIM_OVERWORLD) + 120;
  r.lv = Level::create(fs::path(ud) / "saves", info, nullptr, &bus);
  r.lv->info.rules = json::object(); rules(*r.lv)["commands"] = true; rules(*r.lv)["mobSpawning"] = false;
  Rec fx; r.lv->fx = &fx;
  std::vector<StreamAnchor> anchors = {{vec3(0, r.Y, 0), 2, false}};
  for (int i = 0; i < 200 && r.w().chunks().size() < 25; i++) r.w().stream(anchors, 1000);
  r.arena(); r.lv->tick(TICK_DT);
  auto pl = std::make_unique<Player>(); pl->name = "Sweeper"; pl->pos = vec3(0.5f, r.Y, 0.5f);
  r.p = static_cast<Player*>(r.lv->spawn(std::move(pl)));
  r.c = std::make_unique<PlayerController>(*r.lv, *r.p);
  r.lv->info.spawn = vec3(0.5f, r.Y, 0.5f);
  std::vector<std::string> screens; bus.on("screenOpen", [&](const json& a) { screens.push_back(a.size() > 1 && a[1].is_string() ? a[1].get<std::string>() : "?"); return true; });
  static const std::set<std::string> UI_SCREENS = {"container", "table", "furnace", "station", "stonecutter", "machine", "sign", "lectern", "enchant", "brew"};
  int nCrashFree = 0, nIssues = 0;
  auto issue = [&](const std::string& kind, const std::string& key, const std::string& what) { nIssues++; printf("%-11s %-34s %s\n", kind.c_str(), key.c_str(), what.c_str()); fflush(stdout); };

  const Registry& R = reg();
  if (doItems) {
    printf("== items: use in the air and on the ground (survival)\n");
    for (const ItemDef& d : R.items) {
      const std::string& key = d.key;
      if (key.empty()) continue;
      if (!only.empty() && key != only) continue;
      g_cur = key.c_str();
      bool hasUse = false;
      for (const char* k : {"cape", "heartCrystal", "cart", "boat", "food", "backpack", "bucket", "igniter", "pocketKey", "summon", "armor", "bow", "spyglass", "bonemeal", "plants", "disc", "bottle", "drink", "firework", "pearl", "potion"}) if (d.has(k)) hasUse = true;
      if (!d.block.empty()) hasUse = true;
      for (int pass = 0; pass < 2; pass++) {
        r.reset(); fx.ev.clear(); screens.clear();
        int n = std::max(1, std::min(d.maxStack, 4));
        r.p->inv[0] = ItemStack(key, n);
        if (pass == 0) { r.p->pitch = 1.4f; r.p->yaw = 0; } else r.look(vec3(0.5f, r.Y - 0.5f, -1.5f));
        r.p->hp = r.p->maxHp - 6;   // so food and healing can act
        std::vector<u32> snap; r.changedBlocks(snap, true);
        auto inv0 = r.invSet(); auto ents0 = r.ents();
        try {
          PlayerInput in; in.use = true; in.usePressed = true;
          r.step(0.05, in); in.usePressed = false; r.step(1.8, in); r.step(0.6, PlayerInput{});
        } catch (const std::exception& e) { issue("EXCEPTION", key, std::string(pass ? "on ground: " : "in air: ") + e.what()); continue; }
        auto inv1 = r.invSet(); auto ents1 = r.ents();
        int blocks = r.changedBlocks(snap, false);
        std::vector<std::string> gained, lost;
        for (auto& x : inv1) if (!inv0.count(x)) gained.push_back(x);
        for (auto& x : inv0) if (!inv1.count(x)) lost.push_back(x);
        std::string spawned; for (auto& [t, c] : ents1) if (c > (ents0.count(t) ? ents0[t] : 0)) spawned += t + " ";
        for (auto& g : gained) if (!startsWith(g, "worn:") && !startsWith(g, "off:") && !related(key, g)) issue("TURNS INTO", key, std::string(pass ? "used on ground -> " : "used in air -> ") + g);
        for (auto& s : screens) if (!UI_SCREENS.count(s)) issue("NO SCREEN", key, "asks for screen '" + s + "'");
        bool did = !gained.empty() || !lost.empty() || !spawned.empty() || blocks || !screens.empty() || !fx.ev.empty() ||
                   r.p->inv[0].n != n || r.p->hp != r.p->maxHp - 6 || r.p->realm != REALM_MAIN || glm::distance(r.p->pos, vec3(0.5f, r.Y, 0.5f)) > 0.5f;
        if (pass == 1 && !did && hasUse) issue("NO EFFECT", key, "used in the air and on the ground: nothing happened");
        if (pass == 1 && !d.block.empty() && blocks == 0 && !did) issue("BAD PLACE", key, "places nothing (block " + d.block + ")");
        if (pass == 0 && !did) { /* air use of a block item: nothing is expected */ }
      }
      nCrashFree++;
    }
  }
  if (doBlocks) {
    printf("== blocks: place, use with an empty hand, break (survival, best tool)\n");
    for (BlockId id = 1; id < (BlockId)R.blocks.size(); id++) {
      const BlockDef& b = R.block(id); const std::string& key = b.key;
      if (!only.empty() && key != only) continue;
      if (R.tLiquid[id] || key == "air") continue;
      g_cur = key.c_str();
      r.reset(); fx.ev.clear(); screens.clear();
      int x = 0, y = r.Y, z = -2;
      r.w().setBlock(x, y - 1, z, reg().blockId("grass"));
      try {
        r.w().setBlock(x, y, z, id, 0);
        r.lv->tick(TICK_DT);
        r.look(vec3(x + 0.5f, y + 0.5f, z + 0.5f));
        PlayerInput in; in.use = true; in.usePressed = true;
        r.step(0.05, in); r.step(0.6, PlayerInput{});
        for (auto& s : screens) if (!UI_SCREENS.count(s)) issue("NO SCREEN", key, "using it asks for screen '" + s + "'");
        BlockId now = r.w().getBlock(x, y, z);
        if (now && now != id) { /* toggled (doors, levers) or grew: fine */ }
        if (now) { r.c->breakBlock(x, y, z); r.step(1.0, PlayerInput{}); }
      } catch (const std::exception& e) { issue("EXCEPTION", key, e.what()); }
    }
  }
  if (doInv) {
    printf("== sneak + use with a block in hand on stations places the block (furnish.js)\n");
    for (int fly = 0; fly < 2; fly++) for (const char* st : {"furnace", "chest", "crafting_table", "barrel"}) {   // fly 1: creative flight clears p.sneaking
      if (!reg().blockId(st)) continue;
      r.reset(); screens.clear(); r.p->flying = fly == 1;
      r.w().setBlock(0, r.Y, -2, reg().blockId(st), 0); r.lv->tick(TICK_DT);
      r.p->inv[0] = ItemStack("cobblestone", 8); r.p->selected = 0;
      r.look(vec3(0.5f, r.Y + 1.0f, -1.5f));   // the station's top face
      PlayerInput in; in.sneak = true; r.step(0.15, in); r.look(vec3(0.5f, r.Y + 1.0f, -1.5f));   // (the first frames settle the eye height; aim after them)
      in.use = true; in.usePressed = true;
      r.step(0.05, in); in.usePressed = false; r.step(0.3, in);
      bool placed = r.w().getBlock(0, r.Y + 1, -2) == reg().blockId("cobblestone");
      if (!placed || !screens.empty()) issue("SNEAK PLACE", std::string(st) + (fly ? " (flying)" : ""), std::string(placed ? "placed" : "not placed") + (screens.empty() ? "" : ", opened '" + screens[0] + "'"));
    }
    printf("== inventory: clicks, drags, equip and unequip never make items\n");
    auto total = [&](Player& p) {
      std::map<std::string, int> m;
      auto add = [&](const std::vector<ItemStack>& v) { for (auto& x : v) if (!x.empty()) { m[x.id] += x.n; if (x.tag.is_object() && x.tag.contains("c") && x.tag["c"].is_array()) for (auto& c : x.tag["c"]) { ItemStack y = ItemStack::fromJson(c); if (!y.empty()) m[y.id] += y.n; } } };
      add(p.inv); add(p.armor); add(p.offhand); add(p.back); add(p.hook); add(p.charm); add(p.cape);
      if (!p.cursor.empty()) m[p.cursor.id] += p.cursor.n;
      return m;
    };
    auto check = [&](const char* what, std::map<std::string, int>& before) { auto after = total(*r.p); if (after != before) { std::string d; for (auto& [k, v] : after) if (before[k] != v) d += k + " " + std::to_string(before[k]) + "->" + std::to_string(v) + " "; issue("INV DUPE", what, d); } };
    r.reset();
    Player& p = *r.p;
    p.inv[0] = ItemStack("iron_chestplate", 1); p.inv[1] = ItemStack("cobblestone", 40); p.inv[2] = ItemStack("iron_helmet", 1);
    if (reg().item("leather_backpack")) { ItemStack bpk("leather_backpack", 1); bpk.tag = json::object(); bpk.tag["c"] = json::array({ItemStack("diamond", 5).toJson(), ItemStack("gold_ingot", 7).toJson()}); p.inv[3] = bpk; }
    InvOpen o; InvSession S(p, &r.w(), o, false);
    auto before = total(p);
    // equip by click (press = dragBegin + release = dragEnd, like the UI) and by plain clicks; unequip; put back
    for (int round = 0; round < 3; round++) {
      S.slotClick(Grp::Main, 0, false, false); S.dragBegin(Grp::Armor, 1, false); S.dragEnd(); check("equip chest (drag)", before);
      S.slotClick(Grp::Armor, 1, false, false); S.dragBegin(Grp::Main, 0, false); S.dragEnd(); check("unequip chest (drag)", before);
      S.slotClick(Grp::Main, 2, false, false); S.slotClick(Grp::Armor, 0, false, false); check("equip helmet (click)", before);
      S.slotClick(Grp::Armor, 0, false, false); S.slotClick(Grp::Main, 2, false, false); check("unequip helmet (click)", before);
      S.slotClick(Grp::Main, 3, false, false); S.dragBegin(Grp::Back, 0, false); S.dragEnd(); check("equip backpack", before);
      S.slotClick(Grp::Back, 0, false, false); S.dragBegin(Grp::Main, 3, false); S.dragEnd(); check("unequip backpack", before);
      S.slotClick(Grp::Main, 1, false, false); S.dragBegin(Grp::Main, 5, true); S.dragEnd(); check("pick up then right-click", before);
      S.dragBegin(Grp::Main, 6, true); S.dragAdd(Grp::Main, 7); S.dragEnd(); check("right-drag", before);
      S.dragBegin(Grp::Main, 1, false); S.dragEnd(); check("put the rest back", before);
      S.slotClick(Grp::Main, 0, false, true); check("shift-click armour", before);
      S.slotClick(Grp::Armor, 1, false, true); check("shift-click off", before);
    }
    S.close(); check("close", before);
  }
  printf("SWEEP DONE: %d issues\n", nIssues);
  return nIssues ? 1 : 0;
}
